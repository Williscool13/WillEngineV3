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
import random_common;

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDITemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    uint2 fullPixel = QuadFullPixel(pixel, pc.pixelScale, pc.frameIndex, pc.depthIndex, pc.gbufferOneIndex, 1.0 / float2(scene.mainRenderTargetSize), scene.depthLinearizeMult, scene.depthLinearizeAdd);

    // Velocity reproject.
    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;
    bool validHistory = all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));
    if (!validHistory) {
        pc.outputBuffer[pixelIdx] = pc.currentBuffer[pixelIdx];
        return;
    }
    uint2 historyPixel = uint2(historyUV * float2(pc.renderExtent));
    historyPixel = clamp(historyPixel, uint2(0, 0), pc.renderExtent - uint2(1, 1));
    if (pc.bPermutationSampling != 0u) {
        int2 permuted = ApplyPermutationSampling(int2(historyPixel), pc.frameIndex);
        if (all(permuted >= int2(0, 0)) && all(permuted < int2(pc.renderExtent))) {
            historyPixel = uint2(permuted);
        }
    }
    // History taps use the corner the previous frame's passes sampled (previous depth source)
    uint2 historyFullPixel = QuadFullPixel(historyPixel, pc.pixelScale, pc.frameIndex - 1u, pc.prevDepthIndex, pc.prevGbufferOneIndex, 1.0 / float2(scene.mainRenderTargetSize), scene.depthLinearizeMult, scene.depthLinearizeAdd);

    // Normal disocclusion rejection.
    float3 currentN = normalize(UnpackNormalOct16(g1.r));
    float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.prevGbufferOneIndex].Load(int3(historyFullPixel, 0)).r));
    if (dot(currentN, historyN) < 0.906) {
        pc.outputBuffer[pixelIdx] = pc.currentBuffer[pixelIdx];
        return;
    }

    // Depth disocclusion rejection.
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    if (depth <= 0.0) {
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float historyRawDepth = rdgTexturesFloat[pc.prevDepthIndex].Load(int3(historyFullPixel, 0)).r;
    float historyViewDepth = ScreenToViewSpaceDepth(historyRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    // Plane-distance disocclusion (NRD style). NoV converts the view-depth delta into a distance from the surface plane, so grazing
    // surfaces with steep per-pixel depth gradients are not falsely rejected; the threshold is relative to frustum size, not depth.
    // The expected previous depth comes from the gbuffer view-depth delta, so camera/object motion does not register as disocclusion.
    float expectedPrevViewDepth = viewSpaceDepth + UnpackViewZDelta(g1.w);
    float NoV = abs(dot(currentN, normalize(viewPosition)));
    float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
    float disocclusionThreshold = saturate(0.01 / lerp(0.05, 1.0, NoV)) * frustumSize;
    if (abs(historyViewDepth - expectedPrevViewDepth) * NoV > disocclusionThreshold) {
        pc.outputBuffer[pixelIdx] = pc.currentBuffer[pixelIdx];
        return;
    }

    // Reconstruct full material at the current pixel for consistent p_hat evaluation.
    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        pc.outputBuffer[pixelIdx] = pc.currentBuffer[pixelIdx];
        return;
    }

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    Reservoir current = pc.currentBuffer[pixelIdx];
    bool validCurrent = current.lightIdx != ~0u;

    Reservoir history = pc.historyBuffer[historyPixel.y * pc.renderExtent.x + historyPixel.x];
    bool validHistoryReservoir = history.lightIdx != ~0u && history.lightIdx < uint(pc.lightData->lightCount);
    if (!validCurrent && !validHistoryReservoir) {
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    // WRS combination: w_j = p_hat(y_j at current shading point) * W_j * M_j
    ReservoirAccum accum = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u) ^ 0xA3C59Fu;
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;
    float uSelect0 = HashStep(h);
    float uSelect1 = HashStep(h);

    if (validCurrent) {
        LightVS vsCurrent = UnpackLightVS(pc.lightVS[current.lightIdx]);
        float3 yStarCurrent = DecodeSamplePos(current, vsCurrent);
        LightSample sCurrent = EvalLightSample(vsCurrent, yStarCurrent, viewPosition, N, vsCurrent.range);
        float3 currentLightColor = UnpackUnorm4x8(vsCurrent.packedColor).rgb;
        float p_hat_current = EvalPHat(sCurrent, V, N, li.albedo.xyz, F0, li.metallic, roughness, currentLightColor, vsCurrent.intensity);
        float w_current = p_hat_current * current.W * float(current.M);
        if (ReservoirAddSample(accum, combined, w_current, uSelect0)) {
            combined.lightIdx = current.lightIdx;
            combined.sampleOffsetPacked = current.sampleOffsetPacked;
        }
    }

    uint clampedHistoryM = 0u;

    if (validHistoryReservoir) {
        // Evaluate p_hat for the history reservoir's sample at the current pixel's shading point.
        LightVS vsHistory = UnpackLightVS(pc.lightVS[history.lightIdx]);
        float3 yStarHistory = DecodeSamplePos(history, vsHistory);
        LightSample sHistory = EvalLightSample(vsHistory, yStarHistory, viewPosition, N, vsHistory.range);
        float3 historyLightColor = UnpackUnorm4x8(vsHistory.packedColor).rgb;
        float p_hat_history = EvalPHat(sHistory, V, N, li.albedo.xyz, F0, li.metallic, roughness, historyLightColor, vsHistory.intensity);

        clampedHistoryM = min(history.M, pc.mCap);
        float w_history = p_hat_history * history.W * float(clampedHistoryM);
        if (ReservoirAddSample(accum, combined, w_history, uSelect1)) {
            combined.lightIdx = history.lightIdx;
            combined.sampleOffsetPacked = history.sampleOffsetPacked;
        }
    }

    combined.M = current.M + clampedHistoryM;

    // Finalise: re-evaluate p_hat at the winner with visibility.
    if (combined.lightIdx != ~0u) {
        LightVS vsWinner = UnpackLightVS(pc.lightVS[combined.lightIdx]);
        float3 yStar = DecodeSamplePos(combined, vsWinner);
        LightSample sWinner = EvalLightSample(vsWinner, yStar, viewPosition, N, vsWinner.range);
        float3 winnerLightColor = UnpackUnorm4x8(vsWinner.packedColor).rgb;
        float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        float3 worldyStar = mul(scene.invView, float4(yStar, 1.0)).xyz;
        RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
        float p_hat_winner = EvalPHatVisibility(sWinner, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, vsWinner.intensity,
                                                 tlas, worldPos, worldN, worldyStar);
        ReservoirFinalise(combined, accum, p_hat_winner, float(combined.M));
    }

    pc.outputBuffer[pixelIdx] = combined;
}
