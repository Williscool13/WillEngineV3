import common_interop;
import push_constant_interop;
import ddgi_interop;
import ddgi_common;
import radiance_cache_interop;
import radiance_cache_hash_common;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;

// =============== Shader Summary ============
// GI leak deconstruction: per-pixel diagnostics at the primary surface, written to gi_deconstruct_target for the debug visualizer.
// Modes:
//   1 cache cell identity hash
//   2 cache radiance/servability
//   3 DDGI Chebyshev-gate weight fractions
//   4 dominant-probe mean-vs-distance margin
//   5 coverage/confidence/dominant volume (blue = descriptor index / 15, locals sit past the cascades)
//   6 raw DDGI irradiance.

static const uint GI_DECONSTRUCT_CACHE_KEY = 1u;
static const uint GI_DECONSTRUCT_CACHE_RADIANCE = 2u;
static const uint GI_DECONSTRUCT_DDGI_CHEB = 3u;
static const uint GI_DECONSTRUCT_DDGI_MARGIN = 4u;
static const uint GI_DECONSTRUCT_DDGI_COVERAGE = 5u;
static const uint GI_DECONSTRUCT_DDGI_IRRADIANCE = 6u;

static const float GI_DECONSTRUCT_SKY_MEAN_FRACTION = 0.95;

struct GICageStats
{
    float weightSum;
    float trueWeightSum;
    float wFired;
    float wSkyMean;
    float wClean;
    float margin;
    float stdRatio;
    float fade;
};

/** Mirrors DDGISampleIrradiance's weight chain (wrap, Chebyshev with its private 0.3 cap and 0.05 floor, crush, trilinear) but classifies each probe's weight by whether the occlusion gate ran, instead of fetching irradiance. */
static GICageStats GIDeconstructCage(float3 worldPosition, float3 normal, float3 cameraDirection, DDGIVolumeParams volume, uint visibilityAtlasIndex, float4* probeOffsets, uint bProbeOffsetsValid, float maxBias)
{
    GICageStats s;
    s.weightSum = 0.0;
    s.trueWeightSum = 0.0;
    s.wFired = 0.0;
    s.wSkyMean = 0.0;
    s.wClean = 0.0;
    s.margin = 0.0;
    s.stdRatio = 0.0;
    s.fade = 0.0;

    float3 biasOffset = normal * volume.normalBias - cameraDirection * volume.viewBias;
    float biasLength = length(biasOffset);
    if (biasLength > maxBias) {
        biasOffset *= maxBias / biasLength;
    }
    float3 biasedPosition = worldPosition + biasOffset;
    float3 gridCoord = biasedPosition / volume.probeSpacing;

    float3 edgeDistance = min(gridCoord - float3(volume.baseCell), float3(volume.baseCell + int3(volume.probeCount) - 1) - gridCoord);
    s.fade = saturate(min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) / max(volume.edgeFadeCells, 1.0));
    if (s.fade <= 0.0) {
        return s;
    }

    int3 baseCell = int3(floor(gridCoord));
    float3 alpha = saturate(gridCoord - float3(baseCell));
    float maxDistance = length(volume.probeSpacing) * 1.5;
    float maxWeight = 0.0;
    for (uint i = 0; i < 8; ++i) {
        int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 cell = baseCell + offset;
        if (any(cell < volume.baseCell) || any(cell >= volume.baseCell + int3(volume.probeCount))) {
            continue;
        }
        int3 slot = DDGISlotFromCell(cell, volume);
        float3 probePosition = DDGIProbeWorldPosition(cell, volume);
        if (bProbeOffsetsValid != 0) {
            float4 probeOffset = probeOffsets[DDGIProbeIndexFromSlot(slot, volume)];
            if (probeOffset.w > DDGI_PROBE_DEAD_BACKFACE_FRACTION) {
                continue;
            }
            probePosition += probeOffset.xyz;
        }

        float3 toProbe = normalize(probePosition - worldPosition);
        float wrap = (dot(toProbe, normal) + 1.0) * 0.5;
        float weight = wrap * wrap + 0.2;

        float3 biasedToProbe = probePosition - biasedPosition;
        float distanceToProbe = length(biasedToProbe);
        float2 moments = rdgTexturesFloat2[visibilityAtlasIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], DDGIAtlasUV(slot, -biasedToProbe / max(distanceToProbe, 1e-6), volume, DDGI_VISIBILITY_TILE, DDGI_VISIBILITY_INTERIOR), 0);
        bool bFired = distanceToProbe > moments.x;
        float chebRatio = 1.0;
        if (bFired) {
            float cappedStd = DDGI_VISIBILITY_VARIANCE_CAP * moments.x;
            float variance = min(abs(moments.x * moments.x - moments.y), cappedStd * cappedStd);
            float d = distanceToProbe - moments.x;
            float chebyshev = variance / (variance + d * d);
            float cheb3 = chebyshev * chebyshev * chebyshev;
            weight *= max(cheb3, 0.05);
            chebRatio = cheb3 / max(cheb3, 0.05);
        }

        weight = max(weight, 1e-6);
        const float crushThreshold = 0.2;
        if (weight < crushThreshold) {
            weight *= weight * weight / (crushThreshold * crushThreshold);
        }
        float3 trilinear = lerp(1.0 - alpha, alpha, float3(offset));
        weight *= max(trilinear.x * trilinear.y * trilinear.z, 0.001);

        s.weightSum += weight;
        s.trueWeightSum += weight * chebRatio;
        if (bFired) {
            s.wFired += weight;
        } else if (moments.x > GI_DECONSTRUCT_SKY_MEAN_FRACTION * maxDistance) {
            s.wSkyMean += weight;
        } else {
            s.wClean += weight;
        }
        if (weight > maxWeight) {
            maxWeight = weight;
            s.margin = (moments.x - distanceToProbe) / max(length(volume.probeSpacing), 1e-4);
            s.stdRatio = sqrt(abs(moments.y - moments.x * moments.x)) / max(moments.x, 1e-4);
        }
    }
    return s;
}

static float3 GIDeconstructKeyColor(uint2 packedKey)
{
    uint h = packedKey.x * 0x9E3779B9u ^ packedKey.y * 0x85EBCA77u;
    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    float3 c = float3(float(h & 1023u), float((h >> 10u) & 1023u), float((h >> 20u) & 1023u)) / 1023.0;
    return 0.12 + 0.88 * c;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeGIDeconstruct(uniform GIDeconstructPushConstant pc, uint3 dtid: SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (depth <= 0.0 || (gt1.w & 1u) == 0u) {
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 viewNormal = UnpackNormalOct16(gt1.r);
    float3 worldN = normalize(mul((float3x3)scene.invView, viewNormal));
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 worldPosition = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 cameraDirection = normalize(mul((float3x3)scene.invView, viewPosition));

    float3 color = float3(0.0);
    if (pc.mode == GI_DECONSTRUCT_CACHE_KEY || pc.mode == GI_DECONSTRUCT_CACHE_RADIANCE) {
        RadianceCacheKey key = RadianceCacheComputeKey(worldPosition, worldN, scene.cameraWorldPos.xyz);
        uint slot = RadianceCacheHashFindLodTolerant(pc.cacheEntries, key, worldPosition, worldN);
        if (slot != RADIANCE_CACHE_HASH_INVALID) {
            if (pc.mode == GI_DECONSTRUCT_CACHE_KEY) {
                color = GIDeconstructKeyColor(pc.cacheKeys[slot]);
            } else {
                uint2 packedRadiance = pc.cacheCells[slot].packedRadiance;
                uint cellCount = (pc.cacheCells[slot].changeStreak >> 16u) & 0xFFu;
                if (RadianceCacheRadianceShaded(packedRadiance) && cellCount >= 2u) {
                    color = RadianceCacheUnpackRadiance(packedRadiance);
                } else {
                    color = float3(0.5, 0.0, 0.5);
                }
            }
        }
    } else if (pc.bCascadesValid != 0) {
        if (pc.mode == GI_DECONSTRUCT_DDGI_IRRADIANCE) {
            float edgeFade;
            float4 gi = DDGISampleIrradianceCascaded(worldPosition, worldN, cameraDirection, pc.ddgiCascades, length(viewPosition), edgeFade);
            color = gi.rgb * gi.w;
        } else {
            uint count = min(pc.ddgiCascades->cascadeCount, DDGI_MAX_CASCADES);
            uint localCount = min(pc.ddgiCascades->localCount, DDGI_MAX_CASCADES - count);
            float coverage = 0.0;
            float localFade = 0.0;
            float3 chebSum = float3(0.0);
            float dominantContribution = 0.0;
            float dominantMargin = 0.0;
            float dominantStdRatio = 0.0;
            float dominantTrueRatio = 0.0;
            uint dominantIndex = 0;
            for (uint i = 0; i < localCount + count && coverage < 0.999; ++i) {
                bool bLocal = i < localCount;
                uint c = bLocal ? count + i : i - localCount;
                DDGICascadeDescriptor desc = pc.ddgiCascades->cascades[c];
                if (bLocal) {
                    float3 gridCoord = worldPosition / desc.volume.probeSpacing;
                    float3 edgeDistance = min(gridCoord - float3(desc.volume.baseCell), float3(desc.volume.baseCell + int3(desc.volume.probeCount) - 1) - gridCoord);
                    localFade = max(localFade, saturate(min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) / max(desc.volume.edgeFadeCells, 1.0)));
                }
                if (desc.bValid == 0) {
                    continue;
                }
                GICageStats s = GIDeconstructCage(worldPosition, worldN, cameraDirection, desc.volume, desc.visibilityIndex, desc.probeOffsets, desc.bOffsetsValid, length(viewPosition));
                float w = s.fade * saturate(s.weightSum / 0.02);
                if (!bLocal) {
                    w *= 1.0 - localFade;
                }
                if (w <= 0.0) {
                    continue;
                }
                float contribution = w * (1.0 - coverage);
                chebSum += contribution * float3(s.wSkyMean, s.wClean, s.wFired) / max(s.weightSum, 1e-6);
                if (contribution > dominantContribution) {
                    dominantContribution = contribution;
                    dominantMargin = s.margin;
                    dominantStdRatio = s.stdRatio;
                    dominantTrueRatio = s.trueWeightSum / max(s.weightSum, 1e-6);
                    dominantIndex = c;
                }
                coverage += contribution;
            }
            if (coverage > 0.0) {
                if (pc.mode == GI_DECONSTRUCT_DDGI_CHEB) {
                    color = chebSum / coverage;
                } else if (pc.mode == GI_DECONSTRUCT_DDGI_MARGIN) {
                    color = dominantMargin > 0.0
                                ? float3(saturate(dominantMargin * 3.0), 0.0, saturate(dominantStdRatio))
                                : float3(0.0, saturate(-dominantMargin * 3.0), saturate(dominantStdRatio));
                } else {
                    float edgeFade;
                    float4 gi = DDGISampleIrradianceCascaded(worldPosition, worldN, cameraDirection, pc.ddgiCascades, length(viewPosition), edgeFade);
                    color = float3(gi.w, dominantTrueRatio, float(dominantIndex) / 15.0);
                }
            }
        }
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(color, 1.0);
}
