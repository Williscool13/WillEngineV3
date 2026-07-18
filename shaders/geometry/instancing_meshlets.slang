import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import limits_declarations;
import bindless_declarations;

// =============== Instancing Step 2f: Gather total number of meshlets from final instance postfix sum ===============
[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeTotalMeshletCount(uniform TotalMeshletCountPushConstant pc)
{
    uint lastInstanceIndex = pc.instanceCount - 1;
    uint offset = pc.instanceMeshletOffsets[lastInstanceIndex].offset;
    uint count = pc.instanceMeshletOffsets[lastInstanceIndex].count;
    uint totalMeshlets = offset + count;

    pc.indirectDispatchBuffer.x = (totalMeshlets + 255) / INSTANCING_PREFIX_SUM_DISPATCH_X;
    pc.indirectDispatchBuffer.y = 1;
    pc.indirectDispatchBuffer.z = 1;
    pc.indirectDispatchBuffer.totalMeshlets = totalMeshlets;
}

// =============== Instancing Step 3: Expand Instance -> Meshlet through Binary Search. Also determine meshlet visibility. ===============
[shader("compute")]
[numthreads(INSTANCING_EXPAND_DISPATCH_X, 1, 1)]
void ComputeExpandInstancesToMeshlets(uniform ExpandMeshletsPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint globalMeshletIndex = dtid.x;
    if (globalMeshletIndex >= pc.indirectDispatchBuffer->totalMeshlets) {
        return;
    }

    // Binary search to find which instance owns this meshlet
    uint left = 0;
    uint right = pc.instanceCount - 1;
    uint instanceIndex = 0;

    while (left <= right) {
        uint mid = (left + right) / 2;
        uint offset = pc.instanceMeshletOffsets[mid].offset;
        uint count = pc.instanceMeshletOffsets[mid].count;
        uint endOffset = offset + count;

        if (globalMeshletIndex >= offset && globalMeshletIndex < endOffset) {
            instanceIndex = mid;
            break;
        }
        else if (globalMeshletIndex < offset) {
            right = mid - 1;
        }
        else {
            left = mid + 1;
        }
    }

    // Meshlet Visibility
    uint meshletOffset = pc.instanceMeshletOffsets[instanceIndex].offset;
    uint meshletIndexWithinLOD = globalMeshletIndex - meshletOffset;
    Instance inst = pc.instanceBuffer[instanceIndex];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model model = pc.modelBuffer[inst.modelIndex];

    uint lod = pc.instanceMeshletOffsets[instanceIndex].lod;
    Meshlet mlet = pc.meshletBuffer[prim.meshletOffset[lod] + meshletIndexWithinLOD];

    float3 scale = float3(
        length(model.modelMatrix[0].xyz),
        length(model.modelMatrix[1].xyz),
        length(model.modelMatrix[2].xyz)
    );
    float maxScale = max(max(scale.x, scale.y), scale.z);
    float worldRadius = mlet.meshletBoundingSphere.w * maxScale;
    float3 worldCenter = mul(model.modelMatrix, float4(mlet.meshletBoundingSphere.xyz, 1.0)).xyz;
    float4 worldBounds = float4(worldCenter, worldRadius);

    bool bVisible = true;
    if (!FrustumCullSphere(pc.sceneData[pc.sceneDataIndex].frustum, worldBounds)) {
        bVisible = false;
    }
    if (bVisible && !BackfaceCullCone(mlet.coneAxis, mlet.coneCutoff, worldCenter, worldRadius, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, (float3x3)model.modelMatrix)) {
        bVisible = false;
    }

    // Instance greatest bit is visibility,
    uint packedInstanceIndex = (instanceIndex & 0x7FFFFFFF) | (bVisible ? 0x80000000 : 0);
    // Meshlet 2 greatest bit indicates which LOD to use,
    uint packedLocalMeshlet = (meshletIndexWithinLOD & 0x3FFFFFFF) | (lod << 30);

    if (globalMeshletIndex < pc.currentFrameBufferMeshletLimit) {
        pc.intermediateMeshlets[globalMeshletIndex].instanceIndex = packedInstanceIndex;
        pc.intermediateMeshlets[globalMeshletIndex].meshletIndexWithinLOD = packedLocalMeshlet;
    }
}

// =============== Instancing Step 4f: Generate an indirect command to draw all visible meshlets ===============
[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeCompactedMeshletDispatch(uniform CompactedMeshletDispatchPushConstant pc)
{
    uint totalVisible = min(pc.compactedDispatchBuffer->totalVisibleMeshlets, pc.currentFrameBufferMeshletLimit);

    uint x = min(totalVisible, (MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1);
    uint y = 1;
    if (totalVisible > (MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1) {
        y = (totalVisible + (MAX_MESH_WORK_GROUP_COUNT_X / 2)) / ((MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1);
    }

    pc.compactedDispatchBuffer->x = x;
    pc.compactedDispatchBuffer->y = y;
    pc.compactedDispatchBuffer->z = 1;
}

// =============== Instancing Step 4g: Gather total number of visible meshlets from final meshlet postfix sum ===============
[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeMaxMeshletCount(uniform MaxMeshletCountPushConstant pc)
{
    pc.currentHighest[0] = max(pc.currentHighest[0], pc.indirectDispatchBuffer->totalMeshlets);
}
