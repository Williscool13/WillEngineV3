import push_constant_interop;
import constants_interop;

[[vk::binding(0, 0)]] RWTexture2D<float> storageBuffer[];

static const uint SEARCH_W = 64;
static const uint SEARCH_H = 16;

// x (0-63) encodes a 6-bit edge pattern.
// Count consecutive set bits from bit 0 upward; result normalised to [0,1].
// y selects between the two search directions (x-search vs y-search).
float SearchValue(uint pattern)
{
    uint count = 0u;
    for (uint i = 0u; i < 6u; i++) {
        if (((pattern >> i) & 1u) != 0u) { count++; }
        else { break; }
    }
    return float(count) / 6.0;
}

[numthreads(SMAA_SEARCH_GENERATION_DISPATCH_X, SMAA_SEARCH_GENERATION_DISPATCH_Y, 1)]
void GenerateSMAASearch(uniform SMAASearchGeneratePushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= SEARCH_W || tid.y >= SEARCH_H) { return; }
    storageBuffer[pc.targetIndex][tid.xy] = SearchValue(tid.x);
}
