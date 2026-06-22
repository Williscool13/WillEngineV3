import push_constant_interop;
import bindless_declarations;
import restir_common;
import shader_functions;

// Temporal-gradient antilag, stage 2. Adapted from RTXDI FilterGradientsPass + ConfidencePass:
// blur the downsampled gradient, convert to confidence saturate(1 - |delta| / (max + darknessBias))^sensitivity,
// scale by the master strength, then blend with the reprojected previous confidence in pow(.,0.25) space so confidence
// drops fast on a real change and recovers slowly (gives ReSTIR time to re-converge before RELAX trusts history again).
// GRAD_FACTOR must match SetupReSTIRPasses in restir_passes.cpp.
static const uint GRAD_FACTOR = 3u;

[numthreads(8, 8, 1)]
void ComputeMain(uniform ReSTIRConfidenceResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    int2 gcenter = int2(pixel / GRAD_FACTOR);

    // Pyramid-weighted blur over the downsampled gradient. g.x is the SIGNED luminance delta, g.y the local brightness
    // normalizer; both are blurred before g.x is rectified so the uncorrelated per-pixel noise cancels toward zero in
    // static regions while a spatially-coherent shadow change (consistent sign) survives.
    float2 g = float2(0.0, 0.0);
    float wsum = 0.0;
    int r = int(pc.blurRadius);
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int2 q = gcenter + int2(dx, dy);
            if (any(q < int2(0, 0)) || any(q >= int2(pc.gradientExtent))) {
                continue;
            }
            float w = 1.0 / (1.0 + float(abs(dx) + abs(dy)));
            g += rdgTexturesFloat2[pc.gradientIndex].Load(int3(q, 0)) * w;
            wsum += w;
        }
    }
    if (wsum > 0.0) {
        g /= wsum;
    }

    // Relative gradient -> confidence. darknessBias keeps dark-region noise from producing a huge relative gradient; the max() guards 0/0 when darknessBias is 0.
    float conf = saturate(1.0 - abs(g.x) / max(g.y + pc.darknessBias, 1e-6));
    conf = saturate(pow(conf, pc.sensitivity));

    // Master mix: strength 0 -> confidence forced to 1 (antilag off), strength 1 -> full effect.
    conf = 1.0 - pc.confidenceStrength * (1.0 - conf);

    // Asymmetric temporal filter against reprojected previous confidence.
    if (pc.prevConfidenceIndex != ~0u && pc.blendFactor < 1.0) {
        uint2 fullPixel = FetchFullPixel(pc.quadSelectionIndex, pixel, pc.pixelScale);
        uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));
        float2 velocityNDC = UnpackMotionVectors(g1.g);
        float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
        float2 historyUV = uv + velocityNDC * 0.5;
        if (all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0))) {
            uint2 prevPixel = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
            float prevConf = rdgTexturesFloat[pc.prevConfidenceIndex].Load(int3(prevPixel, 0)).r;
            const float power = 0.25;
            float blended = lerp(pow(saturate(prevConf), power), pow(saturate(conf), power), pc.blendFactor);
            conf = pow(saturate(blended), 1.0 / power);
        }
    }

    rdgStorageFloat[pc.confidenceIndex][pixel] = conf;
}
