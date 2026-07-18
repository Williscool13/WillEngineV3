import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;

import shader_constants;
import shader_functions;
import bindless_declarations;

// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeShadeDispatchBucketingResolve(uint3 dtid : SV_DispatchThreadID, uniform ShadeBucketingResolvePushConstant pc)
{
    uint32_t materialIndex = dtid.x;
    if (materialIndex >= pc.materialCount) {
        return;
    }

    ShadeDispatchParameters dispatchParams = pc.shadeDispatchBuffer[materialIndex];
    if (dispatchParams.minX == UINT32_MAX) {
        pc.shadeDispatchBuffer[materialIndex].xDispatch = 0;
        pc.shadeDispatchBuffer[materialIndex].yDispatch = 0;
        pc.shadeDispatchBuffer[materialIndex].zDispatch = 0;
        return;
    }

    uint32_t width = dispatchParams.maxX - dispatchParams.minX;
    uint32_t height = dispatchParams.maxY - dispatchParams.minY;

    pc.shadeDispatchBuffer[materialIndex].xDispatch = (width + 15) / 16;
    pc.shadeDispatchBuffer[materialIndex].yDispatch = (height + 15) / 16;
    pc.shadeDispatchBuffer[materialIndex].zDispatch = 1;
}
