/*
 * SIGMA shadow denoiser: tile smoothing (dilation).
 * Reduces a 5x5 tile neighborhood into each tile (min of mins, max of maxes). The blur/temporal passes
 * then read only their own smoothed tile, which is equivalent to scanning the 5x5 neighborhood per pixel
 * but moves that work from per-pixel to per-tile, and softens the classification boundary.
 */
import common_interop;
import push_constant_interop;
import bindless_declarations;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uniform SigmaSmoothTilesPushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    int2 tile = int2(tid.xy);
    if (any(tid.xy >= pc.tilesExtent)) {
        return;
    }

    int2 tilesMax = int2(pc.tilesExtent) - 1;
    float windowMin = 1.0;
    float windowMax = 0.0;
    [unroll]
    for (int ty = -2; ty <= 2; ty++)
    {
        [unroll]
        for (int tx = -2; tx <= 2; tx++)
        {
            int2 t = clamp(tile + int2(tx, ty), int2(0), tilesMax);
            float2 mm = rdgTexturesFloat2[pc.inputIndex].Load(int3(t, 0)).rg;
            windowMin = min(windowMin, mm.r);
            windowMax = max(windowMax, mm.g);
        }
    }

    rdgStorageFloat2[pc.outputIndex][tile] = float2(windowMin, windowMax);
}
