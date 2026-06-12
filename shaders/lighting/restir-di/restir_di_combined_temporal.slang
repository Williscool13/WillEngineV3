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

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 fullPixel = pixel * pc.pixelScale;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    if (depth <= 0.0) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = ReservoirEmpty();
        return;
    }

    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = ReservoirEmpty();
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    int lightCount = pc.lightData->areaLightCount;

    // -------------------------------------------------------------------------
    // Phase 1: generate candidates (Talbot MIS, same as restir_di_main)
    // -------------------------------------------------------------------------
    const int M_light = 16;
    const int M_brdf = 1;
    const int M = M_light + M_brdf;

    Reservoir r_gen = ReservoirEmpty();
    ReservoirAccum accum_gen = ReservoirAccumEmpty();

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    if (lightCount > 0) {
        for (int i = 0; i < M_light; i++) {
            float uLight = HashStep(h);
            float uU = HashStep(h);
            float uV = HashStep(h);
            float uSelect = HashStep(h);

            int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
            AreaLightVS vs = UnpackAreaLightVS(pc.lightVS[lightIdx]);
            float3 Xi = SampleAreaLightPos(vs, uU, uV);
            AreaLightSample s = EvalAreaLightSample(vs, Xi, viewPosition, N, vs.range);

            float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
            float p_hat_Xi = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lightColor, vs.intensity);

            float p_light_area = 1.0 / (float(lightCount) * vs.area);
            float p_brdf_sa = GGXPdf(N, V, s.L, roughness);
            float p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sa * s.cosLight / max(s.dist * s.dist, 1e-4)) : 0.0;
            float denom = float(M_light) * p_light_area + float(M_brdf) * p_brdf_area;
            float w_i = (denom > 0.0) ? (p_hat_Xi / denom) : 0.0;
            if (ReservoirAddSample(accum_gen, r_gen, w_i, uSelect)) {
                r_gen.lightIdx = uint(lightIdx);
                r_gen.sampleOffsetPacked = EncodeSampleOffset(Xi, vs);
            }
        }

        for (int i = 0; i < M_brdf; i++) {
            float uXi0 = HashStep(h);
            float uXi1 = HashStep(h);
            float uSelect = HashStep(h);

            float p_brdf_sample;
            float3 brdfL = ImportanceSampleGGX(float2(uXi0, uXi1), N, V, roughness, p_brdf_sample);

            float p_hat_Xi = 0.0;
            int bestLightIdx = -1;
            float3 Xi = float3(0.0);
            float p_light_area = 0.0;
            float p_brdf_area = 0.0;
            AreaLightVS bestVS = (AreaLightVS)0;

            if (dot(N, brdfL) > 0.0 && p_brdf_sample > 0.0) {
                float bestT = 1e30;
                for (int j = 0; j < lightCount; j++) {
                    AreaLightVS vs = UnpackAreaLightVS(pc.lightVS[j]);
                    float t;
                    if (RayIntersectQuad(viewPosition, brdfL, vs.center, vs.normal, vs.right, vs.up, vs.halfWidth, vs.halfHeight, t) && t < bestT) {
                        bestT = t;
                        bestLightIdx = j;
                        bestVS = vs;
                    }
                }

                if (bestLightIdx >= 0) {
                    Xi = viewPosition + brdfL * bestT;
                    AreaLightSample s = EvalAreaLightSample(bestVS, Xi, viewPosition, N, bestVS.range);
                    p_light_area = 1.0 / (float(lightCount) * bestVS.area);
                    p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sample * s.cosLight / max(s.dist * s.dist, 1e-4)) : 0.0;
                    float3 brdfLightColor = UnpackUnorm4x8(bestVS.packedColor).rgb;
                    p_hat_Xi = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, brdfLightColor, bestVS.intensity);
                }
            }

            float denom = float(M_light) * p_light_area + float(M_brdf) * p_brdf_area;
            float w_i = (denom > 0.0) ? (p_hat_Xi / denom) : 0.0;
            if (ReservoirAddSample(accum_gen, r_gen, w_i, uSelect) && bestLightIdx >= 0) {
                r_gen.lightIdx = uint(bestLightIdx);
                r_gen.sampleOffsetPacked = EncodeSampleOffset(Xi, bestVS);
            }
        }
        r_gen.M = uint(M);
    }

    // -------------------------------------------------------------------------
    // Phase 2: temporal combination — stream generated winner + history winner
    // -------------------------------------------------------------------------
    // Velocity reproject
    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;
    bool validHistory = pc.historyBuffer != nullptr && all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));

    uint clampedHistoryM = 0u;
    Reservoir history = ReservoirEmpty();

    if (validHistory) {
        uint2 historyPixel = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
        uint2 historyFullPixel = historyPixel * pc.pixelScale;

        // Normal disocclusion
        float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.prevGbufferOneIndex].Load(int3(historyFullPixel, 0)).r));
        if (dot(N, historyN) >= 0.906) {
            // Plane-distance disocclusion (NRD style); see restir_di_temporal.slang
            float historyRawDepth = rdgTexturesFloat[pc.prevDepthIndex].Load(int3(historyFullPixel, 0)).r;
            float historyViewDepth = ScreenToViewSpaceDepth(historyRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float expectedPrevViewDepth = viewSpaceDepth + UnpackViewZDelta(g1.w);
            float NoV = abs(dot(N, normalize(viewPosition)));
            float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
            float disocclusionThreshold = saturate(0.01 / lerp(0.05, 1.0, NoV)) * frustumSize;
            if (abs(historyViewDepth - expectedPrevViewDepth) * NoV <= disocclusionThreshold) {
                Reservoir candidate = pc.historyBuffer[historyPixel.y * pc.renderExtent.x + historyPixel.x];
                if (candidate.lightIdx != ~0u && candidate.lightIdx < uint(pc.lightData->areaLightCount)) {
                    history = candidate;
                }
            }
        }
    }

    bool validHistoryReservoir = history.lightIdx != ~0u;

    ReservoirAccum accum_combined = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();

    float uSelect0 = HashStep(h);
    float uSelect1 = HashStep(h);

    // Finalise the generated reservoir without a visibility ray and stream it; the temporal-winner finalize below carries the only shadow ray in this pass.
    if (r_gen.lightIdx != ~0u) {
        AreaLightVS vsGen = UnpackAreaLightVS(pc.lightVS[r_gen.lightIdx]);
        float3 yStarGen = DecodeSamplePos(r_gen, vsGen);
        AreaLightSample sGen = EvalAreaLightSample(vsGen, yStarGen, viewPosition, N, vsGen.range);
        float3 genLightColor = UnpackUnorm4x8(vsGen.packedColor).rgb;
        float p_hat_gen = EvalPHat(sGen, V, N, li.albedo.xyz, F0, li.metallic, roughness, genLightColor, vsGen.intensity);
        ReservoirFinalise(r_gen, accum_gen, p_hat_gen, 1.0);

        float w_gen = p_hat_gen * r_gen.W * float(r_gen.M);
        if (ReservoirAddSample(accum_combined, combined, w_gen, uSelect0)) {
            combined.lightIdx = r_gen.lightIdx;
            combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
        }
    }

    // Stream history winner: w = p_hat(y_hist at current shading point) * W_hist * clampedM
    if (validHistoryReservoir) {
        AreaLightVS vsHistory = UnpackAreaLightVS(pc.lightVS[history.lightIdx]);
        float3 yStarHistory = DecodeSamplePos(history, vsHistory);
        AreaLightSample sHistory = EvalAreaLightSample(vsHistory, yStarHistory, viewPosition, N, vsHistory.range);
        float3 historyLightColor = UnpackUnorm4x8(vsHistory.packedColor).rgb;
        float p_hat_history = EvalPHat(sHistory, V, N, li.albedo.xyz, F0, li.metallic, roughness, historyLightColor, vsHistory.intensity);

        clampedHistoryM = min(history.M, pc.mCap);
        float w_history = p_hat_history * history.W * float(clampedHistoryM);
        if (ReservoirAddSample(accum_combined, combined, w_history, uSelect1)) {
            combined.lightIdx = history.lightIdx;
            combined.sampleOffsetPacked = history.sampleOffsetPacked;
        }
    }

    combined.M = r_gen.M + clampedHistoryM;

    // -------------------------------------------------------------------------
    // Phase 3: finalise with visibility
    // -------------------------------------------------------------------------
    if (combined.lightIdx != ~0u) {
        AreaLightVS vsWinner = UnpackAreaLightVS(pc.lightVS[combined.lightIdx]);
        float3 yStar = DecodeSamplePos(combined, vsWinner);
        AreaLightSample sWinner = EvalAreaLightSample(vsWinner, yStar, viewPosition, N, vsWinner.range);
        float3 winnerLightColor = UnpackUnorm4x8(vsWinner.packedColor).rgb;
        float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        float3 worldyStar = mul(scene.invView, float4(yStar, 1.0)).xyz;
        RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
        float p_hat_winner = EvalPHatVisibility(sWinner, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, vsWinner.intensity,
                                                 tlas, worldPos, worldN, worldyStar);
        ReservoirFinalise(combined, accum_combined, p_hat_winner, float(combined.M));
    }

    pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = combined;
}
