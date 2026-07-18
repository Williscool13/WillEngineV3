module world_cache_hash_common;

import world_cache_interop;
import shader_functions;
import packing_functions;

public bool WorldCacheRadianceShaded(uint2 packed)
{
    return (packed.y >> 16u) != 0xFFFFu;
}

public float3 WorldCacheUnpackRadiance(uint2 packed)
{
    return UnpackHalf2x16Pair(packed).rgb;
}

public uint2 WorldCachePackRadiance(float3 radiance)
{
    return PackHalf2x16Pair(float4(radiance, 0.0));
}

public struct WorldCacheKey
{
    public int3 cellPos;
    public uint level;
    public uint normalBucket;
};

public float WorldCacheCellSizeAtLevel(uint level)
{
    return WORLD_CACHE_CELL_SIZE_BASE * exp2(float(level));
}

public uint WorldCacheLevelFromDistance(float dist)
{
    float l = floor(log2(max(dist / WORLD_CACHE_LOD_BASE_DIST, 1.0)));
    return uint(clamp(l, 0.0, float(WORLD_CACHE_MAX_LEVEL)));
}

public uint WorldCacheNormalBucket(float3 n)
{
    // Dominant-axis cube face; front/back separation only.
    float3 a = abs(n);
    if (a.x >= a.y && a.x >= a.z) { return n.x >= 0.0 ? 0u : 1u; }
    if (a.y >= a.z) { return n.y >= 0.0 ? 2u : 3u; }
    return n.z >= 0.0 ? 4u : 5u;
}

public float3 WorldCacheBucketNormal(uint bucket)
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

public WorldCacheKey WorldCacheComputeKey(float3 worldPos, float3 normal, float3 cameraPos)
{
    WorldCacheKey key;
    key.level = WorldCacheLevelFromDistance(distance(worldPos, cameraPos));
    float cs = WorldCacheCellSizeAtLevel(key.level);
    key.cellPos = int3(floor(worldPos / cs));
    key.normalBucket = WorldCacheNormalBucket(normal);
    return key;
}

public float3 WorldCacheCellCenterFromKey(WorldCacheKey key)
{
    float cs = WorldCacheCellSizeAtLevel(key.level);
    return (float3(key.cellPos) + 0.5) * cs;
}

static uint WorldCachePackAxis(int v) { return uint(clamp(v + 32768, 0, 65535)); }
static int WorldCacheUnpackAxis(uint v) { return int(v) - 32768; }

public uint2 WorldCachePackKey(WorldCacheKey key)
{
    uint word0 = WorldCachePackAxis(key.cellPos.x) | (WorldCachePackAxis(key.cellPos.y) << 16u);
    uint word1 = WorldCachePackAxis(key.cellPos.z) | ((key.level & 0xFu) << 16u) | ((key.normalBucket & 0x7u) << 20u);
    return uint2(word0, word1);
}

public WorldCacheKey WorldCacheUnpackKey(uint2 packed)
{
    WorldCacheKey key;
    key.cellPos = int3(WorldCacheUnpackAxis(packed.x & 0xFFFFu), WorldCacheUnpackAxis(packed.x >> 16u), WorldCacheUnpackAxis(packed.y & 0xFFFFu));
    key.level = (packed.y >> 16u) & 0xFu;
    key.normalBucket = (packed.y >> 20u) & 0x7u;
    return key;
}

static uint WorldCacheHashInt3(int3 p, uint level, uint bucket, uint seed)
{
    uint h = uint(p.x) * 0x8da6b343u ^ uint(p.y) * 0xd8163841u ^ uint(p.z) * 0xcb1ab31fu ^ (level * 0x9e3779b1u) ^ (bucket * 0x85ebca77u) ^ (seed * 0x9E3779B9u);
    h ^= h >> 15u; h *= 0x2c1b3c6du; h ^= h >> 12u; h *= 0x297a2d39u; h ^= h >> 15u;
    return h;
}

public uint WorldCacheHashSlot(WorldCacheKey key)
{
    return WorldCacheHashInt3(key.cellPos, key.level, key.normalBucket, 0u) & (WORLD_CACHE_HASH_CAPACITY - 1u);
}

static uint WorldCacheChecksum(WorldCacheKey key)
{
    uint c = WorldCacheHashInt3(key.cellPos, key.level, key.normalBucket, 0x06f3a9b1u);
    return (c == WORLD_CACHE_HASH_EMPTY) ? 1u : c;
}

public uint WorldCacheHashFind(uint* entries, WorldCacheKey key)
{
    uint slot = WorldCacheHashSlot(key);
    uint cs = WorldCacheChecksum(key);
    for (uint i = 0u; i < WORLD_CACHE_HASH_PROBE; i++) {
        uint s = (slot + i) & (WORLD_CACHE_HASH_CAPACITY - 1u);
        uint e = entries[s];
        if (e == cs) { return s; }
        if (e == WORLD_CACHE_HASH_EMPTY) { return WORLD_CACHE_HASH_INVALID; }
    }
    return WORLD_CACHE_HASH_INVALID;
}

// Exact-level find with +/-1 LOD tolerance: a surface that just crossed a LOD ring reads the surviving neighbour-level cell (still smoothed) until its own-level cell converges, instead of the raw probe tier.
public uint WorldCacheHashFindLodTolerant(uint* entries, WorldCacheKey key, float3 worldPos)
{
    uint slot = WorldCacheHashFind(entries, key);
    if (slot != WORLD_CACHE_HASH_INVALID) { return slot; }

    if (key.level > 0u) {
        WorldCacheKey finer = key;
        finer.level = key.level - 1u;
        float fcs = WorldCacheCellSizeAtLevel(finer.level);
        finer.cellPos = int3(floor(worldPos / fcs));
        slot = WorldCacheHashFind(entries, finer);
        if (slot != WORLD_CACHE_HASH_INVALID) { return slot; }
    }

    if (key.level < WORLD_CACHE_MAX_LEVEL) {
        WorldCacheKey coarser = key;
        coarser.level = key.level + 1u;
        coarser.cellPos = key.cellPos >> 1;
        slot = WorldCacheHashFind(entries, coarser);
    }
    return slot;
}

// isNew true only when this call claimed a previously-empty slot; caller must init that slot's payload.
// Checksum-only match below: comparing keys[] here too would race the CAS winner's same-dispatch write to it.
// stats: full-probe failures bump stats->insertsFailed; shared by trace and carry-forward inserts.
public uint WorldCacheHashInsert(uint* entries, uint2* keys, WorldCacheStats* stats, WorldCacheKey key, out bool isNew)
{
    uint slot = WorldCacheHashSlot(key);
    uint cs = WorldCacheChecksum(key);
    isNew = false;
    for (uint i = 0u; i < WORLD_CACHE_HASH_PROBE; i++) {
        uint s = (slot + i) & (WORLD_CACHE_HASH_CAPACITY - 1u);
        uint prev;
        InterlockedCompareExchange(entries[s], WORLD_CACHE_HASH_EMPTY, cs, prev);
        if (prev == WORLD_CACHE_HASH_EMPTY) {
            keys[s] = WorldCachePackKey(key);
            isNew = true;
            return s;
        }
        if (prev == cs) { return s; }
    }
    uint prevFail;
    InterlockedAdd(stats->insertsFailed, 1u, prevFail);
    return WORLD_CACHE_HASH_INVALID;
}

// Seeds a newly-created cell from whichever adjacent LOD already has data, instead of starting black.
// Parent (level+1) is a single lookup, one candidate. Children (level-1) are up to 8, averaged if found.
public uint2 WorldCacheWarmStartRadiance(uint* entries, WorldCacheCell* cells, WorldCacheKey key, out uint seedCount, out uint seedEmissive)
{
    seedCount = 0u;
    seedEmissive = 0u;
    if (key.level < WORLD_CACHE_MAX_LEVEL) {
        WorldCacheKey parentKey = key;
        parentKey.level = key.level + 1u;
        parentKey.cellPos = key.cellPos >> 1;
        uint parentSlot = WorldCacheHashFind(entries, parentKey);
        if (parentSlot != WORLD_CACHE_HASH_INVALID) {
            uint2 packed = cells[parentSlot].packedRadiance;
            if (WorldCacheRadianceShaded(packed)) {
                seedCount = min((cells[parentSlot].changeStreak >> 16u) & 0xFFu, WORLD_CACHE_WARMSTART_SEED_CAP);
                seedEmissive = cells[parentSlot].packedEmissive;
                return packed;
            }
        }
    }

    if (key.level > 0u) {
        float3 sum = float3(0.0);
        float3 emissiveSum = float3(0.0);
        uint found = 0u;
        uint minChildCount = WORLD_CACHE_ACCUM_FRAMES;
        WorldCacheKey childKey = key;
        childKey.level = key.level - 1u;
        for (uint i = 0u; i < 8u; i++) {
            childKey.cellPos = key.cellPos * 2 + int3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
            uint childSlot = WorldCacheHashFind(entries, childKey);
            if (childSlot != WORLD_CACHE_HASH_INVALID) {
                uint2 packed = cells[childSlot].packedRadiance;
                if (WorldCacheRadianceShaded(packed)) {
                    sum += WorldCacheUnpackRadiance(packed);
                    emissiveSum += UnpackEmissiveRGBE(cells[childSlot].packedEmissive);
                    minChildCount = min(minChildCount, (cells[childSlot].changeStreak >> 16u) & 0xFFu);
                    found++;
                }
            }
        }
        if (found > 0u) {
            seedCount = min(minChildCount, WORLD_CACHE_WARMSTART_SEED_CAP);
            seedEmissive = PackEmissiveRGBE(emissiveSum / float(found));
            return WorldCachePackRadiance(sum / float(found));
        }
    }

    return uint2(WORLD_CACHE_RADIANCE_UNSHADED, WORLD_CACHE_RADIANCE_UNSHADED);
}
