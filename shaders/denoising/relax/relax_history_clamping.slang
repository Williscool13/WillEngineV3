/*
 * RELAX: HistoryClamping pass. Anti-lag: clamps slow history to fast history bounds.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import relax_interop;
import relax_utils;
import restir_common;
import shader_functions;

static const int HC_BORDER = 2;
static const int HC_GROUP_X = 8;
static const int HC_GROUP_Y = 8;
static const int HC_BUFFER_X = HC_GROUP_X + HC_BORDER * 2;
static const int HC_BUFFER_Y = HC_GROUP_Y + HC_BORDER * 2;

// f16-packed (4 halves per uint2) to halve LDS and lift occupancy; the signals are RGBA16F-resident anyway.
groupshared uint2 s_SpecResponsiveYCoCg[HC_BUFFER_Y][HC_BUFFER_X];
groupshared uint2 s_SpecNoisy_IsValid[HC_BUFFER_Y][HC_BUFFER_X];
groupshared uint2 s_DiffResponsiveYCoCg[HC_BUFFER_Y][HC_BUFFER_X];
groupshared uint2 s_DiffNoisy_IsValid[HC_BUFFER_Y][HC_BUFFER_X];

uint2 HCPack(float4 v)
{
    return uint2(f32tof16(v.x) | (f32tof16(v.y) << 16), f32tof16(v.z) | (f32tof16(v.w) << 16));
}

float4 HCUnpack(uint2 p)
{
    return float4(f16tof32(p.x & 0xFFFFu), f16tof32(p.x >> 16), f16tof32(p.y & 0xFFFFu), f16tof32(p.y >> 16));
}

void HCPreload(uint2 sharedPos, int2 globalPos, RelaxDiffuseSpecularConstants* rc,
    uint viewZIndex, uint specFastIndex, uint diffFastIndex, uint specNoisyIndex, uint diffNoisyIndex)
{
    globalPos = clamp(globalPos, int2(0, 0), rc.gRectSize - 1);
    float viewZ = rdgTexturesFloat[viewZIndex].Load(int3(globalPos, 0)).r;
    float isValid = float(RelaxIsInDenoisingRange(viewZ, rc.gDenoisingRange));

    float4 specFast = rdgTexturesFloat4[specFastIndex].Load(int3(globalPos, 0));
    s_SpecResponsiveYCoCg[sharedPos.y][sharedPos.x] = HCPack(float4(MLRgbToYCoCg(specFast.rgb), specFast.a));
    float3 specNoisy = rdgTexturesFloat4[specNoisyIndex].Load(int3(globalPos, 0)).xyz;
    s_SpecNoisy_IsValid[sharedPos.y][sharedPos.x] = HCPack(float4(specNoisy, isValid));

    float4 diffFast = rdgTexturesFloat4[diffFastIndex].Load(int3(globalPos, 0));
    s_DiffResponsiveYCoCg[sharedPos.y][sharedPos.x] = HCPack(float4(MLRgbToYCoCg(diffFast.rgb), diffFast.a));
    float3 diffNoisy = rdgTexturesFloat4[diffNoisyIndex].Load(int3(globalPos, 0)).xyz;
    s_DiffNoisy_IsValid[sharedPos.y][sharedPos.x] = HCPack(float4(diffNoisy, isValid));
}

[shader("compute")]
[numthreads(HC_GROUP_X, HC_GROUP_Y, 1)]
void RelaxHistoryClampingMain(uniform RelaxHistoryClampingPushConstant pc, int2 threadPos : SV_GroupThreadID, uint2 groupPos : SV_GroupID, int2 pixelPos : SV_DispatchThreadID, uint threadIndex : SV_GroupIndex)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky == 0.0)
    {
        int2 groupBase = pixelPos - threadPos - HC_BORDER;
        uint stageNum = (HC_BUFFER_X * HC_BUFFER_Y + HC_GROUP_X * HC_GROUP_Y - 1) / (HC_GROUP_X * HC_GROUP_Y);
        for (uint stage = 0; stage < stageNum; stage++)
        {
            uint virtualIndex = threadIndex + stage * HC_GROUP_X * HC_GROUP_Y;
            uint2 newId = uint2(virtualIndex % HC_BUFFER_X, virtualIndex / HC_BUFFER_X);
            if (stage == 0 || virtualIndex < HC_BUFFER_X * HC_BUFFER_Y)
                HCPreload(newId, groupBase + int2(newId), rc, pc.viewZIndex, pc.specFastIndex, pc.diffFastIndex, pc.specNoisyIndex, pc.diffNoisyIndex);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y)
        return;

    uint2 sharedCenter = uint2(threadPos) + HC_BORDER;
    float4 specNoisyCenter = HCUnpack(s_SpecNoisy_IsValid[sharedCenter.y][sharedCenter.x]);
    if (specNoisyCenter.w == 0.0)
        return;

    float historyLength = 255.0 * rdgTexturesFloat[pc.historyLengthIndex].Load(int3(pixelPos, 0)).r;

    // Accumulate 5x5 neighborhood statistics
    float3 specResponsiveM1 = 0, specResponsiveM2 = 0;
    float3 diffResponsiveM1 = 0, diffResponsiveM2 = 0;
    float3 specNoisyM1 = 0, diffNoisyM1 = 0;
    float specNoisyM2 = 0, diffNoisyM2 = 0;
    float sum = 0.0;

    [unroll]
    for (int dx = -2; dx <= 2; dx++)
    {
        [unroll]
        for (int dy = -2; dy <= 2; dy++)
        {
            uint2 si = sharedCenter + uint2(dx, dy);
            float4 specNoisyV = HCUnpack(s_SpecNoisy_IsValid[si.y][si.x]);
            float w = specNoisyV.w;
            if (w != 0.0)
            {
                float3 specYCoCg = HCUnpack(s_SpecResponsiveYCoCg[si.y][si.x]).rgb;
                specResponsiveM1 += specYCoCg;
                specResponsiveM2 += specYCoCg * specYCoCg;

                float3 specNoisy = specNoisyV.rgb;
                specNoisyM1 += specNoisy;
                specNoisyM2 += MLLuminance(specNoisy) * MLLuminance(specNoisy);

                float3 diffYCoCg = HCUnpack(s_DiffResponsiveYCoCg[si.y][si.x]).rgb;
                diffResponsiveM1 += diffYCoCg;
                diffResponsiveM2 += diffYCoCg * diffYCoCg;

                float3 diffNoisy = HCUnpack(s_DiffNoisy_IsValid[si.y][si.x]).rgb;
                diffNoisyM1 += diffNoisy;
                diffNoisyM2 += MLLuminance(diffNoisy) * MLLuminance(diffNoisy);

                sum += w;
            }
        }
    }

    specResponsiveM1 /= sum; specResponsiveM2 /= sum;
    diffResponsiveM1 /= sum; diffResponsiveM2 /= sum;
    specNoisyM1 /= sum; specNoisyM2 /= sum;
    diffNoisyM1 /= sum; diffNoisyM2 /= sum;

    float3 specResponsiveSigma = sqrt(max(0.0, specResponsiveM2 - specResponsiveM1 * specResponsiveM1));
    float3 diffResponsiveSigma = sqrt(max(0.0, diffResponsiveM2 - diffResponsiveM1 * diffResponsiveM1));

    float3 specColorMin = specResponsiveM1 - rc.gFastHistoryClampingSigmaScale * specResponsiveSigma;
    float3 specColorMax = specResponsiveM1 + rc.gFastHistoryClampingSigmaScale * specResponsiveSigma;
    float3 diffColorMin = diffResponsiveM1 - rc.gFastHistoryClampingSigmaScale * diffResponsiveSigma;
    float3 diffColorMax = diffResponsiveM1 + rc.gFastHistoryClampingSigmaScale * diffResponsiveSigma;

    // Expand box with center pixel
    float4 specCenterYCoCg = HCUnpack(s_SpecResponsiveYCoCg[sharedCenter.y][sharedCenter.x]);
    float4 diffCenterYCoCg = HCUnpack(s_DiffResponsiveYCoCg[sharedCenter.y][sharedCenter.x]);
    specColorMin = min(specColorMin, specCenterYCoCg.rgb);
    specColorMax = max(specColorMax, specCenterYCoCg.rgb);
    diffColorMin = min(diffColorMin, diffCenterYCoCg.rgb);
    diffColorMax = max(diffColorMax, diffCenterYCoCg.rgb);

    // Clamp
    float4 specHistory = rdgTexturesFloat4[pc.specIndex].Load(int3(pixelPos, 0));
    float3 specYCoCg = MLRgbToYCoCg(specHistory.rgb);
    float3 clampedSpecYCoCg = rc.gSpecMaxFastAccumulatedFrameNum < rc.gSpecMaxAccumulatedFrameNum
        ? clamp(specYCoCg, specColorMin, specColorMax) : specYCoCg;
    float3 clampedSpec = MLYCoCgToRgb(clampedSpecYCoCg);

    float4 diffHistory = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixelPos, 0));
    float3 diffYCoCg = MLRgbToYCoCg(diffHistory.rgb);
    float3 clampedDiffYCoCg = rc.gDiffMaxFastAccumulatedFrameNum < rc.gDiffMaxAccumulatedFrameNum
        ? clamp(diffYCoCg, diffColorMin, diffColorMax) : diffYCoCg;
    float3 clampedDiff = MLYCoCgToRgb(clampedDiffYCoCg);

    // If history length <= gHistoryFixFrameNum, copy from responsive history (rgb only: the responsive .a is a
    // hitT, not a 2nd moment; the moment correction below retargets the slow .a to the new rgb).
    float4 outSpec = float4(clampedSpec, specHistory.a);
    float3 specResponsiveCenter = MLYCoCgToRgb(specCenterYCoCg.rgb);
    float4 outSpecResponsive = float4(specResponsiveCenter, specCenterYCoCg.a);
    if (historyLength <= rc.gHistoryFixFrameNum)
        outSpec.rgb = outSpecResponsive.rgb;

    float4 outDiff = float4(clampedDiff, diffHistory.a);
    float3 diffResponsiveCenter = MLYCoCgToRgb(diffCenterYCoCg.rgb);
    float4 outDiffResponsive = float4(diffResponsiveCenter, 0);
    if (historyLength <= rc.gHistoryFixFrameNum)
        outDiff.rgb = outDiffResponsive.rgb;

    // Clamping factors
    float specClampingFactor = (clampedSpecYCoCg.x - specYCoCg.x) == 0 ? 0.0
        : saturate((clampedSpecYCoCg.x - specYCoCg.x) / (specCenterYCoCg.x - specYCoCg.x));
    if (historyLength <= rc.gHistoryFixFrameNum) specClampingFactor = 1.0;

    float diffClampingFactor = (clampedDiffYCoCg.x - diffYCoCg.x) == 0 ? 0.0
        : saturate((clampedDiffYCoCg.x - diffYCoCg.x) / (diffCenterYCoCg.x - diffYCoCg.x));
    if (historyLength <= rc.gHistoryFixFrameNum) diffClampingFactor = 1.0;

    // History acceleration for specular
    float specHistDiffL = 0.33 * RELAX_ANTILAG_ACCELERATION_AMOUNT_SCALE * rc.gHistoryAccelerationAmount
        * MLLuminance(abs(specResponsiveCenter - specHistory.rgb)) * specClampingFactor;
    if (historyLength <= rc.gHistoryFixFrameNum) specHistDiffL = 0;

    float3 specColorDist = specNoisyM1 - specResponsiveCenter;
    float specColorDistL = MLLuminance(abs(specColorDist));
    float3 specAccel = (specColorDistL == 0) ? 0.0 : specColorDist * specHistDiffL / specColorDistL;
    float specAccelL = MLLuminance(abs(specAccel));
    float specAccelRatio = (specAccelL == 0) ? 0 : specColorDistL / specAccelL;
    if (specAccelRatio < 1.0) specAccel *= specAccelRatio;
    if (specAccelRatio <= 0.0) specAccel = 0;
    outSpec.rgb += specAccel;
    outSpecResponsive.rgb += specAccel;

    // History acceleration for diffuse
    float diffHistDiffL = RELAX_ANTILAG_ACCELERATION_AMOUNT_SCALE * rc.gHistoryAccelerationAmount
        * MLLuminance(abs(diffResponsiveCenter - diffHistory.rgb)) * diffClampingFactor;
    if (historyLength <= rc.gHistoryFixFrameNum) diffHistDiffL = 0;

    float3 diffColorDist = diffNoisyM1 - diffResponsiveCenter;
    float diffColorDistL = MLLuminance(abs(diffColorDist));
    float3 diffAccel = (diffColorDistL == 0) ? 0.0 : diffColorDist * diffHistDiffL / diffColorDistL;
    float diffAccelL = MLLuminance(abs(diffAccel));
    float diffAccelRatio = (diffAccelL == 0) ? 0 : diffColorDistL / diffAccelL;
    if (diffAccelRatio < 1.0) diffAccel *= diffAccelRatio;
    if (diffAccelRatio <= 0.0) diffAccel = 0;
    outDiff.rgb += diffAccel;
    outDiffResponsive.rgb += diffAccel;

    // History reset
    float specL = MLLuminance(specHistory.rgb);
    float specNoisyL = MLLuminance(specNoisyM1);
    float noisySpecTemporalSigma = rc.gHistoryResetTemporalSigmaScale * sqrt(max(0.0, specNoisyM2 - specNoisyL * specNoisyL));
    float noisySpecSpatialSigma = rc.gHistoryResetSpatialSigmaScale * specResponsiveSigma.x;
    float specResetAmount = saturate(0.5 * rc.gHistoryResetAmount * max(0, abs(specL - specNoisyL) - noisySpecSpatialSigma - noisySpecTemporalSigma)
        / (1e-6 + max(specL, specNoisyL) + noisySpecSpatialSigma + noisySpecTemporalSigma));
    float3 specNoisy = specNoisyCenter.rgb;
    outSpec.rgb = lerp(outSpec.rgb, specNoisy, specResetAmount);
    outSpecResponsive.rgb = lerp(outSpecResponsive.rgb, specNoisy, specResetAmount);

    float diffL = MLLuminance(diffHistory.rgb);
    float diffNoisyL = MLLuminance(diffNoisyM1);
    float noisyDiffTemporalSigma = rc.gHistoryResetTemporalSigmaScale * sqrt(max(0.0, diffNoisyM2 - diffNoisyL * diffNoisyL));
    float noisyDiffSpatialSigma = rc.gHistoryResetSpatialSigmaScale * diffResponsiveSigma.x;
    float diffResetAmount = saturate(rc.gHistoryResetAmount * max(0, abs(diffL - diffNoisyL) - noisyDiffSpatialSigma - noisyDiffTemporalSigma)
        / (1e-6 + max(diffL, diffNoisyL) + noisyDiffSpatialSigma + noisyDiffTemporalSigma));
    float3 diffNoisy = HCUnpack(s_DiffNoisy_IsValid[sharedCenter.y][sharedCenter.x]).rgb;
    outDiff.rgb = lerp(outDiff.rgb, diffNoisy, diffResetAmount);
    outDiffResponsive.rgb = lerp(outDiffResponsive.rgb, diffNoisy, diffResetAmount);

    // 2nd moment correction
    float outSpecL = MLLuminance(outSpec.rgb);
    outSpec.a += outSpecL * outSpecL - specL * specL;
    outSpec.a = max(0, outSpec.a);

    float outDiffL = MLLuminance(outDiff.rgb);
    outDiff.a += outDiffL * outDiffL - diffL * diffL;
    outDiff.a = max(0, outDiff.a);

    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = outSpec;
    rdgStorageFloat4[pc.outSpecFastIndex][pixelPos] = outSpecResponsive;
    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = outDiff;
    rdgStorageFloat4[pc.outDiffFastIndex][pixelPos] = outDiffResponsive;
    rdgStorageFloat[pc.outHistoryLengthIndex][pixelPos] = historyLength / 255.0;
}
