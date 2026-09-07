# Visibility Buffer Slimming — Handoff

Audit of the visibility-buffer chain's memory and bandwidth cost, with a prioritized work plan.
Status: **analysis only, nothing implemented.** Every line/format reference below was read from the
tree at the time of writing — re-verify line numbers before editing.

## 1. Current cost

Four full-res targets, all allocated unconditionally in `src/render/render_thread.cpp:590-606`:

| Target | Format | B/px | Contents |
|---|---|---|---|
| `visibility_target` | `R32G32_UINT` | 8 | `R = meshletIndex<<8 \| triIdx`, `G = instanceIndex` |
| `visibility_barycentric` | `R32G32_SFLOAT` | 8 | 2 barycentrics |
| `visibility_derivatives` | `R16G16B16A16_SFLOAT` | 8 | `uvDdx.xy`, `uvDdy.xy` |
| `stable_id` | `R32G32_UINT` | 8 | 64-bit `StringID` for picking/outline |
| | | **32 B/px** | |

Footprint: **66 MB @ 1080p, 118 MB @ 1440p, 265 MB @ 4K.** Larger than the gbuffer
(`GBUFFER_TARGET_ONE` 16 B + `GBUFFER_TARGET_TWO` 8 B = 24 B/px).

Three of the four carry a clear value, and `RenderGraph::PopulateAutoClearTextures` +
`render_graph.cpp:1505` issue a real `vkCmdClearColorImage` on each one's first-use pass every
frame — ~24 B/px of clear traffic before any geometry renders.

Formats live in `src/render/vulkan/vk_config.h:29-35`.

### Bit budget

| Field | Bits used | Bits needed | Source of the bound |
|---|---|---|---|
| triangle index | 8 | 7 | `MESHLET_MAX_TRIANGLES = 126` (`constants_interop.h:75`) |
| meshlet index | 24 | 24 | global index into `GEOMETRY_MESHLET_BUFFER` |
| instance index | 32 | 20 | `MAX_INSTANCE_SLOTS = 1<<20` (`model_interop.h:180`) |

51 bits of information in 64 bits of storage. Repacking buys nothing — 51 bits still needs a
64-bit texel. Reaching `R32_UINT` requires a **scheme change** (§4), not tighter packing.

### Read amplification (the dominant cost)

`SetupVisibilityShadingPass` (`geometry_passes.cpp:786-877`) issues **one
`vkCmdDispatchIndirect` per active material**, each covering that material's screen-space AABB
(`shadingBucketIndex = stableIndex`, set at `render_thread.cpp:1809`). Every dispatched lane loads
the vis texel and rejects on `instance.materialIndex != materialIndex`
(`visibility_shading_common.slang:52-64`).

With N active materials whose AABBs overlap, the vis buffer is read ~N times over those regions.
Add the bary/deriv pass, the bucketing bounds pass, the lighting resolve
(`lighting_passes.cpp:246, 352`) and the ReSTIR resolve (`restir_passes.cpp:682`), each a further
full-screen-ish read.

**This amplification, not the 66 MB, is the thing worth fixing first.**

## 2. Work plan (do in this order)

### Step 1 — Delete `visibility_barycentric` + `visibility_derivatives` (−16 B/px)

`ComputeVisibilityBarycentricDerivative`
(`shaders/geometry/visibility_barycentric_derivative.slang`) loads the vis texel, fetches 3
`VertexPosition` + 3 `VertexAttribute`, computes `CalcFullBary`, writes 16 B/px.
`ReconstructShadingInputs` (`shaders/shading/visibility_shading_common.slang:79-88`) then fetches
**the exact same 6 vertices again** and reads the 16 B back.

Fold `CalcFullBary` into `ReconstructShadingInputs`; delete the pass, the two targets, and the two
push-constant fields.

- Per-pixel cost: ~2 extra mat-vec (the function already does one for `objPos`) + ~40 ALU.
- Removes: a full-screen dispatch, a full-screen vis read, 6 duplicated buffer loads/px,
  16 B/px clear + 16 B/px write + 16 B/px read, 33 MB @ 1080p.
- Precision improves — fp32 in-register beats the fp32 texture roundtrip. The
  `vk_config.h:32` "half is imprecise" note becomes moot.

Touch list:
- `shaders/shading/visibility_shading_common.slang` — move `CalcFullBary` in, drop the
  `barycentricBufferIndex` / `derivativeBufferIndex` params
- `shaders/shading/shading_default_lit.slang`, `shading_error_unlit.slang`,
  `shading_bucket_visualize.slang` — call-site updates
- `src/render/passes/geometry_passes.cpp` — delete `SetupVisibilityBarycentricDerivativePass`
  and the `ReadStorageImage` declarations at `:815-816` and `:889-890`
- `src/render/passes/geometry_passes.h`, `src/render/render_thread.cpp:577-578, 591-592, 631`,
  `src/render/renderer_types.h:31-32`, `src/render/vulkan/vk_config.h:32-35`,
  `src/render/shaders/push_constant_interop.h:364-365, 429-430`,
  `src/render/pipelines/pipeline_manager.cpp:444`

Validation: pixel-identical output vs. the current path (allow ULP-level diffs), especially mip
selection on high-frequency textures at grazing angles — that is what the derivatives feed via
`SampleGrad`.

### Step 2 — Replace AABB bucketing with tile lists

Kills the read amplification. **Tile lists, not exact pixel lists** — see §3 for why.

`ComputeShadeDispatchBucketing` (`shaders/geometry/visibility_bucketing_bounds.slang`) is already
`[numthreads(16,16,1)]` dispatched over the full screen, so **the workgroup already is the tile**,
and the wave-peel loop already enumerates the distinct buckets in it.

**Bounds pass** — the elected lane's body becomes one atomic instead of four:

```hlsl
uint slot;
InterlockedAdd(pc.shadeDispatchBuffer[activeBucket].tileCount, 1, slot);
if (slot < pc.maxTilesPerBucket) {
    pc.tileListBuffer[activeBucket * pc.maxTilesPerBucket + slot] = tileIndex; // tileIndex = gid.y * tilesX + gid.x
}
```

This pass gets *faster* — the existing comment there records ~7.5 ms at 1440p from atomic
contention, and 4 `InterlockedMin/Max` become 1 `InterlockedAdd`.

**Resolve** (`shaders/geometry/visibility_bucketing_shade_resolve.slang`):
`xDispatch = min(tileCount, maxTilesPerBucket)`, `y = z = 1`.

**Shading entry** — replace the AABB preamble (`shading_default_lit.slang:16-21` and siblings):

```hlsl
uint tile  = pc.tileListBuffer[pc.materialIndex * pc.maxTilesPerBucket + gid.x];
uint2 pixel = uint2(tile % pc.tilesX, tile / pc.tilesX) * 16 + gtid.xy;
if (any(pixel >= pc.extents)) return;
```

Put it in `visibility_shading_common.slang` as a helper so it is one edit, then update
`shading_default_lit`, `shading_error_unlit`, `shading_bucket_visualize`, and the six lighting
resolves that share `visibility_lighting_common.slang`.

**Lighting buckets** get the identical treatment via `LightingDispatchParameters`
(`model_interop.h:231+`).

**CPU side**: reset `tileCount` in the existing per-material loop at `render_thread.cpp:1826`;
size the tile buffer next to `shadeDispatchBufferSize` in
`src/render/render-view/render_view_helpers.cpp:248`.

#### Allocation decision

`BINDLESS_MATERIAL_BUFFER_COUNT = 2048` (`render_config.h:88`), so a naive
`materialCount × tileCount` allocation is 33 MB worst case @ 1080p (8160 tiles × 2 B). Two options:

- **(A) Recommended first cut.** Size by the runtime `viewFamily.materialWatermark`, cap
  `maxTilesPerBucket`, and **keep the AABB fields in `ShadeDispatchParameters` as the overflow
  path**. A bucket that exceeds the cap falls back to its AABB in the resolve. At 128 active
  materials this is ~2 MB. The fallback means the change cannot regress, and gives a runtime A/B.
- **(B) Exact packing at tile granularity.** Pass A writes a per-tile bucket set
  (`tileCount × K × 2 B` ≈ 48 KB @ 1080p); a tiny pass B over ~8160 tiles does
  count → 2048-entry prefix sum → scatter. Exact, ~50 KB, pass B is not full-screen so it is
  nearly free. More code. Do this only if (A)'s memory shows up in profiling.

Tile counts: 1080p = 120×68 = 8160; 1440p = 160×90 = 14400; 4K = 240×135 = 32400. All fit `uint16`;
8K does not, so either use `uint32` or assert.

#### Known wrinkle — duplicate tile appends

The wave-peel loop is per-**wave**, not per-workgroup. A 16×16 group is 4 waves at 64-wide and 8 at
32-wide, so a tile can be appended up to 8 times. **This is a perf loss, not a correctness bug** —
a duplicated tile is shaded again with the same rejections, producing identical results.

Ship the naive version, measure the duplicate rate, and only then add a groupshared dedup (small
open-addressed set + `GroupMemoryBarrierWithGroupSync`, one thread flushes). Note that duplicates
inflate `tileCount`, so size for the worst case or let `maxTilesPerBucket` clamp it.

### Step 3 — Gate `stable_id` out of game builds (−8 B/px)

8 B/px written as MRT target 1 by every geometry, cutout, text and sprite fragment shader, cleared
every frame, consumed **only** by `shaders/utility/selection_outline.slang` and the 1×1 picking
readback (`render_thread.cpp:560`, `resource_manager.cpp:170`). There is no editor/shipping gate
anywhere in `src/render`.

Options, cheapest first:

1. Drop to `R32_UINT` holding a dense selection id (instance slot, 20 bits) instead of the 64-bit
   `StringID`; map to `StringID` on readback. Halves it with no structural change.
2. Note that geometry pixels do not need it at all — `vis.y` already gives the instance index and
   `Instance.stableId` is a buffer lookup. Only text/sprites need an independent channel.
3. Full editor gate: skip the target and drop `SV_Target1` in game builds. Needs pipeline
   permutations for `geometry_visibility_buffer{,_cutout}.slang`, `sprites.slang`,
   `text_default.slang` — hence last.

### Step 4 — `visibility_target` → `R32_UINT` (−4 B/px) — RE-EVALUATE FIRST

**Do not start this until Step 2 is measured.** Tile lists drop vis-buffer read amplification from
`N_materials × overlap` to roughly 1–1.5×, which removes most of this change's payoff while its
cost stays the same.

Scheme: store the index into the compacted visible-meshlet list instead of (global meshlet,
instance): `visibleMeshletIndex(24) | phaseBit(1) | triIndex(7)` = exactly 32 bits. `CompactedMeshlet`
(`instancing_interop.h:77-81`) already carries `instanceIndex` + `meshletIndexWithinLOD`+LOD, and
every consumer already loads `Instance`/`Primitive` immediately after, so `prim.meshletOffset[lod]`
is free.

Two real blockers:

1. **Phase 2 rewrites `visible_meshlets`.** `addCullChain(true)` (`geometry_passes.cpp:652`)
   re-runs compaction into the same buffer (written at `instancing_prefix_sum.slang:287-288`,
   region bases from `compactedDispatchBuffer->regionBase`), so phase-1 pixels' indices go stale.
   Fix: either phase 2 appends after phase 1's `totalVisibleMeshlets`, or use the phase bit above
   with two lists. The phase bit is cleaner and fits the 32.
2. **`visible_meshlets` is aliasable.** `graph.CreateBuffer(visibleMeshlets, size, false)`
   (`geometry_passes.cpp:72`) leaves `bCanAlias = true` and its last reader is the mesh-shading
   pass, so the graph reclaims the memory. Downstream `ReadBuffer` declarations extend the lifetime
   automatically, costing `highestMeshletCount * 8` bytes resident — needs >1M visible meshlets
   before that loses to the 8 MB saved @ 1080p.

Related micro-win, independent of the above: the lighting resolve
(`visibility_lighting_common.slang:34-42`) loads all 8 bytes purely to read `vis.y` for a bucket
gate. `gbufferOne.a` has ~15 free bits (bit 0 = geometry marker, bits 16-31 = viewZ delta, per
`vk_config.h:37-41`). Stash `lightingBucketIndex` there and the lighting/ReSTIR resolves stop
touching the vis buffer entirely.

## 3. Why tile lists and not exact pixel lists

Exact per-pixel compaction costs three things a tile list does not:

1. **count → prefix-sum → scatter.** A single full-screen pass cannot append into per-bucket
   regions without knowing offsets, and W×H per bucket cannot be over-allocated. So it is two
   full-screen passes over the vis buffer, or one plus a per-pixel `R16_UINT` material cache
   (2 B/px).
2. **Write coalescing.** The shading shaders end with `rdgStorageUInt4[gbufferOneIndex][pixel]` and
   `rdgStorageUInt2[gbufferTwoIndex][pixel]`. A compacted list makes adjacent lanes write scattered
   texels. Recovering it needs a Morton-ordered append. Tile lists keep writes perfectly coalesced.
3. **Texture cache.** `SampleGrad` in `shading_default_lit` benefits from neighbouring lanes hitting
   the same texture pages; a list spanning an instance's whole screen footprint scatters that.

The win being chased is "stop dispatching over the whole screen for a material covering 3% of it,"
and tiles capture nearly all of it — material coverage is spatially coherent, so a typical 16×16
tile holds 1–3 materials, not 40. Exact pixel lists buy the remaining lane occupancy and give back
locality. Not worth it.

## 4. Loose end to verify

`geometry_passes.cpp:559` declares `instancedMeshShading.WriteColorAttachment(targets.gbufferOne)`,
but the `VkRenderingInfo` binds only `{visibility, stableId}` (`:589`) and the pipeline declares two
color formats (`pipeline_manager.cpp:764-765`). So the pass declares a write it never performs.

Before removing it, check whether it is load-bearing for **clear placement**: `gbufferOne` is
created via `declareGeometryTarget` → `CreateVersionedTexture(..., CLEAR_COLOR_EMPTY)` on the
`Fresh` path, and `PopulateAutoClearTextures` (`render_graph.cpp:541-547`) attaches the clear to the
texture's `firstPass`. Removing the declaration moves the clear to the shading pass. If that is
fine, the declaration only costs an unnecessary `COLOR_ATTACHMENT_OPTIMAL` transition and pins
`gbufferOne`'s lifetime back to the geometry pass, blocking aliasing.

## 5. Expected result

| | B/px | 1080p |
|---|---|---|
| Today | 32 | 66 MB |
| After Step 1 | 16 | 33 MB |
| After Step 3 (editor) | 12 | 25 MB |
| After Step 3 (game build) | 8 | 17 MB |
| After Step 4 | 4–8 | 8–17 MB |

Plus per-material vis-buffer read traffic reduced from `N × overlap` to ~1× by Step 2.

## 6. Measurement

Everything needed already exists:

- `SetupVisibilityBucketingDebugPass` + `frameBuffer.debug.bEnableShadeDispatchBucketingVisualization`
  visualizes bucket coverage.
- The `ShadeDispatchReadback` debug UI (`render_thread.cpp:1475-1510`) already tables per-material
  dispatch + min/max. Extend it with `tileCount` and the wasted-lane ratio
  (`AABB area / actual covered pixels`) — that number is the direct before/after metric for Step 2.
- `GPU_STATS_ENABLED` / Tracy zones are already on every pass named above.

Baseline before touching anything: per-pass GPU times for `Visibility Barycentric Derivative`,
`Shade Bucketing Bounds`, and `Visibility Shading`, at 1080p and 1440p, in a scene with a high
active-material count (that is where Step 2 pays).
