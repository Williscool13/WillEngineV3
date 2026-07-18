/*
 * ReBLUR: HistoryFix. Bootstraps recently-disoccluded pixels (short accumulation) by spatially
 * reconstructing the slow (accumulated) history from a sparse cross-bilateral neighborhood whose reach
 * grows for shorter history, then reconciles the slow history against the responsive "fast" (luma)
 * history by clamping the slow-history luma to the fast-history box. Signal is YCoCg radiance in .xyz
 * with a normalized hit distance in .w.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import denoiser_common;
import reblur_common;

// REBLUR_Config.hlsli (inlined).
static const int REBLUR_HISTORY_FIX_FILTER_RADIUS = 2;
static const int REBLUR_FAST_HISTORY_CLAMPING_RADIUS = 2;
static const int REBLUR_ANTI_FIREFLY_FILTER_RADIUS = 4;
static const float REBLUR_ANTI_FIREFLY_SIGMA_SCALE = 2.0;

/** Sparse cross-bilateral reconstruction over a (2*R+1)^2 neighborhood (center/corners skipped), taps
 *  at "stride" spacing, weighted by plane*normal*(1+tap accum frames)*hit-dist so converged neighbors dominate. */
float4 ReconstructHistory(uint texIdx, uint data1Index, bool useSpecChannel, float2 pixelUv, float stride, float centerFrameNum, float nonLinearAccumSpeed,
    float3 centerNormal, float3 centerWorldPos, float planeZ, float normalWeightParam,
    ReblurDiffuseSpecularConstants* rc, uint viewZIndex, uint normalRoughnessIndex, float3x3 viewToWorld)
{
    int2 centerPos = int2(pixelUv * float2(rc.gRectSize));
    float4 center = rdgTexturesFloat4[texIdx].Load(int3(centerPos, 0));
    float centerHitDist = center.w;
    float hitDistWeightNorm = 1.0 / (0.5 * nonLinearAccumSpeed);
    float weightSum = 1.0 + centerFrameNum;
    float4 sum = center * weightSum;

    [unroll]
    for (int j = -REBLUR_HISTORY_FIX_FILTER_RADIUS; j <= REBLUR_HISTORY_FIX_FILTER_RADIUS; j++) {
        [unroll]
        for (int i = -REBLUR_HISTORY_FIX_FILTER_RADIUS; i <= REBLUR_HISTORY_FIX_FILTER_RADIUS; i++) {
            if (i == 0 && j == 0) { continue; }
            if (abs(i) + abs(j) == REBLUR_HISTORY_FIX_FILTER_RADIUS * 2) { continue; }

            float2 uv = pixelUv + float2(i, j) * stride * rc.gRectSizeInv;
            uv = RelaxMirrorUv(uv);
            int2 pos = int2(uv * float2(rc.gRectSize));

            float sampleViewZ = RelaxLoadViewZ(viewZIndex, pos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange)) { continue; }

            float3 sampleNormal = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[normalRoughnessIndex].Load(int3(pos, 0)), viewToWorld).xyz;
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float w = RelaxGetPlaneDistanceWeight(centerWorldPos, centerNormal, planeZ, sampleWorldPos, rc.gPlaneDistSensitivity);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNormal));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);
            if (w > 1e-4) {
                float2 tapFrames = ReblurUnpackData1(rdgTexturesFloat2[data1Index].Load(int3(pos, 0)));
                w *= 1.0 + (useSpecChannel ? tapFrames.y : tapFrames.x);
                float4 s = rdgTexturesFloat4[texIdx].Load(int3(pos, 0));
                float d = s.w - centerHitDist;
                w *= exp(-d * d * hitDistWeightNorm);
                sum += s * w;
                weightSum += w;
            }
        }
    }
    return sum / weightSum;
}

/** Clamp currentLuma (slow-history) to the responsive fast-history luma box; the mixed box-center is
 *  written back via fastCenterOut so the carried fast history includes this reconciliation.
 *  If antiFirefly, luma is first clamped to a wide-ring (radius 4, central 3x3 skipped) sigma box. */
float ClampToFastHistory(uint fastIdx, uint viewZIndex, int2 pixelPos, float currentLuma, float f, float frameNum, bool antiFirefly, ReblurDiffuseSpecularConstants* rc, out float fastCenterOut)
{
    float fastCenter = rdgTexturesFloat[fastIdx].Load(int3(pixelPos, 0)).r;
    fastCenter = lerp(currentLuma, fastCenter, f);
    fastCenterOut = fastCenter;

    float m1 = fastCenter;
    float m2 = fastCenter * fastCenter;
    float af1 = 0.0;
    float af2 = 0.0;
    [unroll]
    for (int j = -REBLUR_FAST_HISTORY_CLAMPING_RADIUS; j <= REBLUR_FAST_HISTORY_CLAMPING_RADIUS; j++) {
        [unroll]
        for (int i = -REBLUR_FAST_HISTORY_CLAMPING_RADIUS; i <= REBLUR_FAST_HISTORY_CLAMPING_RADIUS; i++) {
            if (i == 0 && j == 0) { continue; }
            int2 p = clamp(pixelPos + int2(i, j), int2(0, 0), rc.gRectSize - 1);
            float v = fastCenter;
            float z = RelaxLoadViewZ(viewZIndex, p, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (RelaxIsInDenoisingRange(z, rc.gDenoisingRange)) {
                v = rdgTexturesFloat[fastIdx].Load(int3(p, 0)).r;
            }
            m1 += v;
            m2 += v * v;
            if (!(abs(i) <= 1 && abs(j) <= 1)) {
                af1 += v;
                af2 += v * v;
            }
        }
    }

    if (antiFirefly) {
        for (int j2 = -REBLUR_ANTI_FIREFLY_FILTER_RADIUS; j2 <= REBLUR_ANTI_FIREFLY_FILTER_RADIUS; j2++) {
            for (int i2 = -REBLUR_ANTI_FIREFLY_FILTER_RADIUS; i2 <= REBLUR_ANTI_FIREFLY_FILTER_RADIUS; i2++) {
                if (abs(i2) <= REBLUR_FAST_HISTORY_CLAMPING_RADIUS && abs(j2) <= REBLUR_FAST_HISTORY_CLAMPING_RADIUS) { continue; }
                int2 p = clamp(pixelPos + int2(i2, j2), int2(0, 0), rc.gRectSize - 1);
                float v = fastCenter;
                float z = RelaxLoadViewZ(viewZIndex, p, rc.depthLinearizeMult, rc.depthLinearizeAdd);
                if (RelaxIsInDenoisingRange(z, rc.gDenoisingRange)) {
                    v = rdgTexturesFloat[fastIdx].Load(int3(p, 0)).r;
                }
                af1 += v;
                af2 += v * v;
            }
        }
        float invNormAf = 1.0 / float((REBLUR_ANTI_FIREFLY_FILTER_RADIUS * 2 + 1) * (REBLUR_ANTI_FIREFLY_FILTER_RADIUS * 2 + 1) - 9);
        af1 *= invNormAf;
        af2 *= invNormAf;
        float afSigma = sqrt(max(af2 - af1 * af1, 0.0)) * REBLUR_ANTI_FIREFLY_SIGMA_SCALE;
        currentLuma = clamp(currentLuma, af1 - afSigma, af1 + afSigma);
    }

    float invNorm = 1.0 / float((REBLUR_FAST_HISTORY_CLAMPING_RADIUS * 2 + 1) * (REBLUR_FAST_HISTORY_CLAMPING_RADIUS * 2 + 1));
    m1 *= invNorm;
    m2 *= invNorm;

    float sigma = sqrt(max(m2 - m1 * m1, 0.0)) * rc.gFastHistoryClampingSigmaScale;
    float clamped = clamp(currentLuma, m1 - sigma, m1 + sigma);

    // Clamp weakens for short history (just reconstructed) and strengthens as the slow history accumulates.
    float clampLerp = 1.0 / (1.0 + float(rc.gMaxFastAccumulatedFrameNum < rc.gMaxAccumulatedFrameNum) * frameNum * 2.0);
    return lerp(clamped, currentLuma, clampLerp);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ReblurHistoryFixMain(uniform ReblurHistoryFixPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;
    float3x3 viewToWorld = (float3x3)rc.gViewToWorld;

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    float4 diff = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixelPos, 0));
    float4 spec = rdgTexturesFloat4[pc.specIndex].Load(int3(pixelPos, 0));
    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y) {
        rdgStorageFloat4[pc.outDiffIndex][pixelPos] = diff;
        rdgStorageFloat4[pc.outSpecIndex][pixelPos] = spec;
        rdgStorageFloat[pc.outDiffFastIndex][pixelPos] = rdgTexturesFloat[pc.diffFastIndex].Load(int3(pixelPos, 0)).r;
        rdgStorageFloat[pc.outSpecFastIndex][pixelPos] = rdgTexturesFloat[pc.specFastIndex].Load(int3(pixelPos, 0)).r;
        return;
    }

    float centerViewZ = RelaxLoadViewZ(pc.viewZIndex, pixelPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange)) {
        rdgStorageFloat4[pc.outDiffIndex][pixelPos] = diff;
        rdgStorageFloat4[pc.outSpecIndex][pixelPos] = spec;
        rdgStorageFloat[pc.outDiffFastIndex][pixelPos] = rdgTexturesFloat[pc.diffFastIndex].Load(int3(pixelPos, 0)).r;
        rdgStorageFloat[pc.outSpecFastIndex][pixelPos] = rdgTexturesFloat[pc.specFastIndex].Load(int3(pixelPos, 0)).r;
        return;
    }

    float4 centerNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0)), viewToWorld);
    float3 centerNormal = centerNR.xyz;
    float centerRoughness = centerNR.w;
    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    float planeZ = rc.gOrthoMode == 0 ? centerViewZ : 1.0;
    float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;

    // Per-lobe accumulated frame counts (DATA1: x = diff, y = spec).
    float2 frameNums = ReblurUnpackData1(rdgTexturesFloat2[pc.data1Index].Load(int3(pixelPos, 0)));
    float invHistoryFixFrameNum = 1.0 / max(rc.gHistoryFixFrameNum, NRD_EPS);
    float2 frameNumAvgNorm = saturate(frameNums * invHistoryFixFrameNum);

    float frustumSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, min(float(rc.gRectSize.x), float(rc.gRectSize.y)), centerViewZ);
    float smc = RelaxGetSpecMagicCurve(centerRoughness);
    float diffNonLinearAccumSpeed = 1.0 / (1.0 + frameNums.x);
    float specNonLinearAccumSpeed = 1.0 / (1.0 + frameNums.y);

    // Base stride (NRD): gHistoryFixBasePixelStride / 2 (RELAX-matched "1" frame after temporal accumulation),
    // normalized to the 2-tap blur radius, and zeroed per lobe once converged.
    float baseStride = rc.gHistoryFixBasePixelStride;
    baseStride /= 1.0 + 1.0;
    baseStride *= 2.0 / float(REBLUR_HISTORY_FIX_FILTER_RADIUS);
    float diffBaseStride = baseStride * (frameNums.x < rc.gHistoryFixFrameNum ? 1.0 : 0.0);
    float specBaseStride = baseStride * (frameNums.y < rc.gHistoryFixFrameNum ? 1.0 : 0.0);

    float4 outDiff = diff;
    float4 outSpec = spec;

    // ---- Diffuse short-history reconstruction ----
    {
        // Signal .w is normalized; de-normalize for the world-domain hitDistFactor.
        float hitDistFactor = RelaxGetHitDistFactor(ReblurDenormHitDist(diff.w, centerViewZ, rc.gHitDistParams, 1.0), frustumSize);
        float diffStride = diffBaseStride * lerp(0.25 + 0.75 * sqrt(saturate(hitDistFactor)), 1.0, diffNonLinearAccumSpeed);
        diffStride = round(diffStride);
        if (diffStride != 0.0) {
            float normalWeightParam = RelaxGetNormalWeightParam2(1.0, 0.5 * rc.gLobeAngleFraction);
            outDiff = ReconstructHistory(pc.diffIndex, pc.data1Index, false, pixelUv, diffStride, frameNums.x, diffNonLinearAccumSpeed, centerNormal, centerWorldPos, planeZ, normalWeightParam, rc, pc.viewZIndex, pc.normalRoughnessIndex, viewToWorld);
        }
    }

    // ---- Specular short-history reconstruction ----
    {
        float hitDistFactor = RelaxGetHitDistFactor(ReblurDenormHitDist(spec.w, centerViewZ, rc.gHitDistParams, centerRoughness), frustumSize);
        float specStride = specBaseStride * lerp(0.25 + 0.75 * sqrt(saturate(hitDistFactor)), 1.0, specNonLinearAccumSpeed);
        specStride *= lerp(0.25, 1.0, smc);
        specStride = round(specStride);
        if (specStride != 0.0) {
            float normalWeightParam = RelaxGetNormalWeightParam2(centerRoughness, 0.5 * rc.gLobeAngleFraction);
            outSpec = ReconstructHistory(pc.specIndex, pc.data1Index, true, pixelUv, specStride, frameNums.y, specNonLinearAccumSpeed, centerNormal, centerWorldPos, planeZ, normalWeightParam, rc, pc.viewZIndex, pc.normalRoughnessIndex, viewToWorld);
        }
    }

    // ---- Fast-history reconciliation + optional anti-firefly ring clamp; mixed fast center is
    // written back so the carried fast history includes the reconciliation. ----
    {
        bool antiFirefly = rc.gAntiFirefly != 0u;

        float diffFastCenter;
        float diffLuma = ReblurGetLuma(outDiff);
        diffLuma = ClampToFastHistory(pc.diffFastIndex, pc.viewZIndex, pixelPos, diffLuma, frameNumAvgNorm.x, frameNums.x, antiFirefly, rc, diffFastCenter);
        outDiff = ReblurChangeLuma(outDiff, diffLuma);
        rdgStorageFloat[pc.outDiffFastIndex][pixelPos] = diffFastCenter;

        float specLuma = ReblurGetLuma(outSpec);
        // HistoryFix-ed data is undesired in fast history for low roughness (NRD: f = lerp(1, avgNorm, smc)).
        float specF = lerp(1.0, frameNumAvgNorm.y, smc);
        float specFastCenter;
        specLuma = ClampToFastHistory(pc.specFastIndex, pc.viewZIndex, pixelPos, specLuma, specF, frameNums.y, antiFirefly, rc, specFastCenter);
        outSpec = ReblurChangeLuma(outSpec, specLuma);
        rdgStorageFloat[pc.outSpecFastIndex][pixelPos] = specFastCenter;
    }

    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = outDiff;
    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = outSpec;
}
