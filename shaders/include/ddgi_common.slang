module ddgi_common;

import ddgi_interop;
import constants_interop;
import bindless_declarations;

// =============== Shader Summary ============
// Rolling-grid math for the DDGI probe volume. Probes live on an infinite world-space lattice (cell * spacing); the volume is a window of probeCount cells starting at baseCell that follows the camera in whole-cell steps.
// Storage slots map to cells via Euclidean modulo so a scroll only invalidates the newly exposed planes; every other slot keeps its probe.

public int3 DDGIEuclideanMod(int3 v, int3 m)
{
    return ((v % m) + m) % m;
}

/** Lattice cell held by a storage slot for the current window. */
public int3 DDGICellFromSlot(int3 slot, DDGIVolumeParams volume)
{
    return volume.baseCell + DDGIEuclideanMod(slot - volume.baseCell, int3(volume.probeCount));
}

/** Storage slot holding a lattice cell; only meaningful for cells inside the window. */
public int3 DDGISlotFromCell(int3 cell, DDGIVolumeParams volume)
{
    return DDGIEuclideanMod(cell, int3(volume.probeCount));
}

public float3 DDGIProbeWorldPosition(int3 cell, DDGIVolumeParams volume)
{
    return float3(cell) * volume.probeSpacing;
}

public int3 DDGISlotFromProbeIndex(uint probeIndex, DDGIVolumeParams volume)
{
    uint layer = volume.probeCount.x * volume.probeCount.y;
    return int3(probeIndex % volume.probeCount.x, (probeIndex / volume.probeCount.x) % volume.probeCount.y, probeIndex / layer);
}

public uint DDGIProbeIndexFromSlot(int3 slot, DDGIVolumeParams volume)
{
    return uint(slot.x) + uint(slot.y) * volume.probeCount.x + uint(slot.z) * volume.probeCount.x * volume.probeCount.y;
}

public int2 DDGIProbeTileOrigin(int3 slot, DDGIVolumeParams volume, uint tileSize)
{
    return int2(slot.x + slot.y * int(volume.probeCount.x), slot.z) * int(tileSize);
}

public float2 DDGIOctEncode(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return n.xy;
}

public float3 DDGIOctDecode(float2 f)
{
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

/** Direction of an interior tile texel, texel in [1, interior]. */
public float3 DDGIOctTexelDirection(int2 texelInTile, uint interior)
{
    float2 uv = (float2(texelInTile - 1) + 0.5) / float(interior);
    return DDGIOctDecode(uv * 2.0 - 1.0);
}

/** Bilinear atlas UV for a direction within a probe's tile; the 1-texel wrapped border makes edge taps seamless. */
public float2 DDGIAtlasUV(int3 slot, float3 direction, DDGIVolumeParams volume, uint tileSize, uint interior)
{
    float2 uv01 = DDGIOctEncode(direction) * 0.5 + 0.5;
    float2 texel = float2(DDGIProbeTileOrigin(slot, volume, tileSize)) + 1.0 + uv01 * float(interior);
    float2 atlasSize = float2(float(volume.probeCount.x * volume.probeCount.y), float(volume.probeCount.z)) * float(tileSize);
    return texel / atlasSize;
}

// Probes whose rays hit this many backfaces are inside geometry beyond what relocation could fix; the sampler and debug viz treat them as dead.
public static const float DDGI_PROBE_DEAD_BACKFACE_FRACTION = 0.35;

// Non-escape relocation moves below this fraction of spacing are skipped (rotation-noise jitter, not convergence); matches the temporal-restart threshold so every applied move restarts the blends.
public static const float DDGI_RELOCATE_DEADBAND = 0.05;

/**
 * Irradiance lookup at a surface point: the 8 surrounding probes, weighted by trilinear position, soft backface (wrap lighting), and Chebyshev visibility from the distance atlas. cameraDirection points from the viewer toward the surface (bias direction).
 * Atlas args are rdgTexturesFloat4/rdgTexturesFloat2 descriptor indices (resource-typed params through module imports crash slangc's spirv emit).
 * probeOffsets (when bProbeOffsetsValid) holds per-slot relocation offsets in xyz and the backface fraction in w; relocated positions drive direction/distance/Chebyshev, trilinear stays on the lattice, dead probes are skipped.
 * Returns E/PI in rgb (Lambertian outgoing radiance = albedo * rgb); w in [0,1] combines the window edge fade with a weight-support confidence (dead-probe pockets report low w), so callers can blend to the next cascade or their fallback without a hard edge.
 */
public float4 DDGISampleIrradiance(float3 worldPosition, float3 normal, float3 cameraDirection, DDGIVolumeParams volume, uint irradianceAtlasIndex, uint visibilityAtlasIndex, float4* probeOffsets, uint bProbeOffsetsValid)
{
    float3 biasedPosition = worldPosition + normal * volume.normalBias - cameraDirection * volume.viewBias;
    float3 gridCoord = biasedPosition / volume.probeSpacing;

    float3 edgeDistance = min(gridCoord - float3(volume.baseCell), float3(volume.baseCell + int3(volume.probeCount) - 1) - gridCoord);
    float fade = saturate(min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) / max(volume.edgeFadeCells, 1.0));
    if (fade <= 0.0) {
        return float4(0.0);
    }

    int3 baseCell = int3(floor(gridCoord));
    float3 alpha = saturate(gridCoord - float3(baseCell));

    float3 irradianceSum = float3(0.0);
    float weightSum = 0.0;
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

        // Wrap lighting instead of a hard cosine cut so probes just behind thin geometry keep some support.
        float3 toProbe = normalize(probePosition - worldPosition);
        float wrap = (dot(toProbe, normal) + 1.0) * 0.5;
        float weight = wrap * wrap + 0.2;

        // Chebyshev test against the probe's mean/mean^2 distance along this direction: probably-occluded probes fade out.
        float3 biasedToProbe = probePosition - biasedPosition;
        float distanceToProbe = length(biasedToProbe);
        float2 moments = rdgTexturesFloat2[visibilityAtlasIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], DDGIAtlasUV(slot, -biasedToProbe / max(distanceToProbe, 1e-6), volume, DDGI_VISIBILITY_TILE, DDGI_VISIBILITY_INTERIOR), 0);
        if (distanceToProbe > moments.x) {
            float variance = abs(moments.x * moments.x - moments.y);
            float d = distanceToProbe - moments.x;
            float chebyshev = variance / (variance + d * d);
            weight *= max(chebyshev * chebyshev * chebyshev, 0.05);
        }

        weight = max(weight, 1e-6);
        // Crush (rather than clip) small weights so probes fade out without popping.
        const float crushThreshold = 0.2;
        if (weight < crushThreshold) {
            weight *= weight * weight / (crushThreshold * crushThreshold);
        }
        float3 trilinear = lerp(1.0 - alpha, alpha, float3(offset));
        weight *= max(trilinear.x * trilinear.y * trilinear.z, 0.001);

        float3 encoded = rdgTexturesFloat4[irradianceAtlasIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], DDGIAtlasUV(slot, normal, volume, DDGI_IRRADIANCE_TILE, DDGI_IRRADIANCE_INTERIOR), 0).rgb;
        // Blend in half-gamma space and square after the weighted sum: perceptual interpolation between probes (RTXGI).
        irradianceSum += weight * pow(max(encoded, float3(0.0)), volume.irradianceGamma * 0.5);
        weightSum += weight;
    }
    if (weightSum <= 1e-6) {
        return float4(0.0);
    }
    float3 result = irradianceSum / weightSum;
    return float4(result * result, fade * saturate(weightSum / 0.02));
}

/**
 * Cascade-chain lookup: front-to-back composite finest-first, so a fine cascade's edge fade (and dead-pocket zero-weight failures) fall through to the next coarser cascade.
 * Returns the same contract as DDGISampleIrradiance; w is total coverage for the caller's fallback blend, dropping below 1 outside every cascade's interior and in weight-starved dead pockets.
 */
[ForceInline] // the struct ptr fails to compile
public float4 DDGISampleIrradianceCascaded(float3 worldPosition, float3 normal, float3 cameraDirection, DDGICascadeSetGPU* cascadeSet)
{
    float3 color = float3(0.0);
    float coverage = 0.0;
    uint count = min(cascadeSet->cascadeCount, DDGI_MAX_CASCADES);
    for (uint c = 0; c < count; ++c) {
        DDGICascadeDescriptor desc = cascadeSet->cascades[c];
        if (desc.bValid == 0) {
            continue;
        }
        float4 gi = DDGISampleIrradiance(worldPosition, normal, cameraDirection, desc.volume, desc.irradianceIndex, desc.visibilityIndex, desc.probeOffsets, desc.bOffsetsValid);
        if (gi.w <= 0.0) {
            continue;
        }
        color += gi.rgb * gi.w * (1.0 - coverage);
        coverage += gi.w * (1.0 - coverage);
        if (coverage >= 0.999) {
            break;
        }
    }
    if (coverage <= 0.0) {
        return float4(0.0);
    }
    return float4(color / coverage, coverage);
}

/**
 * Geometric interior fade of the cascade set: 1 inside any valid window, falling to 0 over the outermost window's edgeFadeCells.
 * Lets callers distinguish "outside the probe world" (sky is a fair irradiance fallback) from an interior coverage hole (it is not: dead-probe pockets in sealed rooms would leak full-strength sky, and the bounce-feedback sites would spread it through the whole probe/cache field).
 */
[ForceInline]
public float DDGICascadeSetEdgeFade(float3 worldPosition, DDGICascadeSetGPU* cascadeSet)
{
    float fade = 0.0;
    uint count = min(cascadeSet->cascadeCount, DDGI_MAX_CASCADES);
    for (uint c = 0; c < count; ++c) {
        DDGICascadeDescriptor desc = cascadeSet->cascades[c];
        if (desc.bValid == 0) {
            continue;
        }
        float3 gridCoord = worldPosition / desc.volume.probeSpacing;
        float3 edgeDistance = min(gridCoord - float3(desc.volume.baseCell), float3(desc.volume.baseCell + int3(desc.volume.probeCount) - 1) - gridCoord);
        fade = max(fade, saturate(min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) / max(desc.volume.edgeFadeCells, 1.0)));
    }
    return fade;
}

public float3 DDGISphericalFibonacci(uint index, uint count)
{
    float phi = 6.28318530718 * frac(float(index) * 0.61803398875);
    float cosTheta = 1.0 - (2.0 * float(index) + 1.0) / float(count);
    float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

public float3 DDGIRotate(float3 v, float4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
