import push_constant_interop;
import constants_interop;

import bindless_declarations;

static const float bayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21,
};

[shader("compute")]
[numthreads(POST_PROCESS_DITHER_DISPATCH_X, POST_PROCESS_DITHER_DISPATCH_Y, 1)]
void ComputeDither(uniform DitherPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) { return; }
    uint2 pixelCoord = dtid.xy;

    float3 color = rdgTexturesFloat4[pc.inputIndex].Load(int3(pixelCoord, 0)).rgb;

    uint bx = pixelCoord.x & 7u;
    uint by = pixelCoord.y & 7u;
    float threshold = (bayer8[by * 8u + bx] + 0.5) / 64.0 - 0.5;

    // float frameOffset = frac(pc.frameIndex * 0.6180339887) - 0.5;
    // scale threshold to one LDR step (1/255) and apply strength
    // color += (threshold + frameOffset) * pc.ditherStrength * (1.0 / 255.0);
    color += threshold * pc.ditherStrength * (1.0 / 255.0);

    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}
