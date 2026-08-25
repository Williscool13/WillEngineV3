import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;

import limits_declarations;
import bindless_declarations;

import shader_functions;

// Cutout variant of geometry_visibility_buffer.slang. Separate module: two mesh entries in one module trip slang's duplicate PerPrimitiveEXT decoration on SV_PrimitiveID.

// ==================== Mesh Shader ====================
struct VertexOutputCutout
{
    float4 position : SV_Position;
    nointerpolation uint64_t stableId : COLOR0;
    nointerpolation uint2 visibility : COLOR1; // .x = meshletIndex, .y = instanceIndex
    float2 uv : COLOR2;
    float alphaScale : COLOR3; // colorFactor.a * vertexColor.a
    nointerpolation uint2 alphaTexture : COLOR4; // .x = image index (0xFFFFFFFF = none), .y = sampler index
    nointerpolation float alphaCutoff : COLOR5;
    float clipDistance[1] : SV_ClipDistance;
};

struct PrimitiveOutput
{
     [[vk::location(0)]] uint primitiveID : SV_PrimitiveID;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void MeshGeometryVisibilityBufferCutout(
    OutputVertices<VertexOutputCutout, MESHLET_MAX_VERTICES> vertices,
    OutputIndices<uint3, MESHLET_MAX_TRIANGLES> triangles,
    OutputPrimitives<PrimitiveOutput, MESHLET_MAX_TRIANGLES> primitives,
    uint3 gid : SV_GroupID,
    uint3 gtid : SV_GroupThreadID,
    uniform VisibilityBufferAccumulatePushConstant pc)
{
    uint globalMeshletIndex = gid.x + gid.y * ((MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1);

    if (globalMeshletIndex >= pc.compactedDispatchBuffer->regionArgs[pc.drawRegion].w) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    CompactedMeshlet compacted = pc.visibleMeshlets[pc.compactedDispatchBuffer->regionBase[pc.drawRegion] + globalMeshletIndex];
    uint instanceIndex = compacted.instanceIndex;
    uint packedLocal = compacted.meshletIndexWithinLOD;
    uint meshletIndexWithinLOD = packedLocal & 0x3FFFFFFF;
    uint lod = packedLocal >> 30;

    Instance inst = pc.instanceBuffer[instanceIndex];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model m = pc.modelBuffer[inst.modelIndex];
    MaterialProperties material = pc.materialBuffer[inst.materialIndex];

    uint meshletIndex = prim.meshletOffset[lod] + meshletIndexWithinLOD;
    Meshlet meshlet = pc.meshletBuffer[meshletIndex];
    float4x4 jitteredViewProj = pc.sceneData->viewProj;

    SetMeshOutputCounts(meshlet.meshletVertexCount, meshlet.meshletTriangleCount);

    uint alphaImageIndex = material.textureImageIndices.x >= 0 ? uint(material.textureImageIndices.x) : 0xFFFFFFFF;
    uint alphaSamplerIndex = material.textureSamplerIndices.x >= 0 ? uint(material.textureSamplerIndices.x) : 0;

    uint32_t threadIndex = gtid.x;
    for (uint i = threadIndex; i < meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        uint localVertexIndex = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + i];
        VertexPosition v = pc.vertexPosBuffer[meshlet.vertexOffset + localVertexIndex];
        VertexAttribute attr = pc.vertexAttrBuffer[meshlet.vertexOffset + localVertexIndex];
        float3 extent = prim.boundingBoxMax - prim.boundingBoxMin;
        float3 vertexPos = DecodePosition(v, prim.boundingBoxMin, extent);

        float4 worldPos = mul(m.modelMatrix, float4(vertexPos, 1.0));
        float4 clipPos = mul(jitteredViewProj, worldPos);

        vertices[i].clipDistance[0] = dot(worldPos.xyz, pc.sceneData->clipPlane.xyz) + pc.sceneData->clipPlane.w;
        vertices[i].position = clipPos;
        vertices[i].stableId = inst.stableId;
        vertices[i].visibility = uint2(meshletIndex, instanceIndex);
        vertices[i].uv = DecodeUV(attr) * material.colorUvTransform.xy + material.colorUvTransform.zw;
        vertices[i].alphaScale = material.colorFactor.a * DecodeColor(attr).a;
        vertices[i].alphaTexture = uint2(alphaImageIndex, alphaSamplerIndex);
        vertices[i].alphaCutoff = material.alphaProperties.x;
    }

    for (uint i = threadIndex; i < meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) {
        uint triIndex = meshlet.meshletTriangleOffset + i;
        uint packed = pc.meshletTrianglesBuffer[triIndex];

        uint idx0 = (packed >> 0) & 0xFF;
        uint idx1 = (packed >> 8) & 0xFF;
        uint idx2 = (packed >> 16) & 0xFF;

        triangles[i] = uint3(idx0, idx1, idx2);
        primitives[i].primitiveID = i;
    }
}

// ==================== Fragment Shader ====================
struct FragmentOutput {
    uint2 visibilityBuffer : SV_Target0;
    uint2 stableId         : SV_Target1;
};

[shader("fragment")]
FragmentOutput FragmentGeometryVisibilityBufferCutout(VertexOutputCutout vertexInput, PrimitiveOutput primitiveInput)
{
    float alpha = vertexInput.alphaScale;
    if (vertexInput.alphaTexture.x != 0xFFFFFFFF) {
        alpha *= assetTextures[NonUniformResourceIndex(vertexInput.alphaTexture.x)].Sample(assetSamplers[NonUniformResourceIndex(vertexInput.alphaTexture.y)], vertexInput.uv).a;
    }
    if (alpha < vertexInput.alphaCutoff) {
        discard;
    }

    FragmentOutput output;
    output.visibilityBuffer = uint2((vertexInput.visibility.x << 8) | primitiveInput.primitiveID & 0xFF, vertexInput.visibility.y);
    output.stableId = uint2(uint(vertexInput.stableId & 0xFFFFFFFF), uint(vertexInput.stableId >> 32));
    return output;
}
