import push_constant_interop;
import bindless_declarations;

// Temporal-gradient antilag, stage 1. The producer (combined_temporal) already wrote a same-sample re-shade delta per
// half-res pixel into restir_signal (RG16F: x = signed visibility-change * brightness, y = brightness normalizer), so
// this is just a signed stratum-mean downsample. The signal is noise-free (the visibility change comes from tracing the
// SAME winner sample against the current vs carried-previous TLAS), so no cross-frame reprojection is needed here.
// Rectification of x happens only after the stage-2 blur.
// GRAD_FACTOR must match SetupReSTIRPasses in restir_passes.cpp.
static const uint GRAD_FACTOR = 3u;

[numthreads(8, 8, 1)]
void ComputeMain(uniform ReSTIRConfidenceGradientPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 gpix = threadID.xy;
    if (any(gpix >= pc.gradientExtent)) {
        return;
    }

    float2 sum = float2(0.0, 0.0);
    float count = 0.0;
    for (uint yy = 0u; yy < GRAD_FACTOR; yy++) {
        for (uint xx = 0u; xx < GRAD_FACTOR; xx++) {
            uint2 p = gpix * GRAD_FACTOR + uint2(xx, yy);
            if (any(p >= pc.renderExtent)) {
                continue;
            }
            sum += rdgTexturesFloat2[pc.signalIndex].Load(int3(p, 0));
            count += 1.0;
        }
    }

    float2 gradient = (count > 0.0) ? sum / count : float2(0.0, 0.0);
    rdgStorageFloat2[pc.gradientIndex][gpix] = gradient;
}
