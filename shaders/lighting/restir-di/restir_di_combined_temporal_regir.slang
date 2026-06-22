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
import regir_hash;

// ReGIR variant of the ReSTIR DI main pass: light candidates are drawn from the ReGIR world-space grid where it covers
// the surface, falling back to uniform light sampling out of range or on an unbuilt cell. BRDF-sampled candidates are added as in restir_di_combined_temporal.slang.
[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    if (pc.shadowVisIndex != ~0u) { rdgStorageFloat[pc.shadowVisIndex][pixel] = 1.0; }
    // restir_signal (RG16F): re-shade visibility delta + brightness normalizer for the gradient passes. Sky/invalid texels carry 0.
    if (pc.signalIndex != ~0u) { rdgStorageFloat2[pc.signalIndex][pixel] = float2(0.0, 0.0); }

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
    // Phase 1: generate light candidates from the ReGIR grid (uniform-light fallback out of range / unbuilt cell) plus BRDF taps.
    // -------------------------------------------------------------------------
    Reservoir r_gen = ReservoirEmpty();
    ReservoirAccum accum_gen = ReservoirAccumEmpty();

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 cameraPos = scene.cameraWorldPos.xyz;
    float camDist = distance(worldPos, cameraPos);
    bool useGrid = camDist <= REGIR_MAX_DIST;
    float regirJitter = ReGIRCellSizeAtLevel(ReGIRLevelFromDistance(camDist));

    if (lightCount > 0) {
        for (int i = 0; i < RESTIR_M_LIGHT; i++) {
            uint candLightIdx = ~0u;
            float3 candXi = float3(0.0);
            LightVS candVS = (LightVS)0;
            float w_i = 0.0;
            bool gotGrid = false;

            if (useGrid) {
                // Jitter the lookup by up to half a cell per tap so boundary pixels stochastically read neighbouring cells, dithering the hard cell edges into noise the denoiser resolves.
                float3 jitter = (float3(HashStep(h), HashStep(h), HashStep(h)) - 0.5) * regirJitter;
                uint cellSlot = ReGIRHashFind(pc.hashEntries, ReGIRComputeKey(worldPos + jitter, cameraPos));
                if (cellSlot != REGIR_HASH_INVALID) {
                    uint slotInCell = min(uint(HashStep(h) * float(REGIR_RESERVOIRS_PER_CELL)), REGIR_RESERVOIRS_PER_CELL - 1u);
                    ReGIRReservoir gr = pc.reservoirs[cellSlot * REGIR_RESERVOIRS_PER_CELL + slotInCell];
                    if (gr.lightIdx != ~0u && gr.targetPdf > 0.0 && gr.totalWeight > 0.0) {
                        float uU = HashStep(h);
                        float uV = HashStep(h);
                        LightVS vs = UnpackLightVS(pc.lightVS[gr.lightIdx]);
                        // The grid selected the light; sample the surface point fresh at the shading point so the cone/area pdf cancels p_hat's cosLight/dist² (the cell-center bake was the firefly source).
                        float3 Xi = SampleLightSurface(vs, viewPosition, uU, uV);
                        LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);
                        float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
                        float p_hat = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lightColor, vs.intensity);
                        float p_point = LightSamplePdfArea(vs, viewPosition, Xi);
                        // Contribution-weight RIS: the grid's unbiased light-selection weight is W_grid = totalWeight/targetPdf and the fresh point adds 1/p_point. No analytic grid pdf is fabricated, so no balance-heuristic MIS against BRDF; unbiased at any fill count.
                        float W_grid = gr.totalWeight / gr.targetPdf;
                        w_i = (p_point > 0.0) ? (p_hat * W_grid / p_point) : 0.0;
                        candLightIdx = gr.lightIdx;
                        candXi = Xi;
                        candVS = vs;
                        gotGrid = true;
                    }
                }
            }

            if (!gotGrid) {
                // Out of grid range, unbuilt cell, or empty reservoir: uniform light candidate. Keeps every tap viable so the candidate count stays fixed and the RIS normalisation is exact.
                float uLight = HashStep(h);
                float uU = HashStep(h);
                float uV = HashStep(h);
                int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
                LightVS vs = UnpackLightVS(pc.lightVS[lightIdx]);
                float3 Xi = SampleLightSurface(vs, viewPosition, uU, uV);
                LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);
                float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
                float p_hat = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lightColor, vs.intensity);
                float p_point = LightSamplePdfArea(vs, viewPosition, Xi);
                w_i = (p_point > 0.0) ? (p_hat * float(lightCount) / p_point) : 0.0;
                candLightIdx = uint(lightIdx);
                candXi = Xi;
                candVS = vs;
            }

            float uSelect = HashStep(h);
            if (ReservoirAddSample(accum_gen, w_i, uSelect) && candLightIdx != ~0u) {
                r_gen.lightIdx = candLightIdx;
                r_gen.sampleOffsetPacked = EncodeSampleOffset(candXi, candVS);
            }
        }

        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        for (int i = 0; i < RESTIR_M_BRDF; i++) {
            float uXi0 = HashStep(h);
            float uXi1 = HashStep(h);
            float uSelect = HashStep(h);

            float p_brdf_sample;
            float3 brdfL = ImportanceSampleGGX(float2(uXi0, uXi1), N, V, roughness, p_brdf_sample);

            float w_i = 0.0;
            uint brdfLightIdx = ~0u;
            float3 brdfXi = float3(0.0);
            LightVS brdfVS = (LightVS)0;

            if (dot(N, brdfL) > 0.0 && p_brdf_sample > 0.0) {
                RayDesc ray;
                ray.Origin = worldPos + worldN * 0.05;
                ray.Direction = normalize(mul(float3x3(scene.invView), brdfL));
                ray.TMin = 0.001;
                ray.TMax = 1e30;
                RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
                // Mask 0xFF: hit both scene geometry (0x01) and light proxies (0x02); an occluder hit first -> miss.
                RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
                q.TraceRayInline(tlas, 0, 0xFF, ray);
                q.Proceed();
                if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                    uint lightIdx = pc.instanceBuffer[q.CommittedInstanceID()].lightIndex;
                    if (lightIdx != ~0u && lightIdx < uint(lightCount)) {
                        LightVS vs = UnpackLightVS(pc.lightVS[lightIdx]);
                        float t;
                        if (RayIntersectLight(vs, viewPosition, brdfL, t)) {
                            float3 Xi = viewPosition + brdfL * t;
                            LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);
                            float p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sample * s.cosLight / max(s.dist * s.dist, 1e-4)) : 0.0;
                            if (p_brdf_area > 0.0) {
                                float3 lc = UnpackUnorm4x8(vs.packedColor).rgb;
                                float p_hat = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lc, vs.intensity);
                                // Contribution-weight RIS: BRDF candidate weighted by its own source pdf only (p_brdf_area > 0 here), no MIS denom.
                                w_i = p_hat / p_brdf_area;
                                brdfLightIdx = lightIdx;
                                brdfXi = Xi;
                                brdfVS = vs;
                            }
                        }
                    }
                }
            }

            if (ReservoirAddSample(accum_gen, w_i, uSelect) && brdfLightIdx != ~0u) {
                r_gen.lightIdx = brdfLightIdx;
                r_gen.sampleOffsetPacked = EncodeSampleOffset(brdfXi, brdfVS);
            }
        }

        r_gen.M = uint(accum_gen.M);
    }

    // Initial-candidate visibility: an occluded fresh candidate contributes 0 below (so a visible history outweighs it)
    // but is kept as a dark anchor, not dropped, otherwise the shadowed pixel leaks a neighbour light in spatial reuse.
    bool genVisible = true;
    if (pc.bInitialVisibility != 0u && r_gen.lightIdx != ~0u) {
        LightVS vsGen = UnpackLightVS(pc.lightVS[r_gen.lightIdx]);
        float3 yStar = DecodeSamplePos(r_gen, vsGen);
        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        float3 worldyStar = mul(scene.invView, float4(yStar, 1.0)).xyz;
        genVisible = RestirShadowVisible(rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldyStar, worldTexelSize);
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
    bool winnerIsGen = false;

    float uSelect0 = HashStep(h);
    float uSelect1 = HashStep(h);

    // Stream the generated reservoir with its unshadowed target; only the combined winner is traced (Phase 3). An occluded fresh candidate that wins here is zeroed there.
    if (r_gen.lightIdx != ~0u) {
        LightVS vsGen = UnpackLightVS(pc.lightVS[r_gen.lightIdx]);
        float3 yStarGen = DecodeSamplePos(r_gen, vsGen);
        LightSample sGen = EvalLightSample(vsGen, yStarGen, viewPosition, N, vsGen.range);
        float3 genLightColor = UnpackUnorm4x8(vsGen.packedColor).rgb;
        float p_hat_gen = EvalPHat(sGen, V, N, li.albedo.xyz, F0, li.metallic, roughness, genLightColor, vsGen.intensity);
        // Plain RIS over a fixed candidate count (every tap streams a sample): normalise by the number of candidates.
        ReservoirFinalise(r_gen, accum_gen, p_hat_gen, float(r_gen.M));

        float w_gen = genVisible ? (p_hat_gen * r_gen.W * float(r_gen.M)) : 0.0;
        if (ReservoirAddSample(accum_combined, w_gen, uSelect0)) {
            combined.lightIdx = r_gen.lightIdx;
            combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
            winnerIsGen = true;
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
            winnerIsGen = false;
        }
    }

    if (pc.bInitialVisibility != 0u && combined.lightIdx == ~0u && r_gen.lightIdx != ~0u) {
        combined.lightIdx = r_gen.lightIdx;
        combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
    }

    combined.M = r_gen.M + clampedHistoryM;

    // -------------------------------------------------------------------------
    // Phase 3: finalise with visibility
    // -------------------------------------------------------------------------
    // winnerVisibility: visibility of the displayed sample against the current TLAS. visPrev: the SAME sample against the previous-frame TLAS.
    float winnerVisibility = 1.0;
    float visPrev = 1.0;
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
        // The fresh candidate was already shadow-tested in the initial-visibility stage; if it won, reuse that result instead of re-tracing.
        if (pc.bInitialVisibility != 0u && winnerIsGen) {
            p_hat_winner = pHatUnshadowed;
        } else {
            p_hat_winner = EvalPHatVisibility(sWinner, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, vsWinner.intensity,
                                                 rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldyStar, worldTexelSize);
        }
        ReservoirFinalise(combined, accum_combined, p_hat_winner, float(combined.M));
        winnerVisibility = (p_hat_winner > 0.0) ? 1.0 : 0.0;

        // Same-sample re-shade against the previous-frame TLAS (carried): isolates the moving-caster shadow change, noise-free.
        if (pc.prevTlasIndex != ~0u) {
            visPrev = RestirShadowVisible(rdgAccelerationStructures[pc.prevTlasIndex], worldPos, worldN, worldyStar, worldTexelSize) ? 1.0 : 0.0;
        } else {
            visPrev = winnerVisibility;
        }
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
    // restir_signal (RG16F): x = signed visibility-change * brightness, y = brightness normalizer.
    if (pc.signalIndex != ~0u) {
        rdgStorageFloat2[pc.signalIndex][pixel] = float2(pHatUnshadowed * (winnerVisibility - visPrev), pHatUnshadowed * max(winnerVisibility, visPrev));
    }
}
