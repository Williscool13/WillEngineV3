import common_interop;
import model_interop;
import constants_interop;
import instancing_interop;
import shadows_interop;
import push_constant_interop;

import shader_functions;

struct ShadowPayload {
    uint32_t modelIndex;
    uint32_t groupMeshletOffset;
    uint8_t meshletIndices[TASK_SHADER_DISPATCH_X];
};

groupshared ShadowPayload sharedPayload;
groupshared uint32_t visibleMeshletCount;

[shader("task")]
[numthreads(TASK_SHADER_DISPATCH_X, 1, 1)]
void taskMain(uniform ShadowMeshShadingPushConstant pushConstant,
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint drawIndex : SV_DrawIndex)
{
    InstancedMeshIndirectDrawParameters indirectParameters = pushConstant.indirectBuffer[drawIndex];
    uint32_t instanceIndex = indirectParameters.compactedInstanceStart + gid.y;
    Instance inst = pushConstant.compactedInstanceBuffer[instanceIndex];
    uint32_t groupMeshletOffset = indirectParameters.meshletOffset + (gid.x * TASK_SHADER_DISPATCH_X);
    uint32_t totalMeshlets = indirectParameters.meshletCount;

    if (gtid.x == 0) {
        visibleMeshletCount = 0;
        sharedPayload.modelIndex = inst.modelIndex;
        sharedPayload.groupMeshletOffset = groupMeshletOffset;
    }
    GroupMemoryBarrierWithGroupSync();

    Model model = pushConstant.modelBuffer[sharedPayload.modelIndex];
    uint8_t meshletID = (uint8_t)gtid.x;
    uint localMeshletCount = min(TASK_SHADER_DISPATCH_X, totalMeshlets - (gid.x * TASK_SHADER_DISPATCH_X));

    if (meshletID < localMeshletCount) {
        Meshlet mlet = pushConstant.meshletBuffer[groupMeshletOffset + meshletID];

        float3 scale = float3(length(model.modelMatrix[0].xyz), length(model.modelMatrix[1].xyz), length(model.modelMatrix[2].xyz));
        float maxScale = max(max(scale.x, scale.y), scale.z);
        float worldRadius = mlet.meshletBoundingSphere.w * maxScale;
        float3 worldCenter = mul(model.modelMatrix, float4(mlet.meshletBoundingSphere.xyz, 1.0)).xyz;
        float4 worldBounds = float4(worldCenter, worldRadius);

        bool visible = frustumCullSphere(pushConstant.shadowData->lightFrustums[pushConstant.cascadeIndex], worldBounds);

        if (visible) {
            uint index;
            InterlockedAdd(visibleMeshletCount, 1, index);
            sharedPayload.meshletIndices[index] = meshletID;
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (gtid.x == 0) {
        DispatchMesh(visibleMeshletCount, 1, 1, sharedPayload);
    }
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void meshMain(
    in payload ShadowPayload inPayload,
    out indices uint3 triangles[MESHLET_MAX_TRIANGLES],
    out vertices float4 positions[MESHLET_MAX_VERTICES] : SV_Position,
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform ShadowMeshShadingPushConstant pushConstant)
{
    uint32_t meshletIndex = inPayload.groupMeshletOffset + inPayload.meshletIndices[gid.x];
    Meshlet meshlet = pushConstant.meshletBuffer[meshletIndex];
    Model m = pushConstant.modelBuffer[inPayload.modelIndex];
    float4x4 lightViewProj = pushConstant.shadowData->lightSpaceMatrices[pushConstant.cascadeIndex];

    SetMeshOutputCounts(meshlet.meshletVertexCount, meshlet.meshletTriangleCount);

    for (uint i = gtid.x; i < meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        uint localVertexIndex = pushConstant.meshletVerticesBuffer[meshlet.meshletVertexOffset + i];
        Vertex v = pushConstant.vertexBuffer[meshlet.vertexOffset + localVertexIndex];
        positions[i] = mul(lightViewProj, mul(m.modelMatrix, float4(v.position, 1.0)));
    }

    for (uint i = gtid.x; i < meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) {
        uint packed = pushConstant.meshletTrianglesBuffer[meshlet.meshletTriangleOffset + i];
        triangles[i] = uint3((packed >> 0) & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
    }
}