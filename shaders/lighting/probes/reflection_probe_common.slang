module reflection_probe_common;

import constants_interop;
import reflection_probe_interop;
import bindless_declarations;
import pbr_functions;
import world_grid_interop;
import binning_common;

/** Per-axis world half-extent. worldToLocal = inverse(R*S); its mathematical rows have length 1/halfExtent_i, and since matrix subscripting returns columns here, transpose exposes those rows. */
public float3 ReflectionProbeWorldExtent(ReflectionProbeGPU probe)
{
    float4x4 worldToLocalRows = transpose(probe.worldToLocal);
    return float3(
        1.0 / max(length(worldToLocalRows[0].xyz), 1e-8),
        1.0 / max(length(worldToLocalRows[1].xyz), 1e-8),
        1.0 / max(length(worldToLocalRows[2].xyz), 1e-8));
}

/** Influence-volume measure used for the smallest-wins pick tie-break; single source for the world-grid binning kernel. sphere = (4/3)pi r^3, box = 8*ex*ey*ez. */
public float ReflectionProbeVolume(ReflectionProbeGPU probe)
{
    float3 extent = ReflectionProbeWorldExtent(probe);
    if ((probe.flags & REFLECTION_PROBE_FLAG_SPHERE) != 0u) {
        float radius = extent.x;
        return 4.18879020479 * radius * radius * radius;
    }
    return 8.0 * extent.x * extent.y * extent.z;
}

/** Containment + volume + edge-fade for a single probe. False when worldPos lies outside the volume. */
static bool ReflectionProbeContains(ReflectionProbeGPU probe, float3 worldPos, out float volume, out float fade)
{
    float3 localPos = mul(probe.worldToLocal, float4(worldPos, 1.0)).xyz;
    float3 extent = ReflectionProbeWorldExtent(probe);

    volume = ReflectionProbeVolume(probe);
    fade = 0.0;
    if ((probe.flags & REFLECTION_PROBE_FLAG_SPHERE) != 0u) {
        float lenLocal = length(localPos);
        if (lenLocal > 1.0) { return false; }
        float radius = extent.x;
        fade = probe.fadeMargin > 0.0 ? saturate((1.0 - lenLocal) * radius / probe.fadeMargin) : 1.0;
    } else {
        if (any(abs(localPos) > 1.0)) { return false; }
        if (probe.fadeMargin > 0.0) {
            float3 worldDist = (1.0 - abs(localPos)) * extent;
            fade = saturate(min(min(worldDist.x, worldDist.y), worldDist.z) / probe.fadeMargin);
        } else {
            fade = 1.0;
        }
    }
    return true;
}

/** The probes covering a shading point. indexA is the smaller-volume one, indexB is -1 when only one covers. weightA/weightB sum to 1 and split the pair; coverage is what the pair owns against the world tiers. */
public struct ReflectionProbeBlend
{
    public int indexA;
    public int indexB;
    public float weightA;
    public float weightB;
    public float coverage;
};

/** Coverage sums the fades so an overlap hands one probe to the next instead of dipping toward the world tiers. The split is fade/volume, preserving smallest-volume-wins: a probe nested in a looser one owns its interior rather than averaging with it. */
static ReflectionProbeBlend MakeReflectionProbeBlend(int indexA, float fadeA, float volumeA, int indexB, float fadeB, float volumeB)
{
    ReflectionProbeBlend blend;
    blend.indexA = indexA;
    blend.indexB = indexB;
    blend.weightA = 1.0;
    blend.weightB = 0.0;
    blend.coverage = 0.0;
    if (indexA < 0) { return blend; }

    float fadeSum = fadeA + max(fadeB, 0.0);
    blend.coverage = saturate(fadeSum);
    if (indexB >= 0) {
        float priorityA = fadeA / max(volumeA, 1e-6);
        float prioritySum = priorityA + fadeB / max(volumeB, 1e-6);
        if (prioritySum > 1e-12) {
            blend.weightA = priorityA / prioritySum;
            blend.weightB = 1.0 - blend.weightA;
        }
    }
    return blend;
}

/** The no-probe result, for callers that skip the pick entirely. */
public ReflectionProbeBlend ReflectionProbeBlendNone()
{
    return MakeReflectionProbeBlend(-1, 0.0, 1.0, -1, 0.0, 1.0);
}

/** Brute-force pick of the two smallest-volume probes containing worldPos. Tie-break: strictly-less volume, then lowest index. */
public ReflectionProbeBlend PickReflectionProbe(float3 worldPos, ReflectionProbeGPU* probes, uint count)
{
    int bestA = -1;
    int bestB = -1;
    float volumeA = 1e30;
    float volumeB = 1e30;
    float fadeA = 0.0;
    float fadeB = 0.0;

    for (uint i = 0; i < count; i++) {
        float volume;
        float thisFade;
        if (!ReflectionProbeContains(probes[i], worldPos, volume, thisFade)) { continue; }
        if (volume < volumeA) {
            volumeB = volumeA;
            bestB = bestA;
            fadeB = fadeA;
            volumeA = volume;
            bestA = int(i);
            fadeA = thisFade;
        } else if (volume < volumeB) {
            volumeB = volume;
            bestB = int(i);
            fadeB = thisFade;
        }
    }

    return MakeReflectionProbeBlend(bestA, fadeA, volumeA, bestB, fadeB, volumeB);
}

/** Cell-lookup pick: revalidates the probe pair the binning kernel packed into the cell. A null probeGrid falls back to PickReflectionProbe. camPos must be the same cameraWorldPos the binning kernel used. */
public ReflectionProbeBlend PickReflectionProbeBinned(float3 worldPos, ReflectionProbeGPU* probes, uint count, uint* probeGrid, float3 camPos)
{
    if (probeGrid == nullptr) { return PickReflectionProbe(worldPos, probes, count); }

    ReflectionProbeBlend empty = ReflectionProbeBlendNone();
    WorldGridKey key = WorldGridResolveCell(worldPos, camPos);
    if (!key.bValid) { return empty; }

    uint packed = probeGrid[key.cellIndex];
    if (packed == WORLD_GRID_PROBE_SENTINEL) { return empty; }

    uint idxA = packed & 0xFFFFu;
    uint idxB = packed >> 16u;

    int bestA = -1;
    int bestB = -1;
    float fadeA = 0.0;
    float fadeB = 0.0;
    float volumeA = 1.0;
    float volumeB = 1.0;
    if (idxA < count && ReflectionProbeContains(probes[idxA], worldPos, volumeA, fadeA)) { bestA = int(idxA); }
    if (idxB < count && ReflectionProbeContains(probes[idxB], worldPos, volumeB, fadeB)) { bestB = int(idxB); }
    if (bestA < 0) {
        bestA = bestB;
        fadeA = fadeB;
        volumeA = volumeB;
        bestB = -1;
        fadeB = 0.0;
    }

    return MakeReflectionProbeBlend(bestA, fadeA, volumeA, bestB, fadeB, volumeB);
}

/** Prefiltered specular radiance from a probe. worldR is the world-space reflection vector; when the probe is a parallax box the ray is intersected against the OBB interior and re-pointed from the capture origin. Routes the final direction through the shared cubemap Y-flip so it matches SpecularReflection. Returns raw radiance; the caller applies the BRDF term and intensity. */
public float3 SampleReflectionProbeSpecular(ReflectionProbeGPU probe, float3 worldPos, float3 worldR, float roughness)
{
    float3 sampleDir = worldR;
    if ((probe.flags & REFLECTION_PROBE_FLAG_PARALLAX) != 0u) {
        float3 localPos = mul(probe.worldToLocal, float4(worldPos, 1.0)).xyz;
        float3 localDir = mul((float3x3)probe.worldToLocal, worldR);
        float3 invDir = 1.0 / localDir;
        float3 t1 = (float3(-1.0) - localPos) * invDir;
        float3 t2 = (float3(1.0) - localPos) * invDir;
        float3 tMax = max(t1, t2);
        float t = min(min(tMax.x, tMax.y), tMax.z);
        float3 hitWorld = worldPos + worldR * t;
        sampleDir = normalize(hitWorld - probe.capturePosition.xyz);
    }

    float mip = sqrt(roughness) * float(ENVIRONMENT_MAP_SPECULAR_MIPS - 1);
    return assetCubemaps[NonUniformResourceIndex(probe.cubemapIndex)].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], CubemapDirection(sampleDir), mip).rgb;
}

/** Prefiltered diffuse irradiance from a probe along worldNormal. Direction-only (no parallax; irradiance is positionless within the volume). Routes through the shared cubemap Y-flip and samples ENVIRONMENT_MAP_DIFFUSE_MIP, matching DiffuseIrradiance. Returns raw radiance; the caller applies intensity. */
public float3 SampleReflectionProbeIrradiance(ReflectionProbeGPU probe, float3 worldNormal)
{
    return assetCubemaps[NonUniformResourceIndex(probe.cubemapIndex)].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], CubemapDirection(worldNormal), ENVIRONMENT_MAP_DIFFUSE_MIP).rgb;
}

/** Irradiance for a picked pair, per-probe intensity applied. Weighted but not coverage-scaled; the caller lerps by blend.coverage. */
public float3 SampleReflectionProbeIrradianceBlended(ReflectionProbeBlend blend, ReflectionProbeGPU* probes, float3 worldNormal)
{
    float3 irradiance = SampleReflectionProbeIrradiance(probes[blend.indexA], worldNormal) * probes[blend.indexA].intensity * blend.weightA;
    if (blend.indexB >= 0) {
        irradiance += SampleReflectionProbeIrradiance(probes[blend.indexB], worldNormal) * probes[blend.indexB].intensity * blend.weightB;
    }
    return irradiance;
}

/** Specular radiance for a picked pair, each probe parallax-corrected against its own volume and per-probe intensity applied. The caller applies the BRDF term and lerps by blend.coverage. */
public float3 SampleReflectionProbeSpecularBlended(ReflectionProbeBlend blend, ReflectionProbeGPU* probes, float3 worldPos, float3 worldR, float roughness)
{
    float3 radiance = SampleReflectionProbeSpecular(probes[blend.indexA], worldPos, worldR, roughness) * probes[blend.indexA].intensity * blend.weightA;
    if (blend.indexB >= 0) {
        radiance += SampleReflectionProbeSpecular(probes[blend.indexB], worldPos, worldR, roughness) * probes[blend.indexB].intensity * blend.weightB;
    }
    return radiance;
}
