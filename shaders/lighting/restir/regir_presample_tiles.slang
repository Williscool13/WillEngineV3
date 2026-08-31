import push_constant_interop;
import restir_interop;
import light_index_common;

import random_common;

[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeReGIRPresampleTiles(uniform ReGIRPresampleTilesPushConstant pc, uint3 globalID : SV_DispatchThreadID)
{
    uint slotIdx = globalID.x;
    uint totalSlots = REGIR_TILE_COUNT * REGIR_TILE_SIZE;
    if (slotIdx >= totalSlots) { return; }

    uint count = pc.liveCount;

    ReGIRTileSlot slot;
    if (count == 0u) {
        slot.lightIdx = ~0u;
        slot.sourcePdf = 0.0;
        pc.tiles[slotIdx] = slot;
        return;
    }

    uint h = SeedRng(uint2(slotIdx & 0xFFFFu, slotIdx >> 16u), pc.frameIndex, 0x9E3779B9u);

    float total = pc.cdf[count - 1u];
    float u = HashStep(h);
    if (total <= 0.0) {
        slot.lightIdx = LiveLightIndex(min(uint(u * float(count)), count - 1u), pc.analyticCount);
        slot.sourcePdf = 1.0 / float(count);
    } else {
        float target = u * total;
        uint lo = 0u;
        uint hi = count - 1u;
        while (lo < hi) {
            uint mid = (lo + hi) >> 1u;
            if (pc.cdf[mid] > target) { hi = mid; } else { lo = mid + 1u; }
        }
        float lower = lo > 0u ? pc.cdf[lo - 1u] : 0.0;
        slot.lightIdx = LiveLightIndex(lo, pc.analyticCount);
        slot.sourcePdf = (pc.cdf[lo] - lower) / total;
    }

    pc.tiles[slotIdx] = slot;
}
