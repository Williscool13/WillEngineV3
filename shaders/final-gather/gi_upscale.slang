import common_interop;
import push_constant_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;
import gi_gather_common;

// Full-res resolve: 4-tap Poisson bilateral upsample of the gather SH evaluated at the pixel normal, then counter temporal New=(n*Prev+Curr)/(n+1) via gbuffer MV reprojection.

static const float2 GI_UPSCALE_POISSON[4] = { float2(-0.75, -0.25), float2(0.25, -0.75), float2(0.75, 0.25), float2(-0.25, 0.75) };
static const float GI_UPSCALE_PLANE_DIST_SIGMA = 0.01;
static const float GI_UPSCALE_NORMAL_POWER = 32.0;
static const float GI_UPSCALE_NEIGHBORHOOD_MOTION_PIXELS = 1.5;
static const float GI_UPSCALE_MOVING_MAX_ACCUM = 4.0;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeGIUpscale(uniform GIUpscalePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (depth <= 0.0 || (gt1.w & 1u) == 0u) {
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(0.0);
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 centerNormal = UnpackNormalOct16(gt1.r);
    float3 worldN = normalize(mul((float3x3)scene.invView, centerNormal));
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float centerViewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ);
    float invPlaneSigma = 1.0 / (GI_UPSCALE_PLANE_DIST_SIGMA * abs(centerViewZ));

    float4 shR = float4(0.0);
    float4 shG = float4(0.0);
    float4 shB = float4(0.0);
    float sumW = 0.0;
    float4 bestShR = float4(0.0);
    float4 bestShG = float4(0.0);
    float4 bestShB = float4(0.0);
    float bestW = -1.0;
    float2 halfCoord = (float2(pixel) + 0.5) * 0.5;
    [unroll]
    for (uint i = 0; i < 4; i++) {
        int2 tap = clamp(int2(floor(halfCoord + GI_UPSCALE_POISSON[i])), int2(0), int2(pc.gatherExtent) - 1);
        uint2 tapFull = GIGatherRepresentativePixel(uint2(tap), pc.frameIndex, pc.renderExtent);
        float tapDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(tapFull, 0)).r;
        uint4 tapGt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(tapFull, 0));
        float tapTier = rdgTexturesFloat2[pc.dataIndex].Load(int3(tap, 0)).y;
        if (tapDepth <= 0.0 || (tapGt1.w & 1u) == 0u || uint(tapTier + 0.5) == GI_GATHER_TIER_BACKFACE) { continue; }

        float tapViewZ = ScreenToViewSpaceDepth(tapDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float2 tapUv = (float2(tapFull) + 0.5) / float2(pc.renderExtent);
        float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, tapViewZ);
        float3 tapNormal = UnpackNormalOct16(tapGt1.r);

        float w = exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
        w *= pow(saturate(dot(centerNormal, tapNormal)), GI_UPSCALE_NORMAL_POWER);

        float4 tapShR = rdgTexturesFloat4[pc.shRIndex].Load(int3(tap, 0));
        float4 tapShG = rdgTexturesFloat4[pc.shGIndex].Load(int3(tap, 0));
        float4 tapShB = rdgTexturesFloat4[pc.shBIndex].Load(int3(tap, 0));
        shR += tapShR * w;
        shG += tapShG * w;
        shB += tapShB * w;
        sumW += w;
        if (w > bestW) {
            bestW = w;
            bestShR = tapShR;
            bestShG = tapShG;
            bestShB = tapShB;
        }
    }

    bool bHasCurrent = true;
    float3 current;
    if (sumW > 1e-4) {
        float invW = 1.0 / sumW;
        current = max(float3(shR.x + dot(shR.yzw, worldN), shG.x + dot(shG.yzw, worldN), shB.x + dot(shB.yzw, worldN)) * invW, float3(0.0));
    } else if (bestW >= 0.0) {
        // All taps low-weight but a valid surface neighbor exists: take the best-matching tap, not an unguarded bilinear that would blend zero SH from background texels.
        current = max(float3(bestShR.x + dot(bestShR.yzw, worldN), bestShG.x + dot(bestShG.yzw, worldN), bestShB.x + dot(bestShB.yzw, worldN)), float3(0.0));
    } else {
        // No valid half-res neighbor at all this frame (e.g. every tap backfaced): no information, hold history instead of blending zero in.
        current = float3(0.0);
        bHasCurrent = false;
    }

    float3 result = current;
    float accum = 1.0;
    if (pc.bHistoryValid != 0u) {
        float NoV = saturate(abs(dot(centerNormal, normalize(centerXv))));
        GIGatherHistory history = GIGatherFetchHistory(scene, pc.historyIndex, pc.depthHistoryIndex, pc.gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, pc.renderExtent);
        if (history.quality > 0.0) {
            float n = min(history.count, GI_GATHER_MAX_ACCUM - 1.0) * history.quality;

            float3 worldPos = mul(scene.invView, float4(centerXv, 1.0)).xyz;
            float4 prevClip = mul(scene.prevViewProj, float4(worldPos, 1.0));
            if (prevClip.w > 0.0) {
                // prevJitter * 0.5 removes the jitter baked into prevViewProj so the camera-only delta matches the jitter-free MV convention
                float2 staticDeltaUv = ((prevClip.xy / prevClip.w) * 0.5 + 0.5) - uv + scene.prevJitter * 0.5;
                float neighborhoodMotionPixels = 0.0;
                [unroll]
                for (int oy = -1; oy <= 1; oy++) {
                    [unroll]
                    for (int ox = -1; ox <= 1; ox++) {
                        int2 tap = clamp(int2(pixel) + int2(ox, oy) * 2, int2(0), int2(pc.renderExtent) - int2(1));
                        uint4 tapGt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(tap, 0));
                        if ((tapGt1.w & 1u) == 0u) {
                            continue;
                        }
                        float2 tapMvUv = UnpackMotionVectors(tapGt1.g) * 0.5;
                        neighborhoodMotionPixels = max(neighborhoodMotionPixels, length((tapMvUv - staticDeltaUv) * float2(pc.renderExtent)));
                    }
                }
                // Neighborhood object-motion reset: anything moving nearby caps history so indirect shadowing re-converges instead of trailing the mover
                if (neighborhoodMotionPixels > GI_UPSCALE_NEIGHBORHOOD_MOTION_PIXELS) {
                    n = min(n, GI_UPSCALE_MOVING_MAX_ACCUM);
                }
            }

            if (bHasCurrent) {
                result = (n * history.color + current) / (n + 1.0);
                accum = n + 1.0;
            } else {
                result = history.color;
                accum = max(n, 1.0);
            }
        }
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(result, accum);
}
