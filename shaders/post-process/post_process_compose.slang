import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;
import random_common;

import bindless_declarations;

float3 LinearToSrgb(float3 c) {
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(hi, lo, step(c, 0.0031308));
}

float3 SrgbToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow(max(c + 0.055, 0.0) / 1.055, 2.4);
    return lerp(hi, lo, step(c, 0.04045));
}

// Display-referred compose: sharpen -> film grain -> dither, on the finalize output.
[shader("compute")]
[numthreads(POST_PROCESS_COMPOSE_DISPATCH_X, POST_PROCESS_COMPOSE_DISPATCH_Y, 1)]
void ComputeCompose(uniform PostProcessComposePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) { return; }
    uint2 pixelCoord = dtid.xy;
    int2 p = int2(pixelCoord);

    float3 e = rdgTexturesFloat4[pc.inputIndex].Load(int3(p, 0)).rgb;
    float3 color = e;

    if (pc.sharpenStrength > 0.0) {
        int2 maxCoord = int2(pc.outputExtent) - 1;
        float3 b = rdgTexturesFloat4[pc.inputIndex].Load(int3(clamp(p + int2( 0, -1), int2(0, 0), maxCoord), 0)).rgb;
        float3 d = rdgTexturesFloat4[pc.inputIndex].Load(int3(clamp(p + int2(-1,  0), int2(0, 0), maxCoord), 0)).rgb;
        float3 f = rdgTexturesFloat4[pc.inputIndex].Load(int3(clamp(p + int2( 1,  0), int2(0, 0), maxCoord), 0)).rgb;
        float3 h = rdgTexturesFloat4[pc.inputIndex].Load(int3(clamp(p + int2( 0,  1), int2(0, 0), maxCoord), 0)).rgb;

        // Unsharp mask with a bounded delta: high strengths approach but never snap onto the neighborhood extremes, so edges cannot posterize and residual GI speckle does not grow dark rims.
        float3 blurred = (b + d + f + h) * 0.25;
        float3 minRGB = min(e, min(min(b, d), min(f, h)));
        float3 maxRGB = max(e, max(max(b, d), max(f, h)));
        float3 delta = (e - blurred) * pc.sharpenStrength;
        delta = clamp(delta, (minRGB - e) * 0.75, (maxRGB - e) * 0.75);
        color = e + delta;
    }

    if (pc.grainStrength > 0.0) {
        uint2 cell = pc.grainSize > 1.001 ? uint2(float2(pixelCoord) / pc.grainSize) : pixelCoord;
        float noise = SpatioTemporalBlueNoise2D(cell, pc.frameIndex, 2u).x * 2.0 - 1.0;
        // Film-like response: strongest in mids, fading out into clipped whites. Blacks keep a floor rather than nulling, so the strength control still does something on very dark content.
        const float shadowFloor = 0.25;
        float luma = saturate(dot(color, float3(0.2126, 0.7152, 0.0722)));
        float filmResponse = max(4.0 * luma * (1.0 - luma), (1.0 - luma) * shadowFloor);
        float response = lerp(1.0, filmResponse, pc.grainResponse);
        color += noise * pc.grainStrength * response;
        color = max(color, 0.0);
    }

    if (pc.ditherStrength > 0.0) {
        float2 bn = SpatioTemporalBlueNoise2D(pixelCoord, pc.frameIndex, 0u);
        float t = bn.x + bn.y - 1.0;
        float3 encoded = LinearToSrgb(saturate(color));
        // Local linear span of one encoded 8-bit LSB (half the 2-LSB window)
        float3 stepLinear = SrgbToLinear(min(encoded + 1.0 / 255.0, 1.0)) - SrgbToLinear(max(encoded - 1.0 / 255.0, 0.0));
        color += t * pc.ditherStrength * 0.5 * stepLinear;
        color = max(color, 0.0);
    }

    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}
