import push_constant_interop;
import world_cache_hash;
import world_cache_interop;


// Rebuild-from-survivors: doubles as the double-buffer copy and as eviction (stale slots just aren't re-inserted).
[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeWorldCacheCarryForward(uint3 dtid: SV_DispatchThreadID, uniform WorldCacheCarryForwardPushConstant pc)
{
    uint i = dtid.x;
    if (i >= WORLD_CACHE_HASH_CAPACITY) { return; }

    uint checksum = pc.prevEntries[i];
    if (checksum == WORLD_CACHE_HASH_EMPTY) { return; }

    uint prevStat;
    InterlockedAdd(pc.stats->occupiedSlots, 1u, prevStat);

    WorldCacheCell cell = pc.prevCells[i];
    uint age = pc.frameIndex - cell.lastTouched;
    if (age >= WORLD_CACHE_LRU_THRESHOLD) {
        InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
        return;
    }

    WorldCacheKey key = WorldCacheUnpackKey(pc.prevKeys[i]);

    // LOD revalidation
    uint impliedLevel = WorldCacheLevelFromDistance(distance(WorldCacheCellCenterFromKey(key), pc.cameraPos.xyz));
    if (uint(abs(int(key.level) - int(impliedLevel))) >= WORLD_CACHE_LOD_REVALIDATE_MARGIN) {
        InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
        return;
    }

    bool isNew;
    uint slot = WorldCacheHashInsert(pc.nextEntries, pc.nextKeys, pc.stats, key, isNew);
    if (slot == WORLD_CACHE_HASH_INVALID) {
        InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
        return;
    }
    pc.nextCells[slot] = cell;
    InterlockedAdd(pc.stats->cellsCarried, 1u, prevStat);
}
