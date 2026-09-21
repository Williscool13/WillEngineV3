import push_constant_interop;
import bindless_declarations;
import constants_interop;
import restir_common;
import shader_functions;

static const uint GRAD_FACTOR = 3u;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeReSTIRConfidenceResolve(uniform ReSTIRConfidenceResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float dip = 0.0;

    if (pc.gradientIndex != ~0u) {
        int2 gcenter = int2(pixel / GRAD_FACTOR);

        float ratio = 0.0;
        int r = int(pc.blurRadius);
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int2 q = gcenter + int2(dx, dy);
                if (any(q < int2(0, 0)) || any(q >= int2(pc.gradientExtent))) {
                    continue;
                }
                float2 g = rdgTexturesFloat2[pc.gradientIndex].Load(int3(q, 0));
                ratio = max(ratio, g.x / max(g.y + pc.darknessBias, 1e-6));
            }
        }
        dip += ratio * pc.sensitivity;
    }

    if (pc.sunFlipIndex != ~0u) {
        float2 quadUv = float2(pixel) / float2(pc.renderExtent);
        float4 q0 = rdgTexturesFloat[pc.sunFlipIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv);
        float4 q1 = rdgTexturesFloat[pc.sunFlipIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv, int2(1, 0));
        float4 q2 = rdgTexturesFloat[pc.sunFlipIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv, int2(0, 1));
        float4 q3 = rdgTexturesFloat[pc.sunFlipIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv, int2(1, 1));
        float4 quadMax = max(max(q0, q1), max(q2, q3));
        dip += max(max(max(quadMax.x, quadMax.y), max(quadMax.z, quadMax.w)), 0.0);
    }

    if (pc.prevConfidenceIndex != ~0u && pc.blendFactor < 1.0 && pc.confidenceStrength > 0.0) {
        uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
        float2 velocityNDC = UnpackMotionVectors(g1.g);
        float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
        float2 historyUV = uv + velocityNDC * 0.5;
        if (all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0))) {
            uint2 prevPixel = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
            float prevConf = rdgTexturesFloat[pc.prevConfidenceIndex].Load(int3(prevPixel, 0)).r;
            dip += (1.0 - prevConf) / pc.confidenceStrength * (1.0 - pc.blendFactor);
        }
    }

    float conf = 1.0 - pc.confidenceStrength * saturate(dip);
    rdgStorageFloat[pc.confidenceIndex][pixel] = conf;
}
