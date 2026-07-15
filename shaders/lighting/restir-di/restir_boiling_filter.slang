import push_constant_interop;
import restir_interop;
import restir_common;

groupshared float2 gBoiling[256];

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReSTIRBoilingFilter(uniform ReSTIRBoilingFilterPushConstant pc, uint3 threadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    uint field = pc.activeCheckerboardField;
    uint stride = RestirCheckerboardStride(pc.renderExtent.x, field);
    uint2 reservoirCoord = threadID.xy;
    bool oob = reservoirCoord.x >= stride || reservoirCoord.y >= pc.renderExtent.y;
    uint pixelIdx = reservoirCoord.y * stride + reservoirCoord.x;

    Reservoir r = oob ? ReservoirEmpty() : pc.inputBuffer[pixelIdx];

    bool boilable = (r.lightIdx != ~0u && r.lightIdx != LIGHT_IDX_SUN);
    float w = boilable ? r.W : 0.0;

    gBoiling[groupIndex] = float2(w, (w > 0.0) ? 1.0 : 0.0);
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (groupIndex < s) { gBoiling[groupIndex] += gBoiling[groupIndex + s]; }
        GroupMemoryBarrierWithGroupSync();
    }
    float2 totals = gBoiling[0];

    if (oob) { return; }

    if (totals.y > 0.0 && pc.strength > 0.0) {
        float mean = totals.x / totals.y;
        float multiplier = 10.0 / clamp(pc.strength, 1e-6, 1.0) - 9.0;
        if (w > mean * multiplier) { r = ReservoirEmpty(); }
    }

    pc.outputBuffer[pixelIdx] = r;
}
