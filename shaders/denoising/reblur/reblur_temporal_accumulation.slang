/*
 * ReBLUR: TemporalAccumulation. Faithful port of NRD REBLUR_TemporalAccumulation (DIFFUSE_SPECULAR, YCoCg=1).
 * Surface-motion + virtual-motion reprojection, then per-lobe accumulation: accum speeds come from the
 * carried packed internal data, are confidence-modulated and clamped (surface/virtual confidences,
 * responsive limits, NoHistoryFix variants), and go out as DATA1 (RG8) + DATA2 (occlusion bits, curvature,
 * virtualHistoryAmount, catrom flag). Slow history blends in YCoCg with a hit-distance floor, a firefly
 * suppressor clamps slow + fast luma, and the responsive ("fast") history is single-channel luma. Signal is
 * YCoCg radiance in .xyz + normalized hit distance in .w; reprojection/curvature math works in world hit distance.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import relax_utils;
import reblur_utils;
import reblur_common;
import restir_common;
import shader_functions;

// REBLUR_Config.hlsli (inlined).
static const float REBLUR_FIREFLY_SUPPRESSOR_MAX_RELATIVE_INTENSITY = 38.0;
static const float REBLUR_FIREFLY_SUPPRESSOR_RADIUS_SCALE = 0.1;
static const float REBLUR_FIREFLY_SUPPRESSOR_FAST_RELATIVE_INTENSITY = 4.0;
static const float REBLUR_ALMOST_ZERO_ANGLE = 0.01745240643728351; // cos(89 deg)

static const int TA_BORDER = 1;
static const int TA_GROUP_X = 8;
static const int TA_GROUP_Y = 16;
static const int TA_BUFFER_X = TA_GROUP_X + TA_BORDER * 2;
static const int TA_BUFFER_Y = TA_GROUP_Y + TA_BORDER * 2;

groupshared float4 sharedNormalSpecHitT[TA_BUFFER_Y][TA_BUFFER_X];

void TAPreload(uint2 sharedPos, int2 globalPos, int2 rectSize, uint normalRoughnessIndex, uint specInputIndex, float3x3 viewToWorld)
{
    globalPos = clamp(globalPos, int2(0, 0), rectSize - 1);
    uint4 gt1 = rdgTexturesUInt4[normalRoughnessIndex].Load(int3(globalPos, 0));
    float4 normalSpecHitT = float4(RelaxUnpackNormalAndRoughness(gt1, viewToWorld).xyz, 0);
    float4 specIllum = rdgTexturesFloat4[specInputIndex].Load(int3(globalPos, 0));
    normalSpecHitT.a = specIllum.a;
    sharedNormalSpecHitT[sharedPos.y][sharedPos.x] = normalSpecHitT;
}

float IsReprojectionTapValid(float3 currentWorldPos, float3 previousWorldPos, float3 currentNormal, float disocclusionThreshold)
{
    float d = abs(dot(previousWorldPos - currentWorldPos, currentNormal));
    return d > disocclusionThreshold ? 0.0 : 1.0;
}

/** Surface-motion reprojection. Slow history stays float4 (YCoCg + normalized hitDist); fast history is
 *  single-channel luma. Per-lobe accum speeds come from the carried packed internal data.
 *  Returns 0 = not found, 1 = bilinear, 2 = bicubic. */
float LoadSurfaceMotionBasedPrevData(
    ReblurDiffuseSpecularConstants* rc,
    float3 prevWorldPos, float2 prevUVSMB, float currentLinearZ, float3 currentNormal,
    float currentReflectionHitT, float NoV, float smbParallaxInPixelsMax,
    float disocclusionThreshold,
    out float footprintQuality, out float diffAccumSpeed, out float smbSpecAccumSpeed,
    out float fbits, out bool smbAllowCatRom, out float3 prevNormalFlatOut,
    out float4 prevSpecIllum, out float prevSpecResponsiveLuma, out float prevReflectionHitT,
    out float4 prevDiffIllum, out float prevDiffResponsiveLuma,
    uint historySpecIndex, uint historyDiffIndex, uint historySpecFastIndex, uint historyDiffFastIndex,
    uint prevViewZIndex, uint prevInternalDataIndex, uint prevSpecHitDistIndex, uint prevNormalRoughnessIndex)
{
    float2 prevPixelPosFloat = prevUVSMB * rc.gRectSizePrev;
    int2 bilinearOrigin = int2(floor(prevPixelPosFloat - 0.5));
    float2 bilinearWeights = frac(prevPixelPosFloat - 0.5);

    float2 go00 = (float2(bilinearOrigin) + float2(0, 0)) * rc.gResourceSizeInvPrev;
    float2 go10 = (float2(bilinearOrigin) + float2(2, 0)) * rc.gResourceSizeInvPrev;
    float2 go01 = (float2(bilinearOrigin) + float2(0, 2)) * rc.gResourceSizeInvPrev;
    float2 go11 = (float2(bilinearOrigin) + float2(2, 2)) * rc.gResourceSizeInvPrev;

    float4 prevVZ00 = RelaxGatherLinearViewZ(prevViewZIndex, go00).wzxy;
    float4 prevVZ10 = RelaxGatherLinearViewZ(prevViewZIndex, go10).wzxy;
    float4 prevVZ01 = RelaxGatherLinearViewZ(prevViewZIndex, go01).wzxy;
    float4 prevVZ11 = RelaxGatherLinearViewZ(prevViewZIndex, go11).wzxy;

    float pixelSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ);
    float frustumSize = pixelSize * min(float(rc.gRectSize.x), float(rc.gRectSize.y));
    float slopeScale = 1.0 / lerp(lerp(0.05, 1.0, NoV), 1.0, saturate(smbParallaxInPixelsMax / 30.0));
    float4 smbDisocclusionThreshold = saturate(disocclusionThreshold * slopeScale) * frustumSize;
    smbDisocclusionThreshold *= RelaxIsInScreenBilinear(float2(bilinearOrigin), rc.gRectSizePrev);

    float smallParallax = MLLinearStep(0.25, 0.0, smbParallaxInPixelsMax);
    float thresholdAngle = REBLUR_ALMOST_ZERO_ANGLE - 0.25 * smallParallax;
    int3 pn = int3(bilinearOrigin, 0);
    float3 pn00 = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[prevNormalRoughnessIndex].Load(pn)).xyz;
    float3 pn10 = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[prevNormalRoughnessIndex].Load(pn + int3(1, 0, 0))).xyz;
    float3 pn01 = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[prevNormalRoughnessIndex].Load(pn + int3(0, 1, 0))).xyz;
    float3 pn11 = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[prevNormalRoughnessIndex].Load(pn + int3(1, 1, 0))).xyz;
    float4 smbNoN2x2 = float4(dot(pn00, currentNormal), dot(pn10, currentNormal), dot(pn01, currentNormal), dot(pn11, currentNormal));
    smbDisocclusionThreshold *= float4(smbNoN2x2 > thresholdAngle);
    smbDisocclusionThreshold -= NRD_EPS;

    float3 prevViewPos = MLAffineTransform(rc.gWorldToViewPrev, prevWorldPos);

    float3 pd0 = abs(prevVZ00.yzw - prevViewPos.zzz);
    float3 pd1 = abs(prevVZ10.xzw - prevViewPos.zzz);
    float3 pd2 = abs(prevVZ01.xyw - prevViewPos.zzz);
    float3 pd3 = abs(prevVZ11.xyz - prevViewPos.zzz);

    float3 tv0 = step(pd0, smbDisocclusionThreshold.x) * float3(prevVZ00.yzw < rc.gDenoisingRange);
    float3 tv1 = step(pd1, smbDisocclusionThreshold.y) * float3(prevVZ10.xzw < rc.gDenoisingRange);
    float3 tv2 = step(pd2, smbDisocclusionThreshold.z) * float3(prevVZ01.xyw < rc.gDenoisingRange);
    float3 tv3 = step(pd3, smbDisocclusionThreshold.w) * float3(prevVZ11.xyz < rc.gDenoisingRange);

    float bicubicFootprintValid = dot(tv0 + tv1 + tv2 + tv3, 1.0) > 11.5 ? 1.0 : 0.0;
    float4 bilinearTapsValid = float4(tv0.z, tv1.y, tv2.y, tv3.x);

    float2 prevNrUv = (float2(bilinearOrigin) + 1.0) * rc.gResourceSizeInvPrev;
    float3 prevNormalFlat = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[prevNormalRoughnessIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], prevNrUv, 0)).xyz;
    prevNormalFlatOut = prevNormalFlat;

    MLBilinear bilinear;
    bilinear.origin = float2(bilinearOrigin);
    bilinear.weights = bilinearWeights;
    float4 bilinearCustomWeights = MLGetBilinearCustomWeights(bilinear, bilinearTapsValid);

    bool useBicubic = bicubicFootprintValid > 0;
    BicubicFilterDiffSpec(prevPixelPosFloat, rc.gResourceSizeInvPrev,
        bilinearCustomWeights, useBicubic,
        historySpecIndex, prevSpecIllum,
        historyDiffIndex, prevDiffIllum);

    // Fast history uses the same occlusion-aware custom weights as slow history so disoccluded corner
    // taps don't bleed across silhouettes into the fast clamp box.
    float2 gatherOrigin = (float2(bilinearOrigin) + 1.0) * rc.gResourceSizeInvPrev;
    float4 prevSpecFasts = rdgTexturesFloat[historySpecFastIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherOrigin).wzxy;
    prevSpecResponsiveLuma = max(RelaxBilinearWeightedFloat(prevSpecFasts.x, prevSpecFasts.y, prevSpecFasts.z, prevSpecFasts.w, bilinearCustomWeights), 0.0);
    float4 prevDiffFasts = rdgTexturesFloat[historyDiffFastIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherOrigin).wzxy;
    prevDiffResponsiveLuma = max(RelaxBilinearWeightedFloat(prevDiffFasts.x, prevDiffFasts.y, prevDiffFasts.z, prevDiffFasts.w, bilinearCustomWeights), 0.0);
    // Per-lobe accumulated frame counts (already incremented at pack time).
    uint4 prevInternalDatas = rdgTexturesUInt[prevInternalDataIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherOrigin).wzxy;
    float3 id00 = ReblurUnpackInternalData(prevInternalDatas.x);
    float3 id10 = ReblurUnpackInternalData(prevInternalDatas.y);
    float3 id01 = ReblurUnpackInternalData(prevInternalDatas.z);
    float3 id11 = ReblurUnpackInternalData(prevInternalDatas.w);
    diffAccumSpeed = RelaxBilinearWeightedFloat(id00.x, id10.x, id01.x, id11.x, bilinearCustomWeights);
    smbSpecAccumSpeed = RelaxBilinearWeightedFloat(id00.y, id10.y, id01.y, id11.y, bilinearCustomWeights);

    float4 prevHitTs = rdgTexturesFloat[prevSpecHitDistIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherOrigin).wzxy;
    prevReflectionHitT = max(0.001, RelaxBilinearWeightedFloat(prevHitTs.x, prevHitTs.y, prevHitTs.z, prevHitTs.w, bilinearCustomWeights));

    fbits = bilinearTapsValid.x * 1.0 + bilinearTapsValid.y * 2.0 + bilinearTapsValid.z * 4.0 + bilinearTapsValid.w * 8.0;
    smbAllowCatRom = useBicubic;

    float reprojectionFound = bicubicFootprintValid > 0 ? 2.0 : 1.0;
    // sqrt of the bilinear-filtered occlusion (dot(customWeights, 1) is that filter).
    footprintQuality = sqrt(saturate(dot(bilinearCustomWeights, 1.0)));

    if (dot(bilinearTapsValid, 1.0) == 0.0) { reprojectionFound = 0; footprintQuality = 0; }
    return reprojectionFound;
}

/** Virtual-motion reprojection for specular. Slow history stays float4; fast history is single-channel luma.
 *  Also returns the vmb spec accum speed, footprint quality, and 4 occlusion bits (packed <<4 into DATA2
 *  by the caller). Returns 1 = found, 0 = not found. */
float LoadVirtualMotionBasedPrevData(
    ReblurDiffuseSpecularConstants* rc,
    float3 currentWorldPos, float3 currentNormal, float currentLinearZ,
    float3 virtualWorldPos, bool surfaceBicubicValid, float2 prevUVSMB, float disocclusionThreshold,
    out float4 prevSpecIllum, out float prevSpecResponsiveLuma,
    out float3 prevNormal, out float prevRoughness, out float prevReflectionHitT, out float2 prevUVVMB,
    out float vmbSpecAccumSpeed, out float vmbFootprintQuality, out float vmbFbits,
    uint historySpecIndex, uint historySpecFastIndex, uint prevViewZIndex, uint prevSpecHitDistIndex,
    uint prevNormalRoughnessIndex, uint prevInternalDataIndex)
{
    float4 prevVirtualClip = mul(rc.gWorldToClipPrev, float4(virtualWorldPos, 1.0));
    prevVirtualClip.xy /= prevVirtualClip.w;
    prevUVVMB = prevVirtualClip.xy * float2(0.5, 0.5) + 0.5;

    float2 prevVirtualPixelPosFloat = prevUVVMB * rc.gRectSizePrev;
    int2 bilinearOrigin = int2(floor(prevVirtualPixelPosFloat - 0.5));
    float2 bilinearWeights = frac(prevVirtualPixelPosFloat - 0.5);
    float2 gatherOrigin = (float2(bilinearOrigin) + 1.0) * rc.gResourceSizeInvPrev;

    float3 currWorldPosNoDelta = currentWorldPos - rc.gCameraDelta.xyz;

    float4 vmbDisocclusionThreshold = disocclusionThreshold * (rc.gOrthoMode == 0 ? currentLinearZ : 1.0);
    vmbDisocclusionThreshold *= RelaxIsInScreenBilinear(float2(bilinearOrigin), rc.gRectSizePrev);
    vmbDisocclusionThreshold -= NRD_EPS;

    float4 prevVZs = RelaxGatherLinearViewZ(prevViewZIndex, gatherOrigin).wzxy;
    float3 tap00 = RelaxGetPreviousWorldPosFromPixelPos(bilinearOrigin + int2(0,0), prevVZs.x, rc.gRectSizePrev, rc.gPrevFrustumForward, rc.gPrevFrustumRight, rc.gPrevFrustumUp, rc.gOrthoMode);
    float3 tap10 = RelaxGetPreviousWorldPosFromPixelPos(bilinearOrigin + int2(1,0), prevVZs.y, rc.gRectSizePrev, rc.gPrevFrustumForward, rc.gPrevFrustumRight, rc.gPrevFrustumUp, rc.gOrthoMode);
    float3 tap01 = RelaxGetPreviousWorldPosFromPixelPos(bilinearOrigin + int2(0,1), prevVZs.z, rc.gRectSizePrev, rc.gPrevFrustumForward, rc.gPrevFrustumRight, rc.gPrevFrustumUp, rc.gOrthoMode);
    float3 tap11 = RelaxGetPreviousWorldPosFromPixelPos(bilinearOrigin + int2(1,1), prevVZs.w, rc.gRectSizePrev, rc.gPrevFrustumForward, rc.gPrevFrustumRight, rc.gPrevFrustumUp, rc.gOrthoMode);
    float4 bilinearTapsValid = float4(
        IsReprojectionTapValid(currWorldPosNoDelta, tap00, currentNormal, vmbDisocclusionThreshold.x),
        IsReprojectionTapValid(currWorldPosNoDelta, tap10, currentNormal, vmbDisocclusionThreshold.y),
        IsReprojectionTapValid(currWorldPosNoDelta, tap01, currentNormal, vmbDisocclusionThreshold.z),
        IsReprojectionTapValid(currWorldPosNoDelta, tap11, currentNormal, vmbDisocclusionThreshold.w));

    prevSpecIllum = 0; prevSpecResponsiveLuma = 0;
    prevNormal = currentNormal; prevRoughness = 0;
    prevReflectionHitT = rc.gDenoisingRange;

    vmbFbits = bilinearTapsValid.x * 16.0 + bilinearTapsValid.y * 32.0 + bilinearTapsValid.z * 64.0 + bilinearTapsValid.w * 128.0;
    vmbSpecAccumSpeed = 0.0;
    vmbFootprintQuality = 0.0;

    if (dot(bilinearTapsValid, 1.0) > 0.0)
    {
        MLBilinear bilinear;
        bilinear.origin = float2(bilinearOrigin);
        bilinear.weights = bilinearWeights;
        float4 bilinearCustomWeights = MLGetBilinearCustomWeights(bilinear, bilinearTapsValid);
        bool useBicubic = (surfaceBicubicValid) && all(bilinearTapsValid == 1);

        // Per-lobe spec accum speed at the virtual footprint.
        uint4 vmbInternalDatas = rdgTexturesUInt[prevInternalDataIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherOrigin).wzxy;
        vmbSpecAccumSpeed = RelaxBilinearWeightedFloat(
            ReblurUnpackInternalData(vmbInternalDatas.x).y, ReblurUnpackInternalData(vmbInternalDatas.y).y,
            ReblurUnpackInternalData(vmbInternalDatas.z).y, ReblurUnpackInternalData(vmbInternalDatas.w).y,
            bilinearCustomWeights);
        vmbFootprintQuality = sqrt(saturate(dot(bilinearCustomWeights, 1.0)));

        BicubicFilterSingle(prevVirtualPixelPosFloat, rc.gResourceSizeInvPrev, bilinearCustomWeights, useBicubic, historySpecIndex, prevSpecIllum);

        float2 uvVMB = prevUVVMB * rc.gRectSizePrev * rc.gResourceSizeInvPrev;
        SamplerState lin = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];
        float4 vmbSpecFasts = rdgTexturesFloat[historySpecFastIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherOrigin).wzxy;
        prevSpecResponsiveLuma = max(RelaxBilinearWeightedFloat(vmbSpecFasts.x, vmbSpecFasts.y, vmbSpecFasts.z, vmbSpecFasts.w, bilinearCustomWeights), 0.0);

        prevReflectionHitT = max(0.001, rdgTexturesFloat[prevSpecHitDistIndex].SampleLevel(lin, uvVMB, 0).r);

        float4 prevNR = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[prevNormalRoughnessIndex].SampleLevel(lin, uvVMB, 0));
        prevNormal = prevNR.xyz;
        prevRoughness = prevNR.w;
    }

    return dot(bilinearTapsValid, 1.0) == 4.0 ? 1.0 : 0.0;
}

[shader("compute")]
[numthreads(TA_GROUP_X, TA_GROUP_Y, 1)]
void ReblurTemporalAccumulationMain(uniform ReblurTemporalAccumulationPushConstant pc, int2 threadPos : SV_GroupThreadID, uint2 groupPos : SV_GroupID, int2 pixelPos : SV_DispatchThreadID, uint threadIndex : SV_GroupIndex)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;
    float3x3 viewToWorld = (float3x3)rc.gViewToWorld;

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky == 0.0)
    {
        int2 groupBase = pixelPos - threadPos - TA_BORDER;
        uint stageNum = (TA_BUFFER_X * TA_BUFFER_Y + TA_GROUP_X * TA_GROUP_Y - 1) / (TA_GROUP_X * TA_GROUP_Y);
        for (uint stage = 0; stage < stageNum; stage++)
        {
            uint virtualIndex = threadIndex + stage * TA_GROUP_X * TA_GROUP_Y;
            uint2 newId = uint2(virtualIndex % TA_BUFFER_X, virtualIndex / TA_BUFFER_X);
            if (stage == 0 || virtualIndex < TA_BUFFER_X * TA_BUFFER_Y)
                TAPreload(newId, groupBase + int2(newId), rc.gRectSize, pc.normalRoughnessIndex, pc.specInputIndex, viewToWorld);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y)
        return;

    float currentLinearZ = RelaxLoadViewZ(pc.viewZIndex, pixelPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(currentLinearZ, rc.gDenoisingRange))
        return;

    // ReSTIR-provided history confidence (1 = full trust). A single signal drives both lobes.
    float historyConfidence = 1.0;
    if (pc.confidenceIndex != ~0u) {
        historyConfidence = saturate(rdgTexturesFloat[pc.confidenceIndex].Load(int3(pixelPos, 0)).r);
    }

    uint2 sharedMemoryIndex = uint2(threadPos) + TA_BORDER;

    float4 currentNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0)), viewToWorld);
    float3 currentNormal = currentNR.xyz;
    float currentRoughness = currentNR.w;

    float3 currentWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, currentLinearZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    float3 currentViewVec = (rc.gOrthoMode == 0) ? currentWorldPos : currentLinearZ * normalize(rc.gFrustumForward.xyz);
    float3 V = -normalize(currentViewVec);
    float NoV = abs(dot(currentNormal, V));

    float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;
    uint4 gt1 = rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0));
    float2 mvXY = UnpackMotionVectors(gt1.g) * rc.gMvScale.xy;
    float mvZ = UnpackViewZDelta(gt1.w);
    float3 mv = float3(mvXY, mvZ);
    float3 prevWorldPos = currentWorldPos;
    float2 prevUVSMB = pixelUv + mv.xy;

    if (rc.gMvScale.w == 0.0)
    {
        if (rc.gMvScale.z == 0.0)
        {
            mv.z = MLAffineTransform(rc.gWorldToViewPrev, currentWorldPos).z - currentLinearZ;
        }
        prevWorldPos = RelaxGetCurrentWorldPosFromClipXY(prevUVSMB * 2.0 - 1.0, currentLinearZ + mv.z, rc.gPrevFrustumForward, rc.gPrevFrustumRight, rc.gPrevFrustumUp, rc.gOrthoMode) + rc.gCameraDelta.xyz;
    }
    else
    {
        prevWorldPos += mv;
        prevUVSMB = MLGetScreenUv(rc.gWorldToClipPrev, prevWorldPos);
    }

    // Inputs: YCoCg radiance + NORMALIZED hit distance in .w.
    float4 specularIllumination = rdgTexturesFloat4[pc.specInputIndex].Load(int3(pixelPos, 0));
    float4 diffuseIllumination = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(pixelPos, 0));

    // World hit distance for the tracking / reprojection math (signal .w is normalized).
    float specHitDistWorld = ReblurDenormHitDist(specularIllumination.w, currentLinearZ, rc.gHitDistParams, currentRoughness);

    float hitTM1 = sharedNormalSpecHitT[sharedMemoryIndex.y][sharedMemoryIndex.x].a;
    float minHitDist3x3 = hitTM1 == 0.0 ? NRD_INF : hitTM1;
    // Normal average over the 2x2 quad only; min hit distance over the full 3x3.
    float3 currentNormalAveraged = currentNormal;

    [unroll]
    for (int i = -1; i <= 1; i++)
    {
        [unroll]
        for (int j = -1; j <= 1; j++)
        {
            if (i == 0 && j == 0) continue;
            float4 nsh = sharedNormalSpecHitT[sharedMemoryIndex.y + j][sharedMemoryIndex.x + i];
            minHitDist3x3 = min(minHitDist3x3, nsh.a == 0.0 ? NRD_INF : nsh.a);
            if (i >= 0 && j >= 0)
            {
                currentNormalAveraged += nsh.rgb;
            }
        }
    }
    currentNormalAveraged /= 4.0;

    float currentRoughnessModified = MLGetModifiedRoughnessFromNormalVariance(currentRoughness, currentNormalAveraged);

    float smbParallaxInPixels1 = RelaxComputeParallaxInPixels(prevWorldPos + rc.gCameraDelta.xyz, rc.gOrthoMode == 0.0 ? prevUVSMB : pixelUv, rc.gWorldToClipPrev, float2(rc.gRectSize));
    float smbParallaxInPixels2 = RelaxComputeParallaxInPixels(prevWorldPos - rc.gCameraDelta.xyz, rc.gOrthoMode == 0.0 ? pixelUv : prevUVSMB, rc.gWorldToClip, float2(rc.gRectSize));
    float smbParallaxInPixelsMax = max(smbParallaxInPixels1, smbParallaxInPixels2);
    float smbParallaxInPixelsMin = min(smbParallaxInPixels1, smbParallaxInPixels2);

    float disocclusionThreshold = rc.gDisocclusionThreshold;

    float footprintQuality;
    float4 prevSpecIllumSMB, prevDiffIllumSMB;
    float prevSpecResponsiveSMB, prevDiffResponsiveSMB;
    float prevReflectionHitTSMB;
    float diffAccumSpeed, smbSpecAccumSpeed;
    float fbits;
    bool smbAllowCatRom;
    float3 smbPrevNormalFlat;

    float SMBReprojectionFound = LoadSurfaceMotionBasedPrevData(
        rc, prevWorldPos, prevUVSMB, currentLinearZ, normalize(currentNormalAveraged),
        specularIllumination.a, NoV, smbParallaxInPixelsMax, disocclusionThreshold,
        footprintQuality, diffAccumSpeed, smbSpecAccumSpeed,
        fbits, smbAllowCatRom, smbPrevNormalFlat,
        prevSpecIllumSMB, prevSpecResponsiveSMB, prevReflectionHitTSMB,
        prevDiffIllumSMB, prevDiffResponsiveSMB,
        pc.historySpecIndex, pc.historyDiffIndex, pc.historySpecFastIndex, pc.historyDiffFastIndex,
        pc.prevViewZIndex, pc.prevInternalDataIndex, pc.prevSpecHitDistIndex, pc.prevNormalRoughnessIndex);

    float3 Vprev = (rc.gOrthoMode == 0) ? -normalize(prevWorldPos - rc.gCameraDelta.xyz) : -normalize(rc.gPrevFrustumForward.xyz);
    float NoVprev = abs(dot(currentNormal, Vprev));
    float sizeQuality = (NoVprev + 1e-3) / (NoV + 1e-3);
    sizeQuality *= sizeQuality;
    footprintQuality *= lerp(0.1, 1.0, saturate(sizeQuality + abs(rc.gOrthoMode)));

    // --- Per-lobe accumulation speeds; confidence applied non-linearly. ---
    float diffHistoryConfidence = min(footprintQuality, historyConfidence);
    diffAccumSpeed *= lerp(diffHistoryConfidence, 1.0, 1.0 / (1.0 + diffAccumSpeed));

    float smbSpecModConfidence = min(footprintQuality, historyConfidence);
    smbSpecAccumSpeed *= lerp(smbSpecModConfidence, 1.0, 1.0 / (1.0 + smbSpecAccumSpeed));

    float responsiveFactor = ReblurRemapRoughnessToResponsiveFactor(currentRoughness, rc.gResponsiveAccumulationInvRoughnessThreshold);

    // Specular curvature estimation (world hit distance).
    float hitDistNorm = minHitDist3x3 == NRD_INF ? 0.0 : minHitDist3x3;
    float hitDist = ReblurDenormHitDist(hitDistNorm, currentLinearZ, rc.gHitDistParams, currentRoughness);
    float curvature = 0.0;
    {
        float2 uvForZeroParallax = rc.gOrthoMode == 0.0 ? prevUVSMB : pixelUv;
        float2 deltaUv = uvForZeroParallax - MLGetScreenUv(rc.gWorldToClipPrev, prevWorldPos + rc.gCameraDelta.xyz);
        deltaUv *= float2(rc.gRectSize);
        deltaUv /= max(smbParallaxInPixels1, 1.0 / 256.0);

        float3 x10;
        float3 n10;
        {
            float3 xn = RelaxGetCurrentWorldPosFromClipXY((pixelUv + float2(1, 0) * rc.gRectSizeInv) * 2.0 - 1.0, 1.0, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
            float3 vn = rc.gOrthoMode == 0.0 ? normalize(-xn) : rc.gFrustumForward.xyz;
            float3 on = rc.gOrthoMode == 0.0 ? float3(0,0,0) : xn;
            x10 = on + vn * dot(currentWorldPos - on, currentNormal) / dot(currentNormal, vn);
            n10 = sharedNormalSpecHitT[sharedMemoryIndex.y][sharedMemoryIndex.x + 1].xyz;
        }

        float3 x01;
        float3 n01;
        {
            float3 xn = RelaxGetCurrentWorldPosFromClipXY((pixelUv + float2(0, 1) * rc.gRectSizeInv) * 2.0 - 1.0, 1.0, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
            float3 vn = rc.gOrthoMode == 0.0 ? normalize(-xn) : rc.gFrustumForward.xyz;
            float3 on = rc.gOrthoMode == 0.0 ? float3(0,0,0) : xn;
            x01 = on + vn * dot(currentWorldPos - on, currentNormal) / dot(currentNormal, vn);
            n01 = sharedNormalSpecHitT[sharedMemoryIndex.y + 1][sharedMemoryIndex.x].xyz;
        }

        float2 w = abs(deltaUv) + 1.0 / 256.0;
        w /= w.x + w.y;
        float3 xe = x10 * w.x + x01 * w.y;
        float3 ne = normalize(n10 * w.x + n01 * w.y);

        float dither = MLBayer4x4(uint2(pixelPos), rc.gFrameIndex);
        float edgeFix = 1.0 - BRDFPow5(NoV);
        float deltaUvLenFixed = smbParallaxInPixelsMin;
        deltaUvLenFixed *= 1.0 + edgeFix * (1.0 + rc.gFramerateScale * dither);
        float2 motionUvHigh = pixelUv + deltaUvLenFixed * deltaUv * rc.gRectSizeInv;
        motionUvHigh = (floor(motionUvHigh * float2(rc.gRectSize)) + 0.5) * rc.gRectSizeInv;

        if (deltaUvLenFixed > 1.0 && RelaxIsInScreenNearest(motionUvHigh) == 1.0)
        {
            int2 highPos = int2(motionUvHigh * float2(rc.gRectSize));
            float zHigh = RelaxLoadViewZ(pc.viewZIndex, highPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            float3 xHigh = RelaxGetCurrentWorldPosFromClipXY(motionUvHigh * 2.0 - 1.0, zHigh, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
            float3 nHigh = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(highPos, 0)), viewToWorld).xyz;

            float pixSz = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ);
            float fSize = min(float(rc.gRectSize.x), float(rc.gRectSize.y)) * pixSz;
            float2 geomWP = RelaxGetGeometryWeightParams(NRD_CURVATURE_HIGH_PARALLAX_DISOCCLUSION_THRESHOLD, fSize, currentWorldPos, currentNormal);
            float NoXHigh = dot(currentNormal, xHigh);
            float geomW = RelaxApplyGeometryWeightLast(1.0, zHigh, NoXHigh, geomWP, rc.gDenoisingRange);
            if (geomW > 0.5) { ne = nHigh; xe = xHigh; }
        }

        float3 edge = xe - currentWorldPos;
        float edgeLenSq = MLLengthSquared(edge);
        curvature = dot(ne - currentNormal, edge) / edgeLenSq;

        if (curvature < 0)
        {
            float2 uv1 = MLGetScreenUv(rc.gWorldToClipPrev, RelaxGetXvirtual(hitDist, curvature, currentWorldPos, currentWorldPos, currentNormal, V, currentRoughness));
            float2 uv2 = MLGetScreenUv(rc.gWorldToClipPrev, currentWorldPos);
            float a = length((uv1 - uv2) * float2(rc.gRectSize));
            curvature *= float(a < NRD_MAX_ALLOWED_VIRTUAL_MOTION_ACCELERATION * smbParallaxInPixelsMax + rc.gRectSizeInv.x);
        }
    }

    float3 virtualWorldPos = RelaxGetXvirtual(hitDist, curvature, currentWorldPos, prevWorldPos, currentNormal, V, currentRoughness);

    float4 prevSpecIllumVMB;
    float prevSpecResponsiveVMB;
    float3 prevNormalVMB;
    float prevRoughnessVMB, prevReflectionHitTVMB;
    float2 prevUVVMB;

    float vmbSpecAccumSpeed, vmbFootprintQuality, vmbFbits;
    float VMBReprojectionFound = LoadVirtualMotionBasedPrevData(
        rc, currentWorldPos, currentNormal, currentLinearZ,
        virtualWorldPos, SMBReprojectionFound == 2.0, prevUVSMB, disocclusionThreshold,
        prevSpecIllumVMB, prevSpecResponsiveVMB,
        prevNormalVMB, prevRoughnessVMB, prevReflectionHitTVMB, prevUVVMB,
        vmbSpecAccumSpeed, vmbFootprintQuality, vmbFbits,
        pc.historySpecIndex, pc.historySpecFastIndex, pc.prevViewZIndex, pc.prevSpecHitDistIndex,
        pc.prevNormalRoughnessIndex, pc.prevInternalDataIndex);

    float vmbSpecModConfidence = min(vmbFootprintQuality, historyConfidence);
    vmbSpecAccumSpeed *= lerp(vmbSpecModConfidence, 1.0, 1.0 / (1.0 + vmbSpecAccumSpeed));
    fbits += vmbFbits;

    if (rc.gResetHistory != 0) { diffAccumSpeed = 0.0; smbSpecAccumSpeed = 0.0; vmbSpecAccumSpeed = 0.0; }

    // --- Virtual history amount (kept virtual-motion logic) ---
    float4 D = MLGetSpecularDominantDirection(currentNormal, V, currentRoughnessModified);
    float virtualHistoryAmount = VMBReprojectionFound * D.w;
    virtualHistoryAmount *= (rc.gOrthoMode == 0) ? 1.0 : 0.75;
    virtualHistoryAmount *= float(dot(prevNormalVMB, currentNormalAveraged) > 0.0);

    float2 uvDiff = prevUVVMB - prevUVSMB;
    float uvDiffLengthInPixels = length(uvDiff * float2(rc.gRectSize));

    float tanCurvature = abs(curvature * RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ));
    tanCurvature *= max(uvDiffLengthInPixels / max(NoV, 0.01), 1.0);
    tanCurvature *= 2.0; // unexplained NRD fudge factor that works
    float curvatureAngle = atan(tanCurvature);

    // Lobe tolerance tightens as the virtual history accumulates.
    float lobeHalfAngle = max(atan(MLGetSpecularLobeTanHalfAngle(currentRoughnessModified, 0.75 / (1.0 + vmbSpecAccumSpeed))), RELAX_NORMAL_ULP);
    float normalWeight = RelaxGetEncodingAwareNormalWeight(currentNormal, prevNormalVMB, lobeHalfAngle, curvatureAngle, RELAX_NORMAL_ULP);
    virtualHistoryAmount *= lerp(1.0 - saturate(uvDiffLengthInPixels), 1.0, normalWeight);

    float2 relaxedRoughnessWeightParams = RelaxGetRelaxedRoughnessWeightParams(currentRoughness * currentRoughness, rc.gRoughnessFraction);
    float virtualRoughnessWeight = RelaxComputeWeight(prevRoughnessVMB * prevRoughnessVMB, relaxedRoughnessWeightParams.x, relaxedRoughnessWeightParams.y);
    virtualRoughnessWeight = lerp(1.0 - saturate(uvDiffLengthInPixels), 1.0, virtualRoughnessWeight);
    virtualHistoryAmount *= (rc.gOrthoMode == 0) ? virtualRoughnessWeight : 1.0;
    float specVMBConfidence = virtualRoughnessWeight * 0.9 + 0.1;

    float2 uvDiffNorm = uvDiff * MLRsqrt(MLLengthSquared(uvDiff));
    uvDiffNorm /= rc.gRectSizePrev;
    uvDiffNorm *= saturate(uvDiffLengthInPixels / 0.1) + uvDiffLengthInPixels / 2.0;

    float2 prevRSScale = rc.gRectSizePrev * rc.gResourceSizeInvPrev;
    float2 backUV1 = prevUVVMB + 1.0 * uvDiffNorm;
    float2 backUV2 = prevUVVMB + 2.0 * uvDiffNorm;
    float4 backNR1 = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[pc.prevNormalRoughnessIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], backUV1 * prevRSScale, 0));
    float4 backNR2 = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[pc.prevNormalRoughnessIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], backUV2 * prevRSScale, 0));

    float prevPrevNormalWeight = RelaxIsInScreenNearest(backUV1) == 1.0
        ? RelaxGetEncodingAwareNormalWeight(prevNormalVMB, backNR1.rgb, lobeHalfAngle, curvatureAngle * 2.0, RELAX_NORMAL_ULP) : 1.0;
    prevPrevNormalWeight *= RelaxIsInScreenNearest(backUV2) == 1.0
        ? RelaxGetEncodingAwareNormalWeight(prevNormalVMB, backNR2.rgb, lobeHalfAngle, curvatureAngle * 3.0, RELAX_NORMAL_ULP) : 1.0;
    virtualHistoryAmount *= 0.33 + 0.67 * prevPrevNormalWeight;
    specVMBConfidence *= 0.33 + 0.67 * prevPrevNormalWeight;

    float rw = RelaxComputeWeight(backNR1.w * backNR1.w, relaxedRoughnessWeightParams.x, relaxedRoughnessWeightParams.y);
    rw *= RelaxComputeWeight(backNR2.w * backNR2.w, relaxedRoughnessWeightParams.x, relaxedRoughnessWeightParams.y);
    virtualHistoryAmount *= (rc.gOrthoMode == 0) ? rw * 0.9 + 0.1 : 1.0;

    // --- Specular hit-distance confidence (kept; world hit distances) ---
    float SMC = RelaxGetSpecMagicCurve(currentRoughnessModified);
    float hitDistC = lerp(specHitDistWorld, prevReflectionHitTSMB, SMC);
    float hitDist1 = hitDistC / (2.0 * curvature * hitDistC + 1.0);
    float hitDist2 = prevReflectionHitTVMB / (2.0 * curvature * prevReflectionHitTVMB + 1.0);
    float maxDistHD = max(hitDist1, hitDist2);
    float dHitT = abs(hitDist1 - hitDist2);
    float hitDistConfidence = 1.0 - saturate(lerp(20.0, 0.0, SMC) * dHitT / (currentLinearZ + maxDistHD));
    hitDistConfidence = lerp(hitDistConfidence, 1.0, SMC);

    float hitDistForTrackingPrev = prevReflectionHitTVMB;
    float3 prevVirtualWorldPos = RelaxGetXvirtual(hitDistForTrackingPrev, curvature, currentWorldPos, prevWorldPos, currentNormal, V, currentRoughness);
    float2 prevUVVMBTest = MLGetScreenUv(rc.gWorldToClipPrev, prevVirtualWorldPos);
    float lobeTanHalfAngle = max(RelaxGetSpecLobeTanHalfAngle(currentRoughness, 0.6), 0.5 * rc.gRectSizeInv.x);
    float unproj1 = min(hitDist, hitDistForTrackingPrev) / RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, max(length(virtualWorldPos), length(prevVirtualWorldPos)));
    float lobeRadiusInPixels = lobeTanHalfAngle * unproj1;
    float deltaParallaxInPixels = length((prevUVVMBTest - prevUVVMB) * float2(rc.gRectSize));
    hitDistConfidence *= MLSmoothStep(lobeRadiusInPixels + 0.25, 0.0, deltaParallaxInPixels);

    // Fold the hit-distance/parallax confidence into the virtual-motion confidence (NRD virtualHistoryConfidence).
    specVMBConfidence *= hitDistConfidence;

    // Surface history confidence: parallax angle vs an accumulation-tightened lobe angle.
    float surfaceHistoryConfidence = SMBReprojectionFound > 0 ? 1.0 : 0.0;
    {
        float pixelSizeAtX = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ);
        float frustumSizeMain = pixelSizeAtX * min(float(rc.gRectSize.x), float(rc.gRectSize.y));
        float a = atan(smbParallaxInPixelsMax * pixelSizeAtX / max(length(currentWorldPos), NRD_EPS));

        float smbNonLinear = 1.0 / (1.0 + smbSpecAccumSpeed);
        float hitDistNormalization = ReblurGetHitDistNormalization(currentLinearZ, rc.gHitDistParams, currentRoughness);
        float h = lerp(prevSpecIllumSMB.w, specularIllumination.w, smbNonLinear) * hitDistNormalization;

        float tana0 = MLGetSpecularLobeTanHalfAngle(currentRoughnessModified, 0.75);
        tana0 *= lerp(NoV, 1.0, currentRoughnessModified);
        tana0 *= smbNonLinear;
        tana0 /= RelaxGetHitDistFactor(h, frustumSizeMain) + NRD_EPS;

        float a0 = max(atan(tana0), 1.5 / 255.0);
        float f = MLLinearStep(a0, 0.0, a);
        surfaceHistoryConfidence *= f * f * f * f;
    }
    float virtualHistoryConfidence = specVMBConfidence;

    // Responsive accumulation (specular only; diffuse has no roughness-driven cap).
    float2 maxResponsiveFrameNum = float2(rc.gMaxAccumulatedFrameNum, rc.gMaxAccumulatedFrameNum);
    {
        float smcResp = RelaxGetSpecMagicCurve(currentRoughnessModified);
        float2 f2;
        f2.x = saturate(dot(currentNormal, smbPrevNormalFlat));
        f2.y = saturate(dot(currentNormal, prevNormalVMB));
        float e = lerp(32.0, 1.0, smcResp) * (1.0 - responsiveFactor);
        f2 = lerp(smcResp, 1.0, responsiveFactor) * pow(f2, e);
        maxResponsiveFrameNum *= f2;
        maxResponsiveFrameNum = max(maxResponsiveFrameNum, float(rc.gResponsiveAccumulationMinAccumulatedFrameNum));
    }

    // Limit accumulated frames (spec lobes); keep the NoHistoryFix variants for DATA1.
    float2 maxFrameNum = rc.gMaxAccumulatedFrameNum * float2(surfaceHistoryConfidence, virtualHistoryConfidence);
    float2 maxFrameNum_NoHistoryFix = min(maxFrameNum, max(maxResponsiveFrameNum, rc.gHistoryFixFrameNum));
    float smbSpecAccumSpeed_NoHistoryFix = min(smbSpecAccumSpeed, maxFrameNum_NoHistoryFix.x);
    float vmbSpecAccumSpeed_NoHistoryFix = min(vmbSpecAccumSpeed, maxFrameNum_NoHistoryFix.y);

    maxFrameNum = min(maxFrameNum, maxResponsiveFrameNum);
    smbSpecAccumSpeed = min(smbSpecAccumSpeed, maxFrameNum.x);
    vmbSpecAccumSpeed = min(vmbSpecAccumSpeed, maxFrameNum.y);

    // Fallback between smb and vmb driven by their accumulated frame counts.
    float fallbackMagic = vmbSpecAccumSpeed > smbSpecAccumSpeed ? 8.0 : 0.5;
    virtualHistoryAmount *= 1.0 + (vmbSpecAccumSpeed - smbSpecAccumSpeed) / (fallbackMagic * max(vmbSpecAccumSpeed, smbSpecAccumSpeed) + 1.0);
    virtualHistoryAmount = saturate(virtualHistoryAmount);

    // ===== Accumulation (per-lobe) =====
    // Checkerboard: hole pixels carry prepass-reconstructed data; slow their accumulation.
    bool hasData = rc.gCheckerboard == 2u || (((uint(pixelPos.x) ^ uint(pixelPos.y)) ^ rc.gFrameIndex) & 1u) == rc.gCheckerboard;
    bool specHasData = hasData;
    bool diffHasData = hasData;

    // --- Specular slow history (YCoCg + normalized hit distance) ---
    float4 smbSpecHistory = ReblurClampNegativeToZero(prevSpecIllumSMB);
    float4 vmbSpecHistory = ReblurClampNegativeToZero(prevSpecIllumVMB);
    float specMinHitDistLimit = ReblurGetMinAllowedLimitForHitDistNonLinearAccumSpeed(currentRoughnessModified, rc.gMaxAccumulatedFrameNum);
    float smbSpecNonLinear = 1.0 / (1.0 + smbSpecAccumSpeed);
    float vmbSpecNonLinear = 1.0 / (1.0 + vmbSpecAccumSpeed);
    if (!specHasData) {
        smbSpecNonLinear *= lerp(1.0 - rc.gCheckerboardResolveAccumSpeed, 1.0, smbSpecNonLinear);
        vmbSpecNonLinear *= lerp(1.0 - rc.gCheckerboardResolveAccumSpeed, 1.0, vmbSpecNonLinear);
    }
    float4 smbSpec = ReblurMixHistoryAndCurrent(smbSpecHistory, specularIllumination, smbSpecNonLinear, specMinHitDistLimit);
    float4 vmbSpec = ReblurMixHistoryAndCurrent(vmbSpecHistory, specularIllumination, vmbSpecNonLinear, specMinHitDistLimit);
    float4 specResult = lerp(smbSpec, vmbSpec, virtualHistoryAmount);
    float4 specHistory = lerp(smbSpecHistory, vmbSpecHistory, virtualHistoryAmount);
    float specAccumSpeed = lerp(smbSpecAccumSpeed, vmbSpecAccumSpeed, virtualHistoryAmount);

    // Firefly suppressor (slow).
    float specMaxRelativeIntensity = rc.gFireflySuppressorMinRelativeScale + REBLUR_FIREFLY_SUPPRESSOR_MAX_RELATIVE_INTENSITY / (specAccumSpeed + 1.0);
    float specAntifireflyFactor = specAccumSpeed * rc.gMaxBlurRadius * REBLUR_FIREFLY_SUPPRESSOR_RADIUS_SCALE;
    specAntifireflyFactor /= 1.0 + specAntifireflyFactor;
    float specLumaResult = ReblurGetLuma(specResult);
    float specLumaClamped = min(specLumaResult, ReblurGetLuma(specHistory) * specMaxRelativeIntensity);
    specLumaClamped = lerp(specLumaResult, specLumaClamped, specAntifireflyFactor);
    specResult = ReblurChangeLuma(specResult, specLumaClamped);
    // Required for the hit-distance edge-stop weight (blur pass) to work: clamp accumulated .w spikes.
    float specHitDistMaxRelativeIntensity = 1.2 + 1.0 / (specAccumSpeed + 1.0);
    specResult.w = lerp(specResult.w, min(specResult.w, specHistory.w * specHitDistMaxRelativeIntensity), specAntifireflyFactor);

    // --- Specular fast history (luma; blend confidence + fast history once, then one non-linear speed) ---
    float specHistoryConfidence = lerp(surfaceHistoryConfidence, virtualHistoryConfidence, virtualHistoryAmount);
    float specFastNonLinear = ReblurGetNonLinearAccumSpeed(specAccumSpeed, rc.gMaxFastAccumulatedFrameNum, specHistoryConfidence, specHasData, rc.gCheckerboardResolveAccumSpeed);
    float specFastHistory = lerp(prevSpecResponsiveSMB, prevSpecResponsiveVMB, virtualHistoryAmount);
    float currentSpecLuma = ReblurGetLuma(specularIllumination);
    float specFastResult = lerp(specFastHistory, currentSpecLuma, specFastNonLinear);
    float specFastClamped = min(specFastResult, ReblurGetLuma(specHistory) * specMaxRelativeIntensity * REBLUR_FIREFLY_SUPPRESSOR_FAST_RELATIVE_INTENSITY);
    specFastResult = lerp(specFastResult, specFastClamped, specAntifireflyFactor);

    // DATA1 carries the NoHistoryFix variant: responsive clamps must not re-trigger HistoryFix.
    float specAccumSpeedData1 = lerp(smbSpecAccumSpeed_NoHistoryFix, vmbSpecAccumSpeed_NoHistoryFix, virtualHistoryAmount);

    // --- Diffuse slow history (surface motion only) ---
    float4 diffHistory = ReblurClampNegativeToZero(prevDiffIllumSMB);
    float diffMinHitDistLimit = ReblurGetMinAllowedLimitForHitDistNonLinearAccumSpeed(1.0, rc.gMaxAccumulatedFrameNum);
    float diffNonLinear = 1.0 / (1.0 + diffAccumSpeed);
    if (!diffHasData) {
        diffNonLinear *= lerp(1.0 - rc.gCheckerboardResolveAccumSpeed, 1.0, diffNonLinear);
    }
    float4 diffResult = ReblurMixHistoryAndCurrent(diffHistory, diffuseIllumination, diffNonLinear, diffMinHitDistLimit);

    // Firefly suppressor (slow).
    float diffMaxRelativeIntensity = rc.gFireflySuppressorMinRelativeScale + REBLUR_FIREFLY_SUPPRESSOR_MAX_RELATIVE_INTENSITY / (diffAccumSpeed + 1.0);
    float diffAntifireflyFactor = diffAccumSpeed * rc.gMaxBlurRadius * REBLUR_FIREFLY_SUPPRESSOR_RADIUS_SCALE;
    diffAntifireflyFactor /= 1.0 + diffAntifireflyFactor;
    float diffLumaResult = ReblurGetLuma(diffResult);
    float diffLumaClamped = min(diffLumaResult, ReblurGetLuma(diffHistory) * diffMaxRelativeIntensity);
    diffLumaClamped = lerp(diffLumaResult, diffLumaClamped, diffAntifireflyFactor);
    diffResult = ReblurChangeLuma(diffResult, diffLumaClamped);
    // Required for the hit-distance edge-stop weight (blur pass) to work: clamp accumulated .w spikes.
    float diffHitDistMaxRelativeIntensity = 1.2 + 1.0 / (diffAccumSpeed + 1.0);
    diffResult.w = lerp(diffResult.w, min(diffResult.w, diffHistory.w * diffHitDistMaxRelativeIntensity), diffAntifireflyFactor);

    // --- Diffuse fast history (luma; plain curve, no confidence) ---
    float diffFastAccumSpeed = min(diffAccumSpeed, rc.gMaxFastAccumulatedFrameNum);
    float diffFastNonLinear = 1.0 / (1.0 + diffFastAccumSpeed);
    if (!diffHasData) {
        diffFastNonLinear *= lerp(1.0 - rc.gCheckerboardResolveAccumSpeed, 1.0, diffFastNonLinear);
    }
    float currentDiffLuma = ReblurGetLuma(diffuseIllumination);
    float diffFastResult = lerp(prevDiffResponsiveSMB, currentDiffLuma, diffFastNonLinear);
    float diffFastClamped = min(diffFastResult, ReblurGetLuma(diffHistory) * diffMaxRelativeIntensity * REBLUR_FIREFLY_SUPPRESSOR_FAST_RELATIVE_INTENSITY);
    diffFastResult = lerp(diffFastResult, diffFastClamped, diffAntifireflyFactor);

    // ===== Outputs =====
    // Slow YCoCg + NORMALIZED hit distance in .w (the .w accumulates in normalized space through the history).
    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = specResult;
    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = diffResult;
    // Fast history is single-channel luma.
    rdgStorageFloat[pc.outSpecFastIndex][pixelPos] = specFastResult;
    rdgStorageFloat[pc.outDiffFastIndex][pixelPos] = diffFastResult;
    // DATA1 (per-lobe accum frames, NoHistoryFix for spec) + DATA2 (occlusion bits, curvature, vha, catrom).
    rdgStorageFloat2[pc.outData1Index][pixelPos] = ReblurPackData1(diffAccumSpeed, specAccumSpeedData1);
    rdgStorageUInt[pc.outData2Index][pixelPos] = ReblurPackData2(fbits, curvature, virtualHistoryAmount, smbAllowCatRom);
    // Current-frame min-3x3 world hit dist; stabilization rebuilds Xvirtual from this so it lands on the footprint the DATA2 vmb bits were computed for.
    rdgStorageFloat[pc.outSpecHitDistIndex][pixelPos] = hitDist;
    rdgStorageFloat4[pc.outPrevNRIndex][pixelPos] = RelaxPackPrevNormalRoughness(float4(currentNormal, currentRoughness));
}
