import common_interop;
import push_constant_interop;
import shader_functions;
import bindless_declarations;
import constants_interop;

static const float2 KERNEL_OFFSETS[25] = {
    float2(-2, -2), float2(-1, -2), float2(0, -2), float2(1, -2), float2(2, -2),
    float2(-2, -1), float2(-1, -1), float2(0, -1), float2(1, -1), float2(2, -1),
    float2(-2,  0), float2(-1,  0), float2(0,  0), float2(1,  0), float2(2,  0),
    float2(-2,  1), float2(-1,  1), float2(0,  1), float2(1,  1), float2(2,  1),
    float2(-2,  2), float2(-1,  2), float2(0,  2), float2(1,  2), float2(2,  2),
};

// B3 spline kernel weights for 5x5 A-Trous
static const float KERNEL_WEIGHTS[25] = {
    1.0/256.0,  4.0/256.0,  6.0/256.0,  4.0/256.0, 1.0/256.0,
    4.0/256.0, 16.0/256.0, 24.0/256.0, 16.0/256.0, 4.0/256.0,
    6.0/256.0, 24.0/256.0, 36.0/256.0, 24.0/256.0, 6.0/256.0,
    4.0/256.0, 16.0/256.0, 24.0/256.0, 16.0/256.0, 4.0/256.0,
    1.0/256.0,  4.0/256.0,  6.0/256.0,  4.0/256.0, 1.0/256.0,
};

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 ReconstructViewPos(SceneData* sd, float2 uv, float hwDepth)
{
    float viewDepth = ScreenToViewSpaceDepth(hwDepth, sd->depthLinearizeMult, sd->depthLinearizeAdd);
    return CheapReconstructViewSpacePosition(sd->ndcToViewMul, sd->ndcToViewAdd, uv, viewDepth);
}

// Canonical SVGF prefilters variance with a small Gaussian before it drives the luminance
// edge-stop, so the edge-stopping function isn't steered by a noisy per-pixel variance estimate.
float FilterVariance3x3(uint varianceIndex, int2 center, int width, int height)
{
    static const float kernel[9] = {
        1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0,
        2.0 / 16.0, 4.0 / 16.0, 2.0 / 16.0,
        1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0,
    };
    float sum = 0.0;
    int k = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            const int2 p = clamp(center + int2(dx, dy), int2(0, 0), int2(width - 1, height - 1));
            sum += rdgTexturesFloat[varianceIndex].Load(int3(p, 0)).r * kernel[k];
            k++;
        }
    }
    return sum;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SVGFAtrousWaveletMain(uniform SVGFAtrousWaveletPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    const uint2 pixel = dtid.xy;
    if (pixel.x >= pc.width || pixel.y >= pc.height) {
        return;
    }

    const float2 texelSize = float2(1.0 / float(pc.width), 1.0 / float(pc.height));
    const float2 centerUV = (float2(pixel) + 0.5) * texelSize;

    const float4 centerColor = rdgTexturesFloat4[pc.inputColorIndex].Load(int3(pixel, 0));
    const uint4  centerGbuf1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    const float  centerDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    const float  centerVariance = rdgTexturesFloat[pc.inputVarianceIndex].Load(int3(pixel, 0)).r;

    if (centerDepth == 0.0) {
        rdgStorageFloat4[pc.outputColorIndex][pixel] = centerColor;
        rdgStorageFloat[pc.outputVarianceIndex][pixel] = centerVariance;
        return;
    }

    const float3 centerNormal  = UnpackNormalOct16(centerGbuf1.r);
    const float3 centerViewPos = ReconstructViewPos(pc.sceneData, centerUV, centerDepth);
    const float  centerLum     = Luminance(centerColor.rgb);

    // Variance-adaptive luminance sigma: wider kernel when variance is high
    const float varEps = 1e-6;
    const float filteredVariance = FilterVariance3x3(pc.inputVarianceIndex, int2(pixel), int(pc.width), int(pc.height));
    const float sigmaLumSq = pc.sigmaLuminance * pc.sigmaLuminance * max(filteredVariance, varEps);

    float4 sumColor   = float4(0.0);
    float  sumVariance = 0.0;
    float  sumWeight  = 0.0;

    for (int i = 0; i < 25; i++) {
        const int2 samplePixel = clamp(
            int2(pixel) + int2(KERNEL_OFFSETS[i]) * int(pc.stepSize),
            int2(0, 0), int2(int(pc.width) - 1, int(pc.height) - 1)
        );
        const float2 sampleUV = (float2(samplePixel) + 0.5) * texelSize;

        const float sampleDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(samplePixel, 0)).r;
        if (sampleDepth == 0.0) { continue; }

        const float4 sampleColor    = rdgTexturesFloat4[pc.inputColorIndex].Load(int3(samplePixel, 0));
        const uint4  sampleGbuf1    = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(samplePixel, 0));
        const float  sampleVariance = rdgTexturesFloat[pc.inputVarianceIndex].Load(int3(samplePixel, 0)).r;
        const float3 sampleNormal   = UnpackNormalOct16(sampleGbuf1.r);
        const float3 sampleViewPos  = ReconstructViewPos(pc.sceneData, sampleUV, sampleDepth);

        // Variance-guided luminance edge stop
        const float lumDiff = abs(centerLum - Luminance(sampleColor.rgb));
        const float wLum    = exp(-lumDiff * lumDiff / (sigmaLumSq + varEps));

        // Normal edge stop
        const float wNormal = pow(saturate(dot(centerNormal, sampleNormal)), pc.sigmaNormal);

        // Plane-distance depth edge stop
        const float planeDist = abs(dot(centerNormal, sampleViewPos - centerViewPos));
        const float wDepth    = exp(-planeDist / (pc.sigmaDepth + 1e-6));

        const float w = KERNEL_WEIGHTS[i] * wLum * wNormal * wDepth;
        sumColor    += sampleColor * w;
        sumVariance += sampleVariance * w * w;
        sumWeight   += w;
    }

    if (sumWeight > 1e-6) {
        rdgStorageFloat4[pc.outputColorIndex][pixel] = sumColor / sumWeight;
        rdgStorageFloat[pc.outputVarianceIndex][pixel] = sumVariance / (sumWeight * sumWeight);
    } else {
        rdgStorageFloat4[pc.outputColorIndex][pixel] = centerColor;
        rdgStorageFloat[pc.outputVarianceIndex][pixel] = centerVariance;
    }
}
