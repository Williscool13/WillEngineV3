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

/** Brute-force pick of the smallest-volume probe containing worldPos. Returns the array index (or -1), and a world-space edge fade in [0,1] (1 = interior, 0 = boundary). Tie-break: strictly-less volume, then lowest index. */
public int PickReflectionProbe(float3 worldPos, ReflectionProbeGPU* probes, uint count, out float fade)
{
    int best = -1;
    float bestVolume = 1e30;
    float bestFade = 1.0;

    for (uint i = 0; i < count; i++) {
        float volume;
        float thisFade;
        if (!ReflectionProbeContains(probes[i], worldPos, volume, thisFade)) { continue; }
        if (volume < bestVolume) {
            bestVolume = volume;
            best = int(i);
            bestFade = thisFade;
        }
    }

    fade = bestFade;
    return best;
}

/** Cell-lookup pick: reads the per-cell probe index the world-grid binning kernel wrote and validates containment against that one probe. A null probeGrid (debug brute-force toggle, or grid pass absent) falls back to PickReflectionProbe. camPos must be the same cameraWorldPos the binning kernel used. Returns the array index (or -1) and the probe's edge fade. */
public int PickReflectionProbeBinned(float3 worldPos, ReflectionProbeGPU* probes, uint count, uint* probeGrid, float3 camPos, out float fade)
{
    fade = 0.0;
    if (probeGrid == nullptr) { return PickReflectionProbe(worldPos, probes, count, fade); }

    WorldGridKey key = WorldGridResolveCell(worldPos, camPos);
    if (!key.bValid) { return -1; }

    uint idx = probeGrid[key.cellIndex];
    if (idx == WORLD_GRID_PROBE_SENTINEL || idx >= count) { return -1; }

    float volume;
    float thisFade;
    if (!ReflectionProbeContains(probes[idx], worldPos, volume, thisFade)) { return -1; }
    fade = thisFade;
    return int(idx);
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
