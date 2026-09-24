module gi_gather_common;

import common_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;

public static const float GI_GATHER_MAX_ACCUM = 24.0;
public static const float GI_GATHER_DISOCCLUSION_SENSITIVITY = 0.01;
public static const float GI_GATHER_BACKFACE_GUARD_DISTANCE = 0.1;
public static const float GI_GATHER_REVEAL_FACING_SCALE = 2.0;

/** Trace and shade must agree (hitT is measured from here). Lifted along the view ray, free space for any visible pixel, far enough to clear the surface by two pixels; a normal lift crossed into surfaces in contact (chair feet through the floor). */
public float3 GIGatherRayOrigin(float3 worldPosition, float3 worldN, float3 toCamera, float pixelSize)
{
    return worldPosition + toCamera * (2.0 * pixelSize / max(abs(dot(worldN, toCamera)), 0.1)) + worldN * max(0.002, length(worldPosition) * 1e-5);
}

public static const uint GI_GATHER_TIER_SCREEN = 0u;
public static const uint GI_GATHER_TIER_CACHE = 1u;
public static const uint GI_GATHER_TIER_PROBE = 2u;
public static const uint GI_GATHER_TIER_SKY = 3u;
public static const uint GI_GATHER_TIER_BACKFACE = 4u;
public static const uint GI_GATHER_TIER_INVALID = 5u;
public static const uint GI_GATHER_TIER_BAKED = 6u;

/** The full-res pixel a gather texel represents; fixed to the quad's center so the sample sits where the upscale footprint expects the texel and the bias is direction-symmetric (the (0,0) corner biased left/up, which trailed bright edges behind rightward/downward reveals at quarter res). Deliberately not frame-cycled: cycling strobed depth edges every 4 frames; temporal supersampling lives in the ray RNG instead. */
public uint2 GIGatherRepresentativePixel(uint2 gatherPixel, uint2 renderExtent, uint gatherScale)
{
    return min(gatherPixel * gatherScale + (gatherScale >> 1u), renderExtent - 1u);
}

/** Bilinear-samples the half-res gather SH, evaluated at the pixel's world normal; returns indirectDiffuse (E/pi). */
public float3 GIGatherIndirectDiffuse(uint shRIndex, uint shGIndex, uint shBIndex, float2 uv, float3 worldN)
{
    float4 shR = rdgTexturesFloat4[shRIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
    float4 shG = rdgTexturesFloat4[shGIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
    float4 shB = rdgTexturesFloat4[shBIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
    return max(float3(shR.x + dot(shR.yzw, worldN), shG.x + dot(shG.yzw, worldN), shB.x + dot(shB.yzw, worldN)), float3(0.0));
}

public struct GIGatherHistory
{
    public float3 color;
    public float count;
    public float quality;
};

/** View-space geometric normal from a depth texture; each axis takes its shallower step so a silhouette neighbour never enters. */
public float3 GIGatherDepthNormal(SceneData scene, uint depthIndex, int2 px, uint2 renderExtent)
{
    float2 cornerUv = float2(px) / float2(renderExtent);
    float4 ul = rdgTexturesFloat[depthIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], cornerUv);
    float4 br = rdgTexturesFloat[depthIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], cornerUv, int2(1, 1));
    const float rawDepths[5] = { ul.y, ul.x, br.z, ul.z, br.x };
    const int2 offsets[5] = { int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
    float3 pos[5];
    [unroll]
    for (uint i = 0; i < 5; i++) {
        float viewZ = ScreenToViewSpaceDepth(rawDepths[i], scene.depthLinearizeMult, scene.depthLinearizeAdd);
        pos[i] = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, (float2(px + offsets[i]) + 0.5) / float2(renderExtent), viewZ);
    }
    float3 dx = abs(pos[0].z - pos[1].z) < abs(pos[2].z - pos[0].z) ? pos[0] - pos[1] : pos[2] - pos[0];
    float3 dy = abs(pos[0].z - pos[3].z) < abs(pos[4].z - pos[0].z) ? pos[0] - pos[3] : pos[4] - pos[0];
    return normalize(cross(dx, dy));
}

/** Per-tap reprojection gate shared by every full-res history fetch: plane-distance disocclusion (the compared |dz|*NoV already projects out slant, so the tolerance is flat) plus a normal test. bShadingNormalAgrees false = caller settles it on geometric normals. */
public bool GIGatherHistoryTapValid(SceneData scene, float prevRawDepth, uint prevPackedNormal, float expectedPrevViewZ, float NoV, float disocclusionThreshold, float3 worldN, out bool bShadingNormalAgrees)
{
    bShadingNormalAgrees = false;
    if (prevRawDepth <= 0.0) {
        return false;
    }
    float prevViewZ = ScreenToViewSpaceDepth(prevRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    if (abs(prevViewZ - expectedPrevViewZ) * NoV > disocclusionThreshold) {
        return false;
    }
    float3 prevWorldN = normalize(mul(transpose(float3x3(scene.prevView)), UnpackNormalOct16(prevPackedNormal)));
    // 60 degrees, matching the tier-0 screen gate: the plane test is blind to old surfaces CROSSING the current plane (buried pillar faces at ground level), and those arrive with perpendicular normals (dot ~ 0).
    bShadingNormalAgrees = dot(worldN, prevWorldN) >= 0.5;
    return true;
}

/** Flat plane-distance tolerance for GIGatherHistoryTapValid. The NRD-style 1/lerp(0.05,1,NoV) expansion inflated the accepted raw mismatch to ~2x viewZ at grazing (ground history never rejected); do not reintroduce it. */
public float GIGatherDisocclusionThreshold(SceneData scene, float centerViewZ)
{
    return GI_GATHER_DISOCCLUSION_SENSITIVITY * abs(scene.ndcToViewMul.y) * abs(centerViewZ);
}

public struct GIGatherHistoryTaps
{
    public int2 origin;
    public float2 prevPos;
    public float4 weights;
    public float weightSum;
    public float countScale;
    public float2 quadUv;
};

/** Reprojection and per-tap validity for a full-res history fetch, resolved once so several histories at the same pixel share the depth/normal gate. */
public GIGatherHistoryTaps GIGatherResolveHistoryTaps(SceneData scene, uint depthIndex, uint depthHistoryIndex, uint gbufferOneHistoryIndex, float2 uv, uint4 gt1, float centerViewZ, float NoV, float3 worldN, uint2 renderExtent)
{
    GIGatherHistoryTaps taps = { int2(0), float2(0.0), float4(0.0), 0.0, 1.0, float2(0.0) };

    float2 historyUv = uv + UnpackMotionVectors(gt1.g) * 0.5;
    if (any(historyUv < float2(0.0)) || any(historyUv > float2(1.0))) {
        return taps;
    }

    float expectedPrevViewZ = centerViewZ + UnpackViewZDelta(gt1.w);
    float disocclusionThreshold = GIGatherDisocclusionThreshold(scene, centerViewZ);

    float reveal = 1.0;
    float curFacing = dot(mul((float3x3)scene.view, worldN), -normalize(CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ)));
    if (curFacing > 0.0) {
        float prevFacing = dot(mul((float3x3)scene.prevView, worldN), -normalize(CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, historyUv, expectedPrevViewZ)));
        reveal = saturate(GI_GATHER_REVEAL_FACING_SCALE * prevFacing / curFacing);
        if (reveal <= 0.0) {
            return taps;
        }
    }

    taps.prevPos = historyUv * float2(renderExtent) - 0.5;
    taps.origin = int2(floor(taps.prevPos));
    float2 f = taps.prevPos - float2(taps.origin);
    float4 bilinearW = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    const int2 tapOffsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
    bool bGeoNormal = false;
    float3 geoWorldN = float3(0.0);
    taps.quadUv = float2(taps.origin + 1) / float2(renderExtent);
    float4 prevRawDepths = rdgTexturesFloat[depthHistoryIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], taps.quadUv).wzxy;
    uint4 prevPackedNormals = rdgTexturesUInt4[gbufferOneHistoryIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], taps.quadUv).wzxy;

    [unroll]
    for (uint i = 0; i < 4; i++) {
        if (bilinearW[i] <= 1e-4) {
            continue;
        }
        int2 tap = taps.origin + tapOffsets[i];
        if (any(tap < int2(0)) || any(tap >= int2(renderExtent))) {
            continue;
        }
        bool bShadingNormalAgrees;
        if (!GIGatherHistoryTapValid(scene, prevRawDepths[i], prevPackedNormals[i], expectedPrevViewZ, NoV, disocclusionThreshold, worldN, bShadingNormalAgrees)) {
            continue;
        }
        if (!bShadingNormalAgrees) {
            if (!bGeoNormal) {
                geoWorldN = mul((float3x3)scene.invView, GIGatherDepthNormal(scene, depthIndex, int2(uv * float2(renderExtent)), renderExtent));
                bGeoNormal = true;
            }
            float3 prevGeoWorldN = mul(transpose(float3x3(scene.prevView)), GIGatherDepthNormal(scene, depthHistoryIndex, tap, renderExtent));
            if (dot(geoWorldN, prevGeoWorldN) < 0.5) {
                continue;
            }
        }
        taps.weights[i] = bilinearW[i];
        taps.weightSum += bilinearW[i];
    }
    taps.countScale = reveal;
    return taps;
}

/** 2x2 bilinear history fetch over pre-resolved taps with weight renormalization. quality is the surviving bilinear weight; 0 means no usable history. */
public GIGatherHistory GIGatherFetchHistory(GIGatherHistoryTaps taps, uint historyIndex, uint2 renderExtent)
{
    GIGatherHistory result = { float3(0.0), 0.0, 0.0 };
    const int2 tapOffsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

    float3 colorSum = float3(0.0);
    float countSum = 0.0;
    [unroll]
    for (uint i = 0; i < 4; i++) {
        if (taps.weights[i] <= 0.0) {
            continue;
        }
        float4 history = rdgTexturesFloat4[historyIndex].Load(int3(taps.origin + tapOffsets[i], 0));
        colorSum += history.rgb * taps.weights[i];
        countSum += history.a * taps.weights[i];
    }

    if (taps.weightSum > 0.01) {
        float invW = 1.0 / taps.weightSum;
        result.color = colorSum * invW;
        result.count = countSum * invW * taps.countScale;
        result.quality = saturate(taps.weightSum);
    }
    return result;
}

/** MV-reprojected 2x2 bilinear history fetch with per-tap depth/backface validity and weight renormalization. */
public GIGatherHistory GIGatherFetchHistory(SceneData scene, uint historyIndex, uint depthIndex, uint depthHistoryIndex, uint gbufferOneHistoryIndex, float2 uv, uint4 gt1, float centerViewZ, float NoV, float3 worldN, uint2 renderExtent)
{
    return GIGatherFetchHistory(GIGatherResolveHistoryTaps(scene, depthIndex, depthHistoryIndex, gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, renderExtent), historyIndex, renderExtent);
}

public struct GIGatherScalarHistory
{
    public float value;
    public float count;
    public float quality;
};

/** GIGatherFetchHistory for a float2 (value, count) history such as the AO accumulator; bilinear only. count is returned as stored. */
public GIGatherScalarHistory GIGatherFetchScalarHistory(GIGatherHistoryTaps taps, uint historyIndex)
{
    GIGatherScalarHistory result = { 0.0, 0.0, 0.0 };

    float2 sum = float2(0.0);
    if (taps.weightSum > 0.01) {
        float4 values = rdgTexturesFloat2[historyIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], taps.quadUv).wzxy;
        float4 counts = rdgTexturesFloat2[historyIndex].GatherGreen(rdgSamplers[RDG_POINT_SAMPLER_INDEX], taps.quadUv).wzxy;
        [unroll]
        for (uint i = 0; i < 4; i++) {
            if (taps.weights[i] <= 0.0) {
                continue;
            }
            sum += float2(values[i], counts[i]) * taps.weights[i];
        }
    }

    if (taps.weightSum > 0.01) {
        result.value = sum.x / taps.weightSum;
        result.count = sum.y / taps.weightSum * taps.countScale;
        result.quality = saturate(taps.weightSum);
    }
    return result;
}

public GIGatherScalarHistory GIGatherFetchScalarHistory(SceneData scene, uint historyIndex, uint depthIndex, uint depthHistoryIndex, uint gbufferOneHistoryIndex, float2 uv, uint4 gt1, float centerViewZ, float NoV, float3 worldN, uint2 renderExtent)
{
    return GIGatherFetchScalarHistory(GIGatherResolveHistoryTaps(scene, depthIndex, depthHistoryIndex, gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, renderExtent), historyIndex);
}

/** Debug views: 2 = resolved irradiance, 3 = fallback tier (cyan screen / green cache / blue probe / yellow sky / red backface / magenta baked), 4 = hit distance, 5 = temporal accumulation count, 6 = first-ray escape (yellow sky miss, magenta backface, front-face hitT green-to-red over 2-10m). */
public float3 GIGatherDebugColor(uint dataIndex, uint resolvedIndex, uint2 pixel, uint mode, float3 indirectDiffuse, uint gatherScale)
{
    if (mode == 2u) {
        return indirectDiffuse;
    }
    if (mode == 5u) {
        return float3(rdgTexturesFloat4[resolvedIndex].Load(int3(pixel, 0)).a / GI_GATHER_MAX_ACCUM);
    }
    float2 data = rdgTexturesFloat2[dataIndex].Load(int3(pixel / gatherScale, 0));
    if (mode == 3u) {
        switch (uint(data.y)) {
            case 0u: return float3(0.1, 0.9, 0.9);
            case 1u: return float3(0.1, 0.9, 0.1);
            case 2u: return float3(0.15, 0.3, 1.0);
            case 3u: return float3(1.0, 0.9, 0.2);
            case 4u: return float3(1.0, 0.1, 0.1);
            case 6u: return float3(0.9, 0.2, 0.9);
            default: return float3(0.0);
        }
    }
    if (mode == 6u) {
        uint tier = uint(data.y);
        if (tier == 3u) {
            return float3(1.0, 0.9, 0.2);
        }
        if (tier == 4u) {
            return float3(1.0, 0.2, 0.9);
        }
        return lerp(float3(0.1, 0.5, 0.1), float3(1.0, 0.1, 0.1), saturate((data.x - 2.0) / 8.0));
    }
    return float3(data.x / 20.0);
}
