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

#include "restir_features_macros.slang"

// ReSTIR DI temporal reuse.
[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

#if RESTIR_ENABLE_ANTILAG
    if (pc.shadowVisIndex != ~0u) { rdgStorageFloat[pc.shadowVisIndex][pixel] = 1.0; }
#endif
#if RESTIR_ENABLE_CONFIDENCE
    // restir_signal (RG16F): re-shade visibility delta + brightness normalizer for the gradient passes. Sky/invalid texels carry 0.
    if (pc.signalIndex != ~0u) { rdgStorageFloat2[pc.signalIndex][pixel] = float2(0.0, 0.0); }
#endif

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 fullPixel = FetchFullPixel(pc.quadSelectionIndex, pixel, pc.pixelScale);

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;

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
    float worldTexelSize = RestirWorldTexelSize(viewSpaceDepth, scene.ndcToViewMulXPixelSize, pc.pixelScale);
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;

    // History reprojection validity.
    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;
    bool validHistory = pc.historyBuffer != nullptr && all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));
    uint2 historyPixel = uint2(0, 0);
    bool bHistoryUsable = false;
    float3 historyN_cvs = N;

    if (validHistory) {
        historyPixel = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
#if RESTIR_ENABLE_PERMUTATION_SAMPLING
        if (pc.bPermutationSampling != 0u) {
            int2 permuted = ApplyPermutationSampling(int2(historyPixel), pc.frameIndex);
            if (all(permuted >= int2(0, 0)) && all(permuted < int2(pc.renderExtent))) {
                historyPixel = uint2(permuted);
            }
        }
#endif
        // History taps use the corner the previous frame's passes sampled (previous depth source)
        uint2 historyFullPixel = FetchFullPixel(pc.quadSelectionHistoryIndex, historyPixel, pc.pixelScale);

        // Normal disocclusion: gbuffer normals are view-space, so compare in world space; a raw view-space dot falsely rejects history under camera rotation.
        float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.prevGbufferOneIndex].Load(int3(historyFullPixel, 0)).r));
        float3 currWorldN = normalize(mul(float3x3(scene.invView), N));
        float3 historyWorldN = normalize(mul(transpose(float3x3(scene.prevView)), historyN));
        historyN_cvs = normalize(mul(float3x3(scene.view), historyWorldN));
        if (dot(currWorldN, historyWorldN) >= 0.906) {
            // Plane-distance disocclusion (NRD style)
            float historyRawDepth = rdgTexturesFloat[pc.prevDepthIndex].Load(int3(historyFullPixel, 0)).r;
            float historyViewDepth = ScreenToViewSpaceDepth(historyRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float expectedPrevViewDepth = viewSpaceDepth + UnpackViewZDelta(g1.w);
            float NoV = abs(dot(N, normalize(viewPosition)));
            float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
            float disocclusionThreshold = saturate(0.01 / lerp(0.05, 1.0, NoV)) * frustumSize;
            if (abs(historyViewDepth - expectedPrevViewDepth) * NoV <= disocclusionThreshold) {
                bHistoryUsable = true;
            }
        }
    }

    // Stream-selection randoms, salted to decorrelate from the base pass generation stream.
    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;
    h ^= 0x6f3b91a7u;

    // Fresh candidate from the base pass: W is finalised (Talbot MIS) and occlusion is baked in (W = 0 when initial-visibility occluded it).
    Reservoir r_gen = pc.genBuffer[pixelIdx];
    if (isnan(r_gen.W) || isinf(r_gen.W)) { r_gen = ReservoirEmpty(); }

    // -------------------------------------------------------------------------
    // Phase 2: temporal combination. Stream generated winner + history winner
    // -------------------------------------------------------------------------
    uint clampedHistoryM = 0u;
    Reservoir history = ReservoirEmpty();

    if (bHistoryUsable) {
        Reservoir candidate = pc.historyBuffer[historyPixel.y * pc.renderExtent.x + historyPixel.x];
        if (isnan(candidate.W) || isinf(candidate.W)) { candidate = ReservoirEmpty(); }
        if (candidate.lightIdx != ~0u && candidate.lightIdx < uint(pc.lightData->lightCount)) {
            history = candidate;
        }
    }

    bool validHistoryReservoir = history.lightIdx != ~0u;

    ReservoirAccum accum_combined = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();
    bool winnerIsGen = false;

    float uSelect0 = HashStep(h);
    float uSelect1 = HashStep(h);

    // Pairwise MIS
    float Mc = (r_gen.lightIdx != ~0u) ? float(r_gen.M) : 0.0;
    // Neighbour (history) confidence for the MIS denominators, preserving the temporal M cap.
    clampedHistoryM = validHistoryReservoir ? min(history.M, pc.mCap) : 0u;
    float Mi = float(clampedHistoryM);

    // Stream the canonical (generated) sample. Its target is evaluated at the current domain (pGen_cur) and, for the
    // pair's denominator, at the history domain (pGen_hist). Streamed unshadowed; W carries the initial-visibility
    // occlusion (W = 0 streams 0 but still contributes Mc to history's denominator, exactly as the spatial canonical).
    if (r_gen.lightIdx != ~0u) {
        LightVS vsGen = UnpackLightVS(pc.lightVS[r_gen.lightIdx]);
        float3 yStarGen = DecodeSamplePos(r_gen, vsGen);
        float3 genLightColor = UnpackUnorm4x8(vsGen.packedColor).rgb;
        LightSample sGen_cur = EvalLightSample(vsGen, yStarGen, viewPosition, N, vsGen.range);
        float pGen_cur = EvalPHat(sGen_cur, V, N, li.albedo.xyz, F0, li.metallic, roughness, genLightColor, vsGen.intensity);
        float pGen_hist = 0.0;
        if (validHistoryReservoir) {
            LightSample sGen_hist = EvalLightSample(vsGen, yStarGen, viewPosition, historyN_cvs, vsGen.range);
            pGen_hist = EvalPHat(sGen_hist, V, historyN_cvs, li.albedo.xyz, F0, li.metallic, roughness, genLightColor, vsGen.intensity);
        }

        // Canonical's balance-heuristic share for the (gen, history) pair.
        float denomGen = Mc * pGen_cur + Mi * pGen_hist;
        float misGen = (denomGen > 0.0) ? (Mc * pGen_cur) / denomGen : 0.0;
        float w_gen = misGen * pGen_cur * r_gen.W;
        if (ReservoirAddSample(accum_combined, w_gen, uSelect0)) {
            combined.lightIdx = r_gen.lightIdx;
            combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
            winnerIsGen = true;
        }
    }

    // Stream the neighbour (history) sample.
    if (validHistoryReservoir) {
        LightVS vsHistory = UnpackLightVS(pc.lightVS[history.lightIdx]);
        float3 yStarHistory = DecodeSamplePos(history, vsHistory);
        float3 historyLightColor = UnpackUnorm4x8(vsHistory.packedColor).rgb;
        LightSample sHist_cur = EvalLightSample(vsHistory, yStarHistory, viewPosition, N, vsHistory.range);
        float pHist_cur = EvalPHat(sHist_cur, V, N, li.albedo.xyz, F0, li.metallic, roughness, historyLightColor, vsHistory.intensity);
        LightSample sHist_hist = EvalLightSample(vsHistory, yStarHistory, viewPosition, historyN_cvs, vsHistory.range);
        float pHist_hist = EvalPHat(sHist_hist, V, historyN_cvs, li.albedo.xyz, F0, li.metallic, roughness, historyLightColor, vsHistory.intensity);

        // Neighbour's balance-heuristic share for the same pair.
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
    // Occluded candidate that no history outweighed: keep it as the anchor (winnerIsGen stays false, so Phase 3 re-traces it to W=0) instead of leaving the pixel empty for spatial reuse to fill with a neighbour light.
    if (pc.bInitialVisibility != 0u && combined.lightIdx == ~0u && r_gen.lightIdx != ~0u) {
        combined.lightIdx = r_gen.lightIdx;
        combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
    }
#endif

    combined.M = r_gen.M + clampedHistoryM;

    // -------------------------------------------------------------------------
    // Phase 3: finalise with visibility
    // -------------------------------------------------------------------------
    // winnerVisibility: visibility of the displayed sample against the current TLAS. visPrev: the SAME sample against the previous-frame TLAS.
    float winnerVisibility = 1.0;
#if RESTIR_ENABLE_CONFIDENCE
    float visPrev = 1.0;
#endif
    // Visibility-independent target luminance: the gradient's brightness weight (valid whether or not the sample is occluded this frame).
    float pHatUnshadowed = 0.0;
    if (combined.lightIdx != ~0u) {
        LightVS vsWinner = UnpackLightVS(pc.lightVS[combined.lightIdx]);
        float3 yStar = DecodeSamplePos(combined, vsWinner);
        LightSample sWinner = EvalLightSample(vsWinner, yStar, viewPosition, N, vsWinner.range);
        float3 winnerLightColor = UnpackUnorm4x8(vsWinner.packedColor).rgb;
        pHatUnshadowed = EvalPHat(sWinner, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, vsWinner.intensity);

        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        float3 worldyStar = mul(scene.invView, float4(yStar, 1.0)).xyz;

        float p_hat_winner;
#if RESTIR_ENABLE_INITIAL_VISIBILITY
        // The fresh candidate was already shadow-tested in the base pass (W > 0 => visible); if it won, reuse that result instead of re-tracing.
        if (pc.bInitialVisibility != 0u && winnerIsGen) {
            p_hat_winner = pHatUnshadowed;
        } else {
            p_hat_winner = EvalPHatVisibility(sWinner, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, vsWinner.intensity,
                                                 rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldyStar, worldTexelSize);
        }
#else
        p_hat_winner = EvalPHatVisibility(sWinner, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, vsWinner.intensity,
                                             rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldyStar, worldTexelSize);
#endif
        ReservoirFinalise(combined, accum_combined, p_hat_winner, 1.0);
        winnerVisibility = (p_hat_winner > 0.0) ? 1.0 : 0.0;

#if RESTIR_ENABLE_CONFIDENCE
        // Same-sample re-shade against the previous-frame TLAS (carried). Tracing THIS frame's winner sample against the
        // previous occluder positions isolates the moving-caster shadow change with zero Monte-Carlo noise (identical sample),
        // unlike a cross-frame luminance difference. Receiver assumed static (worldPos unchanged). ~0u prev TLAS -> no change.
        if (pc.prevTlasIndex != ~0u) {
            visPrev = RestirShadowVisible(rdgAccelerationStructures[pc.prevTlasIndex], worldPos, worldN, worldyStar, worldTexelSize) ? 1.0 : 0.0;
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

    // After finalise: this frame's W is unchanged, only the carried M shrinks so a moving shadow washes out fast.
    if (pc.antilagStrength > 0.0 && flip > 0.0) {
        combined.M = max(1u, uint(float(combined.M) * (1.0 - pc.antilagStrength * flip)));
    }
#endif

    pc.outputBuffer[pixelIdx] = combined;

#if RESTIR_ENABLE_ANTILAG
    if (pc.shadowVisIndex != ~0u) { rdgStorageFloat[pc.shadowVisIndex][pixel] = winnerVisibility; }
#endif
#if RESTIR_ENABLE_CONFIDENCE
    // restir_signal (RG16F): x = signed visibility-change * brightness, y = brightness normalizer. The gradient pass means these over a stratum; the resolve rectifies after blurring.
    if (pc.signalIndex != ~0u) {
        rdgStorageFloat2[pc.signalIndex][pixel] = float2(pHatUnshadowed * (winnerVisibility - visPrev), pHatUnshadowed * max(winnerVisibility, visPrev));
    }
#endif
}
