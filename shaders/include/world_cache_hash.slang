module world_cache_hash;

import world_cache_interop;
import shader_functions;

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

// isNew true only when this call claimed a previously-empty slot; caller must init that slot's payload.
// Checksum-only match below: comparing keys[] here too would race the CAS winner's same-dispatch write to it.
public uint WorldCacheHashInsert(uint* entries, uint2* keys, WorldCacheKey key, out bool isNew)
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
    return WORLD_CACHE_HASH_INVALID;
}

// Seeds a newly-created cell from whichever adjacent LOD already has data, instead of starting black.
// Parent (level+1) is a single lookup, one candidate. Children (level-1) are up to 8, averaged if found.
public uint WorldCacheWarmStartRadiance(uint* entries, WorldCacheCell* cells, WorldCacheKey key)
{
    if (key.level < WORLD_CACHE_MAX_LEVEL) {
        WorldCacheKey parentKey = key;
        parentKey.level = key.level + 1u;
        parentKey.cellPos = key.cellPos >> 1;
        uint parentSlot = WorldCacheHashFind(entries, parentKey);
        if (parentSlot != WORLD_CACHE_HASH_INVALID) {
            return cells[parentSlot].packedRadiance;
        }
    }

    if (key.level > 0u) {
        float3 sum = float3(0.0);
        uint found = 0u;
        WorldCacheKey childKey = key;
        childKey.level = key.level - 1u;
        for (uint i = 0u; i < 8u; i++) {
            childKey.cellPos = key.cellPos * 2 + int3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
            uint childSlot = WorldCacheHashFind(entries, childKey);
            if (childSlot != WORLD_CACHE_HASH_INVALID) {
                sum += UnpackEmissiveRGBE(cells[childSlot].packedRadiance);
                found++;
            }
        }
        if (found > 0u) {
            return PackEmissiveRGBE(sum / float(found));
        }
    }

    return 0u;
}
