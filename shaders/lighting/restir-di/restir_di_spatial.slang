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

    //
    //pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = center;
    //return;

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float4x4 view = scene.view;
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
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
    AreaLightData alCenter;
    AreaLightVS vsCenter;
    float3 yStarCenter = float3(0);
    float3 centerLightColor = float3(0);
    float p_hat_center_atCenter = 0.0;
    if (validCenter) {
        alCenter = pc.lightData->areaLights[center.lightIdx];
        vsCenter = TransformAreaLight(alCenter, view);
        yStarCenter = DecodeSamplePos(center, vsCenter);
        centerLightColor = UnpackUnorm4x8(alCenter.packedColor).rgb;
        AreaLightSample sCenter = EvalAreaLightSample(vsCenter, yStarCenter, viewPosition, N, alCenter.range);
        p_hat_center_atCenter = EvalPHat(sCenter, V, N, li.albedo.xyz, F0, li.metallic, roughness, centerLightColor, alCenter.intensity);
    }

    ReservoirAccum accum = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();
    uint h = (pixel.x * 2654435761u) ^ (pixel.y * 805459861u) ^ (pc.frameIndex * 1973u);

    // Center-domain target of whichever sample currently wins; finalise divides by this.
    float p_hat_winner = 0.0;
    // Canonical's MIS share summed across the k pairs.
    float centerMisAccum = 0.0;
    uint neighborCount = 0;
    combined.M = validCenter ? center.M : 0u;

    uint spatialNeighbors = pc.spatialNeighbors;
    uint spatialRadius = pc.spatialRadius;
    for (uint i = 0; i < spatialNeighbors; i++) {
        float angle = (float(i) + HashStep(h)) * (6.28318530718 / float(spatialNeighbors));
        float r = sqrt(HashStep(h)) * spatialRadius;
        int2 neighborPixel = int2(pixel) + int2(round(cos(angle) * r), round(sin(angle) * r));

        if (any(neighborPixel < int2(0, 0)) || any(neighborPixel >= int2(pc.renderExtent))) {
            continue;
        }

        // Geometry rejection: depth (abs guards a negative-Z convention).
        float neighborRawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(neighborPixel, 0)).r;
        float neighborViewDepth = ScreenToViewSpaceDepth(neighborRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        if (abs(neighborViewDepth - viewSpaceDepth) > 0.1 * abs(viewSpaceDepth)) {
            continue;
        }

        // Geometry rejection: normal.
        float3 neighborN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(neighborPixel, 0)).r));
        if (dot(N, neighborN) < 0.906) {
            continue;
        }

        Reservoir neighbor = pc.inputBuffer[neighborPixel.y * pc.renderExtent.x + neighborPixel.x];
        if (neighbor.lightIdx == ~0u || neighbor.lightIdx >= uint(pc.lightData->areaLightCount)) {
            continue;
        }

        // Neighbor surface is required to evaluate targets in the neighbor's own domain.
        LightingInputs nli = ReconstructLightingInputs(uint2(neighborPixel), pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
        if (!nli.valid) {
            continue;
        }

        float2 neighborUV = (float2(neighborPixel) + 0.5) / float2(pc.renderExtent);
        float3 neighborViewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, neighborUV, neighborViewDepth);
        float3 neighborV = normalize(-neighborViewPosition);
        float neighborRoughness = max(nli.roughness, 0.045);
        float3 neighborF0 = lerp(float3(0.04, 0.04, 0.04), nli.albedo.xyz, nli.metallic);

        AreaLightData alNeighbor = pc.lightData->areaLights[neighbor.lightIdx];
        AreaLightVS vsNeighbor = TransformAreaLight(alNeighbor, view);
        float3 neighborLightColor = UnpackUnorm4x8(alNeighbor.packedColor).rgb;
        float3 yStarNeighbor = DecodeSamplePos(neighbor, vsNeighbor);

        float Mi = validCenter ? min(float(neighbor.M), 20.0 * float(center.M)) : float(neighbor.M);

        // Neighbor sample evaluated at the neighbor (own domain) and at the center.
        AreaLightSample sNeighbor_atNeighbor = EvalAreaLightSample(vsNeighbor, yStarNeighbor, neighborViewPosition, neighborN, alNeighbor.range);
        float p_hat_neighbor_atNeighbor = EvalPHat(sNeighbor_atNeighbor, neighborV, neighborN, nli.albedo.xyz, neighborF0, nli.metallic, neighborRoughness, neighborLightColor, alNeighbor.intensity);
        AreaLightSample sNeighbor_atCenter = EvalAreaLightSample(vsNeighbor, yStarNeighbor, viewPosition, N, alNeighbor.range);
        float p_hat_neighbor_atCenter = EvalPHat(sNeighbor_atCenter, V, N, li.albedo.xyz, F0, li.metallic, roughness, neighborLightColor, alNeighbor.intensity);

        // Pair (neighbor i, center): balance heuristic for the neighbor's sample.
        float denomNeighbor = Mi * p_hat_neighbor_atNeighbor + Mc * p_hat_neighbor_atCenter;
        float misNeighbor = (denomNeighbor > 0.0) ? (Mi * p_hat_neighbor_atNeighbor) / denomNeighbor : 0.0;

        // Same pair, the canonical's share. Center sample evaluated at this neighbor.
        if (validCenter) {
            AreaLightSample sCenter_atNeighbor = EvalAreaLightSample(vsCenter, yStarCenter, neighborViewPosition, neighborN, alCenter.range);
            float p_hat_center_atNeighbor = EvalPHat(sCenter_atNeighbor, neighborV, neighborN, nli.albedo.xyz, neighborF0, nli.metallic, neighborRoughness, centerLightColor, alCenter.intensity);
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
            p_hat_winner = p_hat_neighbor_atCenter;
        }
        combined.M += uint(Mi);
        neighborCount++;
    }

    // No usable neighbor: nothing to combine.
    if (neighborCount == 0u) {
        pc.outputBuffer[centerIdx] = center;
        return;
    }

    // Stream the canonical with its accumulated share.
    if (validCenter) {
        float w_center = centerMisAccum * p_hat_center_atCenter * center.W;
        if (ReservoirAddSample(accum, combined, w_center, HashStep(h))) {
            combined.lightIdx = center.lightIdx;
            combined.sampleOffsetPacked = center.sampleOffsetPacked;
            p_hat_winner = p_hat_center_atCenter;
        }
    }

    // M_total here is the pair count k (the shared 1/k), distinct from combined.M.
    if (combined.lightIdx != ~0u && p_hat_winner > 0.0) {
        ReservoirFinalise(combined, accum, p_hat_winner, float(neighborCount));
        const float W_CLAMP = 20.0;
        combined.W = min(combined.W, W_CLAMP);
    } else {
        combined.W = 0.0;
    }

    pc.outputBuffer[centerIdx] = combined;
}