import push_constant_interop;

import bindless_declarations;

static const float3x3 ACESInputMat = {
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

static const float3x3 ACESOutputMat = {
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

// Godot (?) https://github.com/TheRealMJP/BakingLab
float3 ACESFitted(float3 color) {
    color = mul(ACESInputMat, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// Uncharted 2 https://www.gdcvault.com/play/1012351/Uncharted-2-HDR
float3 Uncharted2Tonemap(float3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// Classic reinhard
float3 ReinhardTonemap(float3 x) {
    return x / (1.0 + x);
}

// Lottes 2016 https://gpuopen.com/learn/optimized-reversible-tonemapper-for-resolve/
float3 LottesTonemapSimple(float3 c) {
    return c / (max(max(c.r, c.g), c.b) + 1.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeSDRTonemap(uniform TonemapSDRPushConstant pc, uint3 threadId : SV_DispatchThreadID) {
    int2 texelCoord = int2(threadId.xy);
    float3 hdrColor = rdgTextures[pc.srcImageIndex].Load(int3(texelCoord, 0)).rgb;

    float3 ldrColor;
    switch (pc.tonemapOperator) {
        case 0:
            ldrColor = ACESFitted(hdrColor);
            break;
        case 1:
            ldrColor = Uncharted2Tonemap(hdrColor);
            break;
        case 2:
            ldrColor = ReinhardTonemap(hdrColor);
            break;
        case 3:
            ldrColor = LottesTonemapSimple(hdrColor);
            break;
        default:
            ldrColor = hdrColor;
            break;
    }

    rdgRenderTextures[pc.dstImageIndex][texelCoord] = float4(ldrColor, 1.0);
}


// ==================== Exposure ====================
groupshared uint sharedHistogram[256];

uint HDRToHistogramBin(float3 hdrColor, float minLogLuminance, float oneOverLogLuminanceRange)
{
    static const float EPSILON = 0.005;

    float luminance = dot(hdrColor, float3(0.2126, 0.7152, 0.0722));

    if (luminance < EPSILON)
    {
        return 0;
    }

    float logLuminance = saturate((log2(luminance) - minLogLuminance) * oneOverLogLuminanceRange);
    return uint(logLuminance * 254.0 + 1.0);
}

[numthreads(16, 16, 1)]
void ComputeBuildHistogramExposure(uniform HistogramBuildPushConstant pc, uint3 tid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    sharedHistogram[groupIndex] = 0;

    GroupMemoryBarrierWithGroupSync();

    if (tid.x < pc.width && tid.y < pc.height) {
        float3 hdrColor = rdgTextures[pc.hdrImageIndex].Load(int3(tid.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor, pc.minLogLuminance, pc.oneOverLogLuminanceRange);
        InterlockedAdd(sharedHistogram[binIndex], 1);
    }
    GroupMemoryBarrierWithGroupSync();


    InterlockedAdd(pc.histogramBufferAddress[groupIndex], sharedHistogram[groupIndex]);
}
