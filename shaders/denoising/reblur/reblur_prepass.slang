/*
 * ReBLUR: PrePass. Pre-accumulation spatial reuse (Poisson-8) using hit-distance / normal / plane
 * edge-stopping weights. Radiance is YCoCg (blended linearly); hit distance is raw world units.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import relax_utils;
import reblur_utils;
import restir_common;

[shader("compute")]
[numthreads(16, 16, 1)]
void ReblurPrepassMain(uniform ReblurPrepassPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;
    float3x3 viewToWorld = (float3x3)rc.gViewToWorld;

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y) { return; }

    int2 fullPixel = pixelPos;
    float centerViewZ = RelaxLoadViewZ(pc.viewZIndex, fullPixel, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange)) { return; }

    float4 centerNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(fullPixel, 0)), viewToWorld);
    float3 centerNormal = centerNR.xyz;
    float centerRoughness = centerNR.w;
    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

    float angle = MLBayer4x4(uint2(pixelPos), rc.gFrameIndex) * MLPi(2.0);
    float4 rotator = MLCombineRotators(rc.gRotatorPre, MLGetRotator(angle));

    float frustumSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, min(float(rc.gRectSize.x), float(rc.gRectSize.y)), centerViewZ);
    float planeDistanceCenterZ = rc.gOrthoMode == 0 ? centerViewZ : 1.0;

    // ---------------- Diffuse pre-blur ----------------
    float4 diffSum = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(pixelPos, 0));
    {
        // NRD pre-blur: areaFactor = hitDistFactor; blurRadius = gDiffPrepassBlurRadius * sqrt(areaFactor).
        float hitDist = diffSum.a == 0.0 ? 1.0 : diffSum.a;
        float blurRadius = rc.gDiffPrepassBlurRadius * sqrt(saturate(RelaxGetHitDistFactor(hitDist, frustumSize)));
        if (diffSum.a == 0.0) { blurRadius = max(blurRadius, 1.0); }

        float normalWeightParam = RelaxGetNormalWeightParam2(1.0, 0.25 * rc.gLobeAngleFraction);
        float2 hitDistanceWeightParams = RelaxGetHitDistanceWeightParams(diffSum.a, 1.0 / 9.0);
        float weightSum = 1.0;

        for (int i = 0; i < 8; i++) {
            float3 offset = REBLUR_POISSON8[i];
            float2 samplePosF = floor(float2(pixelPos) + 0.5 + MLRotateVector(rotator, offset.xy) * blurRadius) + 0.5;
            float2 uv = samplePosF * rc.gRectSizeInv;
            if (RelaxIsInScreenNearest(uv) == 0.0) { continue; }

            int2 samplePos = int2(samplePosF);
            int2 sampleFullPos = samplePos;
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, sampleFullPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange)) { continue; }

            float3 sampleNormal = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(sampleFullPos, 0)), viewToWorld).xyz;
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(samplePos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float w = RelaxGetPlaneDistanceWeight(centerWorldPos, centerNormal, planeDistanceCenterZ, sampleWorldPos, rc.gPlaneDistSensitivity);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNormal));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);
            if (w > 1e-4) {
                float4 s = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(samplePos, 0));
                w *= lerp(rc.gMinHitDistanceWeight, 1.0, RelaxComputeExponentialWeight(s.a, hitDistanceWeightParams.x, hitDistanceWeightParams.y));
                w *= RelaxGetGaussianWeight(offset.z);
                diffSum += s * w;
                weightSum += w;
            }
        }
        diffSum /= weightSum;
    }
    rdgStorageFloat4[pc.diffOutIndex][pixelPos] = diffSum;

    // ---------------- Specular pre-blur ----------------
    float4 specSum = rdgTexturesFloat4[pc.specInputIndex].Load(int3(pixelPos, 0));
    specSum.a = clamp(specSum.a, 0.0, rc.gDenoisingRange);
    {
        float3 viewVector = rc.gOrthoMode == 0 ? normalize(-centerWorldPos) : rc.gFrustumForward.xyz;
        float4 D = MLGetSpecularDominantDirection(centerNormal, viewVector, centerRoughness);
        float NoD = abs(dot(centerNormal, D.xyz));
        float NoV = abs(dot(centerNormal, viewVector));

        // NRD spec pre-blur: areaFactor = roughness * hitDistFactor; blurRadius = gSpecPrepassBlurRadius * sqrt(areaFactor).
        // hitDistFactor uses world hitDist WITHOUT *NoD (NRD note: *NoD worsens glancing angles); roughness replaces specMagicCurve.
        float smc = RelaxGetSpecMagicCurve(centerRoughness);
        float hitDist = specSum.a == 0.0 ? 1.0 : specSum.a;
        float hitDistFactor = RelaxGetHitDistFactor(hitDist, frustumSize);
        float blurRadius = rc.gSpecPrepassBlurRadius * sqrt(saturate(centerRoughness * hitDistFactor));

        float lobeRadius = hitDist * NoD * MLGetSpecularLobeTanHalfAngle(centerRoughness);
        float minBlurRadius = lobeRadius / RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, centerViewZ + hitDist * D.w);
        blurRadius = min(blurRadius, minBlurRadius);
        if (specSum.a == 0.0) { blurRadius = max(blurRadius, 1.0); }

        float normalWeightParam = RelaxGetNormalWeightParam2(centerRoughness, 0.5 * rc.gLobeAngleFraction);
        float2 hitDistanceWeightParams = RelaxGetHitDistanceWeightParams(specSum.a, 1.0 / 9.0);
        float2 roughnessWeightParams = RelaxGetRoughnessWeightParams(centerRoughness, rc.gRoughnessFraction);
        float specMinHitDistanceWeight = specSum.a == 0.0 ? 1.0 : rc.gMinHitDistanceWeight * smc;

        float minHitT = specSum.a == 0.0 ? rc.gDenoisingRange : specSum.a;
        float weightSum = 1.0;
        uint rngState = MLHashCombine(MLSequenceHash(uint(pixelPos.x) + uint(pixelPos.y) * 65536u), rc.gFrameIndex);

        for (int i = 0; i < 8; i++) {
            float3 offset = REBLUR_POISSON8[i];
            float2 samplePosF = floor(float2(pixelPos) + 0.5 + MLRotateVector(rotator, offset.xy) * blurRadius) + 0.5;
            float2 uv = samplePosF * rc.gRectSizeInv;
            if (RelaxIsInScreenNearest(uv) == 0.0) { continue; }

            int2 samplePos = int2(samplePosF);
            int2 sampleFullPos = samplePos;
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, sampleFullPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange)) { continue; }

            float4 sampleNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(sampleFullPos, 0)), viewToWorld);
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(samplePos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float w = RelaxComputeWeight(sampleNR.w, roughnessWeightParams.x, roughnessWeightParams.y);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNR.xyz));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);
            w *= RelaxGetPlaneDistanceWeight(centerWorldPos, centerNormal, planeDistanceCenterZ, sampleWorldPos, rc.gPlaneDistSensitivity);

            float4 s = rdgTexturesFloat4[pc.specInputIndex].Load(int3(samplePos, 0));

            rngState = MLSequenceHash(rngState);
            if (float(rngState >> 8) * (1.0 / 16777216.0) < w * NoV) {
                minHitT = min(minHitT, s.a == 0.0 ? NRD_INF : s.a);
            }

            w *= lerp(specMinHitDistanceWeight, 1.0, RelaxComputeExponentialWeight(s.a, hitDistanceWeightParams.x, hitDistanceWeightParams.y));
            w *= RelaxGetGaussianWeight(offset.z);

            float d = length(sampleWorldPos - centerWorldPos);
            float t = s.a / (specSum.a + d);
            w *= lerp(saturate(t), 1.0, MLLinearStep(0.5, 1.0, centerRoughness));

            specSum.rgb += s.rgb * w;
            weightSum += w;
        }
        specSum.rgb /= weightSum;
        specSum.a = minHitT >= NRD_INF ? 0.0 : minHitT;
    }
    rdgStorageFloat4[pc.specOutIndex][pixelPos] = specSum;
}
