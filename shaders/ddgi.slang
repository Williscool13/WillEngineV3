import model_interop;
import push_constant_interop;
import ddgi_interop;
import ddgi_common;
import gpu_debug_draw;

// =============== Shader Summary ============
// DDGI probe volume passes. Currently the probe placement debug viz: one flat-colored debug sphere per probe, color keyed to the lattice cell so probe identity stays visible while the window scrolls.

static float3 DDGIDebugCellColor(int3 cell)
{
    uint3 v = uint3(cell);
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return 0.15 + 0.85 * (float3(v & 0xFFFFFFu) / 16777215.0);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeDDGIProbeDebug(uint3 dtid: SV_DispatchThreadID, uniform DDGIProbeDebugPushConstant pc)
{
    if (any(dtid >= pc.volume.probeCount)) {
        return;
    }
    int3 cell = DDGICellFromSlot(int3(dtid), pc.volume);
    float3 position = DDGIProbeWorldPosition(cell, pc.volume);
    float radius = 0.08 * min(pc.volume.probeSpacing.x, min(pc.volume.probeSpacing.y, pc.volume.probeSpacing.z));
    GPUDebugAddSphereFlat(pc.sphereArgs, pc.sphereBuffer, position, radius, DDGIDebugCellColor(cell));
}
