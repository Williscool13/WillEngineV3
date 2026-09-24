/*
 * Fused spatiotemporal reuse: replaces the separate Temporal and Spatial passes with one dispatch that
 * loads the gbuffer once and traces one visibility ray at the final winner. The gen reservoir is the
 * canonical current-domain sample; the reprojected history tap and the spatial taps are all drawn from the
 * previous frame's final reservoirs, so there is no read-after-write hazard against passes still in flight.
 * Spatial information is therefore one frame stale, which costs a little recovery speed after disocclusion.
 */
import common_interop;
import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import light_index_common;
import restir_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;

import bindless_declarations;
import restir_common;
import random_common;

#include "restir_features_macros.h"

struct PrevSurface
{
    float3 viewPosition;
    float3 N;
    float3 V;
    float roughness;
    float metallic;
    bool bValid;
};

static PrevSurface LoadPrevSurface(ReSTIRDISpatioTemporalPushConstant pc, SceneData scene, uint2 tapPixel, float3 currWorldN, float expectedPrevViewDepth,
                                   float disocclusionThreshold, float NoV, out uint4 tapG1)
{
    PrevSurface s;
    s.bValid = false;
    s.viewPosition = float3(0.0, 0.0, 0.0);
    s.N = currWorldN;
    s.V = float3(0.0, 0.0, 1.0);
    s.roughness = RESTIR_MIN_ROUGHNESS;
    s.metallic = 0.0;

    tapG1 = rdgTexturesUInt4[pc.prevGbufferOneIndex].Load(int3(tapPixel, 0));
    float3 tapN = normalize(UnpackNormalOct16(tapG1.r));
    float3 tapWorldN = normalize(mul(transpose(float3x3(scene.prevView)), tapN));
    if (dot(currWorldN, tapWorldN) < 0.5) { return s; }

    float tapRawDepth = rdgTexturesFloat[pc.prevDepthIndex].Load(int3(tapPixel, 0)).r;
    if (tapRawDepth <= 0.0) { return s; }
    float tapViewDepth = ScreenToViewSpaceDepth(tapRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    if (abs(tapViewDepth - expectedPrevViewDepth) * NoV > disocclusionThreshold) { return s; }

    float2 tapUV = (float2(tapPixel) + 0.5) / float2(pc.renderExtent);
    float3 tapPrevViewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUV, tapViewDepth);
    float3 tapWorldPos = mul(scene.invPrevView, float4(tapPrevViewPosition, 1.0)).xyz;
    float3 prevCameraWorldPos = mul(scene.invPrevView, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    s.viewPosition = mul(scene.view, float4(tapWorldPos, 1.0)).xyz;
    s.N = normalize(mul(float3x3(scene.view), tapWorldN));
    s.V = normalize(mul(float3x3(scene.view), prevCameraWorldPos - tapWorldPos));
    UnpackPBR(tapG1.b, s.roughness, s.metallic);
    s.roughness = max(s.roughness, RESTIR_MIN_ROUGHNESS);
    s.bValid = true;
    return s;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReSTIRDISpatioTemporal(uniform ReSTIRDISpatioTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
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
        uint2 orphan = uint2(pc.renderExtent.x - 1u, pixel.y);
#if RESTIR_ENABLE_ANTILAG
        if (pc.shadowVisIndex != ~0u) { rdgStorageFloat[pc.shadowVisIndex][orphan] = 1.0; }
#endif
#if RESTIR_ENABLE_CONFIDENCE
        if (pc.signalIndex != ~0u) { rdgStorageFloat2[pc.signalIndex][orphan] = float2(0.0, 0.0); }
#endif
        return;
    }

    int2 partner = (field != 0u) ? OppositeFieldPixel(pixel, field) : int2(-1, -1);
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
    int lightCount = pc.lightData->lightCount;
    int analyticCount = pc.lightData->analyticLightCount;

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
    float roughness = max(li.roughness, RESTIR_MIN_ROUGHNESS);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float3 worldN = normalize(mul(float3x3(scene.invView), N));

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;
    h ^= 0x6f3b91a7u;

    Reservoir r_gen = pc.genBuffer[pixelIdx];
    if (isnan(r_gen.W) || isinf(r_gen.W)) { r_gen = ReservoirEmpty(); }
    bool validGen = r_gen.lightIdx != ~0u;
    float Mc = float(r_gen.M);

    float p_hat_gen_atCenter = 0.0;
    if (validGen) {
        p_hat_gen_atCenter = EvalReservoirPHatLight(UnpackLightVS(pc.lightVS[r_gen.lightIdx]), r_gen.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, pc.lightSpecularFromReflectionsMax);
    }

    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;
    bool validHistory = pc.historyBuffer != nullptr && all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));

    float NoV = abs(dot(N, normalize(viewPosition)));
    float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
    float disocclusionThreshold = saturate(0.01 / lerp(0.05, 1.0, NoV)) * frustumSize;
    float expectedPrevViewDepth = viewSpaceDepth + UnpackViewZDelta(g1.w);

    uint2 baseHistory = uint2(0, 0);
    bool bHistoryFound = false;
    if (validHistory) {
        baseHistory = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
#if RESTIR_ENABLE_PERMUTATION_SAMPLING
        if (pc.bPermutationSampling != 0u) {
            int2 permuted = ApplyPermutationSampling(int2(baseHistory), pc.frameIndex);
            if (all(permuted >= int2(0, 0)) && all(permuted < int2(pc.renderExtent))) {
                baseHistory = uint2(permuted);
            }
        }
#endif
        bHistoryFound = true;
    }

    ReservoirAccum accum = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();
    bool winnerIsGen = false;
    float genMisAccum = 0.0;
    uint neighborCount = 0;
    uint accumulatedM = r_gen.M;

    uint spatialNeighbors = clamp(pc.spatialNeighbors, 0u, 32u);
    float spatialRadius = float(pc.spatialRadius);
    float bnRot = BlueNoiseAngle(pixel, pc.frameIndex);

    uint tapCount = bHistoryFound ? (1u + spatialNeighbors) : 0u;
    for (uint tap = 0u; tap < tapCount; tap++) {
        uint2 tapPixel = baseHistory;
        if (tap > 0u) {
            float angle = (float(tap - 1u) + HashStep(h)) * (6.28318530718 / float(max(spatialNeighbors, 1u))) + bnRot;
            float r = sqrt(HashStep(h)) * spatialRadius;
            int2 p = int2(baseHistory) + int2(round(cos(angle) * r), round(sin(angle) * r));
            if (any(p < int2(0, 0)) || any(p >= int2(pc.renderExtent))) { continue; }
            tapPixel = uint2(p);
        }

        if (field != 0u) {
            int2 snapped = ActivateCheckerboardPixel(int2(tapPixel), true, field);
            if (any(snapped < int2(0, 0)) || any(snapped >= int2(pc.renderExtent))) { continue; }
            tapPixel = uint2(snapped);
        }

        uint4 tapG1;
        PrevSurface prev = LoadPrevSurface(pc, scene, tapPixel, worldN, expectedPrevViewDepth, disocclusionThreshold, NoV, tapG1);
        if (!prev.bValid) {
#if RESTIR_ENABLE_TEMPORAL_SEARCH
            if (tap == 0u && pc.bTemporalSearch != 0u) {
                for (uint s = 0u; s < 8u; s++) {
                    float2 off = (float2(HashStep(h), HashStep(h)) - 0.5) * 8.0;
                    int2 p = int2(baseHistory) + int2(round(off));
                    if (any(p < int2(0, 0)) || any(p >= int2(pc.renderExtent))) { continue; }
                    uint2 retry = uint2(p);
                    if (field != 0u) {
                        int2 snapped = ActivateCheckerboardPixel(int2(retry), true, field);
                        if (any(snapped < int2(0, 0)) || any(snapped >= int2(pc.renderExtent))) { continue; }
                        retry = uint2(snapped);
                    }
                    prev = LoadPrevSurface(pc, scene, retry, worldN, expectedPrevViewDepth, disocclusionThreshold, NoV, tapG1);
                    if (prev.bValid) { tapPixel = retry; break; }
                }
            }
#endif
            if (!prev.bValid) { continue; }
        }

        uint2 tapRPos = PixelPosToReservoirPos(tapPixel, field);
        Reservoir neighbor = pc.historyBuffer[tapRPos.y * stride + tapRPos.x];
        if (isnan(neighbor.W) || isinf(neighbor.W) || neighbor.M == 0u) { continue; }
        // An empty reservoir is an estimate of zero and still counts; skipping it averages only the neighbours that found light
        bool bNeighborEmpty = neighbor.lightIdx == ~0u || !IsLiveLight(neighbor.lightIdx, lightCount, analyticCount);

        float3 prevF0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, prev.metallic);
        uint cappedM = min(neighbor.M, max(uint(round(float(pc.mCap) * scene.framerateScale)), 1u));
        float Mi = validGen ? min(float(cappedM), 20.0 * max(Mc, 1.0)) : float(cappedM);

        float p_hat_n_atN = 0.0;
        float p_hat_n_atCenter = 0.0;
        if (!bNeighborEmpty) {
            LightView neighborVS = UnpackLightVS(pc.lightVS[neighbor.lightIdx]);
            p_hat_n_atN = EvalReservoirPHatLight(neighborVS, neighbor.sampleOffsetPacked, prev.viewPosition, prev.N, prev.V, li.albedo.xyz, prevF0, prev.metallic, prev.roughness, pc.lightSpecularFromReflectionsMax);
            p_hat_n_atCenter = EvalReservoirPHatLight(neighborVS, neighbor.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, pc.lightSpecularFromReflectionsMax);
        }

        float denomNeighbor = Mi * p_hat_n_atN + Mc * p_hat_n_atCenter;
        float misNeighbor = (denomNeighbor > 0.0) ? (Mi * p_hat_n_atN) / denomNeighbor : 0.0;

        if (validGen) {
            float p_hat_gen_atN = EvalReservoirPHatLight(UnpackLightVS(pc.lightVS[r_gen.lightIdx]), r_gen.sampleOffsetPacked, prev.viewPosition, prev.N, prev.V, li.albedo.xyz, prevF0, prev.metallic, prev.roughness, pc.lightSpecularFromReflectionsMax);
            float denomGen = Mi * p_hat_gen_atN + Mc * p_hat_gen_atCenter;
            if (denomGen > 0.0) {
                genMisAccum += (Mc * p_hat_gen_atCenter) / denomGen;
            }
        }

        float w_neighbor = misNeighbor * p_hat_n_atCenter * abs(neighbor.W);
        if (ReservoirAddSample(accum, w_neighbor, HashStep(h))) {
            combined.lightIdx = neighbor.lightIdx;
            combined.sampleOffsetPacked = neighbor.sampleOffsetPacked;
            winnerIsGen = false;
        }
        float mFactor = (p_hat_n_atN > 0.0) ? saturate(p_hat_n_atCenter / p_hat_n_atN) : 1.0;
        accumulatedM += uint(float(cappedM) * mFactor + 0.5);
        neighborCount++;
    }

    if (validGen) {
        float misGen = (neighborCount == 0u) ? 1.0 : genMisAccum;
        float w_gen = misGen * p_hat_gen_atCenter * r_gen.W;
        if (ReservoirAddSample(accum, w_gen, HashStep(h))) {
            combined.lightIdx = r_gen.lightIdx;
            combined.sampleOffsetPacked = r_gen.sampleOffsetPacked;
            winnerIsGen = true;
        }
    }

    combined.M = accumulatedM;

    float winnerVisibility = 1.0;
#if RESTIR_ENABLE_CONFIDENCE
    float visPrev = 1.0;
#endif
    float pHatUnshadowed = 0.0;
    if (combined.lightIdx != ~0u) {
        bool bSkipWinnerTrace = false;
#if RESTIR_ENABLE_INITIAL_VISIBILITY
        bSkipWinnerTrace = pc.bInitialVisibility != 0u && winnerIsGen;
#endif
        float worldTexelSize = RayOriginPixelFootprint(viewSpaceDepth, scene.ndcToViewMulXPixelSize, 1.0);
        float3 shadowViewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv + scene.jitter * 0.5, viewSpaceDepth);
        float3 shadowWorldPos = mul(scene.invView, float4(shadowViewPosition, 1.0)).xyz + rdgTexturesFloat4[pc.shadowOriginOffsetIndex].Load(int3(pixel, 0)).xyz;

        LightView winnerVS = UnpackLightVS(pc.lightVS[combined.lightIdx]);
        float3 yStar = DecodeSamplePos(combined.sampleOffsetPacked, winnerVS);
        float3 worldyStar = mul(scene.invView, float4(yStar, 1.0)).xyz;
        LightSample s = EvalLightSample(winnerVS, yStar, viewPosition, N, winnerVS.range);
        pHatUnshadowed = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, UnpackUnorm4x8(winnerVS.packedColor).rgb, winnerVS.intensity, pc.lightSpecularFromReflectionsMax);

        bool bWinnerVisible = bSkipWinnerTrace || RestirShadowVisible(rdgAccelerationStructures[pc.tlasIndex], shadowWorldPos, worldN, worldyStar, worldTexelSize);
#if RESTIR_ENABLE_CONFIDENCE
        if (pc.prevTlasIndex != ~0u) {
            visPrev = RestirShadowVisible(rdgAccelerationStructures[pc.prevTlasIndex], shadowWorldPos, worldN, worldyStar, worldTexelSize) ? 1.0 : 0.0;
        }
#endif
        // Occluded winners keep their W so reuse is not darkened; the sign carries visibility
        ReservoirFinalize(combined, accum, pHatUnshadowed, (neighborCount == 0u) ? 1.0 : float(neighborCount));
        winnerVisibility = (bWinnerVisible && pHatUnshadowed > 0.0) ? 1.0 : 0.0;
#if RESTIR_ENABLE_CONFIDENCE
        if (pc.prevTlasIndex == ~0u) { visPrev = winnerVisibility; }
#endif
    } else {
        combined.W = 0.0;
    }

#if RESTIR_ENABLE_ANTILAG
    float flip = 0.0;
    if (pc.prevShadowVisIndex != ~0u && bHistoryFound && neighborCount > 0u) {
        float prevVis = rdgTexturesFloat[pc.prevShadowVisIndex].Load(int3(baseHistory, 0)).r;
        flip = abs(winnerVisibility - prevVis);
    }
    if (pc.antilagStrength > 0.0 && flip > 0.0) {
        combined.M = max(1u, uint(float(combined.M) * (1.0 - pc.antilagStrength * flip)));
    }
#endif

    if (pc.wClamp > 0.0) { combined.W = min(combined.W, pc.wClamp); }
    combined.M = min(combined.M, pc.spatialMCap);
    if (winnerVisibility == 0.0) { combined.W = -combined.W; }

    pc.outputBuffer[pixelIdx] = combined;

#if RESTIR_ENABLE_ANTILAG
    if (pc.shadowVisIndex != ~0u) {
        rdgStorageFloat[pc.shadowVisIndex][pixel] = winnerVisibility;
        if (partnerValid) { rdgStorageFloat[pc.shadowVisIndex][uint2(partner)] = winnerVisibility; }
    }
#endif
#if RESTIR_ENABLE_CONFIDENCE
    if (pc.signalIndex != ~0u) {
        float2 sig = float2(pHatUnshadowed * abs(winnerVisibility - visPrev), pHatUnshadowed * max(winnerVisibility, visPrev)) * scene.preExposure;
        rdgStorageFloat2[pc.signalIndex][pixel] = sig;
        if (partnerValid) { rdgStorageFloat2[pc.signalIndex][uint2(partner)] = sig; }
    }
#endif
}
