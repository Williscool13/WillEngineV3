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
// GPU debug draw support: converts the appended segment count into mesh-task indirect args.

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeGPUDebugBuildIndirect(uniform GPUDebugBuildIndirectPushConstant pc)
{
    uint count = min(pc.args->segmentCount, pc.args->capacity);
    pc.args->segmentCount = count;
    pc.args->groupCountX = (count + 31) / 32;
    pc.args->groupCountY = 1;
    pc.args->groupCountZ = 1;

    pc.sphereArgs->instanceCount = min(pc.sphereArgs->instanceCount, pc.sphereArgs->capacity);
    pc.cubeArgs->instanceCount = min(pc.cubeArgs->instanceCount, pc.cubeArgs->capacity);
}
