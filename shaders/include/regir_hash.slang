module regir_hash;

import restir_interop;

/**
 * World-space hash-grid key for ReGIR. Distance-adaptive: cell size doubles every level so far cells stay ~constant screen size and the visible cell count stays bounded (a fixed cell size explodes the distinct-cell.
 */
public struct ReGIRHashKey
{
    public int3 cellPos;
    public uint level;
};

/** Cell edge length at a LOD level (finest size doubled per level). */
public float ReGIRCellSizeAtLevel(uint level)
{
    return REGIR_CELL_SIZE_X * exp2(float(level));
}

/** LOD level for a world point from its distance to the camera. */
public uint ReGIRLevelFromDistance(float dist)
{
    float l = floor(log2(max(dist / REGIR_LOD_BASE_DIST, 1.0)));
    return uint(clamp(l, 0.0, float(REGIR_MAX_LEVEL)));
}

/** Derives the hash key for a world-space point. The single shared entry point for all passes. */
public ReGIRHashKey ReGIRComputeKey(float3 worldPos, float3 cameraPos)
{
    ReGIRHashKey key;
    key.level = ReGIRLevelFromDistance(distance(worldPos, cameraPos));
    float cs = ReGIRCellSizeAtLevel(key.level);
    key.cellPos = int3(floor(worldPos / cs));
    return key;
}

/** World-space center of a key's cell; the fill pass anchors light sampling here. */
public float3 ReGIRCellCenterFromKey(ReGIRHashKey key)
{
    float cs = ReGIRCellSizeAtLevel(key.level);
    return (float3(key.cellPos) + 0.5) * cs;
}

public uint ReGIRCellReservoirBase(uint cellSlot)
{
    return cellSlot * REGIR_RESERVOIRS_PER_CELL;
}

// activeCells packs (level, slot) into the .w lane so fill recovers both without an inverse hash.
public uint ReGIRPackActive(uint level, uint slot) { return (level << 24u) | (slot & 0x00FFFFFFu); }
public uint ReGIRActiveSlot(uint packed) { return packed & 0x00FFFFFFu; }
public uint ReGIRActiveLevel(uint packed) { return packed >> 24u; }

static uint ReGIRHashInt3(int3 p, uint level, uint seed)
{
    uint h = uint(p.x) * 0x8da6b343u ^ uint(p.y) * 0xd8163841u ^ uint(p.z) * 0xcb1ab31fu ^ (level * 0x9e3779b1u) ^ (seed * 0x9E3779B9u);
    h ^= h >> 15u; h *= 0x2c1b3c6du; h ^= h >> 12u; h *= 0x297a2d39u; h ^= h >> 15u;
    return h;
}

/** Primary bucket for a key. */
public uint ReGIRHashSlot(ReGIRHashKey key)
{
    return ReGIRHashInt3(key.cellPos, key.level, 0u) & (REGIR_HASH_CAPACITY - 1u);
}

/** Per-key checksum stored in the entries buffer to detect collisions; forced nonzero (0 = empty). */
public uint ReGIRChecksum(ReGIRHashKey key)
{
    uint c = ReGIRHashInt3(key.cellPos, key.level, 0x06f3a9b1u);
    return (c == REGIR_HASH_EMPTY) ? 1u : c;
}

/**
 * Read-only probe for an existing cell-slot. Returns REGIR_HASH_INVALID if the key is absent.
 * Used by fill (carry from the previous table) and by consume.
 *
 * @param entries Per-slot checksum buffer (length REGIR_HASH_CAPACITY).
 * @param key Cell key from ReGIRComputeKey.
 */
public uint ReGIRHashFind(uint* entries, ReGIRHashKey key)
{
    uint slot = ReGIRHashSlot(key);
    uint cs = ReGIRChecksum(key);
    for (uint i = 0u; i < REGIR_HASH_PROBE; i++)
    {
        uint s = (slot + i) & (REGIR_HASH_CAPACITY - 1u);
        uint e = entries[s];
        if (e == cs) { return s; }
        if (e == REGIR_HASH_EMPTY) { return REGIR_HASH_INVALID; }
    }
    return REGIR_HASH_INVALID;
}

/**
 * Atomically claims (or finds) the cell-slot for a key. Used by the touch pass.
 * isNew is true only when this call claimed a previously empty slot, so the caller appends it to the active list exactly once.
 *
 * @param entries Per-slot checksum buffer (length REGIR_HASH_CAPACITY).
 * @param key Cell key from ReGIRComputeKey.
 * @param isNew Output: true if this call created the slot (first toucher this frame).
 * @return REGIR_HASH_INVALID if the probe window is full.
 */
public uint ReGIRHashInsert(uint* entries, ReGIRHashKey key, out bool isNew)
{
    uint slot = ReGIRHashSlot(key);
    uint cs = ReGIRChecksum(key);
    isNew = false;
    for (uint i = 0u; i < REGIR_HASH_PROBE; i++)
    {
        uint s = (slot + i) & (REGIR_HASH_CAPACITY - 1u);
        uint prev;
        InterlockedCompareExchange(entries[s], REGIR_HASH_EMPTY, cs, prev);
        if (prev == REGIR_HASH_EMPTY) { isNew = true; return s; }
        if (prev == cs) { return s; }
    }
    return REGIR_HASH_INVALID;
}
