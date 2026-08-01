/*
 * NRD (real library) input adaptation: unpacks engine gbuffer/depth into NRD's IN_NORMAL_ROUGHNESS /
 * IN_MV / IN_VIEWZ conventions, and copies radiance+hitT signals between engine intermediates and
 * NRD IO textures (used for both the input feed and the output writeback).
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import common_interop;
import push_constant_interop;
import bindless_declarations;
import shader_functions;

// Mirrors NRD.hlsli _NRD_EncodeNormalRoughness101010 (NRD_NORMAL_ENCODING == R10G10B10A2_UNORM)
float3 NrdEncodeNormalRoughness101010(float3 n, float roughness)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);

    float3 r;
    r.y = n.y * 0.5 + 0.5;
    r.x = n.x * 0.5 + r.y;
    r.y -= n.x * 0.5;

    // Roughness can't be 0 or it would ruin the n.z sign bit
    roughness = max(roughness, 1.5 / 512.0);
    float s = n.z < 0.0 ? -roughness : roughness;
    r.z = s * 0.5 + 0.5;

    return r;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void NrdPrepGuidesMain(uniform NrdPrepGuidesPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (pixel.x >= pc.rectSize.x || pixel.y >= pc.rectSize.y) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));

    float raw = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    float viewZ = ScreenToViewSpaceDepth(raw, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    rdgStorageFloat[pc.outViewZIndex][pixel] = viewZ;

    // mv.xy stays in NDC units (CommonSettings::motionVectorScale = 0.5 converts to UV), mv.z = viewZprev - viewZ
    float2 mvXY = UnpackMotionVectors(gt1.g);
    float mvZ = UnpackViewZDelta(gt1.w);
    rdgStorageFloat4[pc.outMvIndex][pixel] = float4(mvXY, mvZ, 0.0);

    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    float3 worldNormal = normalize(mul(float3x3(scene.invView), UnpackNormalOct16(gt1.r)));
    // materialID 0 in alpha
    rdgStorageFloat4[pc.outNormalRoughnessIndex][pixel] = float4(NrdEncodeNormalRoughness101010(worldNormal, roughness), 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void NrdRadianceCopyMain(uniform NrdRadianceCopyPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (pixel.x >= pc.rectSize.x || pixel.y >= pc.rectSize.y) {
        return;
    }

    rdgStorageFloat4[pc.outDiffIndex][pixel] = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixel, 0));
    rdgStorageFloat4[pc.outSpecIndex][pixel] = rdgTexturesFloat4[pc.specIndex].Load(int3(pixel, 0));
}

// Mirrors NRD.hlsli _NRD_LinearToYCoCg / _NRD_YCoCgToLinear
float3 NrdLinearToYCoCg(float3 color)
{
    float y = dot(color, float3(0.25, 0.5, 0.25));
    float co = dot(color, float3(0.5, 0.0, -0.5));
    float cg = dot(color, float3(-0.25, 0.5, -0.25));
    return float3(y, co, cg);
}

float3 NrdYCoCgToLinear(float3 color)
{
    float t = color.x - color.z;
    float3 r;
    r.y = color.x + color.z;
    r.x = t + color.y;
    r.z = t - color.y;
    return max(r, 0.0);
}

// Mirrors NRD.hlsli _REBLUR_GetHitDistanceNormalization (NRD 4.17 magic-curve form; the port keeps the older hitDistD curve)
float NrdReblurHitDistNormalization(float viewZ, float3 hitDistParams, float roughness)
{
    float smc = 1.0 - exp2(-200.0 * roughness * roughness);
    smc *= sqrt(saturate(roughness));
    return (hitDistParams.x + abs(viewZ) * hitDistParams.y) * lerp(hitDistParams.z, 1.0, smc);
}

// REBLUR_FrontEnd_GetNormHitDist + REBLUR_FrontEnd_PackRadianceAndNormHitDist (sanitize = true)
float4 NrdReblurFrontEndPack(float4 radianceHitT, float viewZ, float3 hitDistParams, float roughness)
{
    float3 radiance = radianceHitT.rgb;
    radiance = (any(isnan(radiance)) || any(isinf(radiance))) ? float3(0.0) : clamp(radiance, 0.0, 65504.0);
    float norm = saturate(radianceHitT.w / NrdReblurHitDistNormalization(viewZ, hitDistParams, roughness));
    norm = (isnan(norm) || isinf(norm)) ? 0.0 : max(norm, 1e-6);
    return float4(NrdLinearToYCoCg(radiance), norm);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void NrdReblurRadiancePackMain(uniform NrdReblurRadiancePackPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (pixel.x >= pc.rectSize.x || pixel.y >= pc.rectSize.y) {
        return;
    }

    float viewZ = rdgTexturesFloat[pc.viewZIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);

    float4 diff = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixel, 0));
    float4 spec = rdgTexturesFloat4[pc.specIndex].Load(int3(pixel, 0));
    rdgStorageFloat4[pc.outDiffIndex][pixel] = NrdReblurFrontEndPack(diff, viewZ, pc.hitDistParams.xyz, 1.0);
    rdgStorageFloat4[pc.outSpecIndex][pixel] = NrdReblurFrontEndPack(spec, viewZ, pc.hitDistParams.xyz, roughness);
}

// REBLUR_BackEnd_UnpackRadianceAndNormHitDist; .w keeps the normalized hitT (unused downstream)
[shader("compute")]
[numthreads(8, 8, 1)]
void NrdReblurOutputCopyMain(uniform NrdRadianceCopyPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (pixel.x >= pc.rectSize.x || pixel.y >= pc.rectSize.y) {
        return;
    }

    float4 diff = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixel, 0));
    float4 spec = rdgTexturesFloat4[pc.specIndex].Load(int3(pixel, 0));
    rdgStorageFloat4[pc.outDiffIndex][pixel] = float4(NrdYCoCgToLinear(diff.rgb), diff.w);
    rdgStorageFloat4[pc.outSpecIndex][pixel] = float4(NrdYCoCgToLinear(spec.rgb), spec.w);
}
