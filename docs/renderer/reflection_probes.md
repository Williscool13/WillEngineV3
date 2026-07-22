# Reflection Probes: Authoring Rules

## What a bake sees

Everything, except:

1. **Flagged meshes**: "Probe Bake Exclude" checkbox (any mesh component type).
2. **Flagged lights**: same checkbox on point/area/sphere. Excluded = no DI, no GI, no emissive surface.
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
