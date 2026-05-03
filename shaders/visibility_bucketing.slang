import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;

import bindless_declarations;

// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeShadeDispatchBucketing(uint3 dtid : SV_DispatchThreadID, uniform ShadeBucketingPushConstant pc)
{
    if (dtid.x >= pc.extents.x || dtid.y >= pc.extents.y) {
        return;
    }

    uint2 vis = rdgTexturesUInt2[pc.visibilityBufferIndex].Load(int3(dtid.xy, 0)).xy;

    if (vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL) {
        return;
    }

    uint meshletIndex = vis.x >> 8;
    uint triangleIndexWithinMeshlet = vis.x & 0xFF;
    uint instanceIndex = vis.y;

    Instance inst = pc.instanceBuffer[instanceIndex];
    MaterialProperties material = pc.materialBuffer[inst.materialIndex];

    InterlockedMin(pc.shadeDispatchBuffer[material.shadingBucketIndex].minX, dtid.x);
    InterlockedMax(pc.shadeDispatchBuffer[material.shadingBucketIndex].maxX, dtid.x + 1);
    InterlockedMin(pc.shadeDispatchBuffer[material.shadingBucketIndex].minY, dtid.y);
    InterlockedMax(pc.shadeDispatchBuffer[material.shadingBucketIndex].maxY, dtid.y + 1);
}

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
    uint32_t width = dispatchParams.maxX - dispatchParams.minX;
    uint32_t height = dispatchParams.maxY - dispatchParams.minY;

    pc.shadeDispatchBuffer[materialIndex].xDispatch = (width + 15) / 16;
    pc.shadeDispatchBuffer[materialIndex].yDispatch = (height + 15) / 16;
    pc.shadeDispatchBuffer[materialIndex].zDispatch = 1;
}
