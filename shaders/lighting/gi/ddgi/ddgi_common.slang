module ddgi_common;

import ddgi_interop;
import constants_interop;
import bindless_declarations;
import world_grid_interop;
import binning_common;

public int3 DDGIEuclideanMod(int3 v, int3 m)
{
    return ((v % m) + m) % m;
}

public int3 DDGICellFromSlot(int3 slot, DDGIVolumeParams volume)
{
    return volume.baseCell + DDGIEuclideanMod(slot - volume.baseCell, int3(volume.probeCount));
}

/** Only meaningful for cells inside the window. */
public int3 DDGISlotFromCell(int3 cell, DDGIVolumeParams volume)
{
    return DDGIEuclideanMod(cell, int3(volume.probeCount));
}

public float3 DDGIProbeWorldPosition(int3 cell, DDGIVolumeParams volume)
{
    return volume.origin + float3(cell) * volume.probeSpacing;
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
    return int2(slot.x + slot.y * int(volume.probeCount.x), slot.z + int(volume.atlasSlot * volume.probeCount.z)) * int(tileSize);
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

/** texel in [1, interior]. */
public float3 DDGIOctTexelDirection(int2 texelInTile, uint interior)
{
    float2 uv = (float2(texelInTile - 1) + 0.5) / float(interior);
    return DDGIOctDecode(uv * 2.0 - 1.0);
}

public float2 DDGIAtlasUV(int3 slot, float3 direction, DDGIVolumeParams volume, uint tileSize, uint interior)
{
    float2 uv01 = DDGIOctEncode(direction) * 0.5 + 0.5;
    float2 texel = float2(DDGIProbeTileOrigin(slot, volume, tileSize)) + 1.0 + uv01 * float(interior);
    float2 atlasSize = float2(float(volume.probeCount.x * volume.probeCount.y), float(volume.probeCount.z * volume.atlasRows)) * float(tileSize);
    return texel / atlasSize;
}

public static const float DDGI_PROBE_DEAD_BACKFACE_FRACTION = 0.35;

// Must match the temporal-restart threshold so every applied move restarts the blends.
public static const float DDGI_RELOCATE_DEADBAND = 0.05;
public static const float DDGI_CELL_DIAGONAL = 1.7320508;

public static const float DDGI_VISIBILITY_VARIANCE_CAP = 0.3;

// Atlas args are descriptor indices: resource-typed params through module imports crash slangc's spirv emit.
/** Returns E/PI in rgb. Negative maxBias caps at max(-maxBias, half the probe spacing). */
public float4 DDGISampleIrradiance(float3 worldPosition, float3 normal, float3 cameraDirection, DDGIVolumeParams volume, uint irradianceAtlasIndex, uint visibilityAtlasIndex, float4* probeOffsets, uint bProbeOffsetsValid, float maxBias, out float fade)
{
    float3 biasOffset = normal * volume.normalBias - cameraDirection * volume.viewBias;
    float biasLength = length(biasOffset);
    float biasCap = maxBias < 0.0 ? max(-maxBias, 0.5 * volume.probeSpacing) : maxBias;
    if (biasLength > biasCap) {
        biasOffset *= biasCap / biasLength;
    }
    float3 biasedPosition = worldPosition + biasOffset;
    float3 gridCoord = (biasedPosition - volume.origin) / volume.probeSpacing;

    float3 edgeDistance = min(gridCoord - float3(volume.baseCell), float3(volume.baseCell + int3(volume.probeCount) - 1) - gridCoord);
    fade = saturate(min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) / max(volume.edgeFadeCells, 1.0));
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

        float3 toProbe = normalize(probePosition - worldPosition);
        float wrap = (dot(toProbe, normal) + 1.0) * 0.5;
        float weight = wrap * wrap + 0.2;

        float3 biasedToProbe = probePosition - biasedPosition;
        float distanceToProbe = length(biasedToProbe);
        float2 moments = rdgTexturesFloat2[NonUniformResourceIndex(visibilityAtlasIndex)].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], DDGIAtlasUV(slot, -biasedToProbe / max(distanceToProbe, 1e-6), volume, DDGI_VISIBILITY_TILE, DDGI_VISIBILITY_INTERIOR), 0);
        if (distanceToProbe > moments.x) {
            float cappedStd = DDGI_VISIBILITY_VARIANCE_CAP * moments.x;
            float variance = min(abs(moments.x * moments.x - moments.y), cappedStd * cappedStd);
            float d = distanceToProbe - moments.x;
            float chebyshev = variance / (variance + d * d);
            float cheb3 = chebyshev * chebyshev * chebyshev;
            weight *= max(cheb3, 0.05);
        }

        weight = max(weight, 1e-6);
        const float crushThreshold = 0.2;
        if (weight < crushThreshold) {
            weight *= weight * weight / (crushThreshold * crushThreshold);
        }
        float3 trilinear = max(float3(0.001), lerp(1.0 - alpha, alpha, float3(offset)));
        weight *= trilinear.x * trilinear.y * trilinear.z;

        float4 encoded = rdgTexturesFloat4[NonUniformResourceIndex(irradianceAtlasIndex)].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], DDGIAtlasUV(slot, normal, volume, DDGI_IRRADIANCE_TILE, DDGI_IRRADIANCE_INTERIOR), 0);
        // Blended in half-gamma space, squared after the weighted sum.
        irradianceSum += weight * pow(max(encoded.rgb, float3(0.0)), volume.irradianceGamma * 0.5);
        weightSum += weight;
    }
    if (weightSum <= 1e-6) {
        return float4(0.0);
    }
    float3 result = irradianceSum / weightSum;

    return float4(result * result, fade * saturate(weightSum / 0.02));
}

public struct DDGILocalWalk
{
    public uint visit;
    public uint offset;
    public bool bGrid;
};

[ForceInline]
public DDGILocalWalk DDGIResolveLocalWalk(float3 worldPosition, DDGICascadeSetGPU* cascadeSet, uint localCount)
{
    DDGILocalWalk walk;
    walk.visit = localCount;
    walk.offset = 0u;
    walk.bGrid = cascadeSet->bVolumeGridValid != 0u && localCount > 0u;
    if (walk.bGrid) {
        WorldGridKey key = WorldGridResolveCell(worldPosition, cascadeSet->gridCamPos.xyz);
        uint2 entry = cascadeSet->volumeGrid[key.cellIndex];
        if (entry.y > MAX_DDGI_VOLUMES_PER_WORLD_GRID_CELL) {
            walk.bGrid = false;
        } else {
            walk.offset = entry.x;
            walk.visit = entry.y;
        }
    }
    return walk;
}

[ForceInline] // the struct ptr fails to compile
public float4 DDGISampleIrradianceCascaded(float3 worldPosition, float3 normal, float3 cameraDirection, DDGICascadeSetGPU* cascadeSet, float maxBias, out float edgeFade)
{
    float3 color = float3(0.0);
    float coverage = 0.0;
    float edgeCoverage = 0.0;
    bool bSaturated = false;
    edgeFade = 0.0;
    uint count = min(cascadeSet->cascadeCount, DDGI_MAX_VOLUME_SLOTS);
    uint localCount = min(cascadeSet->localCount, DDGI_MAX_VOLUME_SLOTS - count);
    DDGILocalWalk walk = DDGIResolveLocalWalk(worldPosition, cascadeSet, localCount);
    for (uint i = 0; i < walk.visit + count; ++i) {
        bool bLocal = i < walk.visit;
        uint c = bLocal ? (walk.bGrid ? cascadeSet->volumeIndexList[walk.offset + i] : count + i) : i - walk.visit;
        if (c >= DDGI_MAX_VOLUME_SLOTS) {
            continue;
        }
        DDGICascadeDescriptor desc = cascadeSet->cascades[c];
        if (desc.bValid == 0) {
            continue;
        }

        if (!bLocal) {
            float3 gridCoord = (worldPosition - desc.volume.origin) / desc.volume.probeSpacing;
            float3 edgeDistance = min(gridCoord - float3(desc.volume.baseCell), float3(desc.volume.baseCell + int3(desc.volume.probeCount) - 1) - gridCoord);
            edgeFade = max(edgeFade, saturate(min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)) / max(desc.volume.edgeFadeCells, 1.0)));
        }
        if (bSaturated) {
            continue;
        }
        float fade;
        float4 gi = DDGISampleIrradiance(worldPosition, normal, cameraDirection, desc.volume, desc.irradianceIndex, desc.visibilityIndex, desc.probeOffsets, desc.bOffsetsValid, maxBias, fade);
        edgeCoverage += fade * (1.0 - edgeCoverage);
        float w = gi.w;
        if (w <= 0.0) {
            continue;
        }
        color += gi.rgb * w * (1.0 - coverage);
        coverage += w * (1.0 - coverage);
        if (coverage >= 0.999) {
            bSaturated = true;
        }
    }
    if (coverage <= 0.0) {
        return float4(0.0);
    }
    return float4(color / coverage, max(edgeCoverage, coverage));
}

[ForceInline]
public float4 DDGISampleIrradianceCascaded(float3 worldPosition, float3 normal, float3 cameraDirection, DDGICascadeSetGPU* cascadeSet, float maxBias)
{
    float3 color = float3(0.0);
    float coverage = 0.0;
    float edgeCoverage = 0.0;
    uint count = min(cascadeSet->cascadeCount, DDGI_MAX_VOLUME_SLOTS);
    uint localCount = min(cascadeSet->localCount, DDGI_MAX_VOLUME_SLOTS - count);
    DDGILocalWalk walk = DDGIResolveLocalWalk(worldPosition, cascadeSet, localCount);
    for (uint i = 0; i < walk.visit + count; ++i) {
        bool bLocal = i < walk.visit;
        uint c = bLocal ? (walk.bGrid ? cascadeSet->volumeIndexList[walk.offset + i] : count + i) : i - walk.visit;
        if (c >= DDGI_MAX_VOLUME_SLOTS) {
            continue;
        }
        DDGICascadeDescriptor desc = cascadeSet->cascades[c];
        if (desc.bValid == 0) {
            continue;
        }
        float fade;
        float4 gi = DDGISampleIrradiance(worldPosition, normal, cameraDirection, desc.volume, desc.irradianceIndex, desc.visibilityIndex, desc.probeOffsets, desc.bOffsetsValid, maxBias, fade);
        edgeCoverage += fade * (1.0 - edgeCoverage);
        float w = gi.w;
        if (w <= 0.0) {
            continue;
        }
        color += gi.rgb * w * (1.0 - coverage);
        coverage += w * (1.0 - coverage);
        if (coverage >= 0.999) {
            break;
        }
    }
    if (coverage <= 0.0) {
        return float4(0.0);
    }
    return float4(color / coverage, max(edgeCoverage, coverage));
}

public int2 DDGIOctBorderSource(int2 texel, int n)
{
    bool onX = texel.x == 0 || texel.x == n + 1;
    bool onY = texel.y == 0 || texel.y == n + 1;
    if (!onX && !onY) {
        return texel;
    }
    if (onX && onY) {
        return int2(texel.x == 0 ? n : 1, texel.y == 0 ? n : 1);
    }
    if (onY) {
        return int2(n + 1 - texel.x, texel.y == 0 ? 1 : n);
    }
    return int2(texel.x == 0 ? 1 : n, n + 1 - texel.y);
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
