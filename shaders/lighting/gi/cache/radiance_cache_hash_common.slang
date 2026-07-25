module radiance_cache_hash_common;

import radiance_cache_interop;
import shader_functions;
import packing_functions;

public bool RadianceCacheRadianceShaded(uint2 packed)
{
    return (packed.y >> 16u) != 0xFFFFu;
}

public float3 RadianceCacheUnpackRadiance(uint2 packed)
{
    return UnpackHalf2x16Pair(packed).rgb;
}

public uint2 RadianceCachePackRadiance(float3 radiance)
{
    return PackHalf2x16Pair(float4(radiance, 0.0));
}

public struct RadianceCacheKey
{
    public int3 cellPos;
    public uint level;
    public uint normalBucket;
};

public float RadianceCacheCellSizeAtLevel(uint level)
{
    return RADIANCE_CACHE_CELL_SIZE_BASE * exp2(float(level));
}

public uint RadianceCacheLevelFromDistance(float dist)
{
    float l = floor(log2(max(dist / RADIANCE_CACHE_LOD_BASE_DIST, 1.0)));
    return uint(clamp(l, 0.0, float(RADIANCE_CACHE_MAX_LEVEL)));
}

public uint RadianceCacheNormalBucket(float3 n)
{
    // Dominant-axis cube face; front/back separation only.
    float3 a = abs(n);
    if (a.x >= a.y && a.x >= a.z) { return n.x >= 0.0 ? 0u : 1u; }
    if (a.y >= a.z) { return n.y >= 0.0 ? 2u : 3u; }
    return n.z >= 0.0 ? 4u : 5u;
}

public float3 RadianceCacheBucketNormal(uint bucket)
{
    switch (bucket) {
        case 0u: return float3(1.0, 0.0, 0.0);
        case 1u: return float3(-1.0, 0.0, 0.0);
        case 2u: return float3(0.0, 1.0, 0.0);
        case 3u: return float3(0.0, -1.0, 0.0);
        case 4u: return float3(0.0, 0.0, 1.0);
        default: return float3(0.0, 0.0, -1.0);
    }
}

public RadianceCacheKey RadianceCacheComputeKey(float3 worldPos, float3 normal, float3 cameraPos)
{
    RadianceCacheKey key;
    key.level = RadianceCacheLevelFromDistance(distance(worldPos, cameraPos));
    float cs = RadianceCacheCellSizeAtLevel(key.level);
    key.cellPos = int3(floor(worldPos / cs));
    key.normalBucket = RadianceCacheNormalBucket(normal);
    return key;
}

public float3 RadianceCacheCellCenterFromKey(RadianceCacheKey key)
{
    float cs = RadianceCacheCellSizeAtLevel(key.level);
    return (float3(key.cellPos) + 0.5) * cs;
}

static uint RadianceCachePackAxis(int v) { return uint(clamp(v + 32768, 0, 65535)); }
static int RadianceCacheUnpackAxis(uint v) { return int(v) - 32768; }

public uint2 RadianceCachePackKey(RadianceCacheKey key)
{
    uint word0 = RadianceCachePackAxis(key.cellPos.x) | (RadianceCachePackAxis(key.cellPos.y) << 16u);
    uint word1 = RadianceCachePackAxis(key.cellPos.z) | ((key.level & 0xFu) << 16u) | ((key.normalBucket & 0x7u) << 20u);
    return uint2(word0, word1);
}

public RadianceCacheKey RadianceCacheUnpackKey(uint2 packed)
{
    RadianceCacheKey key;
    key.cellPos = int3(RadianceCacheUnpackAxis(packed.x & 0xFFFFu), RadianceCacheUnpackAxis(packed.x >> 16u), RadianceCacheUnpackAxis(packed.y & 0xFFFFu));
    key.level = (packed.y >> 16u) & 0xFu;
    key.normalBucket = (packed.y >> 20u) & 0x7u;
    return key;
}

static uint RadianceCacheHashInt3(int3 p, uint level, uint bucket, uint seed)
{
    uint h = uint(p.x) * 0x8da6b343u ^ uint(p.y) * 0xd8163841u ^ uint(p.z) * 0xcb1ab31fu ^ (level * 0x9e3779b1u) ^ (bucket * 0x85ebca77u) ^ (seed * 0x9E3779B9u);
    h ^= h >> 15u; h *= 0x2c1b3c6du; h ^= h >> 12u; h *= 0x297a2d39u; h ^= h >> 15u;
    return h;
}

public uint RadianceCacheHashSlot(RadianceCacheKey key)
{
    return RadianceCacheHashInt3(key.cellPos, key.level, key.normalBucket, 0u) & (RADIANCE_CACHE_HASH_CAPACITY - 1u);
}

static uint RadianceCacheChecksum(RadianceCacheKey key)
{
    uint c = RadianceCacheHashInt3(key.cellPos, key.level, key.normalBucket, 0x06f3a9b1u);
    return (c == RADIANCE_CACHE_HASH_EMPTY) ? 1u : c;
}

public uint RadianceCacheHashFind(uint* entries, RadianceCacheKey key)
{
    uint slot = RadianceCacheHashSlot(key);
    uint cs = RadianceCacheChecksum(key);
    for (uint i = 0u; i < RADIANCE_CACHE_HASH_PROBE; i++) {
        uint s = (slot + i) & (RADIANCE_CACHE_HASH_CAPACITY - 1u);
        uint e = entries[s];
        if (e == cs) { return s; }
        if (e == RADIANCE_CACHE_HASH_EMPTY) { return RADIANCE_CACHE_HASH_INVALID; }
    }
    return RADIANCE_CACHE_HASH_INVALID;
}

// Exact-level find with +/-1 LOD tolerance: a surface that just crossed a LOD ring reads the surviving neighbour-level cell (still smoothed) until its own-level cell converges, instead of the raw probe tier.
public uint RadianceCacheHashFindLodTolerant(uint* entries, RadianceCacheKey key, float3 worldPos)
{
    uint slot = RadianceCacheHashFind(entries, key);
    if (slot != RADIANCE_CACHE_HASH_INVALID) { return slot; }

    if (key.level > 0u) {
        RadianceCacheKey finer = key;
        finer.level = key.level - 1u;
        float fcs = RadianceCacheCellSizeAtLevel(finer.level);
        finer.cellPos = int3(floor(worldPos / fcs));
        slot = RadianceCacheHashFind(entries, finer);
        if (slot != RADIANCE_CACHE_HASH_INVALID) { return slot; }
    }

    if (key.level < RADIANCE_CACHE_MAX_LEVEL) {
        RadianceCacheKey coarser = key;
        coarser.level = key.level + 1u;
        coarser.cellPos = key.cellPos >> 1;
        slot = RadianceCacheHashFind(entries, coarser);
    }
    return slot;
}

// isNew true only when this call claimed a previously-empty slot; caller must init that slot's payload.
// Checksum-only match below: comparing keys[] here too would race the CAS winner's same-dispatch write to it.
// stats: full-probe failures bump stats->insertsFailed; shared by trace and carry-forward inserts.
public uint RadianceCacheHashInsert(uint* entries, uint2* keys, RadianceCacheStats* stats, RadianceCacheKey key, out bool isNew)
{
    uint slot = RadianceCacheHashSlot(key);
    uint cs = RadianceCacheChecksum(key);
    isNew = false;
    for (uint i = 0u; i < RADIANCE_CACHE_HASH_PROBE; i++) {
        uint s = (slot + i) & (RADIANCE_CACHE_HASH_CAPACITY - 1u);
        uint prev;
        InterlockedCompareExchange(entries[s], RADIANCE_CACHE_HASH_EMPTY, cs, prev);
        if (prev == RADIANCE_CACHE_HASH_EMPTY) {
            keys[s] = RadianceCachePackKey(key);
            isNew = true;
            return s;
        }
        if (prev == cs) { return s; }
    }
    uint prevFail;
    InterlockedAdd(stats->insertsFailed, 1u, prevFail);
    return RADIANCE_CACHE_HASH_INVALID;
}

// Seeds a newly-created cell from whichever adjacent LOD already has data, instead of starting black.
// Parent (level+1) is a single lookup, one candidate. Children (level-1) are up to 8, averaged if found.
public uint2 RadianceCacheWarmStartRadiance(uint* entries, RadianceCacheCell* cells, RadianceCacheKey key, out uint seedCount, out uint seedEmissive)
{
    seedCount = 0u;
    seedEmissive = 0u;
    if (key.level < RADIANCE_CACHE_MAX_LEVEL) {
        RadianceCacheKey parentKey = key;
        parentKey.level = key.level + 1u;
        parentKey.cellPos = key.cellPos >> 1;
        uint parentSlot = RadianceCacheHashFind(entries, parentKey);
        if (parentSlot != RADIANCE_CACHE_HASH_INVALID) {
            uint2 packed = cells[parentSlot].packedRadiance;
            if (RadianceCacheRadianceShaded(packed)) {
                seedCount = min((cells[parentSlot].changeStreak >> 16u) & 0xFFu, RADIANCE_CACHE_WARMSTART_SEED_CAP);
                seedEmissive = cells[parentSlot].packedEmissive;
                return packed;
            }
        }
    }

    if (key.level > 0u) {
        float3 sum = float3(0.0);
        float3 emissiveSum = float3(0.0);
        uint found = 0u;
        uint minChildCount = RADIANCE_CACHE_ACCUM_FRAMES;
        RadianceCacheKey childKey = key;
        childKey.level = key.level - 1u;
        for (uint i = 0u; i < 8u; i++) {
            childKey.cellPos = key.cellPos * 2 + int3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
            uint childSlot = RadianceCacheHashFind(entries, childKey);
            if (childSlot != RADIANCE_CACHE_HASH_INVALID) {
                uint2 packed = cells[childSlot].packedRadiance;
                if (RadianceCacheRadianceShaded(packed)) {
                    sum += RadianceCacheUnpackRadiance(packed);
                    emissiveSum += UnpackEmissiveRGBE(cells[childSlot].packedEmissive);
                    minChildCount = min(minChildCount, (cells[childSlot].changeStreak >> 16u) & 0xFFu);
                    found++;
                }
            }
        }
        if (found > 0u) {
            seedCount = min(minChildCount, RADIANCE_CACHE_WARMSTART_SEED_CAP);
            seedEmissive = PackEmissiveRGBE(emissiveSum / float(found));
            return RadianceCachePackRadiance(sum / float(found));
        }
    }

    return uint2(RADIANCE_CACHE_RADIANCE_UNSHADED, RADIANCE_CACHE_RADIANCE_UNSHADED);
}
