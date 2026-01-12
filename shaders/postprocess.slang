import push_constant_interop;

import bindless_declarations;

float3 ACESFilmic(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 Uncharted2Tonemap(float3 x) {
    float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 ReinhardTonemap(float3 x) {
    return x / (1.0 + x);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeSDRTonemap(uniform TonemapSDRPushConstant pc, uint3 threadId : SV_DispatchThreadID) {
    int2 texelCoord = int2(threadId.xy);

    float2 uv = (float2(texelCoord) + 0.5) / float2(pc.outputWidth, pc.outputHeight);
    float3 hdrColor = rdgTextures[pc.srcImageIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv, 0).rgb;

    float3 ldrColor;
    if (pc.tonemapOperator == 0) {
        ldrColor = ACESFilmic(hdrColor);
    } else if (pc.tonemapOperator == 1) {
        ldrColor = Uncharted2Tonemap(hdrColor);
    } else if (pc.tonemapOperator == 2) {
        ldrColor = ReinhardTonemap(hdrColor);
    }

    rdgRenderTextures[pc.dstImageIndex][texelCoord] = float4(ldrColor, 1.0);
}
