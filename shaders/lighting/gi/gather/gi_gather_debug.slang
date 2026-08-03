import push_constant_interop;
import bindless_declarations;
import gi_gather_common;

// =============== Shader Summary ============
// Gather debug views written to gi_gather_debug_target for the debug visualizer, so the lit composite and its history stay authentic while inspecting.
// Modes (shader-side, UI mode + 1): 2 resolved irradiance, 3 fallback tier, 4 hit distance, 5 accumulation, 6 first-ray escape.

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeGIGatherDebug(uniform GIGatherDebugPushConstant pc, uint3 dtid: SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }
    float3 indirectDiffuse = rdgTexturesFloat4[pc.resolvedIndex].Load(int3(pixel, 0)).rgb;
    float3 color = GIGatherDebugColor(pc.dataIndex, pc.resolvedIndex, pixel, pc.mode, indirectDiffuse);
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(color, 1.0);
}
