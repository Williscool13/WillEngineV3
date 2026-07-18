/*
 * RELAX: HistoryFix pass. Sparse cross-bilateral filter to fill disocclusions.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import relax_interop;
import denoiser_common;
import restir_common;

float GetDiffuseNormalWeight(float3 centerNormal, float3 pointNormal, float power)
{
    return pow(max(0.01, dot(centerNormal, pointNormal)), max(power, 0.01));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void RelaxHistoryFixMain(uniform RelaxHistoryFixPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    RelaxDiffuseSpecularConstants* rc = pc.constants;

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y)
        return;

    // NRD topology: the outputs are the RESPONSIVE (fast) textures, written in place ONLY at short-history
    // pixels; elsewhere they keep TA's responsive value (early-out without writing, like NRD RELAX_HistoryFix).
    // History clamping then promotes the fixed responsive value into the slow output at short-history pixels.
    float historyLength = 255.0 * rdgTexturesFloat[pc.historyLengthIndex].Load(int3(pixelPos, 0)).r;
    if (historyLength > rc.gHistoryFixFrameNum || rc.gHistoryFixFrameNum == 1.0)
    {
        return;
    }

    int2 fullPixel = pixelPos;
    float3 centerNormal;
    float centerRoughness;
    float centerViewZ;
    RelaxLoadGuide(pc.guideIndex, fullPixel, centerNormal, centerRoughness, centerViewZ);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange))
        return;

    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    float3 centerV = -normalize(centerWorldPos);
    float depthThreshold = rc.gDepthThreshold * (rc.gOrthoMode == 0 ? centerViewZ : 1.0);

    float4 specSum = rdgTexturesFloat4[pc.specIndex].Load(int3(pixelPos, 0));
    float4 diffSum = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixelPos, 0));
    float specWSum = 1.0;
    float diffWSum = 1.0;

    float2 specNormalWeightParams = RelaxGetNormalWeightParamsATrous(
        centerRoughness, 5.0, 1.0, 0.0, rc.gLobeAngleFraction, rc.gSpecLobeAngleSlack);

    float baseStride = rc.gHistoryFixBasePixelStride;
    float r = baseStride / (1.0 + historyLength);
    r = round(r);

    [unroll]
    for (int j = -2; j <= 2; j++)
    {
        [unroll]
        for (int i = -2; i <= 2; i++)
        {
            if (i == 0 && j == 0) continue;

            int2 samplePosInt = pixelPos + int2(i, j) * int(r);
            float2 uv = (float2(samplePosInt) + 0.5) * rc.gRectSizeInv;
            uv = RelaxMirrorUv(uv);
            samplePosInt = int2(uv * float2(rc.gRectSize));
            int2 sampleFullPos = samplePosInt;

            float3 sampleNormal;
            float sampleRoughness;
            float sampleViewZ;
            RelaxLoadGuide(pc.guideIndex, sampleFullPos, sampleNormal, sampleRoughness, sampleViewZ);
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(samplePosInt, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float geometryW = RelaxGetPlaneDistanceWeightAtrous(centerWorldPos, centerNormal, sampleWorldPos, depthThreshold);
            geometryW = RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange) ? geometryW : 0.0;

            // Diffuse weight
            float diffW = geometryW * GetDiffuseNormalWeight(centerNormal, sampleNormal, rc.gHistoryFixEdgeStoppingNormalPower);
            if (diffW > 1e-4)
            {
                diffSum += rdgTexturesFloat4[pc.diffIndex].Load(int3(samplePosInt, 0)) * diffW;
                diffWSum += diffW;
            }

            // Specular weight
            float3 sampleV = -normalize(sampleWorldPos + rc.gRoughnessEdgeStoppingRelaxation * centerWorldPos);
            float specW = geometryW * RelaxGetSpecularNormalWeightATrous(specNormalWeightParams, centerNormal, sampleNormal, centerV, sampleV);
            if (specW > 1e-4)
            {
                specSum += rdgTexturesFloat4[pc.specIndex].Load(int3(samplePosInt, 0)) * specW;
                specWSum += specW;
            }
        }
    }

    // Keep the fast .a (spec = hitT for tracking; deliberate improvement over NRD 4.17, which leaks a 2nd moment here).
    float4 fastSpec = rdgStorageFloat4[pc.outSpecIndex][pixelPos];
    float4 fastDiff = rdgStorageFloat4[pc.outDiffIndex][pixelPos];
    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = float4(diffSum.rgb / diffWSum, fastDiff.a);
    rdgStorageFloat4[pc.outSpecIndex][pixelPos] = float4(specSum.rgb / specWSum, fastSpec.a);
}
