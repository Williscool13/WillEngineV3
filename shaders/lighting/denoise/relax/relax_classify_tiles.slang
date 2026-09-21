/*
 * RELAX: ClassifyTiles pass. Marks 16x16 tiles as sky (all pixels beyond denoising range).
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import relax_interop;
import denoiser_common;
import shader_functions;

groupshared uint s_isSky;

[shader("compute")]
[numthreads(8, 4, 1)]
void RelaxClassifyTilesMain(uniform RelaxClassifyTilesPushConstant pc,
    uint2 gtid : SV_GroupThreadID,
    uint2 gid : SV_GroupID,
    uint threadIndex : SV_GroupIndex,
    uint3 dtid : SV_DispatchThreadID)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;

    if (threadIndex == 0) { s_isSky = 0; }
    GroupMemoryBarrierWithGroupSync();

    uint2 pixelPos = gid * 16 + gtid * uint2(2, 4);
    uint isSky = 0;

    float2 quadUv = float2(pixelPos + 1u) * rc.gRectSizeInv;
    float4 viewZQuads[2];
    viewZQuads[0] = rdgTexturesFloat[pc.viewZIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv);
    viewZQuads[1] = rdgTexturesFloat[pc.viewZIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv, int2(0, 2));
    const uint2 quadOffsets[4] = { uint2(0, 1), uint2(1, 1), uint2(1, 0), uint2(0, 0) };
    for (uint q = 0; q < 2; q++) {
        for (uint c = 0; c < 4; c++) {
            uint2 pos = pixelPos + quadOffsets[c] + uint2(0, q * 2u);
            // Out-of-rect texels vote sky (was implicit via the raw-depth OOB load linearizing to far).
            bool bInRect = pos.x < uint(rc.gRectSize.x) && pos.y < uint(rc.gRectSize.y);
            isSky += (!bInRect || !RelaxIsInDenoisingRange(viewZQuads[q][c], rc.gDenoisingRange)) ? 1 : 0;
        }
    }

    InterlockedAdd(s_isSky, isSky);
    GroupMemoryBarrierWithGroupSync();

    if (threadIndex == 0) {
        rdgStorageFloat[pc.tilesOutIndex][gid] = s_isSky > 255 ? 1.0 : 0.0;
    }
}
