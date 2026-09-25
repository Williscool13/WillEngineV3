import common_interop;
import push_constant_interop;
import bindless_declarations;
import shader_functions;
import random_common;
import gi_gather_common;

// Post-temporal spatial blur with radius ~ 1/sqrt(1 + history count) (ReBLUR's history-driven radius): young pixels blur wide, converged ones keep detail.
// The tap pattern rotates per pixel and frame; a fixed 8-tap pattern stamped offset copies of bright regions instead of blurring them.
// Output only; the history keeps the unblurred result so the blur never compounds.

static const float GI_POST_BLUR_RADIUS_GATHER_TEXELS = 14.0;
static const float GI_POST_BLUR_NOISY_SCALE = 1.5;
static const float GI_POST_BLUR_PLANE_DIST_SIGMA = 0.01;
static const float GI_POST_BLUR_NORMAL_POWER = 32.0;
static const uint GI_POST_BLUR_TAPS = 8u;
static const float GOLDEN_ANGLE = 2.39996323;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeGIPostBlur(uniform GIPostBlurPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float4 center = rdgTexturesFloat4[pc.inputIndex].Load(int3(pixel, 0));
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    // Noisy pixels keep the full radius instead of narrowing with history.
    float noisy = GIGatherNoisiness(rdgTexturesFloat2[pc.noiseIndex].Load(int3(pixel, 0)).x);
    float radius = GI_POST_BLUR_RADIUS_GATHER_TEXELS * float(pc.gatherScale) * lerp(1.0 / sqrt(1.0 + center.a), GI_POST_BLUR_NOISY_SCALE, noisy);
    if (depth <= 0.0 || (gt1.w & 1u) == 0u || radius < 1.0) {
        rdgStorageFloat4[pc.outputIndex][pixel] = center;
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 centerNormal = GIGatherGuideNormal(scene, pc.depthIndex, int2(pixel), pc.renderExtent, UnpackNormalOct16(gt1.r));
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float centerViewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ);
    float invPlaneSigma = 1.0 / (GI_POST_BLUR_PLANE_DIST_SIGMA * abs(centerViewZ));

    float3 sum = center.rgb;
    float sumW = 1.0;
    float rotation = InterleavedGradientNoise(float2(pixel) + 5.588238 * float(pc.frameIndex & 63u)) * 6.28318530718;
    [unroll]
    for (uint i = 0u; i < GI_POST_BLUR_TAPS; i++) {
        // Vogel disk: even area coverage.
        float r = sqrt((float(i) + 0.5) / float(GI_POST_BLUR_TAPS)) * radius;
        float phi = float(i) * GOLDEN_ANGLE + rotation;
        int2 tap = int2(float2(pixel) + float2(cos(phi), sin(phi)) * r + 0.5);
        if (any(tap < int2(0)) || any(tap >= int2(pc.renderExtent))) {
            continue;
        }
        float tapDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(tap, 0)).r;
        uint4 tapGt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(tap, 0));
        if (tapDepth <= 0.0 || (tapGt1.w & 1u) == 0u) {
            continue;
        }
        float2 tapUv = (float2(tap) + 0.5) / float2(pc.renderExtent);
        float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, ScreenToViewSpaceDepth(tapDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd));
        float w = exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
        w *= pow(saturate(dot(centerNormal, GIGatherGuideNormal(scene, pc.depthIndex, tap, pc.renderExtent, UnpackNormalOct16(tapGt1.r)))), GI_POST_BLUR_NORMAL_POWER);
        if (w <= 1e-4) {
            continue;
        }
        sum += rdgTexturesFloat4[pc.inputIndex].Load(int3(tap, 0)).rgb * w;
        sumW += w;
    }
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(sum / sumW, center.a);
}
