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

// Poisson disk 8 samples
static const float3 POISSON8[8] = {
    float3(-1.00, 0.00, 1.0),
    float3( 0.00, 1.00, 1.0),
    float3( 1.00, 0.00, 1.0),
    float3( 0.00,-1.00, 1.0),
    float3(-0.25 * 1.41421, 0.25 * 1.41421, 0.5),
    float3( 0.25 * 1.41421, 0.25 * 1.41421, 0.5),
    float3( 0.25 * 1.41421,-0.25 * 1.41421, 0.5),
    float3(-0.25 * 1.41421,-0.25 * 1.41421, 0.5),
};

[shader("compute")]
[numthreads(16, 16, 1)]
void RelaxPrepassMain(uniform RelaxPrepassPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;
    float3x3 viewToWorld = (float3x3)rc.gViewToWorld;

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y)
        return;

    int2 fullPixel = pixelPos * pc.pixelScale;
    float centerViewZ = RelaxLoadViewZ(pc.viewZIndex, fullPixel, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange))
        return;

    float4 centerNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(fullPixel, 0)), viewToWorld);
    float3 centerNormal = centerNR.xyz;
    float centerRoughness = centerNR.w;
    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;

    // Kernel rotation - use Bayer4x4 for FRAME mode
    float angle = MLBayer4x4(uint2(pixelPos), rc.gFrameIndex) * MLPi(2.0);
    float4 rotator = MLCombineRotators(rc.gRotatorPre, MLGetRotator(angle));

    float4 specSum = rdgTexturesFloat4[pc.specInputIndex].Load(int3(pixelPos, 0));
    float4 diffSum = rdgTexturesFloat4[pc.diffInputIndex].Load(int3(pixelPos, 0));
    float specWSum = 1.0;
    float diffWSum = 1.0;

    // BlurRadius adjusted for roughness
    float specBlurRadius = rc.gSpecBlurRadius * centerRoughness;
    float diffBlurRadius = rc.gDiffBlurRadius;

    float pixelSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, 1.0, centerViewZ);
    float frustumSize = pixelSize * min(float(rc.gRectSize.x), float(rc.gRectSize.y));

    float2 specNormalWeightParams = RelaxGetNormalWeightParamsATrous(
        centerRoughness, 255.0, 1.0, 0.0, rc.gLobeAngleFraction, rc.gSpecLobeAngleSlack);
    float diffNormalWeightParam = RelaxGetNormalWeightParam2(1.0, 0.99);

    for (int i = 0; i < 8; i++)
    {
        float3 offset = POISSON8[i];
        float2 rotatedOffset = MLRotateVector(rotator, offset.xy) * offset.z;

        // Specular sample
        float2 specUv = pixelUv + rotatedOffset * specBlurRadius * rc.gRectSizeInv;
        if (RelaxIsInScreenNearest(specUv) == 1.0)
        {
            int2 specPos = int2(specUv * float2(rc.gRectSize));
            int2 specFullPos = specPos * pc.pixelScale;
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, specFullPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange))
            {
                float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(specPos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
                float3 sampleNormal = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(specFullPos, 0)), viewToWorld).xyz;
                float3 sampleV = -normalize(sampleWorldPos);

                float geomW = RelaxGetPlaneDistanceWeightAtrous(centerWorldPos, centerNormal, sampleWorldPos, 0.005 * frustumSize);
                float normalW = RelaxGetSpecularNormalWeightATrous(specNormalWeightParams, centerNormal, sampleNormal, -normalize(centerWorldPos), sampleV);
                float w = geomW * normalW;
                if (w > 1e-4)
                {
                    float4 sample = rdgTexturesFloat4[pc.specInputIndex].Load(int3(specPos, 0));
                    // Weight hit distance by min proximity
                    float hitDistW = 1.0;
                    if (rc.gMinHitDistanceWeight > 0 && specSum.a > 0 && sample.a > 0)
                        hitDistW = saturate(min(specSum.a, sample.a) / max(specSum.a, sample.a));
                    w *= hitDistW;
                    specSum += sample * w;
                    specWSum += w;
                }
            }
        }

        // Diffuse sample
        float2 diffUv = pixelUv + rotatedOffset * diffBlurRadius * rc.gRectSizeInv;
        if (RelaxIsInScreenNearest(diffUv) == 1.0)
        {
            int2 diffPos = int2(diffUv * float2(rc.gRectSize));
            int2 diffFullPos = diffPos * pc.pixelScale;
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, diffFullPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange))
            {
                float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(diffPos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
                float3 sampleNormal = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(diffFullPos, 0)), viewToWorld).xyz;

                float geomW = RelaxGetPlaneDistanceWeightAtrous(centerWorldPos, centerNormal, sampleWorldPos, 0.005 * frustumSize);
                float normalW = RelaxComputeWeight(MLAcosApproxPositive(dot(centerNormal, sampleNormal)), diffNormalWeightParam, 0.0);
                float w = geomW * normalW;
                if (w > 1e-4)
                {
                    diffSum += rdgTexturesFloat4[pc.diffInputIndex].Load(int3(diffPos, 0)) * w;
                    diffWSum += w;
                }
            }
        }
    }

    rdgStorageFloat4[pc.specOutIndex][pixelPos] = specSum / specWSum;
    rdgStorageFloat4[pc.diffOutIndex][pixelPos] = diffSum / diffWSum;
}
