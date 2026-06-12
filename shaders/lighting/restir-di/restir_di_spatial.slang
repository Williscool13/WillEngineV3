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
void ComputeMain(uniform ReSTIRDISpatialPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    uint centerIdx = pixel.y * pc.renderExtent.x + pixel.x;
    Reservoir center = pc.inputBuffer[centerIdx];
    bool validCenter = center.lightIdx != ~0u && center.lightIdx < uint(pc.lightData->areaLightCount);

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    uint2 fullPixel = QuadFullPixel(pixel, pc.pixelScale, pc.frameIndex);
    float viewSpaceDepth = ScreenToViewSpaceDepth(rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        center.W = min(center.W, 20.0);
        center.M = min(center.M, pc.mCap);
        pc.outputBuffer[centerIdx] = center;
        return;
    }

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    // Canonical confidence; zero when the center carries no sample so it can't suppress neighbors.
    float Mc = validCenter ? float(center.M) : 0.0;

    // Center sample and its target at the center.
    AreaLightVS vsCenter;
    float3 yStarCenter = float3(0);
    float3 centerLightColor = float3(0);
    float p_hat_center_atCenter = 0.0;
    if (validCenter) {
        vsCenter = UnpackAreaLightVS(pc.lightVS[center.lightIdx]);
        yStarCenter = DecodeSamplePos(center, vsCenter);
        centerLightColor = UnpackUnorm4x8(vsCenter.packedColor).rgb;
        AreaLightSample sCenter = EvalAreaLightSample(vsCenter, yStarCenter, viewPosition, N, vsCenter.range);
        p_hat_center_atCenter = EvalPHat(sCenter, V, N, li.albedo.xyz, F0, li.metallic, roughness, centerLightColor, vsCenter.intensity);
    }

    ReservoirAccum accum = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();
    // Salted per pass so chained spatial passes draw different neighbor offsets
    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u) ^ (0xF7B3D1u + pc.passIndex * 0x9E3779B9u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    // Canonical's MIS share summed across the k pairs. The shared 1/k is applied via the finalise divisor (neighborCount), uniformly across canonical and neighbor stream weights.
    float centerMisAccum = 0.0;
    uint neighborCount = 0;
    combined.M = validCenter ? center.M : 0u;

    uint spatialNeighbors = pc.spatialNeighbors;
    uint spatialRadius = pc.spatialRadius;
    float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
    for (uint i = 0; i < spatialNeighbors; i++) {
        float angle = (float(i) + HashStep(h)) * (6.28318530718 / float(spatialNeighbors));
        float r = sqrt(HashStep(h)) * spatialRadius;
        int2 neighborPixel = int2(pixel) + int2(round(cos(angle) * r), round(sin(angle) * r));

        if (any(neighborPixel < int2(0, 0)) || any(neighborPixel >= int2(pc.renderExtent)) || all(neighborPixel == int2(pixel))) {
            continue;
        }

        uint2 neighborFullPixel = QuadFullPixel(uint2(neighborPixel), pc.pixelScale, pc.frameIndex);

        // Geometry rejection: plane distance (NRD style), so neighbors on the same plane seen at grazing angles are not falsely rejected by their depth delta.
        float neighborRawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(neighborFullPixel, 0)).r;
        float neighborViewDepth = ScreenToViewSpaceDepth(neighborRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float2 neighborUV = (float2(neighborPixel) + 0.5f) / float2(pc.renderExtent);
        float3 neighborViewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, neighborUV, neighborViewDepth);
        if (abs(dot(N, neighborViewPosition - viewPosition)) > 0.01 * frustumSize) {
            continue;
        }

        // Geometry rejection: normal.
        float3 neighborN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(neighborFullPixel, 0)).r));
        if (dot(N, neighborN) < 0.906) {
            continue;
        }

        Reservoir neighbor = pc.inputBuffer[neighborPixel.y * pc.renderExtent.x + neighborPixel.x];
        if (neighbor.lightIdx == ~0u || neighbor.lightIdx >= uint(pc.lightData->areaLightCount)) {
            continue;
        }

        // Neighbor surface is required to evaluate targets in the neighbor's own domain.
        LightingInputs nli = ReconstructLightingInputs(neighborFullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
        if (!nli.valid) {
            continue;
        }

        float3 neighborV = normalize(-neighborViewPosition);
        float neighborRoughness = max(nli.roughness, 0.045);
        float3 neighborF0 = lerp(float3(0.04, 0.04, 0.04), nli.albedo.xyz, nli.metallic);

        AreaLightVS vsNeighbor = UnpackAreaLightVS(pc.lightVS[neighbor.lightIdx]);
        float3 neighborLightColor = UnpackUnorm4x8(vsNeighbor.packedColor).rgb;
        float3 yStarNeighbor = DecodeSamplePos(neighbor, vsNeighbor);

        // Capped neighbor confidence for the MIS denominators (prevents stale history from suppressing the canonical). This is the MIS-weight M only; combined.M accumulates real M.
        float Mi = validCenter ? min(float(neighbor.M), 20.0 * float(center.M)) : float(neighbor.M);

        // Neighbor sample evaluated at the neighbor (own domain) and at the center.
        AreaLightSample sNeighbor_atNeighbor = EvalAreaLightSample(vsNeighbor, yStarNeighbor, neighborViewPosition, neighborN, vsNeighbor.range);
        float p_hat_neighbor_atNeighbor = EvalPHat(sNeighbor_atNeighbor, neighborV, neighborN, nli.albedo.xyz, neighborF0, nli.metallic, neighborRoughness, neighborLightColor, vsNeighbor.intensity);
        AreaLightSample sNeighbor_atCenter = EvalAreaLightSample(vsNeighbor, yStarNeighbor, viewPosition, N, vsNeighbor.range);
        float p_hat_neighbor_atCenter = EvalPHat(sNeighbor_atCenter, V, N, li.albedo.xyz, F0, li.metallic, roughness, neighborLightColor, vsNeighbor.intensity);

        // Pair (neighbor i, center): balance heuristic for the neighbor's sample.
        float denomNeighbor = Mi * p_hat_neighbor_atNeighbor + Mc * p_hat_neighbor_atCenter;
        float misNeighbor = (denomNeighbor > 0.0) ? (Mi * p_hat_neighbor_atNeighbor) / denomNeighbor : 0.0;

        // Same pair, the canonical's share. Center sample evaluated at this neighbor.
        if (validCenter) {
            AreaLightSample sCenter_atNeighbor = EvalAreaLightSample(vsCenter, yStarCenter, neighborViewPosition, neighborN, vsCenter.range);
            float p_hat_center_atNeighbor = EvalPHat(sCenter_atNeighbor, neighborV, neighborN, nli.albedo.xyz, neighborF0, nli.metallic, neighborRoughness, centerLightColor, vsCenter.intensity);
            float denomCenter = Mi * p_hat_center_atNeighbor + Mc * p_hat_center_atCenter;
            if (denomCenter > 0.0) {
                centerMisAccum += (Mc * p_hat_center_atCenter) / denomCenter;
            }
        }

        // Stream toward the center target. The shared 1/k is applied in finalise.
        float w_neighbor = misNeighbor * p_hat_neighbor_atCenter * neighbor.W;
        if (ReservoirAddSample(accum, combined, w_neighbor, HashStep(h))) {
            combined.lightIdx = neighbor.lightIdx;
            combined.sampleOffsetPacked = neighbor.sampleOffsetPacked;
        }
        combined.M += neighbor.M;
        neighborCount++;
    }

    // No usable neighbor: passthrough with clamp/cap applied.
    if (neighborCount == 0u) {
        center.W = min(center.W, 20.0);
        center.M = min(center.M, pc.mCap);
        pc.outputBuffer[centerIdx] = center;
        return;
    }

    // Stream the canonical with its accumulated share.
    if (validCenter) {
        float w_center = centerMisAccum * p_hat_center_atCenter * center.W;
        if (ReservoirAddSample(accum, combined, w_center, HashStep(h))) {
            combined.lightIdx = center.lightIdx;
            combined.sampleOffsetPacked = center.sampleOffsetPacked;
        }
    }

    // M_total here is the pair count k (the shared 1/k). The per-pair MIS weights partition unity, so the only normalization left is the uniform 1/k across all k streamed weights.
    if (combined.lightIdx != ~0u) {
        AreaLightVS vsFinal = UnpackAreaLightVS(pc.lightVS[combined.lightIdx]);
        float3 yStarFinal = DecodeSamplePos(combined, vsFinal);
        AreaLightSample sFinal = EvalAreaLightSample(vsFinal, yStarFinal, viewPosition, N, vsFinal.range);
        float3 finalLightColor = UnpackUnorm4x8(vsFinal.packedColor).rgb;
        float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        float3 worldyStarFinal = mul(scene.invView, float4(yStarFinal, 1.0)).xyz;
        RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
        float p_hat_final = EvalPHatVisibility(sFinal, V, N, li.albedo.xyz, F0, li.metallic, roughness, finalLightColor, vsFinal.intensity,
                                               tlas, worldPos, worldN, worldyStarFinal);
        ReservoirFinalise(combined, accum, p_hat_final, float(neighborCount));
    } else {
        combined.W = 0.0;
    }

    combined.W = min(combined.W, 20.0);
    combined.M = min(combined.M, pc.mCap);

    pc.outputBuffer[centerIdx] = combined;
}
