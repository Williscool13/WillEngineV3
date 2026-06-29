/*
 * ReBLUR: Front-end pack. Converts the resolve output (raw demodulated RGB + raw hitDist in .w)
 * into the ReBLUR working signal (YCoCg radiance + raw world hit distance in .w).
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import relax_utils;
import reblur_utils;
import restir_common;
import shader_functions;

[shader("compute")]
[numthreads(8, 8, 1)]
void ReblurPackMain(uniform ReblurPackPushConstant pc, int2 pixelPos: SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;
    if (pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y) {
        return;
    }

    float viewZ = RelaxLoadViewZ(pc.viewZIndex, pixelPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    float roughness, metallic;
    UnpackPBR(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0)).b, roughness, metallic);

    float4 diff = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(pixelPos, 0));
    float4 spec = rdgTexturesFloat4[pc.specInputIndex].Load(int3(pixelPos, 0));

    // NRD front-end: YCoCg radiance + NORMALIZED hit distance [0,1] (REBLUR_FrontEnd_GetNormHitDist). Diffuse uses roughness=1.
    float diffNorm = ReblurGetNormHitDist(diff.w, viewZ, rc.gHitDistParams, 1.0);
    float specNorm = ReblurGetNormHitDist(spec.w, viewZ, rc.gHitDistParams, roughness);
    rdgStorageFloat4[pc.diffOutIndex][pixelPos] = float4(MLRgbToYCoCg(clamp(diff.rgb, 0.0, NRD_INF)), diffNorm);
    rdgStorageFloat4[pc.specOutIndex][pixelPos] = float4(MLRgbToYCoCg(clamp(spec.rgb, 0.0, NRD_INF)), specNorm);
}
