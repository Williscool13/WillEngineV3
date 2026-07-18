import common_interop;
import push_constant_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;
import pbr_functions;
import gi_gather_common;
import ddgi_common;

// Full-res resolve: bilateral upsample of the gather SH from the pixel's nearest 2x2 half-res footprint (the own texel is always among the taps), evaluated at the pixel normal, then counter temporal New=(n*Prev+Curr)/(n+1) via gbuffer MV reprojection.

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

    float3 evalN = worldN;
    if (pc.bentNormalIndex != ~0x0u) {
        evalN = normalize(mul((float3x3)scene.invView, UnpackNormalOct16(rdgTexturesUInt[pc.bentNormalIndex].Load(int3(pixel, 0)))));
    }
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float centerViewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ);
    float invPlaneSigma = 1.0 / (GI_UPSCALE_PLANE_DIST_SIGMA * abs(centerViewZ));
    float centerAO = pc.aoIndex != ~0x0u ? rdgTexturesFloat2[pc.aoIndex].Load(int3(pixel, 0)).y : 1.0;

    float4 shR = float4(0.0);
    float4 shG = float4(0.0);
    float4 shB = float4(0.0);
    float sumW = 0.0;
    float4 fbShR = float4(0.0);
    float4 fbShG = float4(0.0);
    float4 fbShB = float4(0.0);
    float fbW = 0.0;
    float3 irrM1 = float3(0.0);
    float3 irrM2 = float3(0.0);
    float clampTaps = 0.0;
    float2 halfCoord = (float2(pixel) + 0.5) * 0.5;
    int2 footprintBase = int2(floor(halfCoord - 0.5));
    float2 footprintFrac = halfCoord - 0.5 - float2(footprintBase);
    bool bAnyBackfaceTap = false;
    [unroll]
    for (uint i = 0; i < 4; i++) {
        int2 tap = clamp(footprintBase + int2(int(i & 1u), int(i >> 1u)), int2(0), int2(pc.gatherExtent) - 1);
        uint2 tapFull = GIGatherRepresentativePixel(uint2(tap), pc.renderExtent);
        uint2 tapGuide = rdgTexturesUInt2[pc.guideIndex].Load(int3(tap, 0)).xy;
        float tapTier = rdgTexturesFloat2[pc.dataIndex].Load(int3(tap, 0)).y;
        if (asfloat(tapGuide.y) == 0.0) { continue; }
        if (uint(tapTier + 0.5) == GI_GATHER_TIER_BACKFACE) { bAnyBackfaceTap = true; continue; }

        float tapViewZ = asfloat(tapGuide.y);
        float2 tapUv = (float2(tapFull) + 0.5) / float2(pc.renderExtent);
        float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, tapViewZ);
        float3 tapNormal = UnpackNormalOct16(tapGuide.x);

        float w = exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
        w *= pow(saturate(dot(centerNormal, tapNormal)), GI_UPSCALE_NORMAL_POWER);
        if (pc.aoIndex != ~0x0u) {
            float tapAO = rdgTexturesFloat2[pc.aoIndex].Load(int3(tapFull, 0)).y;
            w *= exp2(-20.0 * abs(tapAO - centerAO));
        }

        float4 tapShR = rdgTexturesFloat4[pc.shRIndex].Load(int3(tap, 0));
        float4 tapShG = rdgTexturesFloat4[pc.shGIndex].Load(int3(tap, 0));
        float4 tapShB = rdgTexturesFloat4[pc.shBIndex].Load(int3(tap, 0));
        float3 tapIrr = max(float3(tapShR.x + dot(tapShR.yzw, evalN), tapShG.x + dot(tapShG.yzw, evalN), tapShB.x + dot(tapShB.yzw, evalN)), float3(0.0));
        irrM1 += tapIrr;
        irrM2 += tapIrr * tapIrr;
        clampTaps += 1.0;
        shR += tapShR * w;
        shG += tapShG * w;
        shB += tapShB * w;
        sumW += w;
        float2 bw2 = lerp(1.0 - footprintFrac, footprintFrac, float2(float(i & 1u), float(i >> 1u)));
        float bw = bw2.x * bw2.y;
        fbShR += tapShR * bw;
        fbShG += tapShG * bw;
        fbShB += tapShB * bw;
        fbW += bw;
    }

    float3 worldPos = mul(scene.invView, float4(centerXv, 1.0)).xyz;

    bool bHasCurrent = true;
    float3 current;
    if (sumW > 1e-4) {
        float invW = 1.0 / sumW;
        current = max(float3(shR.x + dot(shR.yzw, evalN), shG.x + dot(shG.yzw, evalN), shB.x + dot(shB.yzw, evalN)) * invW, float3(0.0));
    } else if (fbW > 1e-4) {
        // All taps guide-rejected but valid geometry exists (sub-half-res features: slots, trim): bilinear over the valid taps only. A single best tap is quad-constant and shows as 2x2 blocks; bilinear is continuous, and backface/sky texels never enter (validity-gated above).
        float invW = 1.0 / fbW;
        current = max(float3(fbShR.x + dot(fbShR.yzw, evalN), fbShG.x + dot(fbShG.yzw, evalN), fbShB.x + dot(fbShB.yzw, evalN)) * invW, float3(0.0));
    } else if (!bAnyBackfaceTap) {
        current = float3(0.0);
        if (pc.skyboxIndex >= 0) {
            current = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], evalN) * pc.iblIntensity;
        }
        if (pc.bCascadesValid != 0) {
            float3 cameraDirection = normalize(mul((float3x3)scene.invView, centerXv));
            float edgeFade;
            float4 gi = DDGISampleIrradianceCascaded(worldPos, evalN, cameraDirection, pc.ddgiCascades, length(centerXv), edgeFade);
            current = gi.rgb * gi.w + current * (1.0 - max(gi.w, edgeFade));
        }
    } else {
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

            float neighborhoodMotionPixels = 0.0;
            [unroll]
            for (int oy = -1; oy <= 1; oy++) {
                [unroll]
                for (int ox = -1; ox <= 1; ox++) {
                    bool bCenterTap = ox == 0 && oy == 0;
                    int2 tap = clamp(int2(pixel) + int2(ox, oy) * 2, int2(0), int2(pc.renderExtent) - int2(1));
                    uint4 tapGt1 = bCenterTap ? gt1 : rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(tap, 0));
                    if ((tapGt1.w & 1u) == 0u) {
                        continue;
                    }
                    float tapDepth = bCenterTap ? depth : rdgTexturesFloat[pc.depthIndex].Load(int3(tap, 0)).r;
                    if (tapDepth <= 0.0) {
                        continue;
                    }
                    float2 tapUv = (float2(tap) + 0.5) / float2(pc.renderExtent);
                    float tapViewZ = ScreenToViewSpaceDepth(tapDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                    float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, tapViewZ);
                    float4 tapPrevClip = mul(scene.prevViewProj, float4(mul(scene.invView, float4(tapXv, 1.0)).xyz, 1.0));
                    if (tapPrevClip.w <= 0.0) {
                        continue;
                    }

                    float2 tapStaticDeltaUv = ((tapPrevClip.xy / tapPrevClip.w) * 0.5 + 0.5) - tapUv - scene.jitter * 0.5 + scene.prevJitter * 0.5;
                    float2 tapMvUv = UnpackMotionVectors(tapGt1.g) * 0.5;
                    neighborhoodMotionPixels = max(neighborhoodMotionPixels, length((tapMvUv - tapStaticDeltaUv) * float2(pc.renderExtent)));
                }
            }
            // Neighborhood object-motion reset: anything moving nearby caps history so indirect shadowing re-converges instead of trailing the mover
            if (neighborhoodMotionPixels > GI_UPSCALE_NEIGHBORHOOD_MOTION_PIXELS) {
                n = min(n, GI_UPSCALE_MOVING_MAX_ACCUM);
            }

            if (bHasCurrent) {
                if (clampTaps > 1.5) {
                    float3 mean = irrM1 / clampTaps;
                    float3 dev = sqrt(max(irrM2 / clampTaps - mean * mean, float3(0.0)));

                    float devEps = pc.exposureTarget > 0.0 ? 4e-4 / ExposureNoiseScale(pc.exposureLuminance[0], pc.exposureTarget) : 4e-4;
                    float3 devFloor = max(dev * 4.0, abs(history.color) * 0.4 + float3(devEps));
                    float3 histDist = abs(history.color - current) / devFloor;
                    float3 closest = clamp(history.color, current - devFloor, current + devFloor);
                    history.color = lerp(history.color, closest, smoothstep(float3(1.0), float3(3.0), histDist));
                }
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
