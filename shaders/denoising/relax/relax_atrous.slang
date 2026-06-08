/*
 * RELAX: A-Trous wavelet filter. 3x3 with configurable step size.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import relax_interop;
import relax_utils;

[shader("compute")]
[numthreads(16, 16, 1)]
void RelaxAtrousMain(uniform RelaxAtrousPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;
    float3x3 viewToWorld = (float3x3)rc.gViewToWorld;
    int stepSize = int(pc.stepSize);

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y)
        return;

    int2 fullPixel = pixelPos * pc.pixelScale;
    float centerViewZ = RelaxLoadViewZ(pc.viewZIndex, fullPixel, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange))
        return;

    float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;

    float4 centerNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(fullPixel, 0)), viewToWorld);
    float3 centerNormal = centerNR.rgb;
    float centerRoughness = centerNR.a;
    float historyLength = 255.0 * rdgTexturesFloat[pc.historyLengthIndex].Load(int3(pixelPos, 0)).r;

    // Normal weight params
    float diffuseLobeAngleFraction = rc.gLobeAngleFraction / sqrt(float(stepSize));
    diffuseLobeAngleFraction = lerp(0.99, diffuseLobeAngleFraction, saturate(historyLength / 5.0));

    float specularReprojectionConfidence = rdgTexturesFloat[pc.specReprojConfidenceIndex].Load(int3(pixelPos, 0)).r;

    float specularLuminanceWeightRelaxation = 1.0;
    if (stepSize <= 4)
        specularLuminanceWeightRelaxation = lerp(1.0, specularReprojectionConfidence, rc.gLuminanceEdgeStoppingRelaxation);

    float specularLobeAngleFraction = rc.gLobeAngleFraction;
    float diffuseLobeAngleFractionForSpec = diffuseLobeAngleFraction;

    float2 roughnessWeightParams = RelaxGetRoughnessWeightParams(centerRoughness, rc.gRoughnessFraction);
    float specNormalWeightParamSimplified = RelaxGetNormalWeightParam2(1.0, diffuseLobeAngleFractionForSpec);
    float2 specNormalWeightParams = RelaxGetNormalWeightParamsATrous(
        centerRoughness, historyLength, specularReprojectionConfidence,
        rc.gNormalEdgeStoppingRelaxation, specularLobeAngleFraction, rc.gSpecLobeAngleSlack);

    float diffuseNormalWeightParam = RelaxGetNormalWeightParam2(1.0, diffuseLobeAngleFraction);

    float4 centerSpec = rdgTexturesFloat4[pc.specIndex].Load(int3(pixelPos, 0));
    float4 centerDiff = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixelPos, 0));
    float centerSpecLum = MLLuminance(centerSpec.rgb);
    float centerDiffLum = MLLuminance(centerDiff.rgb);

    bool isFirstAtrousPass = (stepSize == 1);
    float centerSpecVar = isFirstAtrousPass ? max(0.0, centerSpec.a - centerSpecLum * centerSpecLum) : centerSpec.a;
    float centerDiffVar = isFirstAtrousPass ? max(0.0, centerDiff.a - centerDiffLum * centerDiffLum) : centerDiff.a;
    float specPhiLumInv = 1.0 / max(1e-4, rc.gSpecPhiLuminance * sqrt(centerSpecVar));
    float diffPhiLumInv = 1.0 / max(1e-4, rc.gDiffPhiLuminance * sqrt(centerDiffVar));

    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    float3 centerV = -normalize(centerWorldPos);
    float depthThreshold = rc.gDepthThreshold * (rc.gOrthoMode == 0 ? centerViewZ : 1.0);

    static const float kernelWeights[2] = { 0.44198, 0.27901 };

    // Random offset for large steps to reduce ringing
    int2 offset = 0;
    if (stepSize > 4)
    {
        float2 rnd = RngHashGetFloat2(uint2(pixelPos), rc.gFrameIndex);
        offset = int2(float(stepSize) * 0.5 * (rnd - 0.5));
    }

    float sumWSpec = kernelWeights[0] * kernelWeights[0];
    float sumWDiff = sumWSpec;
    float4 sumSpec = float4(centerSpec.rgb, centerSpecVar) * float4(sumWSpec.xxx, sumWSpec * sumWSpec);
    float4 sumDiff = float4(centerDiff.rgb, centerDiffVar) * float4(sumWDiff.xxx, sumWDiff * sumWDiff);

    [unroll]
    for (int j = -1; j <= 1; j++)
    {
        [unroll]
        for (int i = -1; i <= 1; i++)
        {
            if (i == 0 && j == 0) continue;

            int2 p = pixelPos + offset + int2(i, j) * stepSize;
            bool isInside = all(p >= int2(0, 0)) && all(p < rc.gRectSize);
            float kernel = kernelWeights[abs(i)] * kernelWeights[abs(j)];

            int2 pFull = p * pc.pixelScale;
            float4 sampleNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pFull, 0)), viewToWorld);
            float3 sampleNormal = sampleNR.rgb;
            float sampleRoughness = sampleNR.a;
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, pFull, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(p, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float geometryW = RelaxGetPlaneDistanceWeightAtrous(centerWorldPos, centerNormal, sampleWorldPos, depthThreshold);
            geometryW *= kernel;
            geometryW *= float(isInside && RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange));

            // Specular weight
            float3 sampleV = -normalize(sampleWorldPos + rc.gRoughnessEdgeStoppingRelaxation * centerWorldPos);
            float anglesSpec = MLAcosApproxPositive(dot(centerNormal, sampleNormal));
            float normalWSpecSimplified = RelaxComputeWeight(anglesSpec, specNormalWeightParamSimplified, 0.0);
            float normalWSpec = RelaxGetSpecularNormalWeightATrous(specNormalWeightParams, centerNormal, sampleNormal, centerV, sampleV);
            float roughnessWSpec = RelaxComputeWeight(sampleRoughness, roughnessWeightParams.x, roughnessWeightParams.y);

            float wSpec = geometryW * (rc.gRoughnessEdgeStoppingEnabled != 0 ? normalWSpec * roughnessWSpec : normalWSpecSimplified);
            if (wSpec > 1e-4)
            {
                float4 sampleSpec = rdgTexturesFloat4[pc.specIndex].Load(int3(p, 0));
                float sampleSpecLum = MLLuminance(sampleSpec.rgb);
                float sampleSpecVar = isFirstAtrousPass ? max(0.0, sampleSpec.a - sampleSpecLum * sampleSpecLum) : sampleSpec.a;
                float specLumW = min(rc.gSpecMaxLuminanceRelativeDifference, abs(centerSpecLum - sampleSpecLum) * specPhiLumInv) * specularLuminanceWeightRelaxation;
                wSpec *= exp(-specLumW);
                sumWSpec += wSpec;
                sumSpec += float4(wSpec.xxx, wSpec * wSpec) * float4(sampleSpec.rgb, sampleSpecVar);
            }

            // Diffuse weight
            float normalWDiff = RelaxComputeWeight(MLAcosApproxPositive(dot(centerNormal, sampleNormal)), diffuseNormalWeightParam, 0.0);
            float wDiff = geometryW * normalWDiff;
            if (wDiff > 1e-4)
            {
                float4 sampleDiff = rdgTexturesFloat4[pc.diffIndex].Load(int3(p, 0));
                float sampleDiffLum = MLLuminance(sampleDiff.rgb);
                float sampleDiffVar = isFirstAtrousPass ? max(0.0, sampleDiff.a - sampleDiffLum * sampleDiffLum) : sampleDiff.a;
                float diffLumW = min(rc.gDiffMaxLuminanceRelativeDifference, abs(centerDiffLum - sampleDiffLum) * diffPhiLumInv);
                wDiff *= exp(-diffLumW);
                sumWDiff += wDiff;
                sumDiff += float4(wDiff.xxx, wDiff * wDiff) * float4(sampleDiff.rgb, sampleDiffVar);
            }
        }
    }

    float4 outSpec = sumSpec / float4(sumWSpec.xxx, sumWSpec * sumWSpec);
    float4 outDiff = sumDiff / float4(sumWDiff.xxx, sumWDiff * sumWDiff);

    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = outSpec;
    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = outDiff;
}
