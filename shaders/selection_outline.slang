import push_constant_interop;
import bindless_declarations;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeMain(uint3 dtid : SV_DispatchThreadID, uniform SelectionOutlinePushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.extents)) {
        return;
    }

    uint64_t selectedId = (uint64_t(pc.selectedStableIdHi) << 32u) | uint64_t(pc.selectedStableIdLo);

    uint2 currRaw = rdgStorageUInt2[pc.stableIdIndex][pixel];
    uint64_t pixelId = (uint64_t(currRaw.y) << 32u) | uint64_t(currRaw.x);

    const int2 offsets[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };

    // Draw on selected pixels whose neighbor is OOB (screen edge)
    if (pixelId == selectedId) {
        for (int i = 0; i < 4; ++i) {
            int2 neighbor = int2(pixel) + offsets[i];
            if (any(neighbor < int2(0, 0)) || any(uint2(neighbor) >= pc.extents)) {
                rdgStorageFloat4[pc.outputColorIndex][pixel] = float4(1.0, 0.5, 0.0, 1.0);
                return;
            }
        }
        return;
    }

    // Draw on not selected pixel w/ adjacent selected
    for (int i = 0; i < 4; ++i) {
        int2 neighbor = int2(pixel) + offsets[i];
        if (any(neighbor < int2(0, 0)) || any(uint2(neighbor) >= pc.extents)) {
            continue;
        }
        uint2 nRaw = rdgStorageUInt2[pc.stableIdIndex][uint2(neighbor)];
        uint64_t neighborId = (uint64_t(nRaw.y) << 32u) | uint64_t(nRaw.x);
        if (neighborId == selectedId) {
            rdgStorageFloat4[pc.outputColorIndex][pixel] = float4(1.0, 0.5, 0.0, 1.0);
            return;
        }
    }
}
