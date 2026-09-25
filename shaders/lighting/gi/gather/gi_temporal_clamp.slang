import push_constant_interop;
import bindless_declarations;
import shader_functions;

// Bounds the long GI history by the short (fast) history's 3x3 luma moments (ReBLUR HistoryFix clamp), hue-preserving. Clamping against the fast history, not raw 1spp input, keeps sigma tight enough to catch real changes without boiling.

static const float GI_TEMPORAL_CLAMP_SIGMA = 1.5;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeGITemporalClamp(uniform GITemporalClampPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float4 slow = rdgTexturesFloat4[pc.upscaledIndex].Load(int3(pixel, 0));
    float2 centerFast = rdgTexturesFloat2[pc.fastIndex].Load(int3(pixel, 0));
    if (centerFast.y <= 0.0) {
        rdgStorageFloat4[pc.outputIndex][pixel] = slow;
        return;
    }

    float m1 = 0.0;
    float m2 = 0.0;
    float count = 0.0;
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            int2 tap = clamp(int2(pixel) + int2(x, y), int2(0), int2(pc.renderExtent) - 1);
            float2 fast = rdgTexturesFloat2[pc.fastIndex].Load(int3(tap, 0));
            // Count 0 = sky or no geometry; excluded so silhouettes don't pull the bounds toward black.
            if (fast.y <= 0.0) {
                continue;
            }
            m1 += fast.x;
            m2 += fast.x * fast.x;
            count += 1.0;
        }
    }
    m1 /= count;
    m2 /= count;
    float sigma = sqrt(max(m2 - m1 * m1, 0.0));

    float luma = Luma(slow.rgb);
    float clampedLuma = clamp(luma, m1 - GI_TEMPORAL_CLAMP_SIGMA * sigma, m1 + GI_TEMPORAL_CLAMP_SIGMA * sigma);
    if (luma > 1e-6) {
        slow.rgb *= clampedLuma / luma;
    }
    rdgStorageFloat4[pc.outputIndex][pixel] = slow;
}
