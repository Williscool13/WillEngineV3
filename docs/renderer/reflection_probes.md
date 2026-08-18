# Reflection Probes: Authoring Rules

## What a bake sees

Everything, except:

1. **Flagged meshes**: "Probe Bake Exclude" checkbox (any mesh component type).
2. **Flagged lights**: same checkbox on area/sphere. Excluded = no DI, no GI, no emissive surface.
3. **Movers**: dynamic and kinematic physics bodies, auto-excluded.
4. **Light proxy surfaces**: all area/sphere emissive proxies, auto-excluded. 

Sun and skybox always bake in. In-world text/sprites bake in. Debug draws/outlines are suppressed by the bake.

## Placement

- One lighting environment per probe. Never span occluding walls.
- Run bounds past the shell's outer face slightly. Keep the overshoot small.
- Extents = entity scale (sphere radius = max component). `fadeMargin` = meters, inward.
- `captureOffset` = world units, scale-independent. Bias toward the brightest region: too-bright is safe, too-dark is destructive.
- Overlaps: the two smallest-volume probes blend, split by fade over volume, then lowest index. Nesting is fine, the tighter one keeps its interior.

## Bake behavior

- Hijacks the main viewport: per face, history resets, settle frames (default 240), snapshot.
- Captured post-TAA, pre-post-processing (raw HDR).
- Bakes record the converged scene. Rebake after lighting changes; rebakes hot-reload in place.
- 2-pass interbounce: pass 1 bakes probes-off, pass 2 rebakes with pass-1 results.
- Ground Truth Bake (`probeBake.bGroundTruth`): faces render via the Full GT path tracer.
- Output = `assets/probes/probe_<id>.wprobe`. Duplicated entities re-roll their ID.

## Bake lighting profile (user responsibility)

The bake captures the active lighting settings; it only forces `reflectionProbe.bEnabled` (off pass 1, on pass 2). Bake with a profile that sets:

- **GI diffuse gather OFF** (DDGI stays ON): the gather's screen-space tier is view-dependent and tints each face differently.
- **GTAO OFF**: also per-face view-dependent, and runtime GTAO already multiplies probe content (baked AO = double-darkened corners). Accepted loss: reflected images carry no AO (runtime GTAO only darkens the receiving surface, not what mirrors show).
- RT reflections: your choice.
