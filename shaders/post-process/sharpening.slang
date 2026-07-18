import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

[shader("compute")]
[numthreads(POST_PROCESS_SHARPENING_DISPATCH_X, POST_PROCESS_SHARPENING_DISPATCH_Y, 1)]
void ComputeSharpening(uniform SharpeningPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) { return; }

    uint2 pixelCoord = dtid.xy;
    float2 uv = (dtid.xy + 0.5) / pc.outputExtent;
    float2 texelSize = 1.0 / pc.outputExtent;

    // Sample center and cross neighbors
    float3 b = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0, -1) * texelSize, 0).rgb;
    float3 d = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  0) * texelSize, 0).rgb;
    float3 e = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rgb;
    float3 f = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  0) * texelSize, 0).rgb;
    float3 h = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  1) * texelSize, 0).rgb;

    // Simple unsharp mask, clamped to the local neighborhood range (CAS-style). The clamp makes undershoot below the
    // darkest neighbor impossible, so bright HDR highlights cannot ring dark halos no matter their magnitude.
    float3 blurred = (b + d + f + h) * 0.25;
    float3 sharpened = e + (e - blurred) * pc.sharpness;

    float3 minRGB = min(e, min(min(b, d), min(f, h)));
    float3 maxRGB = max(e, max(max(b, d), max(f, h)));
    sharpened = clamp(sharpened, minRGB, maxRGB);

    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(sharpened, 1.0);

    // FFX CAS (I think this is wrong somehow/somewhere)
    /*if (any(dtid.xy >= pc.outputExtent)) return;

    uint2 pixelCoord = dtid.xy;
    float2 uv = (dtid.xy + 0.5) / pc.outputExtent;
    float2 texelSize = 1.0 / pc.outputExtent;

    float3 a = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1, -1) * texelSize, 0).rgb;
    float3 b = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0, -1) * texelSize, 0).rgb;
    float3 c = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1, -1) * texelSize, 0).rgb;
    float3 d = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  0) * texelSize, 0).rgb;
    float3 e = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  0) * texelSize, 0).rgb;
    float3 f = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  0) * texelSize, 0).rgb;
    float3 g = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  1) * texelSize, 0).rgb;
    float3 h = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  1) * texelSize, 0).rgb;
    float3 i = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  1) * texelSize, 0).rgb;
    float3 minRGB = min(min(min(d, e), min(f, b)), h);
    float3 maxRGB = max(max(max(d, e), max(f, b)), h);

    // Edge Detection
    float3 amplify = clamp(minRGB / (maxRGB + 0.00001) - 1.0, 0.0, 1.0);
    amplify = sqrt(amplify);

    float3 w = amplify * pc.sharpness;
    float3 weightSum = 1.0 + 4.0 * w;

    float3 sharpened = (b + d + f + h) * w + e;
    sharpened /= weightSum;

    sharpened = clamp(sharpened, minRGB, maxRGB);

    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(sharpened, 1.0);*/
}
