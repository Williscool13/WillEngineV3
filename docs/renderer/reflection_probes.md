# Reflection Probes: Authoring Rules

## What a bake sees

Everything, except:

1. **Flagged meshes**: "Probe Bake Exclude" checkbox (any mesh component type).
2. **Flagged lights**: same checkbox on area/sphere. Excluded = no DI, no GI, no emissive surface.
3. **Movers**: dynamic and kinematic physics bodies, auto-excluded.

Sun and skybox always bake in. In-world text/sprites bake in. Debug draws/outlines are suppressed by the bake.

## Placement

- One lighting environment per probe. Never span occluding walls.
- Overshoot room bounds slightly, but less than wall thickness. Buries the fade band in the wall (full coverage) without leaking into the next room.
- Overshoot cost: the box is also the parallax proxy, so reflections displace slightly outward. Only visible on mirror-flat surfaces; fit those faces tight instead.
- Extents = entity scale (sphere radius = max component). `fadeMargin` = meters, inward.
- `captureOffset` = world units, scale-independent. Bias toward the brightest region: too-bright is safe, too-dark is destructive.
- Overlaps: smallest volume wins. Nesting is fine.

## Bake behavior

- Hijacks the main viewport: per face, history resets, ~48 settle frames, snapshot. ~5s per probe.
- Captured post-TAA, pre-post-processing (raw HDR).
- Bakes record the converged scene. Rebake after lighting changes.

## Bake lighting profile (user responsibility)

The bake captures the active lighting settings; it only forces `reflectionProbe.bEnabled` (off pass 1, on pass 2). Bake with a profile that sets:

- **GI diffuse gather OFF** (DDGI stays ON): the gather's screen-space tier is view-dependent and tints each face differently.
- **GTAO OFF**: also per-face view-dependent, and runtime GTAO already multiplies probe content (baked AO = double-darkened corners). Accepted loss: reflected images carry no AO (runtime GTAO only darkens the receiving surface, not what mirrors show).
- RT reflections: your choice.
