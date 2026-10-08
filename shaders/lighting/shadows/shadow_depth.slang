import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;
import shadow_interop;

import limits_declarations;
import bindless_declarations;

import shader_functions;
import material_uv_common;

struct ShadowMeshlet
{
    Instance inst;
    Primitive prim;
    Model model;
    Meshlet meshlet;
    ShadowViewGPU view;
};

static bool LoadShadowMeshlet(ShadowDepthPushConstant pc, uint3 gid, out ShadowMeshlet m)
{
    m = {};
    uint globalMeshletIndex = gid.x + gid.y * ((MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1);
    if (globalMeshletIndex >= pc.compactedDispatchBuffer->regionArgs[pc.drawRegion].w) {
        return false;
    }

    CompactedMeshlet compacted = pc.visibleMeshlets[pc.compactedDispatchBuffer->regionBase[pc.drawRegion] + globalMeshletIndex];
    uint packedLocal = compacted.meshletIndexWithinLOD;
    uint lod = packedLocal >> SHADOW_LOD_SHIFT;
    m.inst = pc.instanceBuffer[compacted.instanceIndex];
    m.prim = pc.primitiveBuffer[m.inst.primitiveIndex];
    m.model = pc.modelBuffer[m.inst.modelIndex];
    m.meshlet = pc.meshletBuffer[m.prim.meshletOffset[lod] + (packedLocal & SHADOW_MESHLET_MASK)];
    m.view = pc.views[(packedLocal >> SHADOW_VIEW_SHIFT) & SHADOW_VIEW_MASK];
    return true;
}

// Remaps the view's clip space into its atlas tile; linear in clip space, so it holds for perspective views too.
static float4 ViewToAtlasClip(ShadowViewGPU view, float3 worldPos, out float clipDistance[4])
{
    float4 clip = mul(view.viewProj, float4(worldPos, 1.0));
    clipDistance[0] = clip.w - clip.x;
    clipDistance[1] = clip.w + clip.x;
    clipDistance[2] = clip.w - clip.y;
    clipDistance[3] = clip.w + clip.y;
    float2 scale = view.atlasScaleOffset.xy;
    float2 offset = scale + 2.0 * view.atlasScaleOffset.zw - 1.0;
    return float4(clip.xy * scale + clip.w * offset, clip.z, clip.w);
}

static float3 LoadWorldPosition(ShadowDepthPushConstant pc, ShadowMeshlet m, uint localIndex)
{
    uint vertexIndex = pc.meshletVerticesBuffer[m.meshlet.meshletVertexOffset + localIndex];
    VertexPosition v = pc.vertexPosBuffer[m.meshlet.vertexOffset + vertexIndex];
    float3 vertexPos = DecodePosition(v, m.prim.boundingBoxMin, m.prim.boundingBoxMax - m.prim.boundingBoxMin);
    return mul(m.model.modelMatrix, float4(vertexPos, 1.0)).xyz;
}

static uint3 LoadTriangle(ShadowDepthPushConstant pc, ShadowMeshlet m, uint i)
{
    uint packed = pc.meshletTrianglesBuffer[m.meshlet.meshletTriangleOffset + i];
    return uint3(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF);
}

struct ShadowVertex
{
    float4 position : SV_Position;
    float clipDistance[4] : SV_ClipDistance;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void MeshShadowDepth(
    OutputVertices<ShadowVertex, MESHLET_MAX_VERTICES> vertices,
    OutputIndices<uint3, MESHLET_MAX_TRIANGLES> triangles,
    uint3 gid : SV_GroupID,
    uint3 gtid : SV_GroupThreadID,
    uniform ShadowDepthPushConstant pc)
{
    ShadowMeshlet m;
    if (!LoadShadowMeshlet(pc, gid, m)) {
        SetMeshOutputCounts(0, 0);
        return;
    }
    SetMeshOutputCounts(m.meshlet.meshletVertexCount, m.meshlet.meshletTriangleCount);

    for (uint i = gtid.x; i < m.meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        float clipDistance[4];
        vertices[i].position = ViewToAtlasClip(m.view, LoadWorldPosition(pc, m, i), clipDistance);
        vertices[i].clipDistance = clipDistance;
    }
    for (uint i = gtid.x; i < m.meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) {
        triangles[i] = LoadTriangle(pc, m, i);
    }
}

struct ShadowVertexCutout
{
    float4 position : SV_Position;
    float clipDistance[4] : SV_ClipDistance;
    float2 uv : COLOR0;
    float alphaScale : COLOR1;
    nointerpolation uint2 alphaTexture : COLOR2;
    nointerpolation float alphaCutoff : COLOR3;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void MeshShadowDepthCutout(
    OutputVertices<ShadowVertexCutout, MESHLET_MAX_VERTICES> vertices,
    OutputIndices<uint3, MESHLET_MAX_TRIANGLES> triangles,
    uint3 gid : SV_GroupID,
    uint3 gtid : SV_GroupThreadID,
    uniform ShadowDepthPushConstant pc)
{
    ShadowMeshlet m;
    if (!LoadShadowMeshlet(pc, gid, m)) {
        SetMeshOutputCounts(0, 0);
        return;
    }
    SetMeshOutputCounts(m.meshlet.meshletVertexCount, m.meshlet.meshletTriangleCount);

    MaterialProperties material = pc.materialBuffer[m.inst.materialIndex];
    uint alphaImageIndex = material.textureImageIndices.x >= 0 ? uint(material.textureImageIndices.x) : 0xFFFFFFFF;
    uint alphaSamplerIndex = material.textureSamplerIndices.x >= 0 ? uint(material.textureSamplerIndices.x) : 0;

    for (uint i = gtid.x; i < m.meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        uint vertexIndex = pc.meshletVerticesBuffer[m.meshlet.meshletVertexOffset + i];
        VertexAttribute attr = pc.vertexAttrBuffer[m.meshlet.vertexOffset + vertexIndex];
        float3 worldPos = LoadWorldPosition(pc, m, i);
        float clipDistance[4];
        vertices[i].position = ViewToAtlasClip(m.view, worldPos, clipDistance);
        vertices[i].clipDistance = clipDistance;
        vertices[i].uv = MaterialBaseUV(material, DecodeUV(attr), m.model.modelMatrix, worldPos, DecodeNormal(attr)) * material.colorUvTransform.xy + material.colorUvTransform.zw;
        vertices[i].alphaScale = material.colorFactor.a * DecodeColor(attr).a;
        vertices[i].alphaTexture = uint2(alphaImageIndex, alphaSamplerIndex);
        vertices[i].alphaCutoff = material.alphaProperties.x;
    }
    for (uint i = gtid.x; i < m.meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) {
        triangles[i] = LoadTriangle(pc, m, i);
    }
}

[shader("fragment")]
void FragmentShadowDepthCutout(ShadowVertexCutout input)
{
    float alpha = input.alphaScale;
    if (input.alphaTexture.x != 0xFFFFFFFF) {
        alpha *= assetTextures[NonUniformResourceIndex(input.alphaTexture.x)].Sample(assetSamplers[NonUniformResourceIndex(input.alphaTexture.y)], input.uv).a;
    }
    if (alpha < input.alphaCutoff) {
        discard;
    }
}
