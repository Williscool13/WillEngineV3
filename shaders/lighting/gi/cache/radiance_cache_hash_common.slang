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

public static const float RADIANCE_CACHE_KEY_NORMAL_BIAS = 0.5;

// A cell is a surface patch: the tangential footprint grows with level, but the bucket's dominant axis stays at base resolution at every level so co-normal surfaces closer than a coarse cell (interior vs exterior face of one wall) can never share a key.
public float3 RadianceCacheQuantum(uint normalBucket, uint level)
{
    float cs = RadianceCacheCellSizeAtLevel(level);
    float3 quantum = float3(cs, cs, cs);
    uint axis = normalBucket >> 1u;
    if (axis == 0u) {
        quantum.x = RADIANCE_CACHE_CELL_SIZE_BASE;
    } else if (axis == 1u) {
        quantum.y = RADIANCE_CACHE_CELL_SIZE_BASE;
    } else {
        quantum.z = RADIANCE_CACHE_CELL_SIZE_BASE;
    }
    return quantum;
}

// Half-quantum normal bias keys the surface's own air side: geometry sitting exactly on a cell plane otherwise coin-flips between the two adjacent cells (at insert AND find).
static int3 RadianceCacheQuantize(float3 worldPos, float3 normal, uint normalBucket, uint level)
{
    float3 quantum = RadianceCacheQuantum(normalBucket, level);
    return int3(floor((worldPos + normal * (RADIANCE_CACHE_KEY_NORMAL_BIAS * quantum)) / quantum));
}

public RadianceCacheKey RadianceCacheComputeKey(float3 worldPos, float3 normal, float3 cameraPos)
{
    RadianceCacheKey key;
    key.level = RadianceCacheLevelFromDistance(distance(worldPos, cameraPos));
    key.normalBucket = RadianceCacheNormalBucket(normal);
    key.cellPos = RadianceCacheQuantize(worldPos, normal, key.normalBucket, key.level);
    return key;
}

public float3 RadianceCacheCellCenterFromKey(RadianceCacheKey key)
{
    float3 quantum = RadianceCacheQuantum(key.normalBucket, key.level);
    return (float3(key.cellPos) + 0.5) * quantum - RadianceCacheBucketNormal(key.normalBucket) * (RADIANCE_CACHE_KEY_NORMAL_BIAS * RADIANCE_CACHE_CELL_SIZE_BASE);
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
public uint RadianceCacheHashFindLodTolerant(uint* entries, RadianceCacheKey key, float3 worldPos, float3 normal)
{
    uint slot = RadianceCacheHashFind(entries, key);
    if (slot != RADIANCE_CACHE_HASH_INVALID) { return slot; }

    if (key.level > 0u) {
        RadianceCacheKey finer = key;
        finer.level = key.level - 1u;
        finer.cellPos = RadianceCacheQuantize(worldPos, normal, key.normalBucket, finer.level);
        slot = RadianceCacheHashFind(entries, finer);
        if (slot != RADIANCE_CACHE_HASH_INVALID) { return slot; }
    }

    if (key.level < RADIANCE_CACHE_MAX_LEVEL) {
        RadianceCacheKey coarser = key;
        coarser.level = key.level + 1u;
        // Requantized (not cellPos >> 1) so the biased coordinate matches a native coarse-level key.
        coarser.cellPos = RadianceCacheQuantize(worldPos, normal, key.normalBucket, coarser.level);
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
    if (stats != nullptr) {
        uint prevFail;
        InterlockedAdd(stats->insertsFailed, 1u, prevFail);
    }
    return RADIANCE_CACHE_HASH_INVALID;
}

// Seeds a newly-created cell from whichever adjacent LOD already has data, instead of starting black.
// Parent (level+1) is a single lookup, one candidate. Children (level-1) are up to 4 (the bucket axis does not subdivide across levels), averaged if found.
public uint2 RadianceCacheWarmStartRadiance(uint* entries, RadianceCacheCell* cells, RadianceCacheKey key, out uint seedCount, out uint seedEmissive)
{
    seedCount = 0u;
    seedEmissive = 0u;
    uint axis = key.normalBucket >> 1u;
    if (key.level < RADIANCE_CACHE_MAX_LEVEL) {
        RadianceCacheKey parentKey = key;
        parentKey.level = key.level + 1u;
        parentKey.cellPos = key.cellPos >> 1;
        if (axis == 0u) {
            parentKey.cellPos.x = key.cellPos.x;
        } else if (axis == 1u) {
            parentKey.cellPos.y = key.cellPos.y;
        } else {
            parentKey.cellPos.z = key.cellPos.z;
        }
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
        for (uint i = 0u; i < 4u; i++) {
            int u = int(i & 1u);
            int v = int((i >> 1u) & 1u);
            if (axis == 0u) {
                childKey.cellPos = int3(key.cellPos.x, key.cellPos.y * 2 + u, key.cellPos.z * 2 + v);
            } else if (axis == 1u) {
                childKey.cellPos = int3(key.cellPos.x * 2 + u, key.cellPos.y, key.cellPos.z * 2 + v);
            } else {
                childKey.cellPos = int3(key.cellPos.x * 2 + u, key.cellPos.y * 2 + v, key.cellPos.z);
            }
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
