import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;
import restir_interop;

import packing_functions;
import shader_functions;
import bindless_declarations;
import restir_common;

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDISpatialPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    Reservoir center = pc.inputBuffer[pixel.y * pc.renderExtent.x + pixel.x];

    //
    pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = center;
    return;

    if (center.lightIdx == ~0u) {
        pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = ReservoirEmpty();
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float4x4 view = scene.view;
    float2 uv = (float2(pixel) + 0.5) / float2(scene.mainRenderTargetSize);
    float viewSpaceDepth = ScreenToViewSpaceDepth(rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0)).r));

    ReservoirAccum accum = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();

    uint h = (pixel.x * 2654435761u) ^ (pixel.y * 805459861u) ^ (pc.frameIndex * 1973u);

    // Add center to Reservoir.
    AreaLightData alCenter = pc.lightData->areaLights[center.lightIdx];
    AreaLightVS vsCenter = TransformAreaLight(alCenter, view);
    float3 yStarCenter = DecodeSamplePos(center, vsCenter);
    AreaLightSample sCenter = EvalAreaLightSample(vsCenter, yStarCenter, viewPosition, N, alCenter.range);

    float p_hat_center = 0.0;
    if (sCenter.cosShade > 0.0 && sCenter.cosLight > 0.0 && sCenter.attenuation > 0.0) {
        float3 lightColor = UnpackUnorm4x8(alCenter.packedColor).rgb;
        float geomTerm = sCenter.cosLight / max(sCenter.dist * sCenter.dist, 0.0001);
        p_hat_center = dot(sCenter.cosShade * geomTerm * alCenter.intensity * lightColor * sCenter.attenuation, float3(0.2126, 0.7152, 0.0722));
    }

    float w_center = p_hat_center * center.W * float(center.M);
    if (ReservoirAddSample(accum, combined, w_center, HashStep(h))) {
        combined.lightIdx = center.lightIdx;
        combined.sampleOffsetPacked = center.sampleOffsetPacked;
    }
    combined.M = center.M;

    uint32_t spatialRadius = pc.spatialRadius;
    uint32_t spatialNeighbors = pc.spatialNeighbors;
    // Combine with spatially offset neighbors via WRS.
    for (uint i = 0; i < spatialNeighbors; i++) {
        float angle = (float(i) + HashStep(h)) * (6.28318530718 / float(spatialNeighbors));
        float r = sqrt(HashStep(h)) * spatialRadius;
        int2 neighborPixel = int2(pixel) + int2(round(cos(angle) * r), round(sin(angle) * r));

        if (any(neighborPixel < int2(0, 0)) || any(neighborPixel >= int2(pc.renderExtent))) {
            continue;
        }

        Reservoir neighbor = pc.inputBuffer[neighborPixel.y * pc.renderExtent.x + neighborPixel.x];
        if (neighbor.lightIdx == ~0u || neighbor.lightIdx >= uint(pc.lightData->areaLightCount)) {
            continue;
        }

        // Evaluate the neighbor's sample at the center shading point.
        AreaLightData alNeighbor = pc.lightData->areaLights[neighbor.lightIdx];
        AreaLightVS vsNeighbor = TransformAreaLight(alNeighbor, view);
        float3 yStarNeighbor = DecodeSamplePos(neighbor, vsNeighbor);
        AreaLightSample sNeighbor = EvalAreaLightSample(vsNeighbor, yStarNeighbor, viewPosition, N, alNeighbor.range);

        float p_hat_neighbor = 0.0;
        if (sNeighbor.cosShade > 0.0 && sNeighbor.cosLight > 0.0 && sNeighbor.attenuation > 0.0) {
            float3 lightColor = UnpackUnorm4x8(alNeighbor.packedColor).rgb;
            float geomTerm = sNeighbor.cosLight / max(sNeighbor.dist * sNeighbor.dist, 0.0001);
            p_hat_neighbor = dot(sNeighbor.cosShade * geomTerm * alNeighbor.intensity * lightColor * sNeighbor.attenuation, float3(0.2126, 0.7152, 0.0722));
        }

        float uSelect = HashStep(h);
        float w_neighbor = p_hat_neighbor * neighbor.W * float(neighbor.M);
        if (ReservoirAddSample(accum, combined, w_neighbor, uSelect)) {
            combined.lightIdx = neighbor.lightIdx;
            combined.sampleOffsetPacked = neighbor.sampleOffsetPacked;
        }
        combined.M += neighbor.M;
    }

    // Finalise W at the winner.
    if (combined.lightIdx != ~0u) {
        AreaLightData alWinner = pc.lightData->areaLights[combined.lightIdx];
        AreaLightVS vsWinner = TransformAreaLight(alWinner, view);
        float3 yStar = DecodeSamplePos(combined, vsWinner);
        AreaLightSample sWinner = EvalAreaLightSample(vsWinner, yStar, viewPosition, N, alWinner.range);

        float p_hat_winner = 0.0;
        if (sWinner.cosShade > 0.0 && sWinner.cosLight > 0.0 && sWinner.attenuation > 0.0) {
            float3 lightColor = UnpackUnorm4x8(alWinner.packedColor).rgb;
            float geomTerm = sWinner.cosLight / max(sWinner.dist * sWinner.dist, 0.0001);
            p_hat_winner = dot(sWinner.cosShade * geomTerm * alWinner.intensity * lightColor * sWinner.attenuation, float3(0.2126, 0.7152, 0.0722));
        }

        ReservoirFinalise(combined, accum, p_hat_winner, float(combined.M));
        const float W_CLAMP = 20.0;
        combined.W = min(combined.W, W_CLAMP);
    }

    pc.outputBuffer[pixel.y * pc.renderExtent.x + pixel.x] = combined;
}
