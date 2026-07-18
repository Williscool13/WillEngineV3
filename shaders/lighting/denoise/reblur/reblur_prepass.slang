/*
 * ReBLUR: PrePass. Pre-accumulation spatial reuse (Poisson-8) using hit-distance / normal / plane
 * edge-stopping weights. Radiance is YCoCg (blended linearly); hit distance is normalized [0,1].
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import denoiser_common;
import reblur_common;
import restir_common;

static const float REBLUR_PRE_PASS_FRACTION_SCALE = 2.0;

int2 CheckerboardShiftFull(int2 samplePos, uint cbField, uint counter, uint frameIndex, int rectSizeX)
{
    if (cbField == 2u) { return samplePos; }
    uint sc = ((uint(samplePos.x) ^ uint(samplePos.y)) ^ frameIndex) & 1u;
    if (sc != cbField) { samplePos.x += ((counter & 1u) == 0u) ? -1 : 1; }
    samplePos.x = clamp(samplePos.x, 0, rectSizeX - 1);
    return samplePos;
}

int2 CheckerboardSignalPos(int2 samplePos, uint cbField, uint counter, uint frameIndex, int rectSizeX)
{
    int2 p = CheckerboardShiftFull(samplePos, cbField, counter, frameIndex, rectSizeX);
    if (cbField != 2u) { p.x >>= 1; }
    return p;
}

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

    // Checkerboard holes blend the two horizontal packed neighbors, depth-weighted.
    uint cbCheckerboard = ((uint(pixelPos.x) ^ uint(pixelPos.y)) ^ rc.gFrameIndex) & 1u;
    int2 cbPosL = int2(max(pixelPos.x - 1, 0) >> 1, pixelPos.y);
    int2 cbPosR = int2(min(pixelPos.x + 1, rc.gRectSize.x - 1) >> 1, pixelPos.y);
    float2 cbWeights = float2(1.0, 1.0);
    if (rc.gCheckerboard != 2u)
    {
        float zL = RelaxLoadViewZ(pc.viewZIndex, int2(max(pixelPos.x - 1, 0), pixelPos.y), rc.depthLinearizeMult, rc.depthLinearizeAdd);
        float zR = RelaxLoadViewZ(pc.viewZIndex, int2(min(pixelPos.x + 1, rc.gRectSize.x - 1), pixelPos.y), rc.depthLinearizeMult, rc.depthLinearizeAdd);
        cbWeights = saturate(1.0 - abs(float2(zL, zR) - centerViewZ) / max(abs(centerViewZ) * 0.02, 1e-5));
        cbWeights.x = (!RelaxIsInDenoisingRange(zL, rc.gDenoisingRange) || pixelPos.x < 1) ? 0.0 : cbWeights.x;
        cbWeights.y = (!RelaxIsInDenoisingRange(zR, rc.gDenoisingRange) || pixelPos.x > rc.gRectSize.x - 2) ? 0.0 : cbWeights.y;
    }

    float4 centerNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(fullPixel, 0)), viewToWorld);
    float3 centerNormal = centerNR.xyz;
    float centerRoughness = centerNR.w;
    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

    float angle = MLBayer4x4(uint2(pixelPos), rc.gFrameIndex) * MLPi(2.0);
    float4 rotator = MLCombineRotators(rc.gRotatorPre, MLGetRotator(angle));

    float frustumSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, min(float(rc.gRectSize.x), float(rc.gRectSize.y)), centerViewZ);
    float planeDistanceCenterZ = rc.gOrthoMode == 0 ? centerViewZ : 1.0;

    // ---------------- Diffuse pre-blur ----------------
    int2 diffPos = (rc.gCheckerboard != 2u) ? int2(pixelPos.x >> 1, pixelPos.y) : pixelPos;
    float4 diffSum = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(diffPos, 0));
    if (rc.gCheckerboard != 2u && cbCheckerboard != rc.gCheckerboard)
    {
        float2 wc = cbWeights * (1.0 / max(cbWeights.x + cbWeights.y, 1e-5));
        float4 d0 = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(cbPosL, 0));
        float4 d1 = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(cbPosR, 0));
        d0 = wc.x > 0.0 ? d0 : float4(0.0);
        d1 = wc.y > 0.0 ? d1 : float4(0.0);
        diffSum = d0 * wc.x + d1 * wc.y;
    }
    if (rc.gDiffPrepassBlurRadius > 0.0)
    {
        // NRD pre-blur: areaFactor = hitDistFactor; blurRadius = gDiffPrepassBlurRadius * sqrt(areaFactor).
        // Signal .w is normalized; de-normalize before world-domain radius math.
        float hitDist = ReblurDenormHitDist(diffSum.a == 0.0 ? 1.0 : diffSum.a, centerViewZ, rc.gHitDistParams, 1.0);
        float blurRadius = rc.gDiffPrepassBlurRadius * sqrt(saturate(RelaxGetHitDistFactor(hitDist, frustumSize)));
        if (diffSum.a == 0.0) { blurRadius = max(blurRadius, 1.0); }

        float normalWeightParam = RelaxGetNormalWeightParam2(1.0, 0.25 * rc.gLobeAngleFraction);
        float2 hitDistanceWeightParams = ReblurGetHitDistanceWeightParams(diffSum.a, 1.0 / 9.0, 1.0);
        float weightSum = 1.0;

        for (int i = 0; i < 8; i++) {
            float3 offset = REBLUR_POISSON8[i];
            float2 samplePosF = floor(float2(pixelPos) + 0.5 + MLRotateVector(rotator, offset.xy) * blurRadius) + 0.5;
            float2 uv = samplePosF * rc.gRectSizeInv;
            if (RelaxIsInScreenNearest(uv) == 0.0) { continue; }

            int2 samplePos = int2(samplePosF);
            int2 sampleFullPos = CheckerboardShiftFull(samplePos, rc.gCheckerboard, uint(i), rc.gFrameIndex, int(rc.gRectSize.x));
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, sampleFullPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange)) { continue; }

            float3 sampleNormal = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(sampleFullPos, 0)), viewToWorld).xyz;
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(sampleFullPos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float w = RelaxGetPlaneDistanceWeight(centerWorldPos, centerNormal, planeDistanceCenterZ, sampleWorldPos, rc.gPlaneDistSensitivity);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNormal));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);
            if (w > 1e-4) {
                float4 s = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(CheckerboardSignalPos(samplePos, rc.gCheckerboard, uint(i), rc.gFrameIndex, int(rc.gRectSize.x)), 0));
                w *= rc.gMinHitDistanceWeight * REBLUR_PRE_PASS_FRACTION_SCALE + RelaxComputeExponentialWeight(s.a, hitDistanceWeightParams.x, hitDistanceWeightParams.y);
                w *= RelaxGetGaussianWeight(offset.z);
                diffSum += s * w;
                weightSum += w;
            }
        }
        diffSum /= weightSum;
    }
    rdgStorageFloat4[pc.diffOutIndex][pixelPos] = diffSum;

    // ---------------- Specular pre-blur ----------------
    int2 specPos = (rc.gCheckerboard != 2u) ? int2(pixelPos.x >> 1, pixelPos.y) : pixelPos;
    float4 specSum = rdgTexturesFloat4[pc.specInputIndex].Load(int3(specPos, 0));
    if (rc.gCheckerboard != 2u && cbCheckerboard != rc.gCheckerboard)
    {
        float2 wc = cbWeights * (1.0 / max(cbWeights.x + cbWeights.y, 1e-5));
        float4 s0 = rdgTexturesFloat4[pc.specInputIndex].Load(int3(cbPosL, 0));
        float4 s1 = rdgTexturesFloat4[pc.specInputIndex].Load(int3(cbPosR, 0));
        s0 = wc.x > 0.0 ? s0 : float4(0.0);
        s1 = wc.y > 0.0 ? s1 : float4(0.0);
        specSum = s0 * wc.x + s1 * wc.y;
    }
    specSum.a = saturate(specSum.a);
    if (rc.gSpecPrepassBlurRadius > 0.0)
    {
        float3 viewVector = rc.gOrthoMode == 0 ? normalize(-centerWorldPos) : rc.gFrustumForward.xyz;
        float4 D = MLGetSpecularDominantDirection(centerNormal, viewVector, centerRoughness);
        float NoD = abs(dot(centerNormal, D.xyz));
        float NoV = abs(dot(centerNormal, viewVector));

        // NRD spec pre-blur: areaFactor = roughness * hitDistFactor; blurRadius = gSpecPrepassBlurRadius * sqrt(areaFactor).
        // hitDistFactor uses world hitDist WITHOUT *NoD (NRD note: *NoD worsens glancing angles); roughness replaces specMagicCurve.
        float smc = RelaxGetSpecMagicCurve(centerRoughness);
        float hitDist = ReblurDenormHitDist(specSum.a == 0.0 ? 1.0 : specSum.a, centerViewZ, rc.gHitDistParams, centerRoughness);
        float hitDistFactor = RelaxGetHitDistFactor(hitDist, frustumSize);
        float blurRadius = rc.gSpecPrepassBlurRadius * sqrt(saturate(centerRoughness * hitDistFactor));

        float lobeRadius = hitDist * NoD * MLGetSpecularLobeTanHalfAngle(centerRoughness);
        float minBlurRadius = lobeRadius / RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, centerViewZ + hitDist * D.w);
        blurRadius = min(blurRadius, minBlurRadius);
        if (specSum.a == 0.0) { blurRadius = max(blurRadius, 1.0); }

        float normalWeightParam = RelaxGetNormalWeightParam2(centerRoughness, 0.5 * rc.gLobeAngleFraction);
        float2 hitDistanceWeightParams = ReblurGetHitDistanceWeightParams(specSum.a, 1.0 / 9.0, centerRoughness);
        float2 roughnessWeightParams = RelaxGetRoughnessWeightParams(centerRoughness, rc.gRoughnessFraction);
        float specMinHitDistanceWeight = rc.gMinHitDistanceWeight * REBLUR_PRE_PASS_FRACTION_SCALE * smc;

        // Miss center must init to NRD_INF so an all-miss result stores 0, not a bogus value.
        float minHitT = specSum.a == 0.0 ? NRD_INF : specSum.a;
        float weightSum = 1.0;
        uint rngState = MLHashCombine(MLSequenceHash(uint(pixelPos.x) + uint(pixelPos.y) * 65536u), rc.gFrameIndex);

        for (int i = 0; i < 8; i++) {
            float3 offset = REBLUR_POISSON8[i];
            float2 samplePosF = floor(float2(pixelPos) + 0.5 + MLRotateVector(rotator, offset.xy) * blurRadius) + 0.5;
            float2 uv = samplePosF * rc.gRectSizeInv;
            if (RelaxIsInScreenNearest(uv) == 0.0) { continue; }

            int2 samplePos = int2(samplePosF);
            int2 sampleFullPos = CheckerboardShiftFull(samplePos, rc.gCheckerboard, uint(i), rc.gFrameIndex, int(rc.gRectSize.x));
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, sampleFullPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange)) { continue; }

            float4 sampleNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(sampleFullPos, 0)), viewToWorld);
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(sampleFullPos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float w = RelaxComputeWeight(sampleNR.w, roughnessWeightParams.x, roughnessWeightParams.y);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNR.xyz));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);
            w *= RelaxGetPlaneDistanceWeight(centerWorldPos, centerNormal, planeDistanceCenterZ, sampleWorldPos, rc.gPlaneDistSensitivity);

            float4 s = rdgTexturesFloat4[pc.specInputIndex].Load(int3(CheckerboardSignalPos(samplePos, rc.gCheckerboard, uint(i), rc.gFrameIndex, int(rc.gRectSize.x)), 0));

            rngState = MLSequenceHash(rngState);
            if (float(rngState >> 8) * (1.0 / 16777216.0) < w * NoV) {
                minHitT = min(minHitT, s.a == 0.0 ? NRD_INF : s.a);
            }

            // Miss center uses uniform weight so it doesn't reject bright neighbors used for hitDist tracking.
            w *= specSum.a == 0.0 ? 1.0 : (specMinHitDistanceWeight + RelaxComputeExponentialWeight(s.a, hitDistanceWeightParams.x, hitDistanceWeightParams.y));
            w *= RelaxGetGaussianWeight(offset.z);

            float d = length(sampleWorldPos - centerWorldPos);
            float sWorld = ReblurDenormHitDist(s.a, sampleViewZ, rc.gHitDistParams, sampleNR.w);
            float t = sWorld / (hitDist + d);
            w *= lerp(saturate(t), 1.0, MLLinearStep(0.5, 1.0, centerRoughness));

            specSum.rgb += s.rgb * w;
            weightSum += w;
        }
        specSum.rgb /= weightSum;
        specSum.a = minHitT >= NRD_INF ? 0.0 : minHitT;
    }
    rdgStorageFloat4[pc.specOutIndex][pixelPos] = specSum;
}
