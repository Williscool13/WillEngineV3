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

#if WILL_EDITOR
    uint prevStat;
    if (pc.stats != nullptr) {
        InterlockedAdd(pc.stats->occupiedSlots, 1u, prevStat);
    }
#endif

    RadianceCacheCell cell = pc.prevCells[i];
    RadianceCacheKey key = RadianceCacheUnpackKey(pc.prevKeys[i]);
    if (pc.touchEntries != nullptr && RadianceCacheHashFind(pc.touchEntries, nullptr, key) != RADIANCE_CACHE_HASH_INVALID) {
        cell.lastTouched = max(cell.lastTouched, pc.touchFrame);
    }
    if (pc.bFreeze != 0u) {
        cell.lastTouched = pc.frameIndex;
    } else {
        uint age = pc.frameIndex - cell.lastTouched;
        // Only probe rays queue shades, so gather touches alone would keep stale radiance alive. Never-shaded cells await their first shade.
        bool bStale = cell.lastShaded != RADIANCE_CACHE_RADIANCE_UNSHADED && pc.frameIndex - cell.lastShaded > pc.staleShadeAge;
        if (age >= RADIANCE_CACHE_LRU_THRESHOLD || bStale) {
#if WILL_EDITOR
            if (pc.stats != nullptr) {
                InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
            }
#endif
            return;
        }

        // LOD revalidation
        uint impliedLevel = RadianceCacheLevelFromDistance(distance(RadianceCacheCellCenterFromKey(key), pc.cameraPos.xyz));
        if (uint(abs(int(key.level) - int(impliedLevel))) >= RADIANCE_CACHE_LOD_REVALIDATE_MARGIN) {
#if WILL_EDITOR
            if (pc.stats != nullptr) {
                InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
            }
#endif
            return;
        }
    }

    bool isNew;
    uint slot = RadianceCacheHashInsert(pc.nextEntries, pc.nextKeys, pc.stats, key, isNew);
    // !isNew = checksum alias of a survivor already carried; drop this one instead of overwriting that cell.
    if (slot == RADIANCE_CACHE_HASH_INVALID || !isNew) {
#if WILL_EDITOR
        if (pc.stats != nullptr) {
            InterlockedAdd(pc.stats->cellsEvicted, 1u, prevStat);
        }
#endif
        return;
    }
    pc.nextCells[slot] = cell;
#if WILL_EDITOR
    if (pc.stats != nullptr) {
        InterlockedAdd(pc.stats->cellsCarried, 1u, prevStat);
    }
#endif
}
