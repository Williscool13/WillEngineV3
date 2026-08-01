/*
 * SIGMA shadow denoiser: tile smoothing (mirrors NRD SIGMA_SmoothTiles).
 * Gaussian-dilates the per-tile "needsFilter" flag over a 5x5 tile window, with the gaussian width keyed
 * by the tile's max blur radius (bigger penumbra -> wider dilation), so the filter fades in smoothly at
 * penumbra-region borders instead of banding at tile edges. Output RG8: .x = isSky, .y = dilated flag.
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */
import common_interop;
import push_constant_interop;
import bindless_declarations;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeSigmaSmoothTiles(uniform SigmaSmoothTilesPushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    int2 tile = int2(tid.xy);
    if (any(tid.xy >= pc.tilesExtent)) {
        return;
    }

    int2 tilesMax = int2(pc.tilesExtent) - 1;
    float3 center = rdgTexturesFloat4[pc.inputIndex].Load(int3(tile, 0)).xyz; // (needsFilter, radius, isSky)
    float k = 1.01 / (center.y + 0.01);

    float blurry = 0.0;
    float sum = 0.0;
    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 t = clamp(tile + int2(dx, dy), int2(0), tilesMax);
            float flag = rdgTexturesFloat4[pc.inputIndex].Load(int3(t, 0)).x;
            float d2 = float(dx * dx + dy * dy);
            float w = exp2(-k * d2);
            blurry += flag * w;
            sum += w;
        }
    }

    rdgStorageFloat2[pc.outputIndex][tile] = float2(center.z, blurry / sum);
}
