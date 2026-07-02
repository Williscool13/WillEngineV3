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

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReSTIRDITemporal(uniform ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint field = pc.activeCheckerboardField;
    uint stride = RestirCheckerboardStride(pc.renderExtent.x, field);
    uint2 reservoirCoord = threadID.xy;
    if (reservoirCoord.x >= stride || reservoirCoord.y >= pc.renderExtent.y) {
        return;
    }
    uint2 pixel = ReservoirPosToPixelPos(reservoirCoord, field);
    if (pixel.x >= pc.renderExtent.x) {
        // Odd-width phantom column: past extent but still in the aliased buffer; keep initialized.
        pc.outputBuffer[reservoirCoord.y * stride + reservoirCoord.x] = ReservoirEmpty();
        return;
    }

    int2 partner = (field != 0u) ? OtherFieldPixel(pixel, field) : int2(-1, -1);
    bool partnerValid = partner.x >= 0 && partner.x < int(pc.renderExtent.x);
#if RESTIR_ENABLE_ANTILAG
    if (pc.shadowVisIndex != ~0u) {
        rdgStorageFloat[pc.shadowVisIndex][pixel] = 1.0;
        if (partnerValid) { rdgStorageFloat[pc.shadowVisIndex][uint2(partner)] = 1.0; }
    }
#endif
#if RESTIR_ENABLE_CONFIDENCE
    if (pc.signalIndex != ~0u) {
        rdgStorageFloat2[pc.signalIndex][pixel] = float2(0.0, 0.0);
        if (partnerValid) { rdgStorageFloat2[pc.signalIndex][uint2(partner)] = float2(0.0, 0.0); }
    }
#endif

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 fullPixel = pixel;

    uint pixelIdx = reservoirCoord.y * stride + reservoirCoord.x;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    if (depth <= 0.0) {
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float worldTexelSize = RestirWorldTexelSize(viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(N, V)));
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;

    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;
    bool validHistory = pc.historyBuffer != nullptr && all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));
    uint2 historyPixel = uint2(0, 0);
    bool bHistoryUsable = false;
    float3 historyN_cvs = N;

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;
    h ^= 0x6f3b91a7u;

    if (validHistory) {
        uint2 baseHistory = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
#if RESTIR_ENABLE_PERMUTATION_SAMPLING
        if (pc.bPermutationSampling != 0u) {
            int2 permuted = ApplyPermutationSampling(int2(baseHistory), pc.frameIndex);
            if (all(permuted >= int2(0, 0)) && all(permuted < int2(pc.renderExtent))) {
                baseHistory = uint2(permuted);
            }
        }
#endif
        uint searchTaps = 1u;
#if RESTIR_ENABLE_TEMPORAL_SEARCH
        if (pc.bTemporalSearch != 0u) { searchTaps = 5u; }
#endif
        float3 currWorldN = normalize(mul(float3x3(scene.invView), N));
        float NoV = abs(dot(N, normalize(viewPosition)));
        float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
        float disocclusionThreshold = saturate(0.01 / lerp(0.05, 1.0, NoV)) * frustumSize;
        float expectedPrevViewDepth = viewSpaceDepth + UnpackViewZDelta(g1.w);

        for (uint t = 0u; t < searchTaps; t++) {
            uint2 tapPixel = baseHistory;
            if (t > 0u) {
                float2 off = (float2(HashStep(h), HashStep(h)) - 0.5) * 8.0;
                int2 p = int2(baseHistory) + int2(round(off));
                if (any(p < int2(0, 0)) || any(p >= int2(pc.renderExtent))) { continue; }
                tapPixel = uint2(p);
            }
            if (field != 0u) {
                int2 snapped = ActivateCheckerboardPixel(int2(tapPixel), true, field);
                if (any(snapped < int2(0, 0)) || any(snapped >= int2(pc.renderExtent))) { continue; }
                tapPixel = uint2(snapped);
            }
            uint2 historyFullPixel = tapPixel;
            float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.prevGbufferOneIndex].Load(int3(historyFullPixel, 0)).r));
            float3 historyWorldN = normalize(mul(transpose(float3x3(scene.prevView)), historyN));
            if (dot(currWorldN, historyWorldN) < 0.906) { continue; }
            float historyRawDepth = rdgTexturesFloat[pc.prevDepthIndex].Load(int3(historyFullPixel, 0)).r;
            float historyViewDepth = ScreenToViewSpaceDepth(historyRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            if (abs(historyViewDepth - expectedPrevViewDepth) * NoV > disocclusionThreshold) { continue; }
            historyPixel = tapPixel;
            historyN_cvs = normalize(mul(float3x3(scene.view), historyWorldN));
            bHistoryUsable = true;
            break;
        }
    }

    Reservoir r_gen = pc.genBuffer[pixelIdx];
    if (isnan(r_gen.W) || isinf(r_gen.W)) { r_gen = ReservoirEmpty(); }

    uint clampedHistoryM = 0u;
    Reservoir history = ReservoirEmpty();

    if (bHistoryUsable) {
        uint2 histRPos = PixelPosToReservoirPos(historyPixel, field);
        Reservoir candidate = pc.historyBuffer[histRPos.y * stride + histRPos.x];
        if (isnan(candidate.W) || isinf(candidate.W)) { candidate = ReservoirEmpty(); }
        if (candidate.lightIdx != ~0u && (candidate.lightIdx == LIGHT_IDX_SUN || candidate.lightIdx < uint(pc.lightData->lightCount))) {
            history = candidate;
        }
    }

    bool validHistoryReservoir = history.lightIdx != ~0u;

    ReservoirAccum accum_combined = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();
    bool winnerIsGen = false;

    float uSelect0 = HashStep(h);
    float uSelect1 = HashStep(h);

    float Mc = (r_gen.lightIdx != ~0u) ? float(r_gen.M) : 0.0;
    clampedHistoryM = validHistoryReservoir ? min(history.M, pc.mCap) : 0u;
    float Mi = float(clampedHistoryM);
    float mFactorHist = 1.0;

    if (r_gen.lightIdx != ~0u) {
        float pGen_cur = EvalReservoirPHat(r_gen.lightIdx, r_gen.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
        float pGen_hist = 0.0;
        if (validHistoryReservoir) {
            pGen_hist = EvalReservoirPHat(r_gen.lightIdx, r_gen.sampleOffsetPacked, viewPosition, historyN_cvs, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
        }

        float denomGen = Mc * pGen_cur + Mi * pGen_hist;
        float misGen = (denomGen > 0.0) ? (Mc * pGen_cur) / denomGen : 0.0;
        float w_gen = misGen * pGen_cur * r_gen.W;
        if (ReservoirAddSample(accum_combined, w_gen, uSelect0)) {
            combined.lightIdx = r_gen.lightIdx;
            combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
            winnerIsGen = true;
        }
    }

    if (validHistoryReservoir) {
        float pHist_cur = EvalReservoirPHat(history.lightIdx, history.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
        float pHist_hist = EvalReservoirPHat(history.lightIdx, history.sampleOffsetPacked, viewPosition, historyN_cvs, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
        mFactorHist = (pHist_hist > 0.0) ? saturate(pHist_cur / pHist_hist) : 1.0;

        float denomHist = Mi * pHist_hist + Mc * pHist_cur;
        float misHist = (denomHist > 0.0) ? (Mi * pHist_hist) / denomHist : 0.0;
        float w_history = misHist * pHist_cur * history.W;
        if (ReservoirAddSample(accum_combined, w_history, uSelect1)) {
            combined.lightIdx = history.lightIdx;
            combined.sampleOffsetPacked = history.sampleOffsetPacked;
            winnerIsGen = false;
        }
    }

#if RESTIR_ENABLE_INITIAL_VISIBILITY
    if (pc.bInitialVisibility != 0u && combined.lightIdx == ~0u && r_gen.lightIdx != ~0u) {
        combined.lightIdx = r_gen.lightIdx;
        combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
    }
#endif

    combined.M = r_gen.M + uint(float(clampedHistoryM) * mFactorHist + 0.5);

    float winnerVisibility = 1.0;
#if RESTIR_ENABLE_CONFIDENCE
    float visPrev = 1.0;
#endif
    float pHatUnshadowed = 0.0;
    if (combined.lightIdx != ~0u) {
        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        pHatUnshadowed = EvalReservoirPHat(combined.lightIdx, combined.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);

        float p_hat_winner;
#if RESTIR_ENABLE_INITIAL_VISIBILITY
        if (pc.bInitialVisibility != 0u && winnerIsGen) {
            p_hat_winner = pHatUnshadowed;
        } else {
            p_hat_winner = EvalReservoirPHatVisibility(combined.lightIdx, combined.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, scene.invView, pc.lightData->directionalLight, pc.lightVS, rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldTexelSize);
        }
#else
        p_hat_winner = EvalReservoirPHatVisibility(combined.lightIdx, combined.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, scene.invView, pc.lightData->directionalLight, pc.lightVS, rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldTexelSize);
#endif
        ReservoirFinalise(combined, accum_combined, p_hat_winner, 1.0);
        winnerVisibility = (p_hat_winner > 0.0) ? 1.0 : 0.0;

#if RESTIR_ENABLE_CONFIDENCE
        // Re-shade the same winner against the previous frame's TLAS: occluder motion flips the pair and drives the gradient.
        if (pc.prevTlasIndex != ~0u) {
            if (combined.lightIdx == LIGHT_IDX_SUN) {
                float3 worldLsun = normalize(UnpackNormalOct16(combined.sampleOffsetPacked));
                visPrev = RestirSunShadowVisible(rdgAccelerationStructures[pc.prevTlasIndex], worldPos, worldN, worldLsun, worldTexelSize) ? 1.0 : 0.0;
            } else {
                LightVS vsWinner = UnpackLightVS(pc.lightVS[combined.lightIdx]);
                float3 worldyStar = mul(scene.invView, float4(DecodeSamplePos(combined.sampleOffsetPacked, vsWinner), 1.0)).xyz;
                visPrev = RestirShadowVisible(rdgAccelerationStructures[pc.prevTlasIndex], worldPos, worldN, worldyStar, worldTexelSize) ? 1.0 : 0.0;
            }
        } else {
            visPrev = winnerVisibility;
        }
#endif
    }

#if RESTIR_ENABLE_ANTILAG
    float flip = 0.0;
    if (pc.prevShadowVisIndex != ~0u && bHistoryUsable && validHistoryReservoir) {
        float prevVis = rdgTexturesFloat[pc.prevShadowVisIndex].Load(int3(historyPixel, 0)).r;
        flip = abs(winnerVisibility - prevVis);
    }

    if (pc.antilagStrength > 0.0 && flip > 0.0) {
        combined.M = max(1u, uint(float(combined.M) * (1.0 - pc.antilagStrength * flip)));
    }
#endif

    pc.outputBuffer[pixelIdx] = combined;

#if RESTIR_ENABLE_ANTILAG
    if (pc.shadowVisIndex != ~0u) {
        rdgStorageFloat[pc.shadowVisIndex][pixel] = winnerVisibility;
        if (field != 0u) {
            int2 o = OtherFieldPixel(pixel, field);
            if (o.x >= 0 && o.x < int(pc.renderExtent.x)) { rdgStorageFloat[pc.shadowVisIndex][uint2(o)] = winnerVisibility; }
        }
    }
#endif
#if RESTIR_ENABLE_CONFIDENCE
    if (pc.signalIndex != ~0u) {
        float2 sig = float2(pHatUnshadowed * (winnerVisibility - visPrev), pHatUnshadowed * max(winnerVisibility, visPrev));
        rdgStorageFloat2[pc.signalIndex][pixel] = sig;
        if (field != 0u) {
            int2 o = OtherFieldPixel(pixel, field);
            if (o.x >= 0 && o.x < int(pc.renderExtent.x)) { rdgStorageFloat2[pc.signalIndex][uint2(o)] = sig; }
        }
    }
#endif
}
