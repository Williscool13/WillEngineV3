import common_interop;
import push_constant_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import gi_gather_common;

static const float GTAO_TEMPORAL_COUNT_SCALE = 64.0;
static const float GTAO_TEMPORAL_MOTION_PIXELS = 1.5;
static const float GTAO_TEMPORAL_MOVING_MAX_ACCUM = 4.0;
static const float GTAO_TEMPORAL_CLAMP_EPS = 0.02;

// Sun shadow sources store sqrt(visibility); returns linear visibility at full resolution.
static float ResolveSunVisibility(ShadowsResolvePushConstant pc, SceneData scene, uint2 pixel, float depth, uint4 gt1)
{
    if (pc.sunShadowIndex == ~0x0u || depth <= 0.0) {
        return 1.0;
    }

    float visibility;
    if (pc.sunShadowPixelScale == 1u) {
        visibility = rdgTexturesFloat2[pc.sunShadowIndex].Load(int3(pixel, 0)).r;
    }
    else {
        // Joint bilateral upsample of the half-res shadow, guided by this full-res pixel's depth + normal.
        // Map full-res pixel to half-res grid via the exact integer decimation (rt_sun_shadow uses pixel*pixelScale); parity-independent, unlike floored shadowExtent.
        float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float3 N = normalize(UnpackNormalOct16(gt1.r));
        float2 hp = (float2(pixel) + 0.5) / float(pc.sunShadowPixelScale) - 0.5;
        int2 base = int2(floor(hp));
        float2 f = hp - float2(base);
        int2 maxCoord = int2(pc.sunShadowExtent) - 1;
        float wsum = 0.0;
        float vsum = 0.0;
        float bestW = -1.0;
        float nearest = 1.0;
        [unroll]
        for (int j = 0; j < 4; j++) {
            int2 off = int2(j & 1, j >> 1);
            int2 tp = clamp(base + off, int2(0), maxCoord);
            float bw = (off.x == 0 ? 1.0 - f.x : f.x) * (off.y == 0 ? 1.0 - f.y : f.y);
            float td = rdgTexturesFloat[pc.sunShadowDepthIndex].Load(int3(tp, 0)).r;
            if (td <= 0.0) { continue; }
            float tVis = rdgTexturesFloat2[pc.sunShadowIndex].Load(int3(tp, 0)).r;
            float tViewZ = ScreenToViewSpaceDepth(td, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float3 tN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.sunShadowNormalIndex].Load(int3(tp, 0)).r));
            float rel = abs(tViewZ - viewSpaceDepth) / max(max(tViewZ, viewSpaceDepth), 1e-5);
            float wz = saturate(1.0 - rel / 0.03);
            float wn = pow(max(dot(N, tN), 0.0), 16.0);
            float w = bw * wz * wn + 1e-5;
            vsum += tVis * w;
            wsum += w;
            if (w > bestW) { bestW = w; nearest = tVis; }
        }
        visibility = wsum > 1e-4 ? vsum / wsum : nearest;
    }
    visibility *= visibility;
    if (pc.contactShadowIndex != ~0x0u) {
        visibility = min(visibility, rdgTexturesFloat[pc.contactShadowIndex].Load(int3(pixel, 0)).r);
    }
    return visibility;
}

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

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    float sunVisibility = ResolveSunVisibility(pc, scene, pixel, depth, gt1);

    float ao = gtao;
    float count = 1.0;
    if (pc.temporalOutputIndex != ~0x0u && pc.bHistoryValid != 0u) {
        if (depth > 0.0 && (gt1.w & 1u) != 0u) {
            uint2 renderExtent = uint2(scene.mainRenderTargetSize);
            float2 uv = (float2(pixel) + 0.5) / scene.mainRenderTargetSize;
            float3 centerNormal = UnpackNormalOct16(gt1.r);
            float3 worldN = normalize(mul((float3x3)scene.invView, centerNormal));
            float centerViewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ);
            float NoV = saturate(abs(dot(centerNormal, normalize(centerXv))));
            GIGatherScalarHistory history = GIGatherFetchScalarHistory(scene, pc.historyIndex, pc.depthIndex, pc.depthHistoryIndex, pc.gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, renderExtent);
            if (history.quality > 0.0) {
                float motionPixels = length(UnpackMotionVectors(gt1.g) * 0.5 * scene.mainRenderTargetSize);
                float maxAccum = min(pc.temporalMaxAccum * scene.framerateScale, GTAO_TEMPORAL_COUNT_SCALE);
                float n = min(history.count * GTAO_TEMPORAL_COUNT_SCALE, maxAccum - 1.0);
                if (motionPixels > GTAO_TEMPORAL_MOTION_PIXELS) {
                    n = min(n, GTAO_TEMPORAL_MOVING_MAX_ACCUM * scene.framerateScale);
                }
                n *= history.quality;

                if (pc.temporalClampScale > 0.0) {
                    float aoMin = gtao;
                    float aoMax = gtao;
                    float2 quadUv = float2(pixel) / scene.mainRenderTargetSize;
                    float4 q0 = rdgTexturesFloat[pc.gtaoFilteredIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv);
                    float4 q1 = rdgTexturesFloat[pc.gtaoFilteredIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv, int2(1, 0));
                    float4 q2 = rdgTexturesFloat[pc.gtaoFilteredIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv, int2(0, 1));
                    float4 q3 = rdgTexturesFloat[pc.gtaoFilteredIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUv, int2(1, 1));
                    float4 quadMin = min(min(q0, q1), min(q2, q3));
                    float4 quadMax = max(max(q0, q1), max(q2, q3));
                    aoMin = min(min(quadMin.x, quadMin.y), min(quadMin.z, quadMin.w));
                    aoMax = max(max(quadMax.x, quadMax.y), max(quadMax.z, quadMax.w));
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

    rdgStorageFloat2[pc.outputImageIndex][pixel] = float2(sunVisibility, ao);
    if (pc.temporalOutputIndex != ~0x0u) {
        rdgStorageFloat2[pc.temporalOutputIndex][pixel] = float2(ao, count / GTAO_TEMPORAL_COUNT_SCALE);
    }
}
