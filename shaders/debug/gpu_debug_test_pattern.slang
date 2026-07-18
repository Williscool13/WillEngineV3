import common_interop;
import model_interop;
import push_constant_interop;
import gpu_debug_draw_common;
import lights_interop;
import world_grid_interop;
import binning_common;
import packing_functions;
import world_cache_hash_common;
import shader_functions;
import world_cache_interop;

// =============== Shader Summary ============
// GPU debug draw support: a test pattern producer (reference for real producers).

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeGPUDebugTestPattern(uint3 dtid: SV_DispatchThreadID, uniform GPUDebugTestPatternPushConstant pc)
{
    float phase = float(pc.frameIndex) * 0.03 + float(dtid.x + dtid.y) * 0.4;
    float3 center = float3((float(dtid.x) - 3.5) * 3.0, 2.0 + sin(phase), (float(dtid.y) - 3.5) * 3.0);
    float4 color = float4(float(dtid.x) / 7.0, float(dtid.y) / 7.0, 1.0 - float(dtid.x) / 7.0, 1.0);
    GPUDebugAddAABB(pc.args, pc.segmentBuffer, center - 0.5, center + 0.5, 0.02, color);

    float3 axis = normalize(float3(cos(phase), 0.4, sin(phase))) * 0.5;
    GPUDebugAddSphere(pc.sphereArgs, pc.sphereBuffer, center + float3(0.0, 1.6, 0.0), 0.35, float4(0.5, axis), float4(0.5, -axis), float4(0.2, 0.0, 0.0, 0.0));
}
