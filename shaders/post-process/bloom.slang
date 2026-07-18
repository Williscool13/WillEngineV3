import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Soft threshold curve (Jimenez 2014)
float3 ApplyThreshold(float3 color, float threshold, float softThreshold) {
    float brightness = Luminance(color);
    float soft = brightness - threshold + softThreshold;
    soft = clamp(soft, 0.0, 2.0 * softThreshold);
    soft = soft * soft / (4.0 * softThreshold + 1e-4);

    float contribution = max(soft, brightness - threshold);
    contribution /= max(brightness, 1e-4);

    return color * contribution;
}

// Fused threshold + first 13-tap downsample: reads the full-res scene, writes the half-res chain top.
// Threshold/clamp operate in exposed (display-relative) space so their meaning is stable under auto-exposure, and the 5 box groups get a partial Karis average (Jimenez 2014) to suppress fireflies at the source.
[shader("compute")]
[numthreads(POST_PROCESS_BLOOM_DISPATCH_X, POST_PROCESS_BLOOM_DISPATCH_Y, 1)]
void ComputeBloomThreshold(uniform BloomThresholdPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    if (any(pixelCoord >= pc.outputExtent)) {
        return;
    }

    float exposure = 1.0;
    if (pc.bExposureEnabled != 0) {
        float avgLuminance = pc.luminanceBufferAddress[0];
        exposure = pc.targetLuminance / max(avgLuminance, 1e-4);
    }

    float2 uv = (float2(pixelCoord) + 0.5) / float2(pc.outputExtent);
    float2 texelSize = 1.0 / float2(pc.inputExtent);

    float3 taps[13];
    static const float2 offsets[13] = {
        float2(-2, -2), float2(0, -2), float2(2, -2),
        float2(-1, -1), float2(1, -1),
        float2(-2, 0), float2(0, 0), float2(2, 0),
        float2(-1, 1), float2(1, 1),
        float2(-2, 2), float2(0, 2), float2(2, 2),
    };
    [ForceUnroll]
    for (int i = 0; i < 13; ++i) {
        float3 c = rdgTexturesFloat4[pc.inputColorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + offsets[i] * texelSize, 0).rgb;
        c = max(c, 0.0);
        c = min(c * exposure, float3(pc.clampValue, pc.clampValue, pc.clampValue));
        taps[i] = ApplyThreshold(c, pc.threshold, pc.softThreshold);
    }

    // Boxes: taps index map A0 B1 C2 / D3 E4 / F5 G6 H7 / I8 J9 / K10 L11 M12
    float3 boxes[5] = {
        (taps[3] + taps[4] + taps[8] + taps[9]) * 0.25,
        (taps[0] + taps[1] + taps[5] + taps[6]) * 0.25,
        (taps[1] + taps[2] + taps[6] + taps[7]) * 0.25,
        (taps[5] + taps[6] + taps[10] + taps[11]) * 0.25,
        (taps[6] + taps[7] + taps[11] + taps[12]) * 0.25,
    };
    static const float boxWeights[5] = { 0.5, 0.125, 0.125, 0.125, 0.125 };

    float3 result = float3(0, 0, 0);
    float weightSum = 0.0;
    [ForceUnroll]
    for (int b = 0; b < 5; ++b) {
        float w = boxWeights[b] / (1.0 + Luminance(boxes[b]));
        result += boxes[b] * w;
        weightSum += w;
    }
    result /= max(weightSum, 1e-6);

    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(result / exposure, 1.0);
}

[shader("compute")]
[numthreads(POST_PROCESS_BLOOM_DISPATCH_X, POST_PROCESS_BLOOM_DISPATCH_Y, 1)]
void ComputeBloomDownsample(uniform BloomDownsamplePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    if (any(pixelCoord >= pc.outputExtent)) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) / float2(pc.outputExtent);
    float2 texelSize = 1.0 / float2(pc.inputExtent);

    // 13-tap filter (Jimenez 2014)
    float3 A = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-2, -2) * texelSize, pc.srcMipLevel).rgb;
    float3 B = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0, -2) * texelSize, pc.srcMipLevel).rgb;
    float3 C = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 2, -2) * texelSize, pc.srcMipLevel).rgb;

    float3 D = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1, -1) * texelSize, pc.srcMipLevel).rgb;
    float3 E = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1, -1) * texelSize, pc.srcMipLevel).rgb;

    float3 F = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-2,  0) * texelSize, pc.srcMipLevel).rgb;
    float3 G = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  0) * texelSize, pc.srcMipLevel).rgb;
    float3 H = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 2,  0) * texelSize, pc.srcMipLevel).rgb;

    float3 I = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  1) * texelSize, pc.srcMipLevel).rgb;
    float3 J = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  1) * texelSize, pc.srcMipLevel).rgb;

    float3 K = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-2,  2) * texelSize, pc.srcMipLevel).rgb;
    float3 L = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  2) * texelSize, pc.srcMipLevel).rgb;
    float3 M = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 2,  2) * texelSize, pc.srcMipLevel).rgb;

    // Weighted average (weights from Jimenez presentation)
    float3 result =
        (D + E + I + J) * 0.5 / 4.0 +
        (A + B + G + F) * 0.125 / 4.0 +
        (B + C + H + G) * 0.125 / 4.0 +
        (F + G + L + K) * 0.125 / 4.0 +
        (G + H + M + L) * 0.125 / 4.0;

    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(result, 1.0);
}

[shader("compute")]
[numthreads(POST_PROCESS_BLOOM_DISPATCH_X, POST_PROCESS_BLOOM_DISPATCH_Y, 1)]
void ComputeBloomUpsample(uniform BloomUpsamplePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    if (any(pixelCoord >= pc.outputExtent)) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) / float2(pc.outputExtent);
    float2 texelSize = pc.radius / float2(pc.lowerExtent);

    // 9-tap tent filter (3x3 with bilinear, weights sum to 16)
    float3 result = float3(0, 0, 0);

    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1, -1) * texelSize, pc.lowerMipLevel).rgb * 1.0;
    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0, -1) * texelSize, pc.lowerMipLevel).rgb * 2.0;
    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1, -1) * texelSize, pc.lowerMipLevel).rgb * 1.0;

    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  0) * texelSize, pc.lowerMipLevel).rgb * 2.0;
    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  0) * texelSize, pc.lowerMipLevel).rgb * 4.0;
    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  0) * texelSize, pc.lowerMipLevel).rgb * 2.0;

    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  1) * texelSize, pc.lowerMipLevel).rgb * 1.0;
    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  1) * texelSize, pc.lowerMipLevel).rgb * 2.0;
    result += rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  1) * texelSize, pc.lowerMipLevel).rgb * 1.0;

    result /= 16.0;

    // Add higher mip (current level we're upsampling to)
    float3 higher = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, pc.higherMipLevel).rgb;
    result += higher;

    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(result, 1.0);
}
