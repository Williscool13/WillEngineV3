/*
 * ReBLUR: ClassifyTiles pass. Marks 16x16 tiles as sky (all pixels beyond denoising range).
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import relax_utils;
import shader_functions;

groupshared uint s_isSky;

[shader("compute")]
[numthreads(8, 4, 1)]
void ReblurClassifyTilesMain(uniform ReblurClassifyTilesPushConstant pc,
    uint2 gtid : SV_GroupThreadID,
    uint2 gid : SV_GroupID,
    uint threadIndex : SV_GroupIndex,
    uint3 dtid : SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;

    if (threadIndex == 0) { s_isSky = 0; }
    GroupMemoryBarrierWithGroupSync();

    uint2 pixelPos = gid * 16 + gtid * uint2(2, 4);
    uint isSky = 0;

    for (uint i = 0; i < 2; i++) {
        for (uint j = 0; j < 4; j++) {
            uint2 pos = pixelPos + uint2(i, j);
            uint2 fullPos = pos;
            float z = rdgTexturesFloat[pc.viewZIndex].Load(int3(fullPos, 0)).r;
            float screenDepth = ScreenToViewSpaceDepth(z, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            isSky += !RelaxIsInDenoisingRange(screenDepth, rc.gDenoisingRange) ? 1 : 0;
        }
    }

    InterlockedAdd(s_isSky, isSky);
    GroupMemoryBarrierWithGroupSync();

    if (threadIndex == 0) {
        rdgStorageFloat[pc.tilesOutIndex][gid] = s_isSky > 255 ? 1.0 : 0.0;
    }
}
