module ddgi_common;

import ddgi_interop;

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

/** Top-left texel of a probe's tile in the irradiance atlas. Atlas is (countX * countY) tiles wide, countZ tiles tall. */
public int2 DDGIProbeTileOrigin(int3 slot, DDGIVolumeParams volume)
{
    return int2(slot.x + slot.y * int(volume.probeCount.x), slot.z) * int(DDGI_IRRADIANCE_TILE);
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

/** Direction of an interior tile texel, texel in [1, DDGI_IRRADIANCE_INTERIOR]. */
public float3 DDGIOctTexelDirection(int2 texelInTile)
{
    float2 uv = (float2(texelInTile - 1) + 0.5) / float(DDGI_IRRADIANCE_INTERIOR);
    return DDGIOctDecode(uv * 2.0 - 1.0);
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
