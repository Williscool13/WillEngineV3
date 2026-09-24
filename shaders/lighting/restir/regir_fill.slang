import common_interop;
import push_constant_interop;
import lights_interop;
import restir_interop;

import packing_functions;
import shader_functions;
import pbr_functions;
import binning_common;
import restir_common;
import regir_hash_common;

// =============== Shader Summary ============
// One workgroup of 512 per active cell, rebuilding its entry table from scratch: every analytic light and emissive meshlet in reach gets a distance-aware mass, the strongest
// REGIR_ENTRIES_PER_CELL are tempered, sorted by key and prefix-summed into cumMass. Deterministic given the frame's lights, so draws and marginal pdfs are exact.

static const uint FILL_GROUP = 512u;
static const uint ENTRIES_PER_LANE = REGIR_ENTRIES_PER_CELL / FILL_GROUP;

groupshared uint gKey[REGIR_GATHER_SCRATCH];
groupshared float gMass[REGIR_GATHER_SCRATCH];
groupshared uint gCount;

static void PushCandidate(uint key, float mass)
{
    uint slot;
    InterlockedAdd(gCount, 1u, slot);
    if (slot < REGIR_GATHER_SCRATCH) {
        gKey[slot] = key;
        gMass[slot] = mass;
    }
}

/** Bitonic sort of [0, n) by mass, largest first. n must be a power of two. */
static void SortByMassDescending(uint tid, uint n)
{
    for (uint k = 2u; k <= n; k <<= 1u) {
        for (uint j = k >> 1u; j > 0u; j >>= 1u) {
            for (uint i = tid; i < n; i += FILL_GROUP) {
                uint ixj = i ^ j;
                if (ixj > i) {
                    bool ascending = (i & k) == 0u;
                    if ((gMass[i] < gMass[ixj]) == ascending) {
                        uint ka = gKey[i]; gKey[i] = gKey[ixj]; gKey[ixj] = ka;
                        float ma = gMass[i]; gMass[i] = gMass[ixj]; gMass[ixj] = ma;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
}

/** Bitonic sort of [0, n) by key ascending. n must be a power of two. */
static void SortByKeyAscending(uint tid, uint n)
{
    for (uint k = 2u; k <= n; k <<= 1u) {
        for (uint j = k >> 1u; j > 0u; j >>= 1u) {
            for (uint i = tid; i < n; i += FILL_GROUP) {
                uint ixj = i ^ j;
                if (ixj > i) {
                    bool ascending = (i & k) == 0u;
                    if ((gKey[i] > gKey[ixj]) == ascending) {
                        uint ka = gKey[i]; gKey[i] = gKey[ixj]; gKey[ixj] = ka;
                        float ma = gMass[i]; gMass[i] = gMass[ixj]; gMass[ixj] = ma;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
}

/** Hillis-Steele inclusive prefix sum of gMass over [0, REGIR_ENTRIES_PER_CELL). */
static void PrefixSumMass(uint tid)
{
    const uint N = REGIR_ENTRIES_PER_CELL;
    for (uint d = 1u; d < N; d <<= 1u) {
        float add[ENTRIES_PER_LANE];
        for (uint e = 0u; e < ENTRIES_PER_LANE; e++) {
            uint i = tid + e * FILL_GROUP;
            add[e] = (i >= d) ? gMass[i - d] : 0.0;
        }
        GroupMemoryBarrierWithGroupSync();
        for (uint e = 0u; e < ENTRIES_PER_LANE; e++) {
            uint i = tid + e * FILL_GROUP;
            gMass[i] += add[e];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

[shader("compute")]
[numthreads(FILL_GROUP, 1, 1)]
void ComputeReGIRFill(uniform ReGIRFillPushConstant pc, uint3 groupID : SV_GroupID, uint3 localID : SV_GroupThreadID) {
    uint cellIdx = groupID.x;
    if (cellIdx >= pc.activeCount[0]) {
        return;
    }

    int4 active = pc.activeCells[cellIdx];
    uint slot = ReGIRActiveSlot(uint(active.w));
    uint baseOut = ReGIRCellEntryBase(slot);
    int lightCount = pc.lightData->lightCount;

    if (lightCount <= 0) {
        if (localID.x == 0u) { pc.cellData[slot] = uint2(0u, 0u); }
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    ReGIRHashKey key;
    key.cellPos = active.xyz;
    key.level = ReGIRActiveLevel(uint(active.w));
    float3 cellCenterWS = ReGIRCellCenterFromKey(key);
    float3 cellCenterVS = mul(scene.view, float4(cellCenterWS, 1.0)).xyz;
    float targetMinDist2 = ReGIRTargetMinDist2(key.level);
    // Cull against every point that can query this cell, including the jitter
    float serveReach = (0.5 + REGIR_KEY_CAMERA_OFFSET_SCALE) * ReGIRCellSizeAtLevel(key.level);
    float cellHalfDiag = ReGIRCellHalfDiag(key.level) + sqrt(3.0) * serveReach;
    float cellHalfSize = 0.5 * ReGIRCellSizeAtLevel(key.level) + serveReach;
    float3 cellMin = cellCenterWS - cellHalfSize;
    float3 cellMax = cellCenterWS + cellHalfSize;
    float distFloor = max(targetMinDist2, LIGHT_GEOM_MIN_DIST2);

    if (localID.x == 0u) { gCount = 0u; }
    for (uint s = localID.x; s < REGIR_GATHER_SCRATCH; s += FILL_GROUP) {
        gKey[s] = ~0u;
        gMass[s] = -1.0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint analyticCount = uint(pc.lightData->analyticLightCount);
    for (uint i = localID.x; i < analyticCount; i += FILL_GROUP) {
        LightInfo light = pc.lightData->lights[i];
        if (SqDistPointAABB(light.position.xyz, cellMin, cellMax) > light.range * light.range) { continue; }

        LightView vs = UnpackLightVS(pc.lightVS[i]);
        float mass = EvalCellLightTarget(vs, cellCenterVS, cellCenterVS, cellHalfDiag, UnpackUnorm4x8(vs.packedColor).rgb, targetMinDist2);
        if (mass > 0.0) { PushCandidate(i, mass); }
    }

    uint laneConeRejects = 0u;
    uint meshCount = uint(pc.lightData->emissiveMeshCount);
    for (uint p = localID.x; p < meshCount; p += FILL_GROUP) {
        EmissiveMesh mesh = pc.lightData->emissiveMeshes[p];
        if (any(mesh.aabbMin > cellMax) || any(mesh.aabbMax < cellMin)) { continue; }

        for (uint g = mesh.firstMeshlet; g < mesh.firstMeshlet + mesh.meshletCount; g++) {
            EmissiveMeshlet meshlet = pc.lightData->emissiveMeshlets[g];
            if (meshlet.lightCount == 0u || meshlet.power <= 0.0) { continue; }
            if (any(meshlet.aabbMin > cellMax) || any(meshlet.aabbMax < cellMin)) { continue; }

            float3 geomMin = meshlet.aabbMin + meshlet.rangePad;
            float3 geomMax = meshlet.aabbMax - meshlet.rangePad;
            float dist2 = SqDistPointAABB(cellCenterWS, geomMin, geomMax);
            float3 colour = UnpackUnorm4x8(pc.lightData->lights[meshlet.firstLight].packedColor).rgb;
            float colourMax = max(colour.r, max(colour.g, colour.b));
            float mass = meshlet.power * colourMax / max(dist2, distFloor);

            if (meshlet.coneCutoff < 1.0) {
                float3 geomCenter = 0.5 * (geomMin + geomMax);
                float geomRadius = 0.5 * length(geomMax - geomMin);
                float3 toMeshlet = geomCenter - cellCenterWS;
                float dist = length(toMeshlet);
                float backFacingMin = (meshlet.coneCutoff + REGIR_CONE_SLACK) * dist + geomRadius + cellHalfDiag;
                if (dot(toMeshlet, UnpackNormalOct16(meshlet.packedConeAxis)) >= backFacingMin) {
                    laneConeRejects++;
                    continue;
                }
            }

            if (mass > 0.0) { PushCandidate(REGIR_KEY_MESHLET | g, mass); }
        }
    }
#if WILL_EDITOR
    if (pc.coneRejected != nullptr && laneConeRejects > 0u) {
        uint coneAdded;
        InterlockedAdd(pc.coneRejected[0], laneConeRejects, coneAdded);
    }
#endif
    GroupMemoryBarrierWithGroupSync();

    uint gathered = gCount;
    uint kept = min(gathered, REGIR_GATHER_SCRATCH);
    if (kept > REGIR_ENTRIES_PER_CELL) {
        SortByMassDescending(localID.x, REGIR_GATHER_SCRATCH);
        kept = REGIR_ENTRIES_PER_CELL;
    }

    if (kept == 0u) {
        if (localID.x == 0u) { pc.cellData[slot] = uint2(0u, 0u); }
        return;
    }

    // Temper after the rank so the kept set stays the physically strongest; the tail sorts last and adds no mass
    for (uint i = localID.x; i < REGIR_ENTRIES_PER_CELL; i += FILL_GROUP) {
        if (i < kept) { gMass[i] = pow(gMass[i], REGIR_SELECT_TEMPER); }
        else { gKey[i] = ~0u; gMass[i] = 0.0; }
    }
    GroupMemoryBarrierWithGroupSync();

    SortByKeyAscending(localID.x, REGIR_ENTRIES_PER_CELL);
    PrefixSumMass(localID.x);

    for (uint i = localID.x; i < kept; i += FILL_GROUP) {
        ReGIREntry entry;
        entry.key = gKey[i];
        entry.cumMass = gMass[i];
        pc.entries[baseOut + i] = entry;
    }

    if (localID.x == 0u) {
        pc.cellData[slot] = uint2(kept, asuint(gMass[REGIR_ENTRIES_PER_CELL - 1u]));
#if WILL_EDITOR
        if (pc.gatherOverflow != nullptr && gathered > REGIR_GATHER_SCRATCH) {
            uint d;
            InterlockedAdd(pc.gatherOverflow[0], gathered - REGIR_GATHER_SCRATCH, d);
        }
#endif
    }
}
