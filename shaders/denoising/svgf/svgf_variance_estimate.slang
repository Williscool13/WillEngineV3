import common_interop;
import push_constant_interop;
import shader_functions;
import bindless_declarations;

[shader("compute")]
[numthreads(8, 8, 1)]
void SVGFVarianceEstimateMain(uniform SVGFVarianceEstimatePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    const uint2 pixel = dtid.xy;
    if (pixel.x >= pc.width || pixel.y >= pc.height) {
        return;
    }

    const float historyLength = rdgTexturesFloat[pc.historyLengthIndex].Load(int3(pixel, 0)).r;

    // Adequate History
    if (historyLength >= 4.0) {
        const float temporalVariance = rdgTexturesFloat[pc.inputVarianceIndex].Load(int3(pixel, 0)).r;
        rdgStorageFloat[pc.outputVarianceIndex][pixel] = temporalVariance;
        return;
    }

    const float centerDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (centerDepth == 0.0) {
        rdgStorageFloat[pc.outputVarianceIndex][pixel] = 0.0;
        return;
    }

    const uint4 centerG1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    const float3 centerNormal = UnpackNormalOct16(centerG1.r);

    // Accumulate moments over 7x7 window with bilateral weighting
    float sumM1 = 0.0;
    float sumM2 = 0.0;
    float sumW = 0.0;

    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            const int2 samplePixel = clamp(
                int2(pixel) + int2(dx, dy),
                int2(0, 0), int2(int(pc.width) - 1, int(pc.height) - 1)
            );
            const float sampleDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(samplePixel, 0)).r;
            if (sampleDepth == 0.0) { continue; }

            const uint4 sampleG1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(samplePixel, 0));
            const float3 sampleNormal = UnpackNormalOct16(sampleG1.r);

            const float wNormal = pow(saturate(dot(centerNormal, sampleNormal)), 32.0);
            const float wDepth = exp(-abs(sampleDepth - centerDepth) / (0.01 * centerDepth + 1e-6));
            const float w = wNormal * wDepth;

            const float2 sampleMoments = rdgTexturesFloat2[pc.momentsIndex].Load(int3(samplePixel, 0));
            sumM1 += sampleMoments.x * w;
            sumM2 += sampleMoments.y * w;
            sumW += w;
        }
    }

    float variance = 0.0;
    if (sumW > 1e-6) {
        const float m1 = sumM1 / sumW;
        const float m2 = sumM2 / sumW;
        variance = max(0.0, m2 - m1 * m1);
    }

    rdgStorageFloat[pc.outputVarianceIndex][pixel] = variance;
}
