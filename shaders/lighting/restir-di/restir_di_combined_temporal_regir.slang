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

// ReGIR-exclusive variant of the ReSTIR DI main pass: initial candidates are drawn only from the ReGIR world-space grid.
// Surfaces outside the grid generate no fresh candidate and rely on temporal/spatial reuse. The uniform/BRDF generation lives in restir_di_combined_temporal.slang.
[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    if (pc.shadowVisIndex != ~0u) { rdgStorageFloat[pc.shadowVisIndex][pixel] = 1.0; }
    if (pc.confidenceIndex != ~0u) { rdgStorageFloat[pc.confidenceIndex][pixel] = 1.0; }

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
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    int lightCount = pc.lightData->lightCount;

    // History reprojection validity.
    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;
    bool validHistory = pc.historyBuffer != nullptr && all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));
    uint2 historyPixel = uint2(0, 0);
    bool bHistoryUsable = false;

    if (validHistory) {
        historyPixel = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
        if (pc.bPermutationSampling != 0u) {
            int2 permuted = ApplyPermutationSampling(int2(historyPixel), pc.frameIndex);
            if (all(permuted >= int2(0, 0)) && all(permuted < int2(pc.renderExtent))) {
                historyPixel = uint2(permuted);
            }
        }
        // History taps use the corner the previous frame's passes sampled (previous depth source)
        uint2 historyFullPixel = FetchFullPixel(pc.quadSelectionHistoryIndex, historyPixel, pc.pixelScale);

        // Normal disocclusion: gbuffer normals are view-space, so compare in world space; a raw view-space dot falsely rejects history under camera rotation.
        float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.prevGbufferOneIndex].Load(int3(historyFullPixel, 0)).r));
        float3 currWorldN = normalize(mul(float3x3(scene.invView), N));
        float3 historyWorldN = normalize(mul(transpose(float3x3(scene.prevView)), historyN));
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

    // -------------------------------------------------------------------------
    // Phase 1: generate candidates exclusively from ReGIR grid taps. Out-of-grid surfaces generate nothing.
    // -------------------------------------------------------------------------
    Reservoir r_gen = ReservoirEmpty();
    ReservoirAccum accum_gen = ReservoirAccumEmpty();

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    uint regirCell;
    bool inGrid = ReGIRWorldToCellIndex(worldPos, pc.gridOffset, regirCell);

    if (lightCount > 0 && inGrid) {
        for (int i = 0; i < RESTIR_M_LIGHT; i++) {
            // Jitter the lookup by up to half a cell per tap so boundary pixels stochastically read neighbouring cells, dithering the hard cell edges into noise the denoiser resolves.
            float3 jitter = (float3(HashStep(h), HashStep(h), HashStep(h)) - 0.5) * ReGIRCellSize();
            uint cell;
            if (!ReGIRWorldToCellIndex(worldPos + jitter, pc.gridOffset, cell)) { continue; }

            uint slotInCell = min(uint(HashStep(h) * float(REGIR_RESERVOIRS_PER_CELL)), REGIR_RESERVOIRS_PER_CELL - 1u);
            uint slot = cell * REGIR_RESERVOIRS_PER_CELL + slotInCell;

            float uSelect = HashStep(h);
            ReGIRReservoir gr = pc.gridBuffer[slot];
            float cellAvgWeight = pc.cellAvgWeight[cell];
            if (gr.lightIdx == ~0u || gr.targetPdf <= 0.0 || cellAvgWeight <= 0.0) { continue; }
            LightVS vs = UnpackLightVS(pc.lightVS[gr.lightIdx]);
            float3 Xi = DecodeSamplePos(gr.sampleOffsetPacked, vs);
            LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);
            float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
            float p_hat = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lightColor, vs.intensity);
            // RTG2 Ch.23 §23.3.2: sourcePdf = candidate.targetPdf / cellAverageWeight; w_i = p_hat / sourcePdf.
            float sourcePdf = gr.targetPdf / cellAvgWeight;
            float w_i = p_hat / sourcePdf;
            if (ReservoirAddSample(accum_gen, w_i, uSelect)) {
                r_gen.lightIdx = gr.lightIdx;
                r_gen.sampleOffsetPacked = gr.sampleOffsetPacked;
            }
        }
        r_gen.M = uint(accum_gen.M);
    }

    // -------------------------------------------------------------------------
    // Phase 2: temporal combination. Stream generated winner + history winner
    // -------------------------------------------------------------------------
    uint clampedHistoryM = 0u;
    Reservoir history = ReservoirEmpty();

    if (bHistoryUsable) {
        Reservoir candidate = pc.historyBuffer[historyPixel.y * pc.renderExtent.x + historyPixel.x];
        if (candidate.lightIdx != ~0u && candidate.lightIdx < uint(pc.lightData->lightCount)) {
            history = candidate;
        }
    }

    bool validHistoryReservoir = history.lightIdx != ~0u;

    ReservoirAccum accum_combined = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();

    float uSelect0 = HashStep(h);
    float uSelect1 = HashStep(h);

    // Stream the generated reservoir with its unshadowed target; only the combined winner is traced (Phase 3). An occluded fresh candidate that wins here is zeroed there.
    if (r_gen.lightIdx != ~0u) {
        LightVS vsGen = UnpackLightVS(pc.lightVS[r_gen.lightIdx]);
        float3 yStarGen = DecodeSamplePos(r_gen, vsGen);
        LightSample sGen = EvalLightSample(vsGen, yStarGen, viewPosition, N, vsGen.range);
        float3 genLightColor = UnpackUnorm4x8(vsGen.packedColor).rgb;
        float p_hat_gen = EvalPHat(sGen, V, N, li.albedo.xyz, F0, li.metallic, roughness, genLightColor, vsGen.intensity);
        ReservoirFinalise(r_gen, accum_gen, p_hat_gen, float(r_gen.M));

        float w_gen = p_hat_gen * r_gen.W * float(r_gen.M);
        if (ReservoirAddSample(accum_combined, w_gen, uSelect0)) {
            combined.lightIdx = r_gen.lightIdx;
            combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
        }
    }

    // Stream history winner: w = p_hat(y_hist at current shading point) * W_hist * clampedM
    if (validHistoryReservoir) {
        LightVS vsHistory = UnpackLightVS(pc.lightVS[history.lightIdx]);
        float3 yStarHistory = DecodeSamplePos(history, vsHistory);
        LightSample sHistory = EvalLightSample(vsHistory, yStarHistory, viewPosition, N, vsHistory.range);
        float3 historyLightColor = UnpackUnorm4x8(vsHistory.packedColor).rgb;
        float p_hat_history = EvalPHat(sHistory, V, N, li.albedo.xyz, F0, li.metallic, roughness, historyLightColor, vsHistory.intensity);

        clampedHistoryM = min(history.M, pc.mCap);
        float w_history = p_hat_history * history.W * float(clampedHistoryM);
        if (ReservoirAddSample(accum_combined, w_history, uSelect1)) {
            combined.lightIdx = history.lightIdx;
            combined.sampleOffsetPacked = history.sampleOffsetPacked;
        }
    }

    combined.M = r_gen.M + clampedHistoryM;

    // -------------------------------------------------------------------------
    // Phase 3: finalise with visibility
    // -------------------------------------------------------------------------
    // winnerVisibility: 1 = the displayed sample is lit this frame, 0 = occluded. The visibility ray here is binary (single ray to the sample), so this is a clean shadow term.
    float winnerVisibility = 1.0;
    if (combined.lightIdx != ~0u) {
        LightVS vsWinner = UnpackLightVS(pc.lightVS[combined.lightIdx]);
        float3 yStar = DecodeSamplePos(combined, vsWinner);
        LightSample sWinner = EvalLightSample(vsWinner, yStar, viewPosition, N, vsWinner.range);
        float3 winnerLightColor = UnpackUnorm4x8(vsWinner.packedColor).rgb;
        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        float3 worldyStar = mul(scene.invView, float4(yStar, 1.0)).xyz;
        RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
        float p_hat_winner = EvalPHatVisibility(sWinner, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, vsWinner.intensity,
                                                 tlas, worldPos, worldN, worldyStar);
        ReservoirFinalise(combined, accum_combined, p_hat_winner, float(combined.M));
        winnerVisibility = (p_hat_winner > 0.0) ? 1.0 : 0.0;
    }

    float flip = 0.0;
    if (pc.prevShadowVisIndex != ~0u && bHistoryUsable && validHistoryReservoir) {
        float prevVis = rdgTexturesFloat[pc.prevShadowVisIndex].Load(int3(historyPixel, 0)).r;
        flip = abs(winnerVisibility - prevVis);
    }

    // After finalise: this frame's W is unchanged, only the carried M shrinks so a moving shadow washes out fast.
    if (pc.antilagStrength > 0.0 && flip > 0.0) {
        combined.M = max(1u, uint(float(combined.M) * (1.0 - pc.antilagStrength * flip)));
    }

    pc.outputBuffer[pixelIdx] = combined;

    if (pc.shadowVisIndex != ~0u) { rdgStorageFloat[pc.shadowVisIndex][pixel] = winnerVisibility; }
    if (pc.confidenceIndex != ~0u) { rdgStorageFloat[pc.confidenceIndex][pixel] = 1.0 - pc.confidenceStrength * flip; }
}
