/*
 * ReBLUR: diffuse-only chroma widening. Runs after temporal stabilization on the final linear-RGB diffuse,
 * filtering chromaticity (CoCg/Y) with geometric weights only; center luma is preserved exactly.
 * Mirrors RelaxAtrousChromaMain with ReBLUR-native guides (packed normal/roughness, linearized depth, DATA1 accum frames).
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import denoiser_common;
import reblur_common;

[shader("compute")]
[numthreads(16, 16, 1)]
void ReblurChromaMain(uniform ReblurChromaPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;
    int stepSize = int(pc.stepSize);

    // No write on skip: the last iteration writes the ReSTIR resolve target, whose skipped texels must keep the undenoised value.
    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y) {
        return;
    }

    float centerViewZ = RelaxLoadViewZ(pc.viewZIndex, pixelPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange)) {
        return;
    }

    float3x3 viewToWorld = (float3x3)rc.gViewToWorld;
    float3 centerNormal = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0)), viewToWorld).xyz;

    float historyLength = ReblurUnpackData1(rdgTexturesFloat2[pc.data1Index].Load(int3(pixelPos, 0))).x;
    float diffuseLobeAngleFraction = rc.gLobeAngleFraction / sqrt(float(stepSize));
    diffuseLobeAngleFraction = lerp(0.99, diffuseLobeAngleFraction, saturate(historyLength / 5.0));
    float diffuseNormalWeightParam = RelaxGetNormalWeightParam2(1.0, diffuseLobeAngleFraction);

    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);
    float depthThreshold = rc.gPlaneDistSensitivity * (rc.gOrthoMode == 0.0 ? centerViewZ : 1.0);

    static const float kernelWeights[2] = { 0.44198, 0.27901 };

    int2 offset = 0;
    if (stepSize > 4) {
        float2 rnd = RngHashGetFloat2(uint2(pixelPos), rc.gFrameIndex);
        offset = int2(float(stepSize) * 0.5 * (rnd - 0.5));
    }

    float4 centerDiff = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixelPos, 0));
    float centerY = dot(centerDiff.rgb, float3(0.25, 0.5, 0.25));
    float2 centerCoCg = float2(0.5 * (centerDiff.r - centerDiff.b), 0.25 * (2.0 * centerDiff.g - centerDiff.r - centerDiff.b));

    // Filter chromaticity (CoCg/Y), not absolute CoCg: chroma magnitude scales with intensity, so absolute filtering across a steep luminance gradient dumps the bright side's chroma onto dim pixels.
    float sumW = kernelWeights[0] * kernelWeights[0];
    float2 sumCoCg = centerCoCg / max(centerY, 1e-6) * sumW;

    [unroll]
    for (int j = -1; j <= 1; j++) {
        [unroll]
        for (int i = -1; i <= 1; i++) {
            if (i == 0 && j == 0) { continue; }

            int2 p = pixelPos + offset + int2(i, j) * stepSize;
            bool isInside = all(p >= int2(0, 0)) && all(p < rc.gRectSize);
            float kernel = kernelWeights[abs(i)] * kernelWeights[abs(j)];

            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, p, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            float3 sampleNormal = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(p, 0)), viewToWorld).xyz;
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(p, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float geometryW = RelaxGetPlaneDistanceWeightAtrous(centerWorldPos, centerNormal, sampleWorldPos, depthThreshold);
            geometryW *= kernel;
            geometryW *= float(isInside && RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange));

            float normalWDiff = RelaxComputeWeight(MLAcosApproxPositive(dot(centerNormal, sampleNormal)), diffuseNormalWeightParam, 0.0);
            float wDiff = geometryW * normalWDiff;
            if (wDiff > 1e-4) {
                float3 sampleRgb = rdgTexturesFloat4[pc.diffIndex].Load(int3(p, 0)).rgb;
                float sampleY = dot(sampleRgb, float3(0.25, 0.5, 0.25));
                // Floor the ratio denominator against the center luma so a near-black tap cannot contribute an unbounded chromaticity.
                float denom = max(sampleY, max(0.05 * centerY, 1e-6));
                sumCoCg += float2(0.5 * (sampleRgb.r - sampleRgb.b), 0.25 * (2.0 * sampleRgb.g - sampleRgb.r - sampleRgb.b)) / denom * wDiff;
                sumW += wDiff;
            }
        }
    }

    float2 coCg = sumCoCg / sumW * centerY;
    float3 delta = float3(coCg.x - coCg.y, coCg.y, -coCg.x - coCg.y);
    // Scale chroma back rather than per-channel clamping, so widened chroma can neither push RGB negative nor hue-shift via clipping.
    float minDelta = min(delta.r, min(delta.g, delta.b));
    float chromaScale = minDelta < 0.0 ? saturate(centerY / max(-minDelta, 1e-6)) : 1.0;
    rdgStorageFloat4[pc.outDiffIndex][pixelPos] = float4(centerY + chromaScale * delta, centerDiff.a);
}
