import push_constant_interop;
import restir_interop;

// One workgroup per cell reduces the cell's REGIR_RESERVOIRS_PER_CELL reservoir weights to their average.
// The shading-side RIS divides each candidate's build targetPdf by this average to form its source pdf (RTG2 Ch.23, Section 23.3.2).
static const uint REGIR_AVG_GROUP_SIZE = 256u;
groupshared float gPartial[REGIR_AVG_GROUP_SIZE];

[numthreads(256, 1, 1)]
void ComputeMain(uniform ReGIRCellAveragePushConstant pc, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
    uint cell = groupID.x;
    if (cell >= REGIR_CELL_COUNT) {
        return;
    }

    uint base = cell * REGIR_RESERVOIRS_PER_CELL;
    float sum = 0.0;
    for (uint i = groupIndex; i < REGIR_RESERVOIRS_PER_CELL; i += REGIR_AVG_GROUP_SIZE) {
        sum += pc.gridBuffer[base + i].totalWeight;
    }
    gPartial[groupIndex] = sum;
    GroupMemoryBarrierWithGroupSync();

    for (uint s = REGIR_AVG_GROUP_SIZE >> 1u; s > 0u; s >>= 1u) {
        if (groupIndex < s) {
            gPartial[groupIndex] += gPartial[groupIndex + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0u) {
        pc.cellAvgWeight[cell] = gPartial[0] / float(REGIR_RESERVOIRS_PER_CELL);
    }
}
