import push_constant_interop;
import restir_interop;

import random_common;

[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeMain(uniform ReGIRPresampleTilesPushConstant pc, uint3 globalID : SV_DispatchThreadID)
{
    uint slotIdx = globalID.x;
    uint totalSlots = REGIR_TILE_COUNT * REGIR_TILE_SIZE;
    if (slotIdx >= totalSlots) { return; }

    uint count = pc.lightCount;

    ReGIRTileSlot slot;
    if (count == 0u) {
        slot.lightIdx = ~0u;
        slot.sourcePdf = 0.0;
        pc.tiles[slotIdx] = slot;
        return;
    }

    uint h = SeedRng(uint2(slotIdx & 0xFFFFu, slotIdx >> 16u), pc.frameIndex, 0x9E3779B9u);

    uint bin = min(uint(HashStep(h) * float(count)), count - 1u);
    LightAliasEntry e = pc.aliasEntries[bin];
    float u = HashStep(h);
    if (u < e.prob) {
        slot.lightIdx = bin;
        slot.sourcePdf = e.pdf;
    } else {
        slot.lightIdx = e.alias;
        slot.sourcePdf = e.pdfAlias;
    }

    pc.tiles[slotIdx] = slot;
}
