import push_constant_interop;
import shader_functions;
import bindless_declarations;
import constants_interop;

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SVGFGradientSamplesMain(uniform SVGFGradientSamplesPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    // Each thread covers one strided tile; output is at (width/stride) x (height/stride)
    const uint2 tileCoord = dtid.xy;
    const uint2 tileMax = uint2((pc.width + pc.stride - 1) / pc.stride, (pc.height + pc.stride - 1) / pc.stride);
    if (any(tileCoord >= tileMax)) {
        return;
    }

    // Representative pixel: top-left of tile
    const uint2 pixel = tileCoord * pc.stride;
    const uint2 clampedPixel = min(pixel, uint2(pc.width - 1, pc.height - 1));

    const float2 texelSize = float2(1.0 / float(pc.width), 1.0 / float(pc.height));
    const float2 uv = (float2(clampedPixel) + 0.5) * texelSize;

    const float currentDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(clampedPixel, 0)).r;
    if (currentDepth == 0.0) {
        rdgStorageFloat[pc.outputGradientIndex][tileCoord] = 0.0;
        return;
    }

    const float4 currentColor = rdgTexturesFloat4[pc.colorIndex].Load(int3(clampedPixel, 0));
    const float currentLum = Luminance(currentColor.rgb);

    // Reproject to previous frame using motion vectors
    const uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(clampedPixel, 0));
    const float2 velocityNDC = UnpackMotionVectors(g1.g);
    const float2 historyUV = uv + velocityNDC * 0.5;

    float gradient = 0.0;
    if (all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0))) {
        const float4 historyColor = rdgTexturesFloat4[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], historyUV, 0);
        const float historyLum = Luminance(historyColor.rgb);
        gradient = abs(currentLum - historyLum) / max(max(currentLum, historyLum), 0.01);
    }

    rdgStorageFloat[pc.outputGradientIndex][tileCoord] = gradient;
}
