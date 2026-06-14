/*
 * SIGMA shadow denoiser: tile classification.
 * Reduces each 16x16 tile to (minVisibility, maxVisibility). The blur reads a tile neighborhood
 * to skip fully-lit / fully-shadowed regions, so only penumbra tiles pay the full filter.
 */
import common_interop;
import push_constant_interop;
import bindless_declarations;

static const uint SIGMA_TILE_SIZE = 16;

groupshared float gMin[SIGMA_TILE_SIZE * SIGMA_TILE_SIZE];
groupshared float gMax[SIGMA_TILE_SIZE * SIGMA_TILE_SIZE];

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeMain(uniform SigmaClassifyPushConstant pc, uint2 groupThread : SV_GroupThreadID, uint2 group : SV_GroupID)
{
    int2 sp = min(int2(group * SIGMA_TILE_SIZE + groupThread), int2(pc.renderExtent) - 1);
    float vis = rdgTexturesFloat2[pc.shadowIndex].Load(int3(sp, 0)).r;

    uint li = groupThread.y * SIGMA_TILE_SIZE + groupThread.x;
    gMin[li] = vis;
    gMax[li] = vis;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint s = (SIGMA_TILE_SIZE * SIGMA_TILE_SIZE) / 2; s > 0; s >>= 1)
    {
        if (li < s) { gMin[li] = min(gMin[li], gMin[li + s]); gMax[li] = max(gMax[li], gMax[li + s]); }
        GroupMemoryBarrierWithGroupSync();
    }

    if (li == 0) { rdgStorageFloat2[pc.outputIndex][group] = float2(gMin[0], gMax[0]); }
}
