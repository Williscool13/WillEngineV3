/*
 * RELAX: PrePass. Spatial blur to prefilter inputs using Poisson-8 samples.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import relax_interop;
import relax_utils;
import restir_common;

// Poisson disk 8 samples (NRD g_Poisson8); z is the sample radius normalized to 1, used for the Gaussian weight
static const float3 POISSON8[8] = {
    float3(-0.4706069, -0.4427112, 0.6461146),
    float3(-0.9057375,  0.3003471, 0.9542373),
    float3(-0.3487388,  0.4037880, 0.5335386),
    float3( 0.1023042,  0.6439373, 0.6520134),
    float3( 0.5699277,  0.3513750, 0.6695386),
    float3( 0.2939128, -0.1131226, 0.3149309),
    float3( 0.7836658, -0.4208784, 0.8895339),
    float3( 0.1564120, -0.8198990, 0.8346850),
};

int2 CheckerboardShiftFull(int2 samplePos, uint cbField, uint counter, uint frameIndex, int rectSizeX)
{
    if (cbField == 2u) { return samplePos; }
    uint sc = ((uint(samplePos.x) ^ uint(samplePos.y)) ^ frameIndex) & 1u;
    if (sc != cbField) { samplePos.x += ((counter & 1u) == 0u) ? -1 : 1; }
    // The shift can step 1px off-screen (NRD clamps the shifted uv to the viewport).
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
void RelaxPrepassMain(uniform RelaxPrepassPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y)
    {
        return;
    }

    int2 fullPixel = pixelPos;
    float3 centerNormal;
    float centerRoughness;
    float centerViewZ;
    RelaxLoadGuide(pc.guideIndex, fullPixel, centerNormal, centerRoughness, centerViewZ);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange))
    {
        return;
    }

    uint cbCheckerboard = ((uint(pixelPos.x) ^ uint(pixelPos.y)) ^ rc.gFrameIndex) & 1u;
    int2 cbPosL = int2(max(pixelPos.x - 1, 0) >> 1, pixelPos.y);
    int2 cbPosR = int2(min(pixelPos.x + 1, rc.gRectSize.x - 1) >> 1, pixelPos.y);
    float2 cbWeights = float2(1.0, 1.0);
    if (rc.gDiffCheckerboard != 2u || rc.gSpecCheckerboard != 2u)
    {
        float zL = RelaxLoadGuideViewZ(pc.guideIndex, int2(max(pixelPos.x - 1, 0), pixelPos.y));
        float zR = RelaxLoadGuideViewZ(pc.guideIndex, int2(min(pixelPos.x + 1, rc.gRectSize.x - 1), pixelPos.y));
        cbWeights = saturate(1.0 - abs(float2(zL, zR) - centerViewZ) / max(abs(centerViewZ) * 0.02, 1e-5));
        cbWeights.x = (!RelaxIsInDenoisingRange(zL, rc.gDenoisingRange) || pixelPos.x < 1) ? 0.0 : cbWeights.x;
        cbWeights.y = (!RelaxIsInDenoisingRange(zR, rc.gDenoisingRange) || pixelPos.x > rc.gRectSize.x - 2) ? 0.0 : cbWeights.y;
    }

    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

    // Kernel rotation - use Bayer4x4 for FRAME mode
    float angle = MLBayer4x4(uint2(pixelPos), rc.gFrameIndex) * MLPi(2.0);
    float4 rotator = MLCombineRotators(rc.gRotatorPre, MLGetRotator(angle));

    float frustumSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, min(float(rc.gRectSize.x), float(rc.gRectSize.y)), centerViewZ);
    float planeDistanceCenterZ = rc.gOrthoMode == 0 ? centerViewZ : 1.0;

    // ------------------------------------------------------------------
    // Diffuse pre-blur
    // ------------------------------------------------------------------
    int2 diffPos = (rc.gDiffCheckerboard != 2u) ? int2(pixelPos.x >> 1, pixelPos.y) : pixelPos;
    float4 diffSum = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(diffPos, 0));
    if (rc.gDiffCheckerboard != 2u && cbCheckerboard != rc.gDiffCheckerboard)
    {
        float2 wc = cbWeights * (1.0 / max(cbWeights.x + cbWeights.y, 1e-5));
        float4 d0 = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(cbPosL, 0));
        float4 d1 = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(cbPosR, 0));
        // Denanify: zero-weight neighbors can be unwritten aliased texels; NaN * 0 still poisons the blend.
        d0 = wc.x > 0.0 ? d0 : float4(0.0);
        d1 = wc.y > 0.0 ? d1 : float4(0.0);
        diffSum = d0 * wc.x + d1 * wc.y;
    }
    if (rc.gDiffBlurRadius > 0.0)
    {
        float hitDist = diffSum.a == 0.0 ? 1.0 : diffSum.a;
        float blurRadius = rc.gDiffBlurRadius * RelaxGetHitDistFactor(hitDist, frustumSize);
        if (diffSum.a == 0.0)
        {
            blurRadius = max(blurRadius, 1.0);
        }

        float normalWeightParam = RelaxGetNormalWeightParam2(1.0, 0.25 * rc.gLobeAngleFraction);
        float2 hitDistanceWeightParams = RelaxGetHitDistanceWeightParams(diffSum.a, 1.0 / 9.0);
        float weightSum = 1.0;

        for (int i = 0; i < 8; i++)
        {
            float3 offset = POISSON8[i];

            // Sample position in pixel units, snapped to the pixel center
            float2 samplePosF = floor(float2(pixelPos) + 0.5 + MLRotateVector(rotator, offset.xy) * blurRadius) + 0.5;
            float2 uv = samplePosF * rc.gRectSizeInv;
            if (RelaxIsInScreenNearest(uv) == 0.0)
            {
                continue;
            }

            int2 samplePos = int2(samplePosF);
            int2 sampleFullPos = CheckerboardShiftFull(samplePos, rc.gDiffCheckerboard, uint(i), rc.gFrameIndex, int(rc.gRectSize.x));
            float3 sampleNormal;
            float sampleRoughness;
            float sampleViewZ;
            RelaxLoadGuide(pc.guideIndex, sampleFullPos, sampleNormal, sampleRoughness, sampleViewZ);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange))
            {
                continue;
            }

            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(sampleFullPos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float w = RelaxGetPlaneDistanceWeight(centerWorldPos, centerNormal, planeDistanceCenterZ, sampleWorldPos, rc.gDepthThreshold);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNormal));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);
            if (w > 1e-4)
            {
                float4 sample = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(CheckerboardSignalPos(samplePos, rc.gDiffCheckerboard, uint(i), rc.gFrameIndex, int(rc.gRectSize.x)), 0));
                w *= lerp(rc.gMinHitDistanceWeight, 1.0, RelaxComputeExponentialWeight(sample.a, hitDistanceWeightParams.x, hitDistanceWeightParams.y));
                w *= RelaxGetGaussianWeight(offset.z);
                diffSum += sample * w;
                weightSum += w;
            }
        }
        diffSum /= weightSum;
    }
    rdgStorageFloat4[pc.diffOutIndex][pixelPos] = diffSum;

    // ------------------------------------------------------------------
    // Specular pre-blur
    // ------------------------------------------------------------------
    int2 specPos = (rc.gSpecCheckerboard != 2u) ? int2(pixelPos.x >> 1, pixelPos.y) : pixelPos;
    float4 specSum = rdgTexturesFloat4[pc.specInputIndex].Load(int3(specPos, 0));
    if (rc.gSpecCheckerboard != 2u && cbCheckerboard != rc.gSpecCheckerboard)
    {
        float2 wc = cbWeights * (1.0 / max(cbWeights.x + cbWeights.y, 1e-5));
        float4 s0 = rdgTexturesFloat4[pc.specInputIndex].Load(int3(cbPosL, 0));
        float4 s1 = rdgTexturesFloat4[pc.specInputIndex].Load(int3(cbPosR, 0));
        s0 = wc.x > 0.0 ? s0 : float4(0.0);
        s1 = wc.y > 0.0 ? s1 : float4(0.0);
        specSum = s0 * wc.x + s1 * wc.y;
    }
    specSum.a = clamp(specSum.a, 0.0, rc.gDenoisingRange);
    if (rc.gSpecBlurRadius > 0.0)
    {
        float3 viewVector = rc.gOrthoMode == 0 ? normalize(-centerWorldPos) : rc.gFrustumForward.xyz;
        float4 D = MLGetSpecularDominantDirection(centerNormal, viewVector, centerRoughness);
        float NoD = abs(dot(centerNormal, D.xyz));
        float NoV = abs(dot(centerNormal, viewVector));

        float hitDist = specSum.a == 0.0 ? 1.0 : specSum.a;
        float smc = RelaxGetSpecMagicCurve(centerRoughness);
        float blurRadius = rc.gSpecBlurRadius * RelaxGetHitDistFactor(hitDist * NoD, frustumSize) * smc;

        // Blur radius is clamped to the specular lobe footprint at the hit distance, projected back to pixels
        float lobeRadius = hitDist * NoD * MLGetSpecularLobeTanHalfAngle(centerRoughness);
        float minBlurRadius = lobeRadius / RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, centerViewZ + hitDist * D.w);
        blurRadius = min(blurRadius, minBlurRadius);
        if (specSum.a == 0.0)
        {
            blurRadius = max(blurRadius, 1.0);
        }

        float normalWeightParam = RelaxGetNormalWeightParam2(centerRoughness, 0.5 * rc.gLobeAngleFraction);
        float2 hitDistanceWeightParams = RelaxGetHitDistanceWeightParams(specSum.a, 1.0 / 9.0);
        float2 roughnessWeightParams = RelaxGetRoughnessWeightParams(centerRoughness, rc.gRoughnessFraction);
        float specMinHitDistanceWeight = specSum.a == 0.0 ? 1.0 : rc.gMinHitDistanceWeight * smc;

        // Spec hitT is not averaged; a stochastic min is taken instead (NRD), since blurred hitT degrades virtual-motion tracking
        float minHitT = specSum.a == 0.0 ? rc.gDenoisingRange : specSum.a;
        float weightSum = 1.0;
        uint rngState = MLHashCombine(MLSequenceHash(uint(pixelPos.x) + uint(pixelPos.y) * 65536u), rc.gFrameIndex);

        for (int i = 0; i < 8; i++)
        {
            float3 offset = POISSON8[i];

            float2 samplePosF = floor(float2(pixelPos) + 0.5 + MLRotateVector(rotator, offset.xy) * blurRadius) + 0.5;
            float2 uv = samplePosF * rc.gRectSizeInv;
            if (RelaxIsInScreenNearest(uv) == 0.0)
            {
                continue;
            }

            int2 samplePos = int2(samplePosF);
            int2 sampleFullPos = CheckerboardShiftFull(samplePos, rc.gSpecCheckerboard, uint(i), rc.gFrameIndex, int(rc.gRectSize.x));
            float3 sampleNormal;
            float sampleRoughness;
            float sampleViewZ;
            RelaxLoadGuide(pc.guideIndex, sampleFullPos, sampleNormal, sampleRoughness, sampleViewZ);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange))
            {
                continue;
            }

            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(sampleFullPos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float w = RelaxComputeWeight(sampleRoughness, roughnessWeightParams.x, roughnessWeightParams.y);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNormal));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);
            w *= RelaxGetPlaneDistanceWeight(centerWorldPos, centerNormal, planeDistanceCenterZ, sampleWorldPos, rc.gDepthThreshold);

            float4 sample = rdgTexturesFloat4[pc.specInputIndex].Load(int3(CheckerboardSignalPos(samplePos, rc.gSpecCheckerboard, uint(i), rc.gFrameIndex, int(rc.gRectSize.x)), 0));

            rngState = MLSequenceHash(rngState);
            if (float(rngState >> 8) * (1.0 / 16777216.0) < w * NoV)
            {
                minHitT = min(minHitT, sample.a == 0.0 ? NRD_INF : sample.a);
            }

            w *= lerp(specMinHitDistanceWeight, 1.0, RelaxComputeExponentialWeight(sample.a, hitDistanceWeightParams.x, hitDistanceWeightParams.y));
            w *= RelaxGetGaussianWeight(offset.z);

            // Decreasing weight for samples that most likely are very close to reflection contact, which should not be pre-blurred
            float d = length(sampleWorldPos - centerWorldPos);
            float t = sample.a / (specSum.a + d);
            w *= lerp(saturate(t), 1.0, MLLinearStep(0.5, 1.0, centerRoughness));

            specSum.rgb += sample.rgb * w;
            weightSum += w;
        }
        specSum.rgb /= weightSum;
        specSum.a = minHitT >= NRD_INF ? 0.0 : minHitT;
    }
    rdgStorageFloat4[pc.specOutIndex][pixelPos] = specSum;
}
