/*
 * ReBLUR: GenerateViewZ pass. Downsamples full-res raw depth to (half-res) linearized viewZ,
 * carried to next frame as reblur_viewz_history for reprojection.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import reblur_interop;
import relax_utils;
import restir_common;
import shader_functions;

[shader("compute")]
[numthreads(8, 8, 1)]
void ReblurGenerateViewZMain(uniform ReblurGenerateViewZPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;
    uint2 pixelPos = dtid.xy;
    if (pixelPos.x >= uint(rc.gRectSize.x) || pixelPos.y >= uint(rc.gRectSize.y)) { return; }

    int2 fullPixel = FetchFullPixel(pc.quadSelectionIndex, int2(pixelPos), pc.pixelScale);
    float raw = rdgTexturesFloat[pc.viewZIndex].Load(int3(fullPixel, 0)).r;
    float viewZ = ScreenToViewSpaceDepth(raw, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    rdgStorageFloat[pc.outViewZIndex][pixelPos] = viewZ;
}
