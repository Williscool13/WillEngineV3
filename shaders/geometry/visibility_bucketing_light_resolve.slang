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
void ComputeLightDispatchBucketingResolve(uint3 dtid : SV_DispatchThreadID, uniform LightingBucketingResolvePushConstant pc)
{
    uint32_t lightingIndex = dtid.x;
    if (lightingIndex >= pc.lightingCount) {
        return;
    }

    LightingDispatchParameters dispatchParams = pc.lightDispatchBuffer[lightingIndex];
    if (dispatchParams.minX == UINT32_MAX) {
        pc.lightDispatchBuffer[lightingIndex].xDispatch = 0;
        pc.lightDispatchBuffer[lightingIndex].yDispatch = 0;
        pc.lightDispatchBuffer[lightingIndex].zDispatch = 0;
        return;
    }

    uint32_t minX = dispatchParams.minX;
    uint32_t minY = dispatchParams.minY;
    uint32_t maxX = dispatchParams.maxX;
    uint32_t maxY = dispatchParams.maxY;

    uint32_t width = maxX - minX;
    uint32_t height = maxY - minY;

    pc.lightDispatchBuffer[lightingIndex].xDispatch = (width + 15) / 16;
    pc.lightDispatchBuffer[lightingIndex].yDispatch = (height + 15) / 16;
    pc.lightDispatchBuffer[lightingIndex].zDispatch = 1;
}
