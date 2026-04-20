import common_interop;
import model_interop;
import constants_interop;
import instancing_interop;
import shadows_interop;
import push_constant_interop;

import shader_functions;

import limits_declarations;
import bindless_declarations;

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void MeshMain(
    out indices uint3 triangles[MESHLET_MAX_TRIANGLES],
    out vertices float4 positions[MESHLET_MAX_VERTICES] : SV_Position,
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform ShadowMeshShadingPushConstant pc)
{
    uint globalMeshletIndex = gid.x + gid.y * (MAX_MESH_WORK_GROUP_COUNT_X / 2);

    if (globalMeshletIndex >= pc.compactedDispatchBuffer->totalVisibleMeshlets) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    CompactedMeshlet compacted = pc.visibleMeshlets[globalMeshletIndex];
    uint instanceIndex = compacted.instanceIndex;
    uint packedLocal = compacted.meshletIndexWithinLOD;
    uint meshletIndexWithinLOD = packedLocal & 0x3FFFFFFF;
    uint lod = packedLocal >> 30;

    Instance inst = pc.instanceBuffer[instanceIndex];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model m = pc.modelBuffer[inst.modelIndex];

    uint meshletIndex = prim.meshletOffset[lod] + meshletIndexWithinLOD;
    Meshlet meshlet = pc.meshletBuffer[meshletIndex];

    float4x4 lightViewProj = pc.shadowData->lightSpaceMatrices[pc.cascadeIndex];

    SetMeshOutputCounts(meshlet.meshletVertexCount, meshlet.meshletTriangleCount);

    for (uint i = gtid.x; i < meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        uint localVertexIndex = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + i];
        Vertex v = pc.vertexBuffer[meshlet.vertexOffset + localVertexIndex];
        float3 extent = prim.boundingBoxMax - prim.boundingBoxMin;
        float3 vertexPos = DecodePosition(v, prim.boundingBoxMin, extent);
        positions[i] = mul(lightViewProj, mul(m.modelMatrix, float4(vertexPos, 1.0)));
        positions[i] = float4(1.0f);
    }

    for (uint i = gtid.x; i < meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) {
        uint packed = pc.meshletTrianglesBuffer[meshlet.meshletTriangleOffset + i];
        triangles[i] = uint3((packed >> 0) & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
    }
}