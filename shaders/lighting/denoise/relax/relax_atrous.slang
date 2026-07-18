/*
 * RELAX: A-Trous wavelet filter. 3x3 with configurable step size.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import relax_interop;
import denoiser_common;
import restir_common;

[shader("compute")]
[numthreads(16, 16, 1)]
void RelaxAtrousMain(uniform RelaxAtrousPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;
    int stepSize = int(pc.stepSize);

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

    float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;
    float historyLength = 255.0 * rdgTexturesFloat[pc.historyLengthIndex].Load(int3(pixelPos, 0)).r;

    bool isFirstAtrousPass = (stepSize == 1);

    // First iteration with short history: 5x5 spatial variance estimation instead of filtering (NRD AtrousSmem). Inputs carry 2nd moments in .a, output carries variance.
    if (isFirstAtrousPass && historyLength < rc.gHistoryThreshold)
    {
        float normalWeightParam = RelaxGetNormalWeightParam2(1.0, rc.gLobeAngleFraction);
        float sumW = 0.0;
        float3 sumSpecRgb = float3(0.0);
        float specM1 = 0.0;
        float specM2 = 0.0;
        float3 sumDiffRgb = float3(0.0);
        float diffM1 = 0.0;
        float diffM2 = 0.0;

        [unroll]
        for (int cy = -2; cy <= 2; cy++)
        {
            [unroll]
            for (int cx = -2; cx <= 2; cx++)
            {
                int2 p = clamp(pixelPos + int2(cx, cy), int2(0, 0), rc.gRectSize - int2(1, 1));
                int2 tapFullPixel = p;
                float3 sampleNormal;
                float sampleTapRoughness;
                float sampleViewZ;
                RelaxLoadGuide(pc.guideIndex, tapFullPixel, sampleNormal, sampleTapRoughness, sampleViewZ);
                float depthW = RelaxGetBilateralWeight(abs(sampleViewZ), abs(centerViewZ));
                float normalW = RelaxComputeWeight(MLAcosApproxPositive(dot(centerNormal, sampleNormal)), normalWeightParam, 0.0);
                float w = depthW * normalW;

                float4 sampleSpec = rdgTexturesFloat4[pc.specIndex].Load(int3(p, 0));
                float4 sampleDiff = rdgTexturesFloat4[pc.diffIndex].Load(int3(p, 0));
                sumW += w;
                sumSpecRgb += sampleSpec.rgb * w;
                specM1 += MLLuminance(sampleSpec.rgb) * w;
                specM2 += sampleSpec.a * w;
                sumDiffRgb += sampleDiff.rgb * w;
                diffM1 += MLLuminance(sampleDiff.rgb) * w;
                diffM2 += sampleDiff.a * w;
            }
        }

        float boost = max(1.0, 4.0 / (historyLength + 1.0));
        sumW = max(sumW, 1e-6);
        sumSpecRgb /= sumW;
        specM1 /= sumW;
        specM2 /= sumW;
        sumDiffRgb /= sumW;
        diffM1 /= sumW;
        diffM2 /= sumW;

        rdgStorageFloat4[pc.outSpecIndex][pixelPos] = float4(sumSpecRgb, max(0.0, specM2 - specM1 * specM1) * boost);
        rdgStorageFloat4[pc.outDiffIndex][pixelPos] = float4(sumDiffRgb, max(0.0, diffM2 - diffM1 * diffM1) * boost);
        return;
    }

    // Normal weight params. The first iteration uses the plain lobe angle fraction (NRD AtrousSmem); later iterations relax it by history length.
    float diffuseLobeAngleFraction = rc.gLobeAngleFraction / sqrt(float(stepSize));
    if (!isFirstAtrousPass)
    {
        diffuseLobeAngleFraction = lerp(0.99, diffuseLobeAngleFraction, saturate(historyLength / 5.0));
    }

    float specularReprojectionConfidence = rdgTexturesFloat[pc.specReprojConfidenceIndex].Load(int3(pixelPos, 0)).r;

    float specularLuminanceWeightRelaxation = 1.0;
    if (stepSize <= 4)
    {
        specularLuminanceWeightRelaxation = lerp(1.0, specularReprojectionConfidence, rc.gLuminanceEdgeStoppingRelaxation);
    }

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

    float centerSpecVar = centerSpec.a;
    float centerDiffVar = centerDiff.a;
    if (isFirstAtrousPass)
    {
        // First iteration consumes 2nd moments; the variance driving the luminance phi is 3x3 Gaussian-prefiltered (NRD AtrousSmem computeVariance)
        static const float gaussKernel[2][2] = { { 1.0 / 4.0, 1.0 / 8.0 }, { 1.0 / 8.0, 1.0 / 16.0 } };
        float4 specMoments = float4(0.0);
        float4 diffMoments = float4(0.0);
        [unroll]
        for (int dy = -1; dy <= 1; dy++)
        {
            [unroll]
            for (int dx = -1; dx <= 1; dx++)
            {
                int2 p = clamp(pixelPos + int2(dx, dy), int2(0, 0), rc.gRectSize - int2(1, 1));
                float k = gaussKernel[abs(dx)][abs(dy)];
                specMoments += rdgTexturesFloat4[pc.specIndex].Load(int3(p, 0)) * k;
                diffMoments += rdgTexturesFloat4[pc.diffIndex].Load(int3(p, 0)) * k;
            }
        }
        float specM1 = MLLuminance(specMoments.rgb);
        centerSpecVar = max(0.0, specMoments.a - specM1 * specM1);
        float diffM1 = MLLuminance(diffMoments.rgb);
        centerDiffVar = max(0.0, diffMoments.a - diffM1 * diffM1);
    }
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

    // First iteration: .a accumulates the raw 2nd moment with weight w and converts to variance at the end (NRD AtrousSmem). Later iterations: .a is variance, weighted by w^2.
    float sumWSpec = kernelWeights[0] * kernelWeights[0];
    float sumWDiff = sumWSpec;
    float4 sumSpec = float4(centerSpec.rgb, isFirstAtrousPass ? centerSpec.a : centerSpecVar) * float4(sumWSpec.xxx, isFirstAtrousPass ? sumWSpec : sumWSpec * sumWSpec);
    float4 sumDiff = float4(centerDiff.rgb, isFirstAtrousPass ? centerDiff.a : centerDiffVar) * float4(sumWDiff.xxx, isFirstAtrousPass ? sumWDiff : sumWDiff * sumWDiff);

    [unroll]
    for (int j = -1; j <= 1; j++)
    {
        [unroll]
        for (int i = -1; i <= 1; i++)
        {
            if (i == 0 && j == 0) { continue; }

            int2 p = pixelPos + offset + int2(i, j) * stepSize;
            bool isInside = all(p >= int2(0, 0)) && all(p < rc.gRectSize);
            float kernel = kernelWeights[abs(i)] * kernelWeights[abs(j)];

            int2 pFull = p;
            float3 sampleNormal;
            float sampleRoughness;
            float sampleViewZ;
            RelaxLoadGuide(pc.guideIndex, pFull, sampleNormal, sampleRoughness, sampleViewZ);
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
                float specLumW = min(rc.gSpecMaxLuminanceRelativeDifference, abs(centerSpecLum - sampleSpecLum) * specPhiLumInv) * specularLuminanceWeightRelaxation;
                wSpec *= exp(-specLumW);
                sumWSpec += wSpec;
                sumSpec += float4(wSpec.xxx, isFirstAtrousPass ? wSpec : wSpec * wSpec) * sampleSpec;
            }

            // Diffuse weight
            float normalWDiff = RelaxComputeWeight(MLAcosApproxPositive(dot(centerNormal, sampleNormal)), diffuseNormalWeightParam, 0.0);
            float wDiff = geometryW * normalWDiff;
            if (wDiff > 1e-4)
            {
                float4 sampleDiff = rdgTexturesFloat4[pc.diffIndex].Load(int3(p, 0));
                float sampleDiffLum = MLLuminance(sampleDiff.rgb);
                float diffLumW = min(rc.gDiffMaxLuminanceRelativeDifference, abs(centerDiffLum - sampleDiffLum) * diffPhiLumInv);
                wDiff *= exp(-diffLumW);
                sumWDiff += wDiff;
                sumDiff += float4(wDiff.xxx, isFirstAtrousPass ? wDiff : wDiff * wDiff) * sampleDiff;
            }
        }
    }

    float4 outSpec = sumSpec / float4(sumWSpec.xxx, isFirstAtrousPass ? sumWSpec : sumWSpec * sumWSpec);
    float4 outDiff = sumDiff / float4(sumWDiff.xxx, isFirstAtrousPass ? sumWDiff : sumWDiff * sumWDiff);

    if (isFirstAtrousPass)
    {
        float specM1 = MLLuminance(outSpec.rgb);
        outSpec.a = max(0.0, outSpec.a - specM1 * specM1);
        float diffM1 = MLLuminance(outDiff.rgb);
        outDiff.a = max(0.0, outDiff.a - diffM1 * diffM1);
    }

    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = outSpec;
    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = outDiff;
}
