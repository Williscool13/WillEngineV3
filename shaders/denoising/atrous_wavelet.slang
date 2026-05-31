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

[shader("compute")]
[numthreads(8, 8, 1)]
void ATrousWaveletMain(uniform ATrousWaveletPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    const uint2 pixel = dtid.xy;
    if (pixel.x >= pc.width || pixel.y >= pc.height) {
        return;
    }

    const float2 texelSize = float2(1.0 / float(pc.width), 1.0 / float(pc.height));
    const float2 centerUV = (float2(pixel) + 0.5) * texelSize;

    const float4 centerColor = rdgTexturesFloat4[pc.inputColorIndex].Load(int3(pixel, 0));
    const uint4 centerGbuf1  = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    const float centerDepth  = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;


    if (centerDepth == 0.0) {
        rdgStorageFloat4[pc.outputColorIndex][pixel] = centerColor;
        return;
    }

    const float3 centerNormal  = UnpackNormalOct16(centerGbuf1.r);
    const float3 centerViewPos = ReconstructViewPos(pc.sceneData, centerUV, centerDepth);
    const float  centerLum     = Luminance(centerColor.rgb);

    float4 sumColor  = float4(0.0);
    float  sumWeight = 0.0;

    for (int i = 0; i < 25; i++) {
        const int2 samplePixel = clamp(
            int2(pixel) + int2(KERNEL_OFFSETS[i]) * int(pc.stepSize),
            int2(0, 0), int2(int(pc.width) - 1, int(pc.height) - 1)
        );
        const float2 sampleUV = (float2(samplePixel) + 0.5) * texelSize;

        const float sampleDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(samplePixel, 0)).r;
        if (sampleDepth == 0.0) { continue; }

        const float4 sampleColor = rdgTexturesFloat4[pc.inputColorIndex].Load(int3(samplePixel, 0));
        const uint4  sampleGbuf1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(samplePixel, 0));
        const float3 sampleNormal   = UnpackNormalOct16(sampleGbuf1.r);
        const float3 sampleViewPos  = ReconstructViewPos(pc.sceneData, sampleUV, sampleDepth);

        // Luminance edge stop
        const float lumDiff = abs(centerLum - Luminance(sampleColor.rgb));
        const float wLum    = exp(-lumDiff * lumDiff / (2.0 * pc.sigmaLuminance * pc.sigmaLuminance + 1e-6));

        // Normal edge stop
        const float wNormal = pow(saturate(dot(centerNormal, sampleNormal)), pc.sigmaNormal);

        // Plane-distance depth edge stop
        const float planeDist = abs(dot(centerNormal, sampleViewPos - centerViewPos));
        const float wDepth    = exp(-planeDist / (pc.sigmaDepth + 1e-6));

        const float w = KERNEL_WEIGHTS[i] * wLum * wNormal * wDepth;
        sumColor  += sampleColor * w;
        sumWeight += w;
    }

    rdgStorageFloat4[pc.outputColorIndex][pixel] = (sumWeight > 1e-6) ? (sumColor / sumWeight) : centerColor;
}
