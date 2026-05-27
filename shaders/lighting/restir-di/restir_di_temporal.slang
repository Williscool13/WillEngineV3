import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;
import restir_interop;

import packing_functions;
import shader_functions;
import visibility_lighting_common;
import bindless_declarations;
import restir_common;

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDITemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    Reservoir current = pc.currentBuffer[pixel.y * pc.renderExtent.x + pixel.x];

    //pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = current;
    //return;

    // No valid sample
    if (current.lightIdx == ~0u) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = ReservoirEmpty();
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float4x4 view = scene.view;
    float2 uv = (float2(pixel) + 0.5) / float2(scene.mainRenderTargetSize);

    // Velocity reproject.
    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;

    bool validHistory = all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));

    if (!validHistory) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = current;
        return;
    }

    uint2 historyPixel = uint2(historyUV * float2(scene.mainRenderTargetSize));
    historyPixel = clamp(historyPixel, uint2(0, 0), pc.renderExtent - uint2(1, 1));

    // Disocclusion rejection.
    float3 currentN = normalize(UnpackNormalOct16(g1.r));
    float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(historyPixel, 0)).r));
    if (dot(currentN, historyN) < 0.906) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = current;
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = ReservoirEmpty();
        return;
    }
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);

    float historyRawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(historyPixel, 0)).r;
    float historyViewDepth = ScreenToViewSpaceDepth(historyRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    if (abs(historyViewDepth - viewSpaceDepth) > 0.1 * viewSpaceDepth) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = current;
        return;
    }

    Reservoir history = pc.historyBuffer[historyPixel.y * pc.renderExtent.x + historyPixel.x];

    if (history.lightIdx == ~0u || history.lightIdx >= uint(pc.lightData->areaLightCount)) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = current;
        return;
    }

    // Reconstruct full material at the current pixel for consistent p_hat evaluation.
    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = current;
        return;
    }

    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    // Evaluate p_hat for the current reservoir's sample at the current pixel's shading point.
    AreaLightData alCurrent = pc.lightData->areaLights[current.lightIdx];
    AreaLightVS vsCurrent = TransformAreaLight(alCurrent, view);
    float3 yStarCurrent = DecodeSamplePos(current, vsCurrent);
    AreaLightSample sCurrent = EvalAreaLightSample(vsCurrent, yStarCurrent, viewPosition, N, alCurrent.range);
    float3 currentLightColor = UnpackUnorm4x8(alCurrent.packedColor).rgb;
    float p_hat_current = EvalPHat(sCurrent, V, N, li.albedo.xyz, F0, li.metallic, roughness, currentLightColor, alCurrent.intensity);

    // Evaluate p_hat for the history reservoir's sample at the current pixel's shading point.
    AreaLightData alHistory = pc.lightData->areaLights[history.lightIdx];
    AreaLightVS vsHistory = TransformAreaLight(alHistory, view);
    float3 yStarHistory = DecodeSamplePos(history, vsHistory);
    AreaLightSample sHistory = EvalAreaLightSample(vsHistory, yStarHistory, viewPosition, N, alHistory.range);
    float3 historyLightColor = UnpackUnorm4x8(alHistory.packedColor).rgb;
    float p_hat_history = EvalPHat(sHistory, V, N, li.albedo.xyz, F0, li.metallic, roughness, historyLightColor, alHistory.intensity);

    // WRS combination: w_j = p_hat(y_j at current shading point) * W_j * M_j
    ReservoirAccum accum = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();

    uint h = (pixel.x * 2654435761u) ^ (pixel.y * 805459861u) ^ (pc.frameIndex * 1973u);
    float uSelect0 = HashStep(h);
    float uSelect1 = HashStep(h);

    // Paper suggests 5-30
    const uint M_cap = 20u * current.M;
    uint clampedHistoryM = min(history.M, M_cap);

    float w_current = p_hat_current * current.W * float(current.M);
    if (ReservoirAddSample(accum, combined, w_current, uSelect0)) {
        combined.lightIdx = current.lightIdx;
        combined.sampleOffsetPacked = current.sampleOffsetPacked;
    }

    float w_history = p_hat_history * history.W * float(clampedHistoryM);
    if (ReservoirAddSample(accum, combined, w_history, uSelect1)) {
        combined.lightIdx = history.lightIdx;
        combined.sampleOffsetPacked = history.sampleOffsetPacked;
    }

    combined.M = current.M + clampedHistoryM;

    // Finalise: re-evaluate p_hat at the winner.
    if (combined.lightIdx != ~0u) {
        AreaLightData alWinner = pc.lightData->areaLights[combined.lightIdx];
        AreaLightVS vsWinner = TransformAreaLight(alWinner, view);
        float3 yStar = DecodeSamplePos(combined, vsWinner);
        AreaLightSample sWinner = EvalAreaLightSample(vsWinner, yStar, viewPosition, N, alWinner.range);
        float3 winnerLightColor = UnpackUnorm4x8(alWinner.packedColor).rgb;
        float p_hat_winner = EvalPHat(sWinner, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, alWinner.intensity);
        ReservoirFinalise(combined, accum, p_hat_winner, float(combined.M));
    }

    pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = combined;
}
