import push_constant_interop;
import bindless_declarations;
import gi_gather_common;

// =============== Shader Summary ============
// Gather debug views written to gi_gather_debug_target for the debug visualizer, so the lit composite and its history stay authentic while inspecting.
// Modes (shader-side, UI mode + 1): 2 resolved irradiance, 3 fallback tier, 4 hit distance, 5 accumulation, 6 first-ray escape, 7 variance guide (encoded relDev / GI_GATHER_VARIANCE_GUIDE_SCALE).

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeGIGatherDebug(uniform GIGatherDebugPushConstant pc, uint3 dtid: SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }
    if (pc.mode == 7u) {
        float g = pc.varGuideIndex != ~0x0u ? rdgTexturesFloat[pc.varGuideIndex].Load(int3(pixel >> 1u, 0)).r : 0.0;
        float relDev = g * GI_GATHER_VARIANCE_GUIDE_SCALE;
        // Below 1 stays a gray ramp; banded above so the magnitude reads at a glance.
        float3 color = float3(relDev);
        if (relDev >= 4.0) {
            color = float3(1.0, 0.0, 1.0);
        } else if (relDev >= 3.0) {
            color = float3(0.1, 0.9, 0.1);
        } else if (relDev >= 2.0) {
            color = float3(0.15, 0.3, 1.0);
        } else if (relDev >= 1.0) {
            color = float3(1.0, 0.1, 0.1);
        }
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(color, 1.0);
        return;
    }
    float3 indirectDiffuse = rdgTexturesFloat4[pc.resolvedIndex].Load(int3(pixel, 0)).rgb;
    float3 color = GIGatherDebugColor(pc.dataIndex, pc.resolvedIndex, pixel, pc.mode, indirectDiffuse);
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(color, 1.0);
}
