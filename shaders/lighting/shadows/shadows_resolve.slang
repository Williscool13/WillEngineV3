import common_interop;
import push_constant_interop;
import bindless_declarations;
import shader_functions;
import gi_gather_common;

static const float GTAO_TEMPORAL_COUNT_SCALE = 64.0;
static const float GTAO_TEMPORAL_MOTION_PIXELS = 1.5;
static const float GTAO_TEMPORAL_MOVING_MAX_ACCUM = 4.0;
static const float GTAO_TEMPORAL_CLAMP_EPS = 0.02;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeShadowsResolve(uniform ShadowsResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    SceneData scene = pc.sceneData[0];
    if (pixel.x >= scene.mainRenderTargetSize.x || pixel.y >= scene.mainRenderTargetSize.y) {
        return;
    }

    float gtao = 1.0;
    if (pc.gtaoFilteredIndex >= 0) {
        gtao = rdgTexturesFloat[pc.gtaoFilteredIndex].Load(int3(pixel, 0)).x;
    }

    float ao = gtao;
    float count = 1.0;
    if (pc.temporalOutputIndex != ~0x0u && pc.bHistoryValid != 0u) {
        float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
        uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
        if (depth > 0.0 && (gt1.w & 1u) != 0u) {
            uint2 renderExtent = uint2(scene.mainRenderTargetSize);
            float2 uv = (float2(pixel) + 0.5) / scene.mainRenderTargetSize;
            float3 centerNormal = UnpackNormalOct16(gt1.r);
            float3 worldN = normalize(mul((float3x3)scene.invView, centerNormal));
            float centerViewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ);
            float NoV = saturate(abs(dot(centerNormal, normalize(centerXv))));
            GIGatherScalarHistory history = GIGatherFetchScalarHistory(scene, pc.historyIndex, pc.depthHistoryIndex, pc.gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, renderExtent);
            if (history.quality > 0.0) {
                float motionPixels = length(UnpackMotionVectors(gt1.g) * 0.5 * scene.mainRenderTargetSize);
                float n = min(history.count * GTAO_TEMPORAL_COUNT_SCALE, pc.temporalMaxAccum - 1.0);
                if (motionPixels > GTAO_TEMPORAL_MOTION_PIXELS) {
                    n = min(n, GTAO_TEMPORAL_MOVING_MAX_ACCUM);
                }
                n *= history.quality;

                if (pc.temporalClampScale > 0.0) {
                    float aoMin = gtao;
                    float aoMax = gtao;
                    [unroll]
                    for (int dy = -1; dy <= 1; dy++) {
                        [unroll]
                        for (int dx = -1; dx <= 1; dx++) {
                            int2 tap = clamp(int2(pixel) + int2(dx, dy), int2(0), int2(renderExtent) - 1);
                            float v = rdgTexturesFloat[pc.gtaoFilteredIndex].Load(int3(tap, 0)).x;
                            aoMin = min(aoMin, v);
                            aoMax = max(aoMax, v);
                        }
                    }
                    // Reprojection blur under motion reads as change; widen the box with velocity (Jimenez) so it cuts trails, not moving-but-valid history.
                    float center = 0.5 * (aoMin + aoMax);
                    float halfWidth = (0.5 * (aoMax - aoMin) + GTAO_TEMPORAL_CLAMP_EPS) * pc.temporalClampScale * (1.0 + 0.5 * min(motionPixels, 2.0));
                    float histDist = abs(history.value - center) / halfWidth;
                    float closest = clamp(history.value, center - halfWidth, center + halfWidth);
                    history.value = lerp(history.value, closest, smoothstep(1.0, 3.0, histDist));
                }

                ao = (n * history.value + gtao) / (n + 1.0);
                count = n + 1.0;
            }
        }
    }

    rdgStorageFloat2[pc.outputImageIndex][pixel] = float2(1.0, ao);
    if (pc.temporalOutputIndex != ~0x0u) {
        rdgStorageFloat2[pc.temporalOutputIndex][pixel] = float2(ao, count / GTAO_TEMPORAL_COUNT_SCALE);
    }
}
