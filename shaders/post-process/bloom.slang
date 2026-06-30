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

[shader("compute")]
[numthreads(POST_PROCESS_BLOOM_DISPATCH_X, POST_PROCESS_BLOOM_DISPATCH_Y, 1)]
void ComputeBloomThreshold(uniform BloomThresholdPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    if (any(pixelCoord >= pc.outputExtent)) {
        return;
    }

    float3 color = rdgTexturesFloat4[pc.inputColorIndex].Load(int3(pixelCoord, 0)).rgb;
    color = min(color, float3(pc.clampValue, pc.clampValue, pc.clampValue));
    float3 thresholded = ApplyThreshold(color, pc.threshold, pc.softThreshold);
    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(thresholded, 1.0);
}

[shader("compute")]
[numthreads(POST_PROCESS_BLOOM_DISPATCH_X, POST_PROCESS_BLOOM_DISPATCH_Y, 1)]
void ComputeBloomDownsample(uniform BloomDownsamplePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    if (any(pixelCoord >= pc.outputExtent)) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) / float2(pc.outputExtent);

    uint2 inputSize;
    uint numMips;
    rdgTexturesFloat4[pc.inputIndex].GetDimensions(0, inputSize.x, inputSize.y, numMips);

    inputSize = inputSize >> pc.srcMipLevel;
    float2 texelSize = 1.0 / float2(inputSize);

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

    uint2 outputSize;
    rdgStorageFloat4[pc.outputIndex].GetDimensions(outputSize.x, outputSize.y);

    if (any(pixelCoord >= outputSize)) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) / float2(outputSize);

    // Lower mip size
    uint2 lowerSize;
    uint numMips;
    rdgTexturesFloat4[pc.inputIndex].GetDimensions(0, lowerSize.x, lowerSize.y, numMips);
    lowerSize = lowerSize >> pc.lowerMipLevel;
    float2 texelSize = pc.radius / float2(lowerSize);

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
