import push_constant_interop;
import radiance_cache_hash_common;
import radiance_cache_interop;


// Rebuild-from-survivors: doubles as the double-buffer copy and as eviction (stale slots just aren't re-inserted).
[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeRadianceCacheCarryForward(uint3 dtid: SV_DispatchThreadID, uniform RadianceCacheCarryForwardPushConstant pc)
{
    uint i = dtid.x;
    if (i >= RADIANCE_CACHE_HASH_CAPACITY) { return; }

    uint checksum = pc.prevEntries[i];
    if (checksum == RADIANCE_CACHE_HASH_EMPTY) { return; }

    uint prevStat;
    InterlockedAdd(pc.stats->occupiedSlots, 1u, prevStat);

    RadianceCacheCell cell = pc.prevCells[i];
    RadianceCacheKey key = RadianceCacheUnpackKey(pc.prevKeys[i]);
    if (pc.bFreeze != 0u) {
        cell.lastTouched = pc.frameIndex;
    } else {
        uint age = pc.frameIndex - cell.lastTouched;
        if (age >= RADIANCE_CACHE_LRU_THRESHOLD) {
            InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
            return;
        }

        // LOD revalidation
        uint impliedLevel = RadianceCacheLevelFromDistance(distance(RadianceCacheCellCenterFromKey(key), pc.cameraPos.xyz));
        if (uint(abs(int(key.level) - int(impliedLevel))) >= RADIANCE_CACHE_LOD_REVALIDATE_MARGIN) {
            InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
            return;
        }
    }

    bool isNew;
    uint slot = RadianceCacheHashInsert(pc.nextEntries, pc.nextKeys, pc.stats, key, isNew);
    if (slot == RADIANCE_CACHE_HASH_INVALID) {
        InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
        return;
    }
    pc.nextCells[slot] = cell;
    InterlockedAdd(pc.stats->cellsCarried, 1u, prevStat);
}
