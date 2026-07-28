/*
 * Directional-sun reservoir: fused candidate generation + temporal reuse, one anyhit shadow ray per
 * reservoir per frame on the merged winner. Over the sun's angular disk the target pdf is flat, so a
 * single uniform cone sample is a complete candidate set (RIS over more candidates selects nothing a
 * fresh sample doesn't). No spatial reuse: the only thing it would transfer is the stored visibility
 * bit, which smears shadows; all spatial work belongs to the denoiser.
 * Reservoir encoding: sampleOffsetPacked = unorm16x2 cone sample (u1, u2), re-anchored to the current
 * sun axis at decode. lightIdx: ~0u empty, 0u winner shadowed, 1u winner lit. W deliberately excludes
 * visibility: the winner is re-traced every frame, so folding visibility into the carried W would only
 * let stale-dark history dilute recovery through the MIS denominator (~M frames of unshadowing lag).
 */
import common_interop;
import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import restir_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;

import bindless_declarations;
import restir_common;
import random_common;

#include "restir_features_macros.h"

// Blocker-distance drift as a pseudo-flip for the confidence signal: coherent occluder motion along the
// ray survives the gradient's stratum mean, stochastic penumbra noise cancels (direction persistence
// makes the re-trace deterministic in static scenes).
static const float BLOCKER_SENSITIVITY = 4.0;
static const float BLOCKER_NORM_BASE = 1.0;
static const float BLOCKER_FRACTION_ALPHA = 0.125;
static const float BLOCKER_DIST_ALPHA = 0.25;
static const float BLOCKER_TRUST_FRACTION = 0.25;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReSTIRDISun(uniform ReSTIRDISunPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint field = pc.activeCheckerboardField;
    uint stride = RestirCheckerboardStride(pc.renderExtent.x, field);
    uint2 reservoirCoord = threadID.xy;
    if (reservoirCoord.x >= stride || reservoirCoord.y >= pc.renderExtent.y) {
        return;
    }
    uint2 pixel = ReservoirPosToPixelPos(reservoirCoord, field);
    uint pixelIdx = reservoirCoord.y * stride + reservoirCoord.x;
    if (pixel.x >= pc.renderExtent.x) {
        // Odd-width phantom column: past extent but still in the aliased buffer; keep initialized.
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    LightingInputs li = ReconstructLightingInputs(pixel, g1, pc.gbufferTwoIndex);

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float worldTexelSize = RayOriginPixelFootprint(viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(N, V)));
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 worldN = normalize(mul(float3x3(scene.invView), N));

    DirectionalLightData sun = pc.lightData->directionalLight;
    float3 sunAxisW = SunDirectionWorld(sun);
    float3 sunRad = SunRadiance(sun);

    float2 candXi = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 1u);
    float candPdf;
    float3 candDirW = SampleSunConeDirection(sunAxisW, sun.angularRadius, candXi.x, candXi.y, candPdf);
    float3 candDirV = normalize(mul(float3x3(scene.view), candDirW));
    float pCand_cur = EvalSunPHat(candDirV, V, N, li.albedo.xyz, F0, li.metallic, roughness, sunRad);

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;
    h ^= 0x51ed270bu;

    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;
    bool bHistoryUsable = false;
    float3 historyN_cvs = N;
    uint2 historyPixel = uint2(0, 0);

    if (pc.historyBuffer != nullptr && all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0))) {
        uint2 tapPixel = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
        bool bInBounds = true;
        if (field != 0u) {
            int2 snapped = ActivateCheckerboardPixel(int2(tapPixel), true, field);
            bInBounds = all(snapped >= int2(0, 0)) && all(snapped < int2(pc.renderExtent));
            if (bInBounds) { tapPixel = uint2(snapped); }
        }
        if (bInBounds) {
            float NoV = abs(dot(N, normalize(viewPosition)));
            float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
            float disocclusionThreshold = saturate(0.01 / lerp(0.05, 1.0, NoV)) * frustumSize;
            float expectedPrevViewDepth = viewSpaceDepth + UnpackViewZDelta(g1.w);

            float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.prevGbufferOneIndex].Load(int3(tapPixel, 0)).r));
            float3 historyWorldN = normalize(mul(transpose(float3x3(scene.prevView)), historyN));
            if (dot(worldN, historyWorldN) >= 0.906) {
                float historyRawDepth = rdgTexturesFloat[pc.prevDepthIndex].Load(int3(tapPixel, 0)).r;
                float historyViewDepth = ScreenToViewSpaceDepth(historyRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                if (abs(historyViewDepth - expectedPrevViewDepth) * NoV <= disocclusionThreshold) {
                    historyPixel = tapPixel;
                    historyN_cvs = normalize(mul(float3x3(scene.view), historyWorldN));
                    bHistoryUsable = true;
                }
            }
        }
    }

    Reservoir history = ReservoirEmpty();
    if (bHistoryUsable) {
        uint2 histRPos = PixelPosToReservoirPos(historyPixel, field);
        Reservoir candidate = pc.historyBuffer[histRPos.y * stride + histRPos.x];
        if (!isnan(candidate.W) && !isinf(candidate.W) && candidate.lightIdx != ~0u) {
            history = candidate;
        }
    }
    bool validHistory = history.lightIdx != ~0u;
    uint clampedHistoryM = validHistory ? min(history.M, pc.mCap) : 0u;
    float Mi = float(clampedHistoryM);

    ReservoirAccum accum = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();
    combined.sampleOffsetPacked = PackUnorm2x16(candXi);

    float pWinner_cur = 0.0;
    float3 winnerDirW = candDirW;

    float uSelect0 = HashStep(h);
    float uSelect1 = HashStep(h);

    {
        float pCand_hist = validHistory ? EvalSunPHat(candDirV, V, historyN_cvs, li.albedo.xyz, F0, li.metallic, roughness, sunRad) : 0.0;
        float denomCand = pCand_cur + Mi * pCand_hist;
        float misCand = (denomCand > 0.0) ? (pCand_cur / denomCand) : 0.0;
        // Candidate as a one-sample reservoir: W_c = (p_hat/pdf)/p_hat = 1/pdf.
        float w_cand = (pCand_cur > 0.0) ? (misCand * pCand_cur / candPdf) : 0.0;
        if (ReservoirAddSample(accum, w_cand, uSelect0)) {
            pWinner_cur = pCand_cur;
        }
    }

    if (validHistory) {
        float2 histXi = UnpackUnorm2x16(history.sampleOffsetPacked);
        float histPdf;
        float3 histDirW = SampleSunConeDirection(sunAxisW, sun.angularRadius, histXi.x, histXi.y, histPdf);
        float3 histDirV = normalize(mul(float3x3(scene.view), histDirW));
        float pHist_cur = EvalSunPHat(histDirV, V, N, li.albedo.xyz, F0, li.metallic, roughness, sunRad);
        float pHist_hist = EvalSunPHat(histDirV, V, historyN_cvs, li.albedo.xyz, F0, li.metallic, roughness, sunRad);
        float denomHist = Mi * pHist_hist + pHist_cur;
        float misHist = (denomHist > 0.0) ? (Mi * pHist_hist / denomHist) : 0.0;
        float w_hist = misHist * pHist_cur * history.W;
        if (ReservoirAddSample(accum, w_hist, uSelect1)) {
            combined.sampleOffsetPacked = history.sampleOffsetPacked;
            pWinner_cur = pHist_cur;
            winnerDirW = histDirW;
        }
    }

    combined.M = 1u + clampedHistoryM;

    bool bTraced = accum.wSum > 0.0 && pWinner_cur > 0.0;
    float blockerT = -1.0;
    if (bTraced) {
        ReservoirFinalize(combined, accum, pWinner_cur, 1.0);
        blockerT = RestirSunShadowBlockerT(rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, winnerDirW, worldTexelSize);
        combined.lightIdx = (blockerT < 0.0) ? 1u : 0u;
    } else {
        // Facing away from the sun: definitively shadowed, no ray needed.
        combined.W = 0.0;
        combined.lightIdx = 0u;
    }

    pc.outputBuffer[pixelIdx] = combined;

#if RESTIR_ENABLE_CONFIDENCE
    if (bTraced) {
        bool bOccluded = blockerT >= 0.0;
        float winnerVis = bOccluded ? 0.0 : 1.0;

        float blockerFlip = 0.0;
        if (pc.blockerIndex != ~0u) {
            float2 prevAcc = float2(0.0, 0.0);
            bool bAccValid = false;
            if (bHistoryUsable && pc.prevBlockerIndex != ~0u) {
                prevAcc = rdgTexturesFloat2[pc.prevBlockerIndex].Load(int3(historyPixel, 0)).rg;
                if (!any(isnan(prevAcc)) && !any(isinf(prevAcc))) { bAccValid = true; }
            }
            float accT = bAccValid ? prevAcc.x : 0.0;
            float fr = bAccValid ? prevAcc.y : 0.0;
            float frNew = lerp(fr, bOccluded ? 1.0 : 0.0, bAccValid ? BLOCKER_FRACTION_ALPHA : 1.0);
            float accTNew = accT;
            if (bOccluded) {
                // Delta vs the ACCUMULATED blocker distance, and only where occluded history exists:
                // raw-vs-raw at 1spp is a noise detector, and lit<->shadowed transitions belong to the
                // visibility-flip term below, so unshadowed samples need no sentinel here.
                if (bAccValid && fr > BLOCKER_TRUST_FRACTION) {
                    blockerFlip = clamp(BLOCKER_SENSITIVITY * (blockerT - accT) / (abs(viewSpaceDepth) + BLOCKER_NORM_BASE), -1.0, 1.0);
                }
                accTNew = (bAccValid && fr > 0.0) ? lerp(accT, blockerT, BLOCKER_DIST_ALPHA) : blockerT;
            }
            rdgStorageFloat2[pc.blockerIndex][pixel] = float2(accTNew, frNew);
        }

        if (pc.signalIndex != ~0u) {
            float visPrev = winnerVis;
            if (pc.prevTlasIndex != ~0u) {
                visPrev = RestirSunShadowVisible(rdgAccelerationStructures[pc.prevTlasIndex], worldPos, worldN, winnerDirW, worldTexelSize) ? 1.0 : 0.0;
            }
            float sunFlip = clamp((winnerVis - visPrev) + blockerFlip, -1.0, 1.0);
            float sunMag = max(max(winnerVis, visPrev), abs(blockerFlip));
            float2 sunSig = float2(pWinner_cur * sunFlip, pWinner_cur * sunMag);

            float2 sig = rdgStorageFloat2[pc.signalIndex][pixel] + sunSig;
            rdgStorageFloat2[pc.signalIndex][pixel] = sig;
            if (field != 0u) {
                int2 o = OppositeFieldPixel(pixel, field);
                if (o.x >= 0 && o.x < int(pc.renderExtent.x)) {
                    rdgStorageFloat2[pc.signalIndex][uint2(o)] = rdgStorageFloat2[pc.signalIndex][uint2(o)] + sunSig;
                }
            }
        }
    } else if (pc.blockerIndex != ~0u) {
        // No trace this frame (facing away): decay the occluded fraction, keep the accumulated distance.
        float2 prevAcc = (bHistoryUsable && pc.prevBlockerIndex != ~0u) ? rdgTexturesFloat2[pc.prevBlockerIndex].Load(int3(historyPixel, 0)).rg : float2(0.0, 0.0);
        if (any(isnan(prevAcc)) || any(isinf(prevAcc))) { prevAcc = float2(0.0, 0.0); }
        rdgStorageFloat2[pc.blockerIndex][pixel] = float2(prevAcc.x, prevAcc.y * (1.0 - BLOCKER_FRACTION_ALPHA));
    }
#endif
}
