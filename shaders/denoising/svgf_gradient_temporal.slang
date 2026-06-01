import push_constant_interop;
import shader_functions;
import bindless_declarations;
import constants_interop;

[shader("compute")]
[numthreads(8, 8, 1)]
void SVGFGradientTemporalMain(uniform SVGFGradientTemporalPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    // Operates at strided resolution
    const uint2 tileCoord = dtid.xy;
    const uint2 tileMax = uint2((pc.width + pc.stride - 1) / pc.stride, (pc.height + pc.stride - 1) / pc.stride);
    if (any(tileCoord >= tileMax)) {
        return;
    }

    const float currentGradient = rdgTexturesFloat[pc.gradientSamplesIndex].Load(int3(tileCoord, 0)).r;

    // Reproject tile representative pixel to history
    const uint2 pixel = min(tileCoord * pc.stride, uint2(pc.width - 1, pc.height - 1));
    const float2 texelSize = float2(1.0 / float(pc.width), 1.0 / float(pc.height));
    const float2 uv = (float2(pixel) + 0.5) * texelSize;

    const float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;

    float blended = currentGradient;
    if (depth > 0.0) {
        const uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
        const float2 velocityNDC = UnpackMotionVectors(g1.g);
        const float2 historyUV = uv + velocityNDC * 0.5;

        if (all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0))) {
            // Map history UV to strided history texture coord
            const float2 historyStridedUV = historyUV;
            const float historyGradient = rdgTexturesFloat[pc.gradientHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], historyStridedUV, 0).r;
            const float alpha = 0.2;
            blended = lerp(historyGradient, currentGradient, alpha);
        }
    }

    rdgStorageFloat[pc.outputGradientIndex][tileCoord] = blended;
}
