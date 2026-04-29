import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

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
void ComputeSDRTonemap(uniform TonemapSDRPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    int2 pixelCoord = int2(dtid.xy);
    float3 sceneColor = rdgTexturesFloat4[pc.srcImageIndex].Load(int3(pixelCoord, 0)).rgb;

    float3 bloom = rdgTexturesFloat4[pc.bloomImageIndex].Load(int3(pixelCoord, 0)).rgb;
    float3 hdrColor = sceneColor + bloom * pc.bloomIntensity;

    float avgLuminance = pc.luminanceBufferAddress[0];
    float exposure = pc.targetLuminance / max(avgLuminance, 0.001);
    float3 exposedColor = hdrColor * exposure;

    float3 ldrColor;
    switch (pc.tonemapOperator) {
        case 0:
            ldrColor = ACESFitted(exposedColor);
            break;
        case 1:
            ldrColor = Uncharted2Tonemap(exposedColor);
            break;
        case 2:
            ldrColor = ReinhardTonemap(exposedColor);
            break;
        case 3:
            ldrColor = LottesTonemapSimple(exposedColor);
            break;
        default:
            ldrColor = exposedColor;
            break;
    }

    rdgStorageFloat4[pc.dstImageIndex][pixelCoord] = float4(ldrColor, 1.0);
}
