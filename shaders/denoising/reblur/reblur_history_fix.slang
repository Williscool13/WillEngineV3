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
import relax_utils;
import reblur_common;

// REBLUR_Config.hlsli (inlined): REBLUR_HISTORY_FIX_FILTER_RADIUS = 2, REBLUR_FAST_HISTORY_CLAMPING_RADIUS = 2.
static const int REBLUR_HISTORY_FIX_FILTER_RADIUS = 2;
static const int REBLUR_FAST_HISTORY_CLAMPING_RADIUS = 2;

/** Sparse cross-bilateral reconstruction over a (2*R+1)^2 neighborhood (center and corners skipped),
 *  taps placed at "stride" pixel spacing. Weighted by plane distance * normal. Reconstructs the full
 *  YCoCg + normalized-hitDist signal. */
float4 ReconstructHistory(uint texIdx, float2 pixelUv, float stride, float3 centerNormal, float3 centerWorldPos, float planeZ, float normalWeightParam,
    ReblurDiffuseSpecularConstants* rc, uint viewZIndex, uint normalRoughnessIndex, float3x3 viewToWorld)
{
    int2 centerPos = int2(pixelUv * float2(rc.gRectSize));
    float4 sum = rdgTexturesFloat4[texIdx].Load(int3(centerPos, 0));
    float weightSum = 1.0;

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
                sum += rdgTexturesFloat4[texIdx].Load(int3(pos, 0)) * w;
                weightSum += w;
            }
        }
    }
    return sum / weightSum;
}

/** Clamp "currentLuma" (slow-history luma) to the responsive fast-history luma box. The box center mixes
 *  the slow luma toward the fast luma by "f" (avgNorm history); clamp strength grows with frame count. */
float ClampToFastHistory(uint fastIdx, int2 pixelPos, float currentLuma, float f, float frameNum, ReblurDiffuseSpecularConstants* rc)
{
    float fastCenter = rdgTexturesFloat[fastIdx].Load(int3(pixelPos, 0)).r;
    fastCenter = lerp(currentLuma, fastCenter, f);

    float m1 = fastCenter;
    float m2 = fastCenter * fastCenter;
    [unroll]
    for (int j = -REBLUR_FAST_HISTORY_CLAMPING_RADIUS; j <= REBLUR_FAST_HISTORY_CLAMPING_RADIUS; j++) {
        [unroll]
        for (int i = -REBLUR_FAST_HISTORY_CLAMPING_RADIUS; i <= REBLUR_FAST_HISTORY_CLAMPING_RADIUS; i++) {
            if (i == 0 && j == 0) { continue; }
            int2 p = clamp(pixelPos + int2(i, j), int2(0, 0), rc.gRectSize - 1);
            float v = rdgTexturesFloat[fastIdx].Load(int3(p, 0)).r;
            m1 += v;
            m2 += v * v;
        }
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
        return;
    }

    float centerViewZ = RelaxLoadViewZ(pc.viewZIndex, pixelPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange)) {
        rdgStorageFloat4[pc.outDiffIndex][pixelPos] = diff;
        rdgStorageFloat4[pc.outSpecIndex][pixelPos] = spec;
        return;
    }

    float4 centerNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0)), viewToWorld);
    float3 centerNormal = centerNR.xyz;
    float centerRoughness = centerNR.w;
    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    float planeZ = rc.gOrthoMode == 0 ? centerViewZ : 1.0;
    float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;

    // Unified accumulated frame count (engine has no separate diff/spec DATA1; reblur_history_length serves both).
    float frameNum = 255.0 * rdgTexturesFloat[pc.historyLengthIndex].Load(int3(pixelPos, 0)).r;
    float invHistoryFixFrameNum = 1.0 / max(rc.gHistoryFixFrameNum, NRD_EPS);
    float frameNumAvgNorm = saturate(frameNum * invHistoryFixFrameNum);

    float frustumSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, min(float(rc.gRectSize.x), float(rc.gRectSize.y)), centerViewZ);
    float smc = RelaxGetSpecMagicCurve(centerRoughness);
    float nonLinearAccumSpeed = 1.0 / (1.0 + frameNum);

    // Base stride (NRD): gHistoryFixBasePixelStride / 2 (RELAX-matched "1" frame after temporal accumulation),
    // normalized to the 2-tap blur radius, and zeroed once converged.
    float baseStride = rc.gHistoryFixBasePixelStride;
    baseStride /= 1.0 + 1.0;
    baseStride *= 2.0 / float(REBLUR_HISTORY_FIX_FILTER_RADIUS);
    baseStride *= frameNum < rc.gHistoryFixFrameNum ? 1.0 : 0.0;

    float blend = 1.0 - frameNumAvgNorm;

    float4 outDiff = diff;
    float4 outSpec = spec;

    // ---- Diffuse short-history reconstruction ----
    {
        float hitDistFactor = RelaxGetHitDistFactor(diff.w, frustumSize);
        float diffStride = baseStride * lerp(0.25 + 0.75 * sqrt(saturate(hitDistFactor)), 1.0, nonLinearAccumSpeed);
        diffStride = round(diffStride);
        if (diffStride != 0.0) {
            float normalWeightParam = RelaxGetNormalWeightParam2(1.0, 0.5 * rc.gLobeAngleFraction);
            float4 reconstructed = ReconstructHistory(pc.diffIndex, pixelUv, diffStride, centerNormal, centerWorldPos, planeZ, normalWeightParam, rc, pc.viewZIndex, pc.normalRoughnessIndex, viewToWorld);
            outDiff = lerp(diff, reconstructed, blend);
        }
    }

    // ---- Specular short-history reconstruction ----
    {
        float hitDistFactor = RelaxGetHitDistFactor(spec.w, frustumSize);
        float specStride = baseStride * lerp(0.25 + 0.75 * sqrt(saturate(hitDistFactor)), 1.0, nonLinearAccumSpeed);
        specStride *= lerp(0.25, 1.0, smc);
        specStride = round(specStride);
        if (specStride != 0.0) {
            float normalWeightParam = RelaxGetNormalWeightParam2(centerRoughness, 0.5 * rc.gLobeAngleFraction);
            float4 reconstructed = ReconstructHistory(pc.specIndex, pixelUv, specStride, centerNormal, centerWorldPos, planeZ, normalWeightParam, rc, pc.viewZIndex, pc.normalRoughnessIndex, viewToWorld);
            outSpec = lerp(spec, reconstructed, blend);
        }
    }

    // ---- Fast-history reconciliation (clamp slow-history luma to the responsive fast-history box) ----
    {
        float diffLuma = ReblurGetLuma(outDiff);
        diffLuma = ClampToFastHistory(pc.diffFastIndex, pixelPos, diffLuma, frameNumAvgNorm, frameNum, rc);
        outDiff = ReblurChangeLuma(outDiff, diffLuma);

        float specLuma = ReblurGetLuma(outSpec);
        // HistoryFix-ed data is undesired in fast history for low roughness (NRD: f = lerp(1, avgNorm, smc)).
        float specF = lerp(1.0, frameNumAvgNorm, smc);
        specLuma = ClampToFastHistory(pc.specFastIndex, pixelPos, specLuma, specF, frameNum, rc);
        outSpec = ReblurChangeLuma(outSpec, specLuma);
    }

    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = outDiff;
    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = outSpec;
}
