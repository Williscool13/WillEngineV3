/*
 * RELAX: TemporalAccumulation pass. Surface-motion + virtual-motion reprojection.
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import relax_interop;
import denoiser_common;
import restir_common;
import shader_functions;

static const int TA_BORDER = 1;
static const int TA_GROUP_X = 8;
static const int TA_GROUP_Y = 16;
static const int TA_BUFFER_X = TA_GROUP_X + TA_BORDER * 2;
static const int TA_BUFFER_Y = TA_GROUP_Y + TA_BORDER * 2;

groupshared float4 sharedNormalSpecHitT[TA_BUFFER_Y][TA_BUFFER_X];

void TAPreload(uint2 sharedPos, int2 globalPos, int2 rectSize, uint guideIndex, uint specInputIndex)
{
    globalPos = clamp(globalPos, int2(0, 0), rectSize - 1);
    uint packedNR = rdgTexturesUInt2[guideIndex].Load(int3(globalPos, 0)).x;
    float4 normalSpecHitT = float4(RelaxUnpackGuideNormalRoughness(packedNR).xyz, 0);
    float4 specIllum = rdgTexturesFloat4[specInputIndex].Load(int3(globalPos, 0));
    normalSpecHitT.a = specIllum.a;
    sharedNormalSpecHitT[sharedPos.y][sharedPos.x] = normalSpecHitT;
}

float IsReprojectionTapValid(float3 currentWorldPos, float3 previousWorldPos, float3 currentNormal, float disocclusionThreshold)
{
    float d = abs(dot(previousWorldPos - currentWorldPos, currentNormal));
    return d > disocclusionThreshold ? 0.0 : 1.0;
}

/** Surface-motion reprojection. Returns 0 = not found, 1 = bilinear, 2 = bicubic. */
float LoadSurfaceMotionBasedPrevData(
    RelaxDiffuseSpecularConstants* rc,
    float3 prevWorldPos, float2 prevUVSMB, float currentLinearZ, float3 currentNormal,
    float currentReflectionHitT, float NoV, float smbParallaxInPixelsMax,
    float disocclusionThreshold,
    out float footprintQuality, out float historyLength,
    out float4 prevSpecIllum, out float3 prevSpecResponsiveIllum, out float prevReflectionHitT,
    out float4 prevDiffIllum, out float3 prevDiffResponsiveIllum,
    uint historySpecIndex, uint historyDiffIndex, uint historySpecFastIndex, uint historyDiffFastIndex,
    uint prevViewZIndex, uint prevHistoryLengthIndex, uint prevSpecHitDistIndex, uint prevNormalRoughnessIndex)
{
    float2 prevPixelPosFloat = prevUVSMB * rc.gRectSizePrev;
    int2 bilinearOrigin = int2(floor(prevPixelPosFloat - 0.5));
    float2 bilinearWeights = frac(prevPixelPosFloat - 0.5);

    // Gather 4x4 region viewZ for bicubic validity check
    float2 go00 = (float2(bilinearOrigin) + float2(0, 0)) * rc.gResourceSizeInvPrev;
    float2 go10 = (float2(bilinearOrigin) + float2(2, 0)) * rc.gResourceSizeInvPrev;
    float2 go01 = (float2(bilinearOrigin) + float2(0, 2)) * rc.gResourceSizeInvPrev;
    float2 go11 = (float2(bilinearOrigin) + float2(2, 2)) * rc.gResourceSizeInvPrev;

    float4 prevVZ00 = RelaxGatherLinearViewZ(prevViewZIndex, go00).wzxy;
    float4 prevVZ10 = RelaxGatherLinearViewZ(prevViewZIndex, go10).wzxy;
    float4 prevVZ01 = RelaxGatherLinearViewZ(prevViewZIndex, go01).wzxy;
    float4 prevVZ11 = RelaxGatherLinearViewZ(prevViewZIndex, go11).wzxy;

    // Disocclusion threshold
    float pixelSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ);
    float frustumSize = pixelSize * min(float(rc.gRectSize.x), float(rc.gRectSize.y));
    float slopeScale = 1.0 / lerp(lerp(0.05, 1.0, NoV), 1.0, saturate(smbParallaxInPixelsMax / 30.0));
    float4 smbDisocclusionThreshold = saturate(disocclusionThreshold * slopeScale) * frustumSize;
    smbDisocclusionThreshold *= RelaxIsInScreenBilinear(float2(bilinearOrigin), rc.gRectSizePrev);
    smbDisocclusionThreshold -= NRD_EPS;

    float3 prevViewPos = MLAffineTransform(rc.gWorldToViewPrev, prevWorldPos);

    float3 pd0 = abs(prevVZ00.yzw - prevViewPos.zzz);
    float3 pd1 = abs(prevVZ10.xzw - prevViewPos.zzz);
    float3 pd2 = abs(prevVZ01.xyw - prevViewPos.zzz);
    float3 pd3 = abs(prevVZ11.xyz - prevViewPos.zzz);

    float3 tv0 = step(pd0, smbDisocclusionThreshold.x);
    float3 tv1 = step(pd1, smbDisocclusionThreshold.y);
    float3 tv2 = step(pd2, smbDisocclusionThreshold.z);
    float3 tv3 = step(pd3, smbDisocclusionThreshold.w);

    float bicubicFootprintValid = dot(tv0 + tv1 + tv2 + tv3, 1.0) > 11.5 ? 1.0 : 0.0;
    float4 bilinearTapsValid = float4(tv0.z, tv1.y, tv2.y, tv3.x);

    // Reject backfacing history: bilinear-averaged prev normal facing away means the footprint reprojected onto the wrong side of the surface
    float2 prevNrUv = (float2(bilinearOrigin) + 1.0) * rc.gResourceSizeInvPrev;
    float3 prevNormalFlat = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[prevNormalRoughnessIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], prevNrUv, 0)).xyz;
    if (dot(currentNormal, prevNormalFlat) < 0.0)
    {
        bilinearTapsValid = 0;
        bicubicFootprintValid = 0;
    }

    MLBilinear bilinear;
    bilinear.origin = float2(bilinearOrigin);
    bilinear.weights = bilinearWeights;
    float4 bilinearCustomWeights = MLGetBilinearCustomWeights(bilinear, bilinearTapsValid);

    bool useBicubic = bicubicFootprintValid > 0;
    BicubicFilterDiffSpec(prevPixelPosFloat, rc.gResourceSizeInvPrev,
        bilinearCustomWeights, useBicubic,
        historySpecIndex, prevSpecIllum,
        historyDiffIndex, prevDiffIllum);

    float4 specFast, diffFast;
    BicubicFilterDiffSpec(prevPixelPosFloat, rc.gResourceSizeInvPrev,
        bilinearCustomWeights, useBicubic,
        historySpecFastIndex, specFast,
        historyDiffFastIndex, diffFast);

    prevSpecIllum = max(prevSpecIllum, 0);
    prevSpecResponsiveIllum = max(specFast.rgb, 0);
    prevDiffIllum = max(prevDiffIllum, 0);
    prevDiffResponsiveIllum = max(diffFast.rgb, 0);

    // Fetch history length and prevSpecHitDist via bilinear
    float2 gatherOrigin = (float2(bilinearOrigin) + 1.0) * rc.gResourceSizeInvPrev;
    float4 prevHistLengths = rdgTexturesFloat[prevHistoryLengthIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherOrigin).wzxy;
    historyLength = 255.0 * RelaxBilinearWeightedFloat(prevHistLengths.x, prevHistLengths.y, prevHistLengths.z, prevHistLengths.w, bilinearCustomWeights);

    float4 prevHitTs = rdgTexturesFloat[prevSpecHitDistIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherOrigin).wzxy;
    // On history reset relax_spec_hit_dist is an unwritten same-frame aliased transient; source current hitT so the accumulation lerp is a no-op instead of blending garbage.
    prevReflectionHitT = (rc.gResetHistory != 0) ? currentReflectionHitT : max(0.001, RelaxBilinearWeightedFloat(prevHitTs.x, prevHitTs.y, prevHitTs.z, prevHitTs.w, bilinearCustomWeights));

    float reprojectionFound = bicubicFootprintValid > 0 ? 2.0 : 1.0;
    footprintQuality = bicubicFootprintValid > 0 ? 1.0 : dot(bilinearCustomWeights, 1.0);

    if (dot(bilinearTapsValid, 1.0) == 0.0) { reprojectionFound = 0; footprintQuality = 0; }
    return reprojectionFound;
}

/** Virtual-motion reprojection for specular. Returns 1 = found, 0 = not found. */
float LoadVirtualMotionBasedPrevData(
    RelaxDiffuseSpecularConstants* rc,
    float3 currentWorldPos, float3 currentNormal, float currentLinearZ,
    float3 virtualWorldPos, bool surfaceBicubicValid, float2 prevUVSMB, float disocclusionThreshold,
    out float4 prevSpecIllum, out float4 prevSpecResponsiveIllum,
    out float3 prevNormal, out float prevRoughness, out float prevReflectionHitT, out float2 prevUVVMB,
    uint historySpecIndex, uint historySpecFastIndex, uint prevViewZIndex, uint prevSpecHitDistIndex,
    uint prevNormalRoughnessIndex)
{
    float4 prevVirtualClip = mul(rc.gWorldToClipPrev, float4(virtualWorldPos, 1.0));
    prevVirtualClip.xy /= prevVirtualClip.w;
    prevUVVMB = prevVirtualClip.xy * float2(0.5, 0.5) + 0.5;

    float2 prevVirtualPixelPosFloat = prevUVVMB * rc.gRectSizePrev;
    int2 bilinearOrigin = int2(floor(prevVirtualPixelPosFloat - 0.5));
    float2 bilinearWeights = frac(prevVirtualPixelPosFloat - 0.5);
    float2 gatherOrigin = (float2(bilinearOrigin) + 1.0) * rc.gResourceSizeInvPrev;

    // Without camera delta for tap validation
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

    prevSpecIllum = 0; prevSpecResponsiveIllum = 0;
    prevNormal = currentNormal; prevRoughness = 0;
    prevReflectionHitT = rc.gDenoisingRange;

    if (dot(bilinearTapsValid, 1.0) > 0.0)
    {
        MLBilinear bilinear;
        bilinear.origin = float2(bilinearOrigin);
        bilinear.weights = bilinearWeights;
        float4 bilinearCustomWeights = MLGetBilinearCustomWeights(bilinear, bilinearTapsValid);
        bool useBicubic = (surfaceBicubicValid) && all(bilinearTapsValid == 1);

        float4 specFastResult;
        BicubicFilterSingle(prevVirtualPixelPosFloat, rc.gResourceSizeInvPrev, bilinearCustomWeights, useBicubic, historySpecIndex, prevSpecIllum);
        BicubicFilterSingle(prevVirtualPixelPosFloat, rc.gResourceSizeInvPrev, bilinearCustomWeights, useBicubic, historySpecFastIndex, specFastResult);
        prevSpecIllum = max(prevSpecIllum, 0);
        prevSpecResponsiveIllum = max(specFastResult, 0);

        float2 uvVMB = prevUVVMB * rc.gRectSizePrev * rc.gResourceSizeInvPrev;
        // On reset skip the unwritten same-frame aliased read; keep the finite gDenoisingRange not-found default to avoid NaN in confidence math.
        prevReflectionHitT = (rc.gResetHistory != 0) ? rc.gDenoisingRange : max(0.001, rdgTexturesFloat[prevSpecHitDistIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvVMB, 0).r);

        float4 prevNR = RelaxUnpackPrevNormalRoughness(rdgTexturesFloat4[prevNormalRoughnessIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvVMB, 0));
        prevNormal = prevNR.xyz;
        prevRoughness = prevNR.w;
    }

    return dot(bilinearTapsValid, 1.0) == 4.0 ? 1.0 : 0.0;
}

/** RDG aliases transients and never clears them, so a skipped texel would hand the next reader another resource's content. Every dispatched thread must leave a defined value. */
void TAWriteEmpty(RelaxTemporalAccumulationPushConstant pc, int2 pixelPos)
{
    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = float4(0.0);
    rdgStorageFloat4[pc.outDiffFastIndex][pixelPos] = float4(0.0);
    rdgStorageFloat[pc.outHistoryLengthIndex][pixelPos] = 0.0;
    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = float4(0.0);
    rdgStorageFloat4[pc.outSpecFastIndex][pixelPos] = float4(0.0);
    rdgStorageFloat[pc.outSpecHitDistIndex][pixelPos] = 0.0;
    rdgStorageFloat[pc.outSpecReprojConfidenceIndex][pixelPos] = 0.0;
    rdgStorageFloat4[pc.outPrevNRIndex][pixelPos] = RelaxPackPrevNormalRoughness(float4(0.0, 0.0, 1.0, 0.0));
}

[shader("compute")]
[numthreads(TA_GROUP_X, TA_GROUP_Y, 1)]
void RelaxTemporalAccumulationMain(uniform RelaxTemporalAccumulationPushConstant pc, int2 threadPos : SV_GroupThreadID, uint2 groupPos : SV_GroupID, int2 pixelPos : SV_DispatchThreadID, uint threadIndex : SV_GroupIndex)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;

    // Preload
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
            {
                TAPreload(newId, groupBase + int2(newId), rc.gRectSize, pc.guideIndex, pc.specInputIndex);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    bool bInRect = pixelPos.x < rc.gRectSize.x && pixelPos.y < rc.gRectSize.y;
    if (isSky != 0.0 || !bInRect)
    {
        if (bInRect) { TAWriteEmpty(pc, pixelPos); }
        return;
    }

    int2 fullPixel = pixelPos;
    float3 currentNormal;
    float currentRoughness;
    float currentLinearZ;
    RelaxLoadGuide(pc.guideIndex, fullPixel, currentNormal, currentRoughness, currentLinearZ);
    if (!RelaxIsInDenoisingRange(currentLinearZ, rc.gDenoisingRange))
    {
        TAWriteEmpty(pc, pixelPos);
        return;
    }

    // ReSTIR-provided history confidence (IN_DIFF/SPEC_CONFIDENCE equivalent). 1 = full trust; lower scales down the effective max accumulated frames here-and-now, collapsing history where the lighting changed in a way reprojection cannot see (moving-caster shadows on static surfaces).
    // Single ReSTIR signal drives both lobes.
    float historyConfidence = 1.0;
    if (pc.confidenceIndex != ~0u) {
        historyConfidence = saturate(rdgTexturesFloat[pc.confidenceIndex].Load(int3(pixelPos, 0)).r);
    }

    uint2 sharedMemoryIndex = uint2(threadPos) + TA_BORDER;

    float3 currentWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, currentLinearZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    float3 currentViewVec = (rc.gOrthoMode == 0) ? currentWorldPos : currentLinearZ * normalize(rc.gFrustumForward.xyz);
    float3 V = -normalize(currentViewVec);
    float NoV = abs(dot(currentNormal, V));

    // Motion vectors from GBuffer1 (.g = R16G16 XY, .w bits 16-31 = view-depth delta float16, NRD mv.z semantics)
    float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;
    uint4 gt1 = rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(fullPixel, 0));
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

    // Input noisy data
    float4 specularIllumination = rdgTexturesFloat4[pc.specInputIndex].Load(int3(pixelPos, 0));
    float4 diffuseIllumination = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(pixelPos, 0));

    // Average normal and minHitDist from 3x3 neighborhood (shared memory)
    float hitTM1 = sharedNormalSpecHitT[sharedMemoryIndex.y][sharedMemoryIndex.x].a;
    float minHitDist3x3 = hitTM1 == 0.0 ? NRD_INF : hitTM1;
    float3 currentNormalAveraged = currentNormal;

    [unroll]
    for (int i = -1; i <= 1; i++)
    {
        [unroll]
        for (int j = -1; j <= 1; j++)
        {
            if (i == 0 && j == 0) { continue; }
            float4 nsh = sharedNormalSpecHitT[sharedMemoryIndex.y + j][sharedMemoryIndex.x + i];
            minHitDist3x3 = min(minHitDist3x3, nsh.a == 0.0 ? NRD_INF : nsh.a);
            currentNormalAveraged += nsh.rgb;
        }
    }
    currentNormalAveraged /= 9.0;

    float currentRoughnessModified = MLGetModifiedRoughnessFromNormalVariance(currentRoughness, currentNormalAveraged);

    // 2nd moments of input
    float spec1stMoment = MLLuminance(specularIllumination.rgb);
    float spec2ndMoment = spec1stMoment * spec1stMoment;
    float diff1stMoment = MLLuminance(diffuseIllumination.rgb);
    float diff2ndMoment = diff1stMoment * diff1stMoment;

    // Parallax
    float smbParallaxInPixels1 = RelaxComputeParallaxInPixels(prevWorldPos + rc.gCameraDelta.xyz, rc.gOrthoMode == 0.0 ? prevUVSMB : pixelUv, rc.gWorldToClipPrev, float2(rc.gRectSize));
    float smbParallaxInPixels2 = RelaxComputeParallaxInPixels(prevWorldPos - rc.gCameraDelta.xyz, rc.gOrthoMode == 0.0 ? pixelUv : prevUVSMB, rc.gWorldToClip, float2(rc.gRectSize));
    float smbParallaxInPixelsMax = max(smbParallaxInPixels1, smbParallaxInPixels2);
    float smbParallaxInPixelsMin = min(smbParallaxInPixels1, smbParallaxInPixels2);

    float disocclusionThreshold = rc.gDisocclusionThreshold;

    // Surface motion reprojection
    float footprintQuality;
    float4 prevSpecIllumSMB, prevDiffIllumSMB;
    float3 prevSpecResponsiveSMB, prevDiffResponsiveSMB;
    float prevReflectionHitTSMB;
    float historyLength;

    float SMBReprojectionFound = LoadSurfaceMotionBasedPrevData(
        rc, prevWorldPos, prevUVSMB, currentLinearZ, normalize(currentNormalAveraged),
        specularIllumination.a, NoV, smbParallaxInPixelsMax, disocclusionThreshold,
        footprintQuality, historyLength,
        prevSpecIllumSMB, prevSpecResponsiveSMB, prevReflectionHitTSMB,
        prevDiffIllumSMB, prevDiffResponsiveSMB,
        pc.historySpecIndex, pc.historyDiffIndex, pc.historySpecFastIndex, pc.historyDiffFastIndex,
        pc.prevViewZIndex, pc.prevHistoryLengthIndex, pc.prevSpecHitDistIndex, pc.prevNormalRoughnessIndex);

    historyLength = min(historyLength + 1.0, RELAX_MAX_ACCUM_FRAME_NUM);

    // Footprint quality adjustment
    float3 Vprev = (rc.gOrthoMode == 0) ? -normalize(prevWorldPos - rc.gCameraDelta.xyz) : -normalize(rc.gPrevFrustumForward.xyz);
    float NoVprev = abs(dot(currentNormal, Vprev));
    float sizeQuality = (NoVprev + 1e-3) / (NoV + 1e-3);
    sizeQuality *= sizeQuality; sizeQuality *= sizeQuality;
    footprintQuality *= lerp(0.1, 1.0, saturate(sizeQuality + abs(rc.gOrthoMode)));

    if (footprintQuality < 1.0)
    {
        historyLength *= sqrt(footprintQuality);
        historyLength = max(historyLength, 1.0);
    }

    if (rc.gResetHistory != 0) { historyLength = 1.0; }
    float maxAccumulatedFrameNum = 1.0 + max(rc.gDiffMaxAccumulatedFrameNum, rc.gSpecMaxAccumulatedFrameNum);
    historyLength = min(historyLength, maxAccumulatedFrameNum);
    // Confidence flushes the accumulated frame count itself (not just the per-frame alpha cap), so a shadow flip persists as faster blend + more spatial fill for several frames instead of one.
    historyLength = max(historyLength * historyConfidence, 1.0);

    // Diffuse accumulation
    float diffMaxAccumFrameNum = rc.gDiffMaxAccumulatedFrameNum * historyConfidence;
    float diffMaxFastAccumFrameNum = rc.gDiffMaxFastAccumulatedFrameNum * historyConfidence;
    float diffHistoryLength = historyLength;
    float diffAlpha = (SMBReprojectionFound > 0) ? max(1.0 / (diffMaxAccumFrameNum + 1.0), 1.0 / diffHistoryLength) : 1.0;
    float diffAlphaResponsive = (SMBReprojectionFound > 0) ? max(1.0 / (diffMaxFastAccumFrameNum + 1.0), 1.0 / diffHistoryLength) : 1.0;

    if (rc.gDiffCheckerboard != 2u && diffHistoryLength > 1.0 && (((uint(pixelPos.x) ^ uint(pixelPos.y)) ^ rc.gFrameIndex) & 1u) != rc.gDiffCheckerboard)
    {
        diffAlpha *= 1.0 - rc.gCheckerboardResolveAccumSpeed;
        diffAlphaResponsive *= 1.0 - rc.gCheckerboardResolveAccumSpeed;
    }

    float4 accDiffIllum = lerp(prevDiffIllumSMB, float4(diffuseIllumination.rgb, diff2ndMoment), diffAlpha);
    float3 accDiffResponsive = lerp(prevDiffResponsiveSMB, diffuseIllumination.rgb, diffAlphaResponsive);

    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = accDiffIllum;
    rdgStorageFloat4[pc.outDiffFastIndex][pixelPos] = float4(accDiffResponsive, 0);
    rdgStorageFloat[pc.outHistoryLengthIndex][pixelPos] = historyLength / 255.0;

    // Specular curvature estimation
    float hitDist = minHitDist3x3 == NRD_INF ? 0.0 : minHitDist3x3;
    float curvature = 0.0;
    {
        float2 uvForZeroParallax = rc.gOrthoMode == 0.0 ? prevUVSMB : pixelUv;
        float2 deltaUv = uvForZeroParallax - MLGetScreenUv(rc.gWorldToClipPrev, prevWorldPos + rc.gCameraDelta.xyz);
        deltaUv *= float2(rc.gRectSize);
        deltaUv /= max(smbParallaxInPixels1, 1.0 / 256.0);

        // 10 edge neighbor
        float3 x10;
        float3 n10;
        {
            float3 xn = RelaxGetCurrentWorldPosFromClipXY((pixelUv + float2(1, 0) * rc.gRectSizeInv) * 2.0 - 1.0, 1.0, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
            float3 vn = rc.gOrthoMode == 0.0 ? normalize(-xn) : rc.gFrustumForward.xyz;
            float3 on = rc.gOrthoMode == 0.0 ? float3(0,0,0) : xn;
            x10 = on + vn * dot(currentWorldPos - on, currentNormal) / dot(currentNormal, vn);
            n10 = sharedNormalSpecHitT[sharedMemoryIndex.y][sharedMemoryIndex.x + 1].xyz;
        }

        // 01 edge neighbor
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
            int2 highFullPos = highPos;
            float3 nHigh;
            float rHigh;
            float zHigh;
            RelaxLoadGuide(pc.guideIndex, highFullPos, nHigh, rHigh, zHigh);
            float3 xHigh = RelaxGetCurrentWorldPosFromClipXY(motionUvHigh * 2.0 - 1.0, zHigh, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float pixSz = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ);
            float fSize = min(float(rc.gRectSize.x), float(rc.gRectSize.y)) * pixSz;
            float2 geomWP = RelaxGetGeometryWeightParams(NRD_CURVATURE_HIGH_PARALLAX_DISOCCLUSION_THRESHOLD, fSize, currentWorldPos, currentNormal);
            float NoXHigh = dot(currentNormal, xHigh);
            float geomW = RelaxApplyGeometryWeightAndRangeCheck(1.0, zHigh, NoXHigh, geomWP, rc.gDenoisingRange);
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

    float3 hitDelta = float3(0.0);
    float3 hitDeltaVirtual = float3(0.0);
    if (pc.hitDeltaIndex != ~0u) {
        hitDelta = rdgTexturesFloat4[pc.hitDeltaIndex].Load(int3(pixelPos, 0)).xyz;
        hitDeltaVirtual = hitDelta - 2.0 * dot(hitDelta, currentNormal) * currentNormal;
    }

    float3 virtualWorldPos = RelaxGetXvirtual(hitDist, curvature, currentWorldPos, prevWorldPos, currentNormal, V, currentRoughness) + hitDeltaVirtual;

    // Virtual motion reprojection for specular
    float4 prevSpecIllumVMB;
    float4 prevSpecResponsiveVMB;
    float3 prevNormalVMB;
    float prevRoughnessVMB, prevReflectionHitTVMB;
    float2 prevUVVMB;

    float VMBReprojectionFound = LoadVirtualMotionBasedPrevData(
        rc, currentWorldPos, currentNormal, currentLinearZ,
        virtualWorldPos, SMBReprojectionFound == 2.0, prevUVSMB, disocclusionThreshold,
        prevSpecIllumVMB, prevSpecResponsiveVMB,
        prevNormalVMB, prevRoughnessVMB, prevReflectionHitTVMB, prevUVVMB,
        pc.historySpecIndex, pc.historySpecFastIndex, pc.prevViewZIndex, pc.prevSpecHitDistIndex,
        pc.prevNormalRoughnessIndex);

    // Virtual history amount
    float4 D = MLGetSpecularDominantDirection(currentNormal, V, currentRoughnessModified);
    float virtualHistoryAmount = VMBReprojectionFound * D.w;
    virtualHistoryAmount *= (rc.gOrthoMode == 0) ? 1.0 : 0.75;
    virtualHistoryAmount *= float(dot(prevNormalVMB, currentNormalAveraged) > 0.0);

    float2 uvDiff = prevUVVMB - prevUVSMB;
    float uvDiffLengthInPixels = length(uvDiff * float2(rc.gRectSize));

    float tanCurvature = abs(curvature * RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ));
    tanCurvature *= max(uvDiffLengthInPixels / max(NoV, 0.01), 1.0);
    float curvatureAngle = atan(tanCurvature);

    float lobeHalfAngle = max(atan(RelaxGetSpecLobeTanHalfAngle(currentRoughnessModified)), RELAX_NORMAL_ULP);
    float normalWeight = RelaxGetEncodingAwareNormalWeight(currentNormal, prevNormalVMB, lobeHalfAngle, curvatureAngle, RELAX_NORMAL_ULP);
    virtualHistoryAmount *= lerp(1.0 - saturate(uvDiffLengthInPixels), 1.0, normalWeight);

    float2 relaxedRoughnessWeightParams = RelaxGetRelaxedRoughnessWeightParams(currentRoughness * currentRoughness, rc.gRoughnessFraction);
    float virtualRoughnessWeight = RelaxComputeWeight(prevRoughnessVMB * prevRoughnessVMB, relaxedRoughnessWeightParams.x, relaxedRoughnessWeightParams.y);
    virtualRoughnessWeight = lerp(1.0 - saturate(uvDiffLengthInPixels), 1.0, virtualRoughnessWeight);
    virtualHistoryAmount *= (rc.gOrthoMode == 0) ? virtualRoughnessWeight : 1.0;
    float specVMBConfidence = virtualRoughnessWeight * 0.9 + 0.1;

    // Look back 1 and 2 frames
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

    // Virtual history confidence - hit distance
    // Thin lens equation: I = O / (2 * curvature * O + 1) (matches NRD ApplyThinLensEquation).
    float SMC = RelaxGetSpecMagicCurve(currentRoughnessModified);
    float hitDistC = lerp(specularIllumination.a, prevReflectionHitTSMB, SMC);
    float hitDist1 = hitDistC / (2.0 * curvature * hitDistC + 1.0);
    float hitDist2 = prevReflectionHitTVMB / (2.0 * curvature * prevReflectionHitTVMB + 1.0);
    float maxDistHD = max(hitDist1, hitDist2);
    float dHitT = abs(hitDist1 - hitDist2);
    float hitDistConfidence = 1.0 - saturate(lerp(20.0, 0.0, SMC) * dHitT / (currentLinearZ + maxDistHD));
    hitDistConfidence = lerp(hitDistConfidence, 1.0, SMC);

    float hitDistForTrackingPrev = prevSpecResponsiveVMB.a;
    float3 prevVirtualWorldPos = RelaxGetXvirtual(hitDistForTrackingPrev, curvature, currentWorldPos, prevWorldPos, currentNormal, V, currentRoughness) + hitDeltaVirtual;
    float2 prevUVVMBTest = MLGetScreenUv(rc.gWorldToClipPrev, prevVirtualWorldPos);
    float lobeTanHalfAngle = max(RelaxGetSpecLobeTanHalfAngle(currentRoughness, 0.6), 0.5 * rc.gRectSizeInv.x);
    float unproj1 = min(hitDist, hitDistForTrackingPrev) / RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, max(length(virtualWorldPos), length(prevVirtualWorldPos)));
    float lobeRadiusInPixels = lobeTanHalfAngle * unproj1;
    float deltaParallaxInPixels = length((prevUVVMBTest - prevUVVMB) * float2(rc.gRectSize));
    hitDistConfidence *= MLSmoothStep(lobeRadiusInPixels + 0.25, 0.0, deltaParallaxInPixels);

    float smbDeltaConfidence = 1.0;
    float vmbDeltaConfidence = 1.0;
    if (pc.hitDeltaIndex != ~0u && pc.hitDeltaHistoryIndex != ~0u && rc.gResetHistory == 0) {
        float2 prevScale = rc.gRectSizePrev * rc.gResourceSizeInvPrev;
        float3 prevDeltaSMB = rdgTexturesFloat4[pc.hitDeltaHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], prevUVSMB * prevScale, 0).xyz;
        float3 prevDeltaVMB = rdgTexturesFloat4[pc.hitDeltaHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], prevUVVMB * prevScale, 0).xyz;
        float deltaTolerancePixels = 2.0 + lobeRadiusInPixels;
        float mismatchPixelsSMB = length(hitDelta - prevDeltaSMB) / RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ + min(hitDist, prevReflectionHitTSMB));
        float mismatchPixelsVMB = length(hitDelta - prevDeltaVMB) / RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, currentLinearZ + min(hitDist, prevReflectionHitTVMB));
        smbDeltaConfidence = MLSmoothStep(deltaTolerancePixels, 0.0, mismatchPixelsSMB);
        vmbDeltaConfidence = MLSmoothStep(deltaTolerancePixels, 0.0, mismatchPixelsVMB);
    }

    // Surface motion confidence
    float specSMBConfidence = (SMBReprojectionFound > 0 ? 1.0 : 0.0) * RelaxGetEncodingAwareNormalWeight(V, Vprev, lobeHalfAngle * NoV / rc.gFramerateScale, 0.0, 0.0);

    float specHistoryFrames = min(rc.gSpecMaxAccumulatedFrameNum * historyConfidence, historyLength);
    float specHistoryResponsiveFrames = min(rc.gSpecMaxFastAccumulatedFrameNum * historyConfidence, historyLength);

    float specSMBAlpha = max(1.0 - specSMBConfidence, 1.0 / (1.0 + specHistoryFrames));
    float specSMBResponsiveAlpha = max(specSMBAlpha, 1.0 / (1.0 + specHistoryResponsiveFrames));

    bool specHole = rc.gSpecCheckerboard != 2u && (((uint(pixelPos.x) ^ uint(pixelPos.y)) ^ rc.gFrameIndex) & 1u) != rc.gSpecCheckerboard;
    float specSMBCb = (specHole && smbParallaxInPixelsMax < 0.5) ? (1.0 - rc.gCheckerboardResolveAccumSpeed * (SMBReprojectionFound > 0 ? 1.0 : 0.0)) : 1.0;
    specSMBAlpha *= specSMBCb;
    specSMBResponsiveAlpha *= specSMBCb;
    specSMBAlpha = max(specSMBAlpha, 1.0 - smbDeltaConfidence);
    specSMBResponsiveAlpha = max(specSMBResponsiveAlpha, 1.0 - smbDeltaConfidence);

    // SMB accumulation
    float4 accSpecSMB;
    accSpecSMB.rgb = lerp(prevSpecIllumSMB.rgb, specularIllumination.rgb, specSMBAlpha);
    accSpecSMB.w = lerp(prevReflectionHitTSMB, specularIllumination.w, max(specSMBAlpha, 0.1));
    float accSpecM2SMB = lerp(prevSpecIllumSMB.a, spec2ndMoment, specSMBAlpha);
    float3 accSpecSMBResponsive = lerp(prevSpecResponsiveSMB, specularIllumination.xyz, specSMBResponsiveAlpha);

    // VMB accumulation
    float specVMBAlpha = max(1.0 - specVMBConfidence, 1.0 / (1.0 + specHistoryFrames));
    float specVMBResponsiveAlpha = max(1.0 - specVMBConfidence * hitDistConfidence, 1.0 / (1.0 + specHistoryResponsiveFrames));
    float specVMBHitTAlpha = max(1.0 - specVMBConfidence * hitDistConfidence, 1.0 / (1.0 + specHistoryFrames));
    float specVMBCb = (specHole && smbParallaxInPixelsMax < 0.5) ? (1.0 - rc.gCheckerboardResolveAccumSpeed * (VMBReprojectionFound > 0 ? 1.0 : 0.0)) : 1.0;
    specVMBAlpha *= specVMBCb;
    specVMBResponsiveAlpha *= specVMBCb;
    specVMBHitTAlpha *= specVMBCb;
    specVMBAlpha = max(specVMBAlpha, 1.0 - vmbDeltaConfidence);
    specVMBResponsiveAlpha = max(specVMBResponsiveAlpha, 1.0 - vmbDeltaConfidence);
    specVMBHitTAlpha = max(specVMBHitTAlpha, 1.0 - vmbDeltaConfidence);

    float4 accSpecVMB;
    accSpecVMB.rgb = lerp(prevSpecIllumVMB.rgb, specularIllumination.rgb, specVMBAlpha);
    accSpecVMB.a = (rc.gResetHistory != 0) ? specularIllumination.a : lerp(prevReflectionHitTVMB, specularIllumination.a, max(specVMBHitTAlpha, 0.1));
    float accSpecM2VMB = lerp(prevSpecIllumVMB.a, spec2ndMoment, specVMBAlpha);
    float3 accSpecVMBResponsive = lerp(prevSpecResponsiveVMB.rgb, specularIllumination.xyz, specVMBResponsiveAlpha);

    // Fallback
    virtualHistoryAmount *= saturate(specVMBConfidence / (specSMBConfidence + NRD_EPS));

    float accReflectionHitT = lerp(accSpecSMB.a, accSpecVMB.a, virtualHistoryAmount);
    float3 accSpecIllum = lerp(accSpecSMB.xyz, accSpecVMB.xyz, virtualHistoryAmount);
    float3 accSpecIllumResponsive = lerp(accSpecSMBResponsive, accSpecVMBResponsive, virtualHistoryAmount);
    float accSpec2ndMoment = lerp(accSpecM2SMB, accSpecM2VMB, virtualHistoryAmount);

    float specularHistoryConfidence = lerp(specSMBConfidence, specVMBConfidence, virtualHistoryAmount);
    if (accSpec2ndMoment == 0) { accSpec2ndMoment = rc.gSpecVarianceBoost * (1.0 - specularHistoryConfidence); }

    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = float4(accSpecIllum, accSpec2ndMoment);
    rdgStorageFloat4[pc.outSpecFastIndex][pixelPos] = float4(accSpecIllumResponsive, hitDist);
    rdgStorageFloat[pc.outSpecHitDistIndex][pixelPos] = accReflectionHitT;
    rdgStorageFloat[pc.outSpecReprojConfidenceIndex][pixelPos] = specularHistoryConfidence;
    rdgStorageFloat4[pc.outPrevNRIndex][pixelPos] = RelaxPackPrevNormalRoughness(float4(currentNormal, currentRoughness));
}
