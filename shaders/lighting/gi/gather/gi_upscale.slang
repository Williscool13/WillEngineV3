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

// Full-res resolve: bilateral upsample of the gather SH from the pixel's nearest 2x2 gather-res footprint (the own texel is always among the taps), evaluated at the pixel normal, then counter temporal New=(n*Prev+Curr)/(n+1) via gbuffer MV reprojection. Also updates the short fast-luma history that gi_temporal_clamp bounds this result against.

static const float GI_UPSCALE_PLANE_DIST_SIGMA = 0.01;
static const float GI_UPSCALE_NORMAL_POWER = 32.0;
static const float GI_UPSCALE_HISTORY_FIX_FRAMES = 8.0;
static const float GI_UPSCALE_HISTORY_FIX_STRIDE = 8.0;
static const float GI_UPSCALE_FAST_ACCUM = 6.0;
// Visibility does not change with lighting (sky light is applied after), so it can average far longer than the radiance without lagging light changes.
static const float GI_UPSCALE_SKY_VIS_MAX_ACCUM = 64.0;
static const float GI_UPSCALE_SKY_VIS_FIX_FRAMES = 16.0;
static const float GI_UPSCALE_SKY_VIS_FIX_STRIDE = 12.0;
static const float GI_UPSCALE_PRIOR_FRAMES = 2.0;
// Probes miss the local enclosure of a disocclusion (slits behind furniture) and read too bright there.
static const float GI_UPSCALE_PRIOR_SCALE = 0.6;

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
    return irr * scene.preExposure;
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
        rdgStorageFloat2[pc.fastOutIndex][pixel] = float2(0.0);
        rdgStorageFloat2[pc.skyVisOutIndex][pixel] = float2(0.0);
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    const float historyRatio = scene.preExposure / scene.prevPreExposure;
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

    // Restores the sky the gather SH no longer carries: filtered miss fraction times prefiltered sky irradiance.
    float3 skyTerm = float3(0.0);
    if (pc.skyVisIndex != ~0x0u && pc.skyboxIndex >= 0) {
        skyTerm = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], evalN) * pc.iblIntensity * scene.preExposure;
    }

    float4 shR = float4(0.0);
    float4 shG = float4(0.0);
    float4 shB = float4(0.0);
    float3 mean = float3(0.0);
    float sumW = 0.0;
    float sumV = 0.0;
    float4 fbShR = float4(0.0);
    float4 fbShG = float4(0.0);
    float4 fbShB = float4(0.0);
    float3 fbMean = float3(0.0);
    float fbW = 0.0;
    float fbV = 0.0;
    float2 gatherCoord = (float2(pixel) + 0.5) / float(pc.gatherScale);
    int2 footprintBase = int2(floor(gatherCoord - 0.5));
    float2 footprintFrac = gatherCoord - 0.5 - float2(footprintBase);
    bool bAnyBackfaceTap = false;
    float2 footprintUv = float2(footprintBase + 1) / float2(pc.gatherExtent);
    uint4 footprintNormals = rdgTexturesUInt2[pc.guideIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], footprintUv).wzxy;
    uint4 footprintViewZBits = rdgTexturesUInt2[pc.guideIndex].GatherGreen(rdgSamplers[RDG_POINT_SAMPLER_INDEX], footprintUv).wzxy;
    float4 footprintTiers = rdgTexturesFloat2[pc.dataIndex].GatherGreen(rdgSamplers[RDG_POINT_SAMPLER_INDEX], footprintUv).wzxy;
    [unroll]
    for (uint i = 0; i < 4; i++) {
        int2 tap = clamp(footprintBase + int2(int(i & 1u), int(i >> 1u)), int2(0), int2(pc.gatherExtent) - 1);
        uint2 tapFull = GIGatherRepresentativePixel(uint2(tap), pc.renderExtent, pc.gatherScale);
        uint2 tapGuide = uint2(footprintNormals[i], footprintViewZBits[i]);
        float tapTier = footprintTiers[i];
        if (asfloat(tapGuide.y) == 0.0) { continue; }
        if (uint(tapTier + 0.5) == GI_GATHER_TIER_BACKFACE) { bAnyBackfaceTap = true; continue; }

        float tapViewZ = asfloat(tapGuide.y);
        float2 tapUv = (float2(tapFull) + 0.5) / float2(pc.renderExtent);
        float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, tapViewZ);
        float3 tapNormal = UnpackNormalOct16(tapGuide.x);

        float2 bw2 = lerp(1.0 - footprintFrac, footprintFrac, float2(float(i & 1u), float(i >> 1u)));
        float bw = bw2.x * bw2.y;
        float w = bw * exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
        w *= pow(saturate(dot(centerNormal, tapNormal)), GI_UPSCALE_NORMAL_POWER);

        float4 tapShR = rdgTexturesFloat4[pc.shRIndex].Load(int3(tap, 0));
        float4 tapShG = rdgTexturesFloat4[pc.shGIndex].Load(int3(tap, 0));
        float4 tapShB = rdgTexturesFloat4[pc.shBIndex].Load(int3(tap, 0));
        float4 tapMeanV = rdgTexturesFloat4[pc.skyVisIndex].Load(int3(tap, 0));
        float tapV = tapMeanV.a;
        shR += tapShR * w;
        shG += tapShG * w;
        shB += tapShB * w;
        mean += tapMeanV.rgb * w;
        sumW += w;
        sumV += tapV * w;
        fbShR += tapShR * bw;
        fbShG += tapShG * bw;
        fbShB += tapShB * bw;
        fbMean += tapMeanV.rgb * bw;
        fbW += bw;
        fbV += tapV * bw;
    }

    bool bHasCurrent = true;
    bool bSkyCurrent = false;
    float skyVisCurrent = 0.0;
    float3 current;
    if (sumW > 1e-4) {
        float invW = 1.0 / sumW;
        current = GIGatherEvaluate(shR, shG, shB, mean * invW, worldN, evalN);
        skyVisCurrent = sumV * invW;
        bSkyCurrent = true;
    } else if (fbW > 1e-4) {
        // All taps guide-rejected but valid geometry exists (sub-half-res features: slots, trim): bilinear over the valid taps only. A single best tap is quad-constant and shows as 2x2 blocks; bilinear is continuous, and backface/sky texels never enter (validity-gated above).
        float invW = 1.0 / fbW;
        current = GIGatherEvaluate(fbShR, fbShG, fbShB, fbMean * invW, worldN, evalN);
        skyVisCurrent = fbV * invW;
        bSkyCurrent = true;
    } else if (!bAnyBackfaceTap) {
        current = GIUpscaleWorldIrradiance(pc, scene, mul(scene.invView, float4(centerXv, 1.0)).xyz, evalN, centerXv);
    } else {
        current = float3(0.0);
        bHasCurrent = false;
    }

    GIGatherHistory history = { float3(0.0), 0.0, 0.0 };
    GIGatherScalarHistory fastHistory = { 0.0, 0.0, 0.0 };
    GIGatherScalarHistory skyVisHistory = { 0.0, 0.0, 0.0 };
    if (pc.bHistoryValid != 0u) {
        float NoV = saturate(abs(dot(centerNormal, normalize(centerXv))));
        GIGatherHistoryTaps taps = GIGatherResolveHistoryTaps(scene, pc.depthIndex, pc.depthHistoryIndex, pc.gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, pc.renderExtent);
        history = GIGatherFetchHistory(taps, pc.historyIndex, pc.renderExtent);
        history.color *= historyRatio;
        if (pc.fastHistoryIndex != ~0x0u) {
            fastHistory = GIGatherFetchScalarHistory(taps, pc.fastHistoryIndex);
            fastHistory.value *= historyRatio;
            fastHistory.count *= taps.countScale;
        }
        if (pc.skyVisHistoryIndex != ~0x0u) {
            skyVisHistory = GIGatherFetchScalarHistory(taps, pc.skyVisHistoryIndex);
        }
    }

    float n = min(history.count, (GI_GATHER_MAX_ACCUM - 1.0) * scene.framerateScale);
    float nBlend = n * history.quality;

    // NRD HistoryFix: young pixels trade spatial resolution for variance.
    if (bHasCurrent && nBlend < GI_UPSCALE_HISTORY_FIX_FRAMES) {
        int stride = int(round(GI_UPSCALE_HISTORY_FIX_STRIDE / (1.0 + nBlend)));
        if (stride >= 1) {
            int2 gatherCenter = int2(pixel / pc.gatherScale);
            float3 fixSum = current;
            float fixV = skyVisCurrent;
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
                    if (w <= 1e-4) { continue; }

                    float4 tapShR = rdgTexturesFloat4[pc.shRIndex].Load(int3(tap, 0));
                    float4 tapShG = rdgTexturesFloat4[pc.shGIndex].Load(int3(tap, 0));
                    float4 tapShB = rdgTexturesFloat4[pc.shBIndex].Load(int3(tap, 0));
                    float4 tapMeanV = rdgTexturesFloat4[pc.skyVisIndex].Load(int3(tap, 0));
                    fixSum += GIGatherEvaluate(tapShR, tapShG, tapShB, tapMeanV.rgb, worldN, evalN) * w;
                    fixV += tapMeanV.a * w;
                    fixW += w;
                }
            }
            if (bSkyCurrent) {
                current = fixSum / fixW;
                skyVisCurrent = fixV / fixW;
            } else {
                current = (fixSum + fixV * skyTerm) / fixW;
            }
        }
    }
    float nV = min(skyVisHistory.count, (GI_UPSCALE_SKY_VIS_MAX_ACCUM - 1.0) * scene.framerateScale) * skyVisHistory.quality;

    // Young visibility against a 64-frame neighbourhood shows as a noisy trail (disocclusion, jittered edges); widen its own fix until the history catches up. Taps are pre-blurred +-6 texels, so stride 12 still tiles.
    if (bSkyCurrent && nV < GI_UPSCALE_SKY_VIS_FIX_FRAMES) {
        int stride = int(round(GI_UPSCALE_SKY_VIS_FIX_STRIDE / (1.0 + nV)));
        if (stride >= 1) {
            int2 gatherCenter = int2(pixel / pc.gatherScale);
            float fixV = skyVisCurrent;
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
                    if (w <= 1e-4) { continue; }

                    fixV += rdgTexturesFloat4[pc.skyVisIndex].Load(int3(tap, 0)).a * w;
                    fixW += w;
                }
            }
            skyVisCurrent = fixV / fixW;
        }
    }
    float skyVisAccum = skyVisHistory.value;
    float skyVisCount = max(nV, 1.0);
    if (bSkyCurrent) {
        skyVisAccum = (nV * skyVisHistory.value + skyVisCurrent) / (nV + 1.0);
        skyVisCount = nV + 1.0;
        current += skyVisAccum * skyTerm;
    }
    rdgStorageFloat2[pc.skyVisOutIndex][pixel] = float2(skyVisAccum, skyVisCount);

    float3 result = current;
    float accum = 1.0;
    if (history.quality <= 0.0 && bSkyCurrent) {
        // Disocclusion (chair slits) has no same-surface neighbours to filter with; seed from the probe field as a few frames of prior that the counter then averages out.
        float3 prior = GIUpscaleWorldIrradiance(pc, scene, mul(scene.invView, float4(centerXv, 1.0)).xyz, evalN, centerXv) * GI_UPSCALE_PRIOR_SCALE;
        result = (GI_UPSCALE_PRIOR_FRAMES * prior + current) / (GI_UPSCALE_PRIOR_FRAMES + 1.0);
        accum = GI_UPSCALE_PRIOR_FRAMES + 1.0;
    } else if (history.quality > 0.0) {
        if (bHasCurrent) {
            result = (nBlend * history.color + current) / (nBlend + 1.0);
            accum = n + 1.0;
        } else {
            result = history.color;
            accum = max(n, 1.0);
        }
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(result, accum);

    float nFast = min(fastHistory.count, (GI_UPSCALE_FAST_ACCUM - 1.0) * scene.framerateScale) * fastHistory.quality;
    float fastLuma = fastHistory.value;
    float fastCount = max(nFast, 1.0);
    if (bHasCurrent) {
        fastLuma = (nFast * fastHistory.value + Luma(current)) / (nFast + 1.0);
        fastCount = nFast + 1.0;
    }
    rdgStorageFloat2[pc.fastOutIndex][pixel] = float2(fastLuma, fastCount);
}
