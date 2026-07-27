import common_interop;
import push_constant_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;
import pbr_functions;
import gi_gather_common;
import ddgi_common;
import reflection_probe_interop;
import reflection_probe_common;

// Full-res resolve: bilateral upsample of the gather SH from the pixel's nearest 2x2 half-res footprint (the own texel is always among the taps), evaluated at the pixel normal, then counter temporal New=(n*Prev+Curr)/(n+1) via gbuffer MV reprojection.

static const float GI_UPSCALE_PLANE_DIST_SIGMA = 0.01;
static const float GI_UPSCALE_NORMAL_POWER = 32.0;
static const float GI_UPSCALE_NEIGHBORHOOD_MOTION_PIXELS = 1.5;
static const float GI_UPSCALE_MOVING_MAX_ACCUM = 4.0;
static const float GI_UPSCALE_CLAMP_MIN_HISTORY = 4.0;
static const float GI_UPSCALE_CLAMP_DEV_SCALE = 4.0;
static const float GI_UPSCALE_CLAMP_RELATIVE = 1.0;
static const float GI_UPSCALE_CLAMP_EPS = 4e-3;
static const float GI_UPSCALE_HISTORY_FIX_FRAMES = 8.0;
static const float GI_UPSCALE_HISTORY_FIX_STRIDE = 8.0;

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
        float giW = 0.0;
        if (pc.bCascadesValid != 0) {
            float3 cameraDirection = normalize(mul((float3x3)scene.invView, centerXv));
            float edgeFade;
            float4 gi = DDGISampleIrradianceCascaded(worldPos, evalN, cameraDirection, pc.ddgiCascades, length(centerXv), edgeFade);
            current = gi.rgb * gi.w + current * (1.0 - max(gi.w, edgeFade));
            giW = gi.w;
        }
        if (pc.reflectionProbeCount > 0u) {
            float probeFade;
            int probeIndex = PickReflectionProbeBinned(worldPos, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, scene.cameraWorldPos.xyz, probeFade);
            if (probeIndex >= 0) {
                ReflectionProbeGPU probe = pc.reflectionProbes[probeIndex];
                float3 probeIrr = SampleReflectionProbeIrradiance(probe, evalN) * probe.intensity;
                probeIrr *= lerp(1.0, min(1.0, Luma(current) / max(Luma(probeIrr), 1e-4)), giW);
                current = lerp(current, probeIrr, probeFade);
            }
        }
    } else {
        current = float3(0.0);
        bHasCurrent = false;
    }

    GIGatherHistory history = { float3(0.0), 0.0, 0.0 };
    if (pc.bHistoryValid != 0u) {
        float NoV = saturate(abs(dot(centerNormal, normalize(centerXv))));
        history = GIGatherFetchHistory(scene, pc.historyIndex, pc.depthHistoryIndex, pc.gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, pc.renderExtent);
    }

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
    float n = min(history.count, GI_GATHER_MAX_ACCUM - 1.0);
    if (neighborhoodMotionPixels > GI_UPSCALE_NEIGHBORHOOD_MOTION_PIXELS) {
        n = min(n, GI_UPSCALE_MOVING_MAX_ACCUM);
    }
    
    float nBlend = n * history.quality;

    // History fix: a pixel with little accumulation to lean on trades spatial resolution for variance, widening over the half-res SH with the same guides the footprint uses.
    // Gated on nBlend, not the stored count: since quality no longer feeds back, a chronically thin-quality reprojection edge carries count 32 while blending at nBlend < 2, and that is exactly the pixel that needs the fill.
    if (bHasCurrent && nBlend < GI_UPSCALE_HISTORY_FIX_FRAMES) {
        int stride = int(round(GI_UPSCALE_HISTORY_FIX_STRIDE / (1.0 + nBlend)));
        if (stride >= 1) {
            int2 gatherCenter = int2(pixel >> 1u);
            float3 fixSum = current;
            float fixW = 1.0;
            [unroll]
            for (int fy = -1; fy <= 1; fy++) {
                [unroll]
                for (int fx = -1; fx <= 1; fx++) {
                    if (fx == 0 && fy == 0) { continue; }
                    int2 tap = clamp(gatherCenter + int2(fx, fy) * stride, int2(0), int2(pc.gatherExtent) - 1);
                    uint2 tapGuide = rdgTexturesUInt2[pc.guideIndex].Load(int3(tap, 0)).xy;
                    if (asfloat(tapGuide.y) == 0.0) { continue; }
                    if (uint(rdgTexturesFloat2[pc.dataIndex].Load(int3(tap, 0)).y + 0.5) == GI_GATHER_TIER_BACKFACE) { continue; }

                    uint2 tapFull = GIGatherRepresentativePixel(uint2(tap), pc.renderExtent);
                    float2 tapUv = (float2(tapFull) + 0.5) / float2(pc.renderExtent);
                    float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, asfloat(tapGuide.y));
                    float w = exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
                    w *= pow(saturate(dot(centerNormal, UnpackNormalOct16(tapGuide.x))), GI_UPSCALE_NORMAL_POWER);
                    // Same AO guide the footprint taps use; at wide stride the plane/normal pair alone will happily reach across a contact or corner boundary.
                    if (pc.aoIndex != ~0x0u) {
                        w *= exp2(-20.0 * abs(rdgTexturesFloat2[pc.aoIndex].Load(int3(tapFull, 0)).y - centerAO));
                    }
                    if (w <= 1e-4) { continue; }

                    float4 tapShR = rdgTexturesFloat4[pc.shRIndex].Load(int3(tap, 0));
                    float4 tapShG = rdgTexturesFloat4[pc.shGIndex].Load(int3(tap, 0));
                    float4 tapShB = rdgTexturesFloat4[pc.shBIndex].Load(int3(tap, 0));
                    fixSum += max(float3(tapShR.x + dot(tapShR.yzw, evalN), tapShG.x + dot(tapShG.yzw, evalN), tapShB.x + dot(tapShB.yzw, evalN)), float3(0.0)) * w;
                    fixW += w;
                }
            }
            current = fixSum / fixW;
        }
    }

    float3 result = current;
    float accum = 1.0;
    if (history.quality > 0.0) {
        if (bHasCurrent) {
            // A short history cannot ghost, and min() keeps the relative floor from collapsing on 1-spp noise while still shrinking to the smaller side on a real step change.
            if (clampTaps > 1.5 && history.count > GI_UPSCALE_CLAMP_MIN_HISTORY) {
                float3 mean = irrM1 / clampTaps;
                float3 dev = sqrt(max(irrM2 / clampTaps - mean * mean, float3(0.0)));

                float3 relative = min(abs(history.color), abs(current)) * GI_UPSCALE_CLAMP_RELATIVE;
                float3 devFloor = max(dev * GI_UPSCALE_CLAMP_DEV_SCALE, relative + float3(GI_UPSCALE_CLAMP_EPS));
                float3 histDist = abs(history.color - current) / devFloor;
                float3 closest = clamp(history.color, current - devFloor, current + devFloor);
                history.color = lerp(history.color, closest, smoothstep(float3(1.0), float3(3.0), histDist));
            }
            result = (nBlend * history.color + current) / (nBlend + 1.0);
            accum = n + 1.0;
        } else {
            result = history.color;
            accum = max(n, 1.0);
        }
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(result, accum);
}
