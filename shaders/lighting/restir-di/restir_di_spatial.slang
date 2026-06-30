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

#include "restir_features_macros.slang"

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDISpatialPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    uint centerIdx = pixel.y * pc.renderExtent.x + pixel.x;

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    uint2 fullPixel = pixel;
    float viewSpaceDepth = ScreenToViewSpaceDepth(rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float worldTexelSize = RestirWorldTexelSize(viewSpaceDepth, scene.ndcToViewMulXPixelSize);

    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        Reservoir passthrough = pc.inputBuffer[centerIdx];
        if (isnan(passthrough.W) || isinf(passthrough.W)) { passthrough = ReservoirEmpty(); }
        passthrough.W = (pc.wClamp > 0.0) ? min(passthrough.W, pc.wClamp) : passthrough.W;
        passthrough.M = min(passthrough.M, pc.mCap);
        pc.outputBuffer[centerIdx] = passthrough;
        return;
    }

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    Reservoir center = pc.inputBuffer[centerIdx];
    if (isnan(center.W) || isinf(center.W)) { center = ReservoirEmpty(); }
    if (center.lightIdx == LIGHT_IDX_SUN) {
        center.W = (pc.wClamp > 0.0) ? min(center.W, pc.wClamp) : center.W;
        center.M = min(center.M, pc.mCap);
        pc.outputBuffer[centerIdx] = center;
        return;
    }
    bool validCenter = center.lightIdx != ~0u && center.lightIdx < uint(pc.lightData->lightCount);

    float Mc = validCenter ? float(center.M) : 0.0;

    float p_hat_center_atCenter = 0.0;
    if (validCenter) {
        p_hat_center_atCenter = EvalReservoirPHat(center.lightIdx, center.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
    }

    ReservoirAccum accum = ReservoirAccumEmpty();
    Reservoir combined = ReservoirEmpty();
    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u) ^ (0xF7B3D1u + pc.passIndex * 0x9E3779B9u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    float centerMisAccum = 0.0;
    uint neighborCount = 0;
    combined.M = validCenter ? center.M : 0u;

#if RESTIR_ENABLE_SPATIAL_DILATE
    float convergence = saturate(Mc / float(max(pc.adaptiveMReference, 1u)));
    float adaptiveBoost = (pc.bAdaptiveSpatial != 0u) ? (1.0 + pc.adaptiveSpatialBoost * (1.0 - convergence)) : 1.0;
    uint spatialNeighbors = clamp(uint(round(float(pc.spatialNeighbors) * adaptiveBoost)), 1u, 32u);
    float spatialRadius = float(pc.spatialRadius) * adaptiveBoost;
#else
    uint spatialNeighbors = clamp(pc.spatialNeighbors, 1u, 32u);
    float spatialRadius = float(pc.spatialRadius);
#endif
    float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
    float bnRot = BlueNoiseAngle(pixel, pc.frameIndex + pc.passIndex * 32u);
    for (uint i = 0; i < spatialNeighbors; i++) {
        float angle = (float(i) + HashStep(h)) * (6.28318530718 / float(spatialNeighbors)) + bnRot;
        float r = sqrt(HashStep(h)) * spatialRadius;
        int2 neighborPixel = int2(pixel) + int2(round(cos(angle) * r), round(sin(angle) * r));

        if (any(neighborPixel < int2(0, 0)) || any(neighborPixel >= int2(pc.renderExtent)) || all(neighborPixel == int2(pixel))) {
            continue;
        }

        uint2 neighborFullPixel = uint2(neighborPixel);

        float neighborRawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(neighborFullPixel, 0)).r;
        float neighborViewDepth = ScreenToViewSpaceDepth(neighborRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float2 neighborUV = (float2(neighborPixel) + 0.5f) / float2(pc.renderExtent);
        float3 neighborViewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, neighborUV, neighborViewDepth);
        if (abs(dot(N, neighborViewPosition - viewPosition)) > 0.01 * frustumSize) {
            continue;
        }

        float3 neighborN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(neighborFullPixel, 0)).r));
        if (dot(N, neighborN) < 0.906) {
            continue;
        }

        Reservoir neighbor = pc.inputBuffer[neighborPixel.y * pc.renderExtent.x + neighborPixel.x];
        if (isnan(neighbor.W) || isinf(neighbor.W)) { neighbor = ReservoirEmpty(); }
        if (neighbor.lightIdx == ~0u || neighbor.lightIdx == LIGHT_IDX_SUN || neighbor.lightIdx >= uint(pc.lightData->lightCount)) {
            continue;
        }
        if (validCenter && center.M >= 4u && neighbor.M < 2u) {
            continue;
        }

        LightingInputs nli = ReconstructLightingInputs(neighborFullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
        if (!nli.valid) {
            continue;
        }
        if (abs(nli.roughness - li.roughness) > 0.5) {
            continue;
        }
        if (any(abs(nli.albedo.xyz - li.albedo.xyz) > float3(0.5))) {
            continue;
        }

        float3 neighborV = normalize(-neighborViewPosition);
        float neighborRoughness = max(nli.roughness, 0.045);
        float3 neighborF0 = lerp(float3(0.04, 0.04, 0.04), nli.albedo.xyz, nli.metallic);

        float Mi = validCenter ? min(float(neighbor.M), 20.0 * float(center.M)) : float(neighbor.M);

        float p_hat_neighbor_atNeighbor = EvalReservoirPHat(neighbor.lightIdx, neighbor.sampleOffsetPacked, neighborViewPosition, neighborN, neighborV, nli.albedo.xyz, neighborF0, nli.metallic, neighborRoughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
        float p_hat_neighbor_atCenter = EvalReservoirPHat(neighbor.lightIdx, neighbor.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);

        float denomNeighbor = Mi * p_hat_neighbor_atNeighbor + Mc * p_hat_neighbor_atCenter;
        float misNeighbor = (denomNeighbor > 0.0) ? (Mi * p_hat_neighbor_atNeighbor) / denomNeighbor : 0.0;

        if (validCenter) {
            float p_hat_center_atNeighbor = EvalReservoirPHat(center.lightIdx, center.sampleOffsetPacked, neighborViewPosition, neighborN, neighborV, nli.albedo.xyz, neighborF0, nli.metallic, neighborRoughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
            float denomCenter = Mi * p_hat_center_atNeighbor + Mc * p_hat_center_atCenter;
            if (denomCenter > 0.0) {
                centerMisAccum += (Mc * p_hat_center_atCenter) / denomCenter;
            }
        }

        float w_neighbor = misNeighbor * p_hat_neighbor_atCenter * neighbor.W;
        if (ReservoirAddSample(accum, w_neighbor, HashStep(h))) {
            combined.lightIdx = neighbor.lightIdx;
            combined.sampleOffsetPacked = neighbor.sampleOffsetPacked;
        }
        float mFactor = (p_hat_neighbor_atNeighbor > 0.0) ? saturate(p_hat_neighbor_atCenter / p_hat_neighbor_atNeighbor) : 1.0;
        combined.M += uint(float(neighbor.M) * mFactor + 0.5);
        neighborCount++;
    }

    if (neighborCount == 0u) {
        center.W = (pc.wClamp > 0.0) ? min(center.W, pc.wClamp) : center.W;
        center.M = min(center.M, pc.mCap);
        pc.outputBuffer[centerIdx] = center;
        return;
    }

    if (validCenter) {
        float w_center = centerMisAccum * p_hat_center_atCenter * center.W;
        if (ReservoirAddSample(accum, w_center, HashStep(h))) {
            combined.lightIdx = center.lightIdx;
            combined.sampleOffsetPacked = center.sampleOffsetPacked;
        }
    }

    if (combined.lightIdx != ~0u) {
        float p_hat_final;
        if (pc.bValidateVisibility != 0u) {
            float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
            float3 worldN = normalize(mul(float3x3(scene.invView), N));
            p_hat_final = EvalReservoirPHatVisibility(combined.lightIdx, combined.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, scene.invView, pc.lightData->directionalLight, pc.lightVS, rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldTexelSize);
        } else {
            p_hat_final = EvalReservoirPHat(combined.lightIdx, combined.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
        }
        ReservoirFinalise(combined, accum, p_hat_final, float(neighborCount));
    } else {
        combined.W = 0.0;
    }

    combined.W = (pc.wClamp > 0.0) ? min(combined.W, pc.wClamp) : combined.W;
    combined.M = min(combined.M, pc.mCap);

    pc.outputBuffer[centerIdx] = combined;
}
