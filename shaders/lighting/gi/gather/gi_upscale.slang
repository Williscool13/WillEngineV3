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

// Full-res resolve: bilateral upsample of the gather SH from the pixel's nearest 2x2 gather-res footprint (the own texel is always among the taps), evaluated at the pixel normal, then counter temporal New=(n*Prev+Curr)/(n+1) via gbuffer MV reprojection.

static const float GI_UPSCALE_PLANE_DIST_SIGMA = 0.01;
static const float GI_UPSCALE_NORMAL_POWER = 32.0;
static const float GI_UPSCALE_NEIGHBORHOOD_MOTION_PIXELS = 1.5;
static const float GI_UPSCALE_MOVING_MAX_ACCUM = 4.0;
static const float GI_UPSCALE_CLAMP_MIN_HISTORY = 4.0;
static const float GI_UPSCALE_CLAMP_DEV_SCALE = 4.0;
static const float GI_UPSCALE_CLAMP_RELATIVE = 1.0;
static const float GI_UPSCALE_CLAMP_PRIOR_SCALE = 0.5;
static const float GI_UPSCALE_CLAMP_EPS = 4e-3;
static const float GI_UPSCALE_MOMENTS_LERP = 0.25;
static const float GI_UPSCALE_CLAMP_TEMPORAL_SCALE = 3.0;
static const float GI_UPSCALE_CHANGE_SENSITIVITY = 0.05;
static const float GI_UPSCALE_HISTORY_FIX_FRAMES = 8.0;
static const float GI_UPSCALE_HISTORY_FIX_STRIDE = 8.0;
static const float GI_UPSCALE_SUPPORT_FULL = 2.0;

/** Stable world-tier irradiance (sky + DDGI cascades + reflection probe) at the pixel's own surface; the fallback when the half-res gather field cannot represent it. */
static float3 GIUpscaleWorldIrradiance(GIUpscalePushConstant pc, SceneData scene, float3 worldPos, float3 evalN, float3 centerXv)
{
    float3 irr = float3(0.0);
    if (pc.skyboxIndex >= 0) {
        irr = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], evalN) * pc.iblIntensity;
    }
    float giW = 0.0;
    if (pc.bCascadesValid != 0) {
        float3 cameraDirection = normalize(mul((float3x3)scene.invView, centerXv));
        float edgeFade;
        float4 gi = DDGISampleIrradianceCascaded(worldPos, evalN, cameraDirection, pc.ddgiCascades, length(centerXv), edgeFade);
        irr = gi.rgb * gi.w + irr * (1.0 - max(gi.w, edgeFade));
        giW = gi.w;
    }
    if (pc.reflectionProbeCount > 0u) {
        ReflectionProbeBlend probeBlend = PickReflectionProbeBinned(worldPos, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, scene.cameraWorldPos.xyz);
        if (probeBlend.indexA >= 0) {
            float3 probeIrr = SampleReflectionProbeIrradianceBlended(probeBlend, pc.reflectionProbes, evalN);
            irr = lerp(irr, probeIrr, probeBlend.coverage * (1.0 - giW));
        }
    }
    return irr;
}

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
        // The bilinear moments fetch touches these texels at geometry/sky boundaries, and aliased RDG memory is never zeroed.
        rdgStorageFloat2[pc.momentsIndex][pixel] = float2(0.0);
        if (pc.debugPathIndex != ~0x0u) {
            rdgStorageFloat4[pc.debugPathIndex][pixel] = float4(0.0);
        }
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

    // World-space variance prior from the DDGI probes (via the gather's half-res writeout): predicted relative deviation of a 1spp cosine estimate at this surface.
    // Unlike the temporal moments below it does not reset on motion/disocclusion and is not estimated from the noisy signal itself; 0 where unavailable = current behavior.
    float priorRelDev = 0.0;
    if (pc.varGuideIndex != ~0x0u) {
        priorRelDev = rdgTexturesFloat[pc.varGuideIndex].Load(int3(pixel / pc.gatherScale, 0)).r * GI_GATHER_VARIANCE_GUIDE_SCALE;
    }

    // Restores the sky the gather SH no longer carries: filtered miss fraction times prefiltered sky irradiance.
    float3 skyTerm = float3(0.0);
    if (pc.skyVisIndex != ~0x0u && pc.skyboxIndex >= 0) {
        skyTerm = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], evalN) * pc.iblIntensity;
    }

    float4 shR = float4(0.0);
    float4 shG = float4(0.0);
    float4 shB = float4(0.0);
    float sumW = 0.0;
    float sumV = 0.0;
    float4 fbShR = float4(0.0);
    float4 fbShG = float4(0.0);
    float4 fbShB = float4(0.0);
    float fbW = 0.0;
    float fbV = 0.0;
    float3 irrM1 = float3(0.0);
    float3 irrM2 = float3(0.0);
    float clampTaps = 0.0;
    float2 gatherCoord = (float2(pixel) + 0.5) / float(pc.gatherScale);
    int2 footprintBase = int2(floor(gatherCoord - 0.5));
    float2 footprintFrac = gatherCoord - 0.5 - float2(footprintBase);
    bool bAnyBackfaceTap = false;
    [unroll]
    for (uint i = 0; i < 4; i++) {
        int2 tap = clamp(footprintBase + int2(int(i & 1u), int(i >> 1u)), int2(0), int2(pc.gatherExtent) - 1);
        uint2 tapFull = GIGatherRepresentativePixel(uint2(tap), pc.renderExtent, pc.gatherScale);
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
        float tapV = pc.skyVisIndex != ~0x0u ? rdgTexturesFloat[pc.skyVisIndex].Load(int3(tap, 0)).r : 0.0;
        // Clamp moments must see the same total the blend produces, or the box drags history toward the sky-less SH.
        float3 tapIrr = max(float3(tapShR.x + dot(tapShR.yzw, evalN), tapShG.x + dot(tapShG.yzw, evalN), tapShB.x + dot(tapShB.yzw, evalN)), float3(0.0)) + tapV * skyTerm;
        irrM1 += tapIrr;
        irrM2 += tapIrr * tapIrr;
        clampTaps += 1.0;
        shR += tapShR * w;
        shG += tapShG * w;
        shB += tapShB * w;
        sumW += w;
        sumV += tapV * w;
        float2 bw2 = lerp(1.0 - footprintFrac, footprintFrac, float2(float(i & 1u), float(i >> 1u)));
        float bw = bw2.x * bw2.y;
        fbShR += tapShR * bw;
        fbShG += tapShG * bw;
        fbShB += tapShB * bw;
        fbW += bw;
        fbV += tapV * bw;
    }

    float3 worldPos = mul(scene.invView, float4(centerXv, 1.0)).xyz;

    bool bHasCurrent = true;
    float3 current;
    uint debugPath = 0u;
    float debugSupport = 1.0;
    if (sumW > 1e-4) {
        float invW = 1.0 / sumW;
        current = max(float3(shR.x + dot(shR.yzw, evalN), shG.x + dot(shG.yzw, evalN), shB.x + dot(shB.yzw, evalN)) * invW, float3(0.0)) + sumV * invW * skyTerm;
        debugPath = 1u;
    } else if (fbW > 1e-4) {
        // All taps guide-rejected but valid geometry exists (sub-half-res features: slots, trim): bilinear over the valid taps only. A single best tap is quad-constant and shows as 2x2 blocks; bilinear is continuous, and backface/sky texels never enter (validity-gated above).
        float invW = 1.0 / fbW;
        current = max(float3(fbShR.x + dot(fbShR.yzw, evalN), fbShG.x + dot(fbShG.yzw, evalN), fbShB.x + dot(fbShB.yzw, evalN)) * invW, float3(0.0)) + fbV * invW * skyTerm;
        debugPath = 2u;
    } else if (!bAnyBackfaceTap) {
        current = GIUpscaleWorldIrradiance(pc, scene, worldPos, evalN, centerXv);
        debugPath = 3u;
    } else {
        current = float3(0.0);
        bHasCurrent = false;
        debugPath = 4u;
    }

    GIGatherHistory history = { float3(0.0), 0.0, 0.0 };
    if (pc.bHistoryValid != 0u) {
        float NoV = saturate(abs(dot(centerNormal, normalize(centerXv))));
        history = GIGatherFetchHistory(scene, pc.historyIndex, pc.depthHistoryIndex, pc.gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, pc.renderExtent);
    }

    float2 tileMotion = rdgTexturesFloat2[pc.motionTileIndex].Load(int3(pixel / uint(GI_MOTION_TILE_SIZE), 0)).rg;
    float neighborhoodMotionPixels = length(tileMotion * float2(pc.renderExtent));

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
            int2 gatherCenter = int2(pixel / pc.gatherScale);
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

                    uint2 tapFull = GIGatherRepresentativePixel(uint2(tap), pc.renderExtent, pc.gatherScale);
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
                    float3 fixIrr = max(float3(tapShR.x + dot(tapShR.yzw, evalN), tapShG.x + dot(tapShG.yzw, evalN), tapShB.x + dot(tapShB.yzw, evalN)), float3(0.0));
                    if (pc.skyVisIndex != ~0x0u) {
                        fixIrr += rdgTexturesFloat[pc.skyVisIndex].Load(int3(tap, 0)).r * skyTerm;
                    }
                    fixSum += fixIrr * w;
                    fixW += w;
                }
            }
            current = fixSum / fixW;

            // Thin geometry (railings, trim): nothing else lies on the surface, so the footprint and fix taps reject, jitter keeps history from accumulating, and the pixel shows raw 1spp and boils. Trade precision for stability with the world tiers.
            float support = max(saturate((sumW + fixW - 1.0) / GI_UPSCALE_SUPPORT_FULL), nBlend / GI_UPSCALE_HISTORY_FIX_FRAMES);
            debugSupport = support;
            if (support < 1.0) {
                current = lerp(GIUpscaleWorldIrradiance(pc, scene, worldPos, evalN, centerXv), current, support);
            }
        }
    }

    // Temporal luma moments: spatial dev over 4 correlated post-atrous taps underestimates true 1rpp deviation, so stationary noise is measured over time instead.
    float curLuma = Luma(current);
    float2 curMoments = float2(curLuma, curLuma * curLuma);
    float2 prevMoments = curMoments;
    bool bMoments = false;
    if (pc.bMomentsValid != 0u && history.quality > 0.0) {
        float2 momentsUv = uv + UnpackMotionVectors(gt1.g) * 0.5;
        if (all(momentsUv >= float2(0.0)) && all(momentsUv <= float2(1.0))) {
            prevMoments = rdgTexturesFloat2[pc.momentsHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], momentsUv, 0).rg;
            bMoments = true;
        }
    }
    // Deviation from PRE-update moments so this frame's change cannot mask itself.
    float temporalDev = sqrt(max(prevMoments.y - prevMoments.x * prevMoments.x, 0.0));
    rdgStorageFloat2[pc.momentsIndex][pixel] = bHasCurrent ? lerp(prevMoments, curMoments, GI_UPSCALE_MOMENTS_LERP) : prevMoments;

    float changeResponse = 0.0;
    if (bMoments && bHasCurrent) {
        float histLuma = Luma(history.color);
        float temporalChange = abs(histLuma - curLuma) / max(histLuma + curLuma, 1e-4);
        // Relative change against relative deviation: fires on genuine lighting change, stays quiet where the pixel is merely noisy.
        // The probe prior joins via max(): where incident light is intrinsically spiky, a proportionally larger swing is required before history is cut, so 1spp noise stops masquerading as change in dark/indirect-dominated regions. A genuine step still dwarfs both estimates and fires.
        float relDev = temporalDev / max(prevMoments.x, 1e-4);
        changeResponse = smoothstep(0.1, 1.0, GI_UPSCALE_CHANGE_SENSITIVITY * temporalChange / max(max(relDev, priorRelDev), 0.02));
        if (changeResponse > 0.0) {
            n = min(n, lerp(GI_GATHER_MAX_ACCUM - 1.0, GI_UPSCALE_MOVING_MAX_ACCUM, changeResponse));
            nBlend = n * history.quality;
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
                // A correlated noise frame collapses both spatial dev and the relative floor, letting the box snap converged history onto it; genuine change shrinks the temporal floor so the clamp still bites.
                devFloor = max(devFloor, float3(temporalDev * GI_UPSCALE_CLAMP_TEMPORAL_SCALE * lerp(1.0, 0.25, changeResponse)));
                // Probe-prior floor: predicted 1spp deviation scaled to the local level, so the box cannot snap converged history onto a noisy current in intrinsically high-variance regions. Half scale because the spatial denoise has already shrunk current's deviation below the raw prior; shares the changeResponse shrink so detected change still clamps.
                devFloor = max(devFloor, float3(Luma(mean) * priorRelDev * GI_UPSCALE_CLAMP_PRIOR_SCALE * lerp(1.0, 0.25, changeResponse)));
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

    if (pc.debugPathIndex != ~0x0u) {
        float3 tint = debugPath == 1u ? (sumW < 0.1 ? float3(1.0, 0.1, 0.1) : float3(0.1, 0.6, 0.1)) : debugPath == 2u ? float3(1.0, 0.1, 1.0) : debugPath == 3u ? float3(0.1, 0.8, 1.0) : float3(1.0, 0.9, 0.1);
        if (debugSupport < 1.0) {
            tint = float3(1.0, 0.5, 0.1);
        }
        rdgStorageFloat4[pc.debugPathIndex][pixel] = float4(tint * lerp(0.15, 1.0, 1.0 / (1.0 + nBlend)), 1.0);
    }
}

groupshared float g_motionLenSq[GI_MOTION_TILE_SIZE * GI_MOTION_TILE_SIZE];
groupshared float2 g_motion[GI_MOTION_TILE_SIZE * GI_MOTION_TILE_SIZE];

// Per-tile max object motion from the shared object_motion extract (UV domain). Dilated by the motion blur neighbor-max pass, consumed by the upscale's history cap.
[shader("compute")]
[numthreads(GI_MOTION_TILE_SIZE, GI_MOTION_TILE_SIZE, 1)]
void ComputeGIMotionTileMax(uniform GIMotionTileMaxPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    uint2 pixel = gid.xy * uint(GI_MOTION_TILE_SIZE) + gtid.xy;

    float2 objectMotion = float2(0.0);
    if (all(pixel < pc.renderExtent)) {
        objectMotion = rdgTexturesFloat4[pc.objectMotionIndex].Load(int3(pixel, 0)).rg;
    }

    g_motionLenSq[groupIndex] = dot(objectMotion, objectMotion);
    g_motion[groupIndex] = objectMotion;
    GroupMemoryBarrierWithGroupSync();

    for (uint cutoff = uint(GI_MOTION_TILE_SIZE * GI_MOTION_TILE_SIZE) >> 1; cutoff > 0; cutoff >>= 1) {
        if (groupIndex < cutoff) {
            if (g_motionLenSq[groupIndex + cutoff] > g_motionLenSq[groupIndex]) {
                g_motionLenSq[groupIndex] = g_motionLenSq[groupIndex + cutoff];
                g_motion[groupIndex] = g_motion[groupIndex + cutoff];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        rdgStorageFloat2[pc.tileMaxIndex][gid.xy] = g_motion[0];
    }
}
