/*
 * ReBLUR: TemporalStabilization. Faithful port of NRD REBLUR_TemporalStabilization (DIFFUSE_SPECULAR, YCoCg=1).
 * Stabilizes a single-channel luma history per signal (ping-pong carried): diffuse reprojects by surface
 * motion, specular blends surface + virtual motion; antilag modulates history weight and feeds back into
 * the carried packed accum speeds so a lighting change cuts next frame's accumulation.
 * Engine deviation: NRD's spec-MV upscaler modification not ported (engine MVs are packed in the gbuffer
 * and TAA reads them directly).
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import denoiser_common;
import reblur_common;
import shader_functions;

/** 3x3 luma mean + standard deviation over a YCoCg signal (luma = .x). Out-of-range neighbors
 *  substitute the center luma. */
void NeighborhoodLumaMoments(uint texIdx, uint viewZIndex, int2 pixelPos, int2 rectSize, ReblurDiffuseSpecularConstants* rc, out float mean, out float sigma)
{
    float centerLuma = ReblurGetLuma(rdgTexturesFloat4[texIdx].Load(int3(pixelPos, 0)));
    float m1 = 0.0;
    float m2 = 0.0;
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            int2 p = clamp(pixelPos + int2(x, y), int2(0, 0), rectSize - 1);
            float luma = centerLuma;
            float z = RelaxLoadViewZ(viewZIndex, p, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (RelaxIsInDenoisingRange(z, rc.gDenoisingRange)) {
                luma = ReblurGetLuma(rdgTexturesFloat4[texIdx].Load(int3(p, 0)));
            }
            m1 += luma;
            m2 += luma * luma;
        }
    }
    m1 /= 9.0;
    m2 /= 9.0;
    mean = m1;
    sigma = sqrt(max(m2 - m1 * m1, 0.0));
}

/** Occlusion-weighted bilinear fetch of a single-channel luma history. */
float FetchLumaHistory(uint texIdx, float2 uv, float2 rectSizePrev, float2 resourceSizeInvPrev, float4 occlusion)
{
    float2 pos = saturate(uv) * rectSizePrev;
    int2 bilinearOrigin = int2(floor(pos - 0.5));
    MLBilinear bilinear;
    bilinear.origin = float2(bilinearOrigin);
    bilinear.weights = frac(pos - 0.5);
    float4 w = MLGetBilinearCustomWeights(bilinear, occlusion);
    float2 gatherUv = (float2(bilinearOrigin) + 1.0) * resourceSizeInvPrev;
    float4 taps = rdgTexturesFloat[texIdx].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherUv).wzxy;
    return RelaxBilinearWeightedFloat(taps.x, taps.y, taps.z, taps.w, w);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ReblurStabilizationMain(uniform ReblurStabilizationPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;

    float4 diffCur = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixelPos, 0));
    float4 specCur = rdgTexturesFloat4[pc.specIndex].Load(int3(pixelPos, 0));

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    bool outOfRange = isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y;
    float viewZ = 0.0;
    if (!outOfRange) {
        viewZ = RelaxLoadViewZ(pc.viewZIndex, pixelPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
        outOfRange = !RelaxIsInDenoisingRange(viewZ, rc.gDenoisingRange);
    }

    if (outOfRange) {
        if (pixelPos.x < rc.gRectSize.x && pixelPos.y < rc.gRectSize.y) {
            rdgStorageFloat[pc.outDiffLumaStabIndex][pixelPos] = ReblurGetLuma(diffCur);
            rdgStorageFloat[pc.outSpecLumaStabIndex][pixelPos] = ReblurGetLuma(specCur);
            rdgStorageUInt[pc.outInternalDataIndex][pixelPos] = 0u;
            rdgStorageFloat4[pc.outDiffFinalIndex][pixelPos] = float4(max(MLYCoCgToRgb(diffCur.xyz), 0.0), diffCur.w);
            rdgStorageFloat4[pc.outSpecFinalIndex][pixelPos] = float4(max(MLYCoCgToRgb(specCur.xyz), 0.0), specCur.w);
        }
        return;
    }

    float3x3 viewToWorld = (float3x3)rc.gViewToWorld;
    float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;

    // Position + surface motion (camera-relative, rotation-only view).
    float3 X = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, viewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    uint4 gt1 = rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0));
    float3 mv = float3(UnpackMotionVectors(gt1.g) * rc.gMvScale.xy, UnpackViewZDelta(gt1.w));
    float3 Xprev = X;
    float2 smbPixelUv = pixelUv + mv.xy;
    if (rc.gMvScale.w == 0.0) {
        if (rc.gMvScale.z == 0.0) {
            mv.z = MLAffineTransform(rc.gWorldToViewPrev, X).z - viewZ;
        }
        Xprev = RelaxGetCurrentWorldPosFromClipXY(smbPixelUv * 2.0 - 1.0, viewZ + mv.z, rc.gPrevFrustumForward, rc.gPrevFrustumRight, rc.gPrevFrustumUp, rc.gOrthoMode) + rc.gCameraDelta.xyz;
    } else {
        Xprev += mv;
        smbPixelUv = MLGetScreenUv(rc.gWorldToClipPrev, Xprev);
    }

    float4 nr = RelaxUnpackNormalAndRoughness(gt1, viewToWorld);
    float3 N = nr.xyz;
    float roughness = nr.w;

    // Shared data from TA.
    float2 data1 = ReblurUnpackData1(rdgTexturesFloat2[pc.data1Index].Load(int3(pixelPos, 0)));
    uint bits;
    bool smbAllowCatRom;
    float2 data2 = ReblurUnpackData2(rdgTexturesUInt[pc.data2Index].Load(int3(pixelPos, 0)), bits, smbAllowCatRom);
    float virtualHistoryAmount = data2.x;
    float curvature = data2.y;

    if (rc.gResetHistory != 0) { bits = 0u; data1 = float2(0.0, 0.0); }

    // Surface-motion footprint from the DATA2 occlusion bits.
    float4 smbOcclusion = float4((bits & 1u) != 0u ? 1.0 : 0.0, (bits & 2u) != 0u ? 1.0 : 0.0, (bits & 4u) != 0u ? 1.0 : 0.0, (bits & 8u) != 0u ? 1.0 : 0.0);
    smbOcclusion *= RelaxIsInScreenNearest(smbPixelUv);
    MLBilinear smbBilinear;
    float2 smbPos = saturate(smbPixelUv) * rc.gRectSizePrev;
    smbBilinear.origin = floor(smbPos - 0.5);
    smbBilinear.weights = frac(smbPos - 0.5);
    float smbFootprintQuality = sqrt(saturate(dot(MLGetBilinearCustomWeights(smbBilinear, smbOcclusion), 1.0)));

    // Effective stabilization strength: user knob * frame-count saturation; antilag feedback still runs even at 0.
    float stabilizationStrength = rc.gStabilizationStrength * (rc.gMaxStabilizedFrameNum / (1.0 + rc.gMaxStabilizedFrameNum));

    // ===== Diffuse =====
    float diffLumaM1, diffLumaSigma;
    NeighborhoodLumaMoments(pc.diffIndex, pc.viewZIndex, pixelPos, rc.gRectSize, rc, diffLumaM1, diffLumaSigma);
    float diffLuma = ReblurGetLuma(diffCur);

    // NRD short-history firefly cap; on sparse just-disoccluded signal M1 ~ luma/9 and it eats the band's energy, so default OFF.
    if (rc.gStabilizationFireflyCleanup != 0u && data1.x < rc.gHistoryFixFrameNum) {
        diffLuma = min(diffLuma, diffLumaM1 * (1.2 + 1.0 / (1.0 + data1.x)));
    }

    // The carried luma history aliases unwritten memory on the first frame, so seed it from the current signal.
    float smbDiffLumaHistory = diffLuma;
    if (rc.gResetHistory == 0) {
        smbDiffLumaHistory = max(FetchLumaHistory(pc.prevDiffLumaStabIndex, smbPixelUv, rc.gRectSizePrev, rc.gResourceSizeInvPrev, smbOcclusion), 0.0);
    }

    float diffAntilag = ReblurComputeAntilag(smbDiffLumaHistory, diffLumaM1, diffLumaSigma, smbFootprintQuality * data1.x, rc.gAntilagSettings, rc.gFramerateScale);

    // Antilag feedback: cut next frame's accumulation without re-invoking HistoryFix.
    float diffMinAccumSpeed = min(data1.x, rc.gHistoryFixFrameNum);
    data1.x = lerp(diffMinAccumSpeed, data1.x, diffAntilag);

    // History weight + color-box sigma scale.
    float diffW = smbFootprintQuality * (1.0 - ReblurGetAdvancedNonLinearAccumSpeed(data1.x, rc.gMaxAccumulatedFrameNum, rc.gConvergenceSettings.xyz)) * diffAntilag;
    float diffSigmaScale = 1.0 + 3.0 * rc.gFramerateScale * diffW;

    smbDiffLumaHistory = clamp(smbDiffLumaHistory, diffLumaM1 - diffLumaSigma * diffSigmaScale, diffLumaM1 + diffLumaSigma * diffSigmaScale);
    float diffLumaStabilized = lerp(diffLuma, smbDiffLumaHistory, min(diffW, stabilizationStrength));
    float4 diffStab = ReblurChangeLuma(diffCur, diffLumaStabilized);

    // ===== Specular =====
    float specLumaM1, specLumaSigma;
    NeighborhoodLumaMoments(pc.specIndex, pc.viewZIndex, pixelPos, rc.gRectSize, rc, specLumaM1, specLumaSigma);
    float specLuma = ReblurGetLuma(specCur);

    if (rc.gStabilizationFireflyCleanup != 0u && data1.y < rc.gHistoryFixFrameNum) {
        specLuma = min(specLuma, specLumaM1 * (1.2 + 1.0 / (1.0 + data1.y)));
    }

    // TA's min-3x3 world hit distance; must match what the DATA2 vmb occlusion bits were computed for.
    float hitDistForTracking = rdgTexturesFloat[pc.specHitDistIndex].Load(int3(pixelPos, 0)).r;

    // Virtual motion uv.
    float3 V = rc.gOrthoMode == 0.0 ? -normalize(X) : -normalize(rc.gFrustumForward.xyz);
    float3 Xvirtual = RelaxGetXvirtual(hitDistForTracking, curvature, X, Xprev, N, V, roughness);
    float2 vmbPixelUv = MLGetScreenUv(rc.gWorldToClipPrev, Xvirtual);

    float4 vmbOcclusion = float4((bits & 16u) != 0u ? 1.0 : 0.0, (bits & 32u) != 0u ? 1.0 : 0.0, (bits & 64u) != 0u ? 1.0 : 0.0, (bits & 128u) != 0u ? 1.0 : 0.0);
    vmbOcclusion *= RelaxIsInScreenNearest(vmbPixelUv);
    MLBilinear vmbBilinear;
    float2 vmbPos = saturate(vmbPixelUv) * rc.gRectSizePrev;
    vmbBilinear.origin = floor(vmbPos - 0.5);
    vmbBilinear.weights = frac(vmbPos - 0.5);
    float vmbFootprintQuality = sqrt(saturate(dot(MLGetBilinearCustomWeights(vmbBilinear, vmbOcclusion), 1.0)));

    float smbSpecLumaHistory = specLuma;
    float vmbSpecLumaHistory = specLuma;
    if (rc.gResetHistory == 0) {
        smbSpecLumaHistory = max(FetchLumaHistory(pc.prevSpecLumaStabIndex, smbPixelUv, rc.gRectSizePrev, rc.gResourceSizeInvPrev, smbOcclusion), 0.0);
        vmbSpecLumaHistory = max(FetchLumaHistory(pc.prevSpecLumaStabIndex, vmbPixelUv, rc.gRectSizePrev, rc.gResourceSizeInvPrev, vmbOcclusion), 0.0);
    }
    float specLumaHistory = lerp(smbSpecLumaHistory, vmbSpecLumaHistory, virtualHistoryAmount);

    float specFootprintQuality = lerp(smbFootprintQuality, vmbFootprintQuality, virtualHistoryAmount);
    float specAntilag = ReblurComputeAntilag(specLumaHistory, specLumaM1, specLumaSigma, specFootprintQuality * data1.y, rc.gAntilagSettings, rc.gFramerateScale);

    float specMinAccumSpeed = min(data1.y, rc.gHistoryFixFrameNum);
    data1.y = lerp(specMinAccumSpeed, data1.y, specAntilag);

    float specW = specFootprintQuality * (1.0 - ReblurGetAdvancedNonLinearAccumSpeed(data1.y, rc.gMaxAccumulatedFrameNum, rc.gConvergenceSettings.xyz)) * specAntilag;
    float specSigmaScale = 1.0 + 3.0 * rc.gFramerateScale * specW;

    // Weaken spec stabilization on smooth surfaces so reflections stay responsive.
    float responsiveFactor = ReblurRemapRoughnessToResponsiveFactor(roughness, rc.gResponsiveAccumulationInvRoughnessThreshold);
    float acceleration = lerp(RelaxGetSpecMagicCurve(roughness), 1.0, 0.5 + 0.5 * responsiveFactor);
    specW *= acceleration;

    float specLumaHistoryRaw = specLumaHistory;
    specLumaHistory = clamp(specLumaHistory, specLumaM1 - specLumaSigma * specSigmaScale, specLumaM1 + specLumaSigma * specSigmaScale);
    float specLumaStabilized = lerp(specLuma, specLumaHistory, min(specW, stabilizationStrength));
    // Up-scaling only where own luma is significant vs the neighborhood: unbounded explodes on near-zero luma, full saturate darkens short-history pixels.
    float specLumaRaw = ReblurGetLuma(specCur);
    float upAllowance = saturate(specLumaRaw / (0.25 * specLumaM1 + NRD_EPS));
    float specLumaScale = min(ReblurGetLumaScale(specLumaRaw, specLumaStabilized), lerp(1.0, 4.0, upAllowance));
    float4 specStab = specCur;
    specStab.xyz *= specLumaScale;

    // ===== Outputs =====
    // Carried luma histories for next-frame reprojection.
    rdgStorageFloat[pc.outDiffLumaStabIndex][pixelPos] = diffLumaStabilized;
    rdgStorageFloat[pc.outSpecLumaStabIndex][pixelPos] = specLumaStabilized;

    // Carried internal data: antilag-modified per-lobe accum speeds (+1 increment at pack).
    float maxAccum = min(rc.gMaxAccumulatedFrameNum, REBLUR_MAX_ACCUM_FRAME_NUM);
    rdgStorageUInt[pc.outInternalDataIndex][pixelPos] = ReblurPackInternalData(data1.x, data1.y, 0.0, maxAccum);

    // Final linear-RGB result for remodulation (max 0); keep normalized hit distance in .w.
    rdgStorageFloat4[pc.outDiffFinalIndex][pixelPos] = float4(max(MLYCoCgToRgb(diffStab.xyz), 0.0), diffStab.w);
    rdgStorageFloat4[pc.outSpecFinalIndex][pixelPos] = float4(max(MLYCoCgToRgb(specStab.xyz), 0.0), specStab.w);
}
