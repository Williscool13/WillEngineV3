import push_constant_interop;
import bindless_declarations;

static const uint GRAD_FACTOR = 3u;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeReSTIRConfidenceGradient(uniform ReSTIRConfidenceGradientPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
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
