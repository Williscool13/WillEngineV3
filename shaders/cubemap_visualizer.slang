import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;

import limits_declarations;
import bindless_declarations;

import shader_functions;

// =============== Shader Summary ============
/*  Visualizes a cubemap by rendering it onto a cube mesh in the scene.
*   Used for debugging environment maps, reflection probes, etc.
*
*   Uses the standard instanced mesh shading pipeline with custom fragment shader
*   that samples from a cubemap using world-space normals at a specific LOD.
*/

// ==================== Mesh Shader ====================
struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldNormal : TEXCOORD0;
    float4 currentClipPos : COLOR1;
    float4 previousClipPos : COLOR2;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void MeshMain(
    out indices uint3 triangles[MESHLET_MAX_TRIANGLES], out vertices VertexOutput vertices[MESHLET_MAX_VERTICES],
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform BaseMeshShadingPushConstant pc)
{
    uint globalMeshletIndex = gid.x + gid.y * (MAX_MESH_WORK_GROUP_COUNT_X / 2);

    if (globalMeshletIndex >= pc.compactedDispatchBuffer->totalVisibleMeshlets) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    CompactedMeshlet compacted = pc.visibleMeshlets[globalMeshletIndex];
    uint instanceIndex = compacted.instanceIndex;
    uint packedLocal = compacted.localMeshletIndex;
    uint localMeshletIndex = packedLocal & 0x3FFFFFFF;
    uint lod = packedLocal >> 30;

    Instance inst = pc.instanceBuffer[instanceIndex];
    MeshletPrimitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model m = pc.modelBuffer[inst.modelIndex];

    uint meshletIndex = prim.meshletOffset[lod] + localMeshletIndex;
    Meshlet meshlet = pc.meshletBuffer[meshletIndex];

    float4x4 jitteredViewProj = pc.sceneData[pc.sceneDataIndex].viewProj;
    float4x4 jitteredPrevViewProj = pc.sceneData[pc.sceneDataIndex].prevViewProj;

    SetMeshOutputCounts(meshlet.meshletVertexCount, meshlet.meshletTriangleCount);

    uint32_t threadIndex = gtid.x;
    for (uint i = threadIndex; i < meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        uint localVertexIndex = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + i];
        Vertex v = pc.vertexBuffer[meshlet.vertexOffset + localVertexIndex];
        float4 worldPos = mul(m.modelMatrix, float4(v.position, 1.0));
        float4 clipPos = mul(jitteredViewProj, worldPos);
        float4 prevClipPos = mul(jitteredPrevViewProj, mul(m.prevModelMatrix, float4(v.position, 1.0)));

        vertices[i].position = clipPos;
        vertices[i].worldNormal = normalize(mul((float3x3)m.modelMatrix, v.normal));
        vertices[i].currentClipPos = clipPos;
        vertices[i].previousClipPos = prevClipPos;
    }

    for (uint i = threadIndex; i < meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) {
        uint triIndex = meshlet.meshletTriangleOffset + i;
        uint packed = pc.meshletTrianglesBuffer[triIndex];

        uint idx0 = (packed >> 0) & 0xFF;
        uint idx1 = (packed >> 8) & 0xFF;
        uint idx2 = (packed >> 16) & 0xFF;

        triangles[i] = uint3(idx0, idx1, idx2);
    }
}

// ==================== Fragment Shader ====================
struct FragmentOutput {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 pbr : SV_Target2;
    float4 emissive : SV_Target3;
    float4 velocity : SV_Target4;
};

[shader("fragment")]
FragmentOutput FragmentMain(VertexOutput vertexInput, uniform BaseMeshShadingPushConstant pc)
{
    uint cubemapIndex = pc.customData[0];
    uint samplerIndex = pc.customData[1];
    float lodLevel = asfloat(pc.customData[2]);

    // Sample cubemap using world-space normal as direction at specified LOD
    float3 sampleDir = normalize(vertexInput.worldNormal);
    float4 cubemapColor = assetCubemaps[NonUniformResourceIndex(cubemapIndex)].SampleLevel(assetSamplers[NonUniformResourceIndex(samplerIndex)],sampleDir, 1);

    FragmentOutput output;
    output.albedo = cubemapColor;
    // output.albedo = float4(vertexInput.worldNormal);
    output.normal = float4(0.5, 0.5, 1.0, 1.0); // Flat normal
    output.pbr = float4(0.0, 1.0, 0.0, 0.0); // Non-metallic, rough
    output.emissive = float4(0.0, 0.0, 0.0, 0.0);

    float2 currNdc = vertexInput.currentClipPos.xy / vertexInput.currentClipPos.w;
    float2 prevNdc = vertexInput.previousClipPos.xy / vertexInput.previousClipPos.w;
    float2 currJitter = pc.sceneData[pc.sceneDataIndex].jitter;
    float2 prevJitter = pc.sceneData[pc.sceneDataIndex].prevJitter;
    float2 velocity = (currNdc + currJitter) - (prevNdc + prevJitter);
    output.velocity = float4(velocity, 0.0, 0.0);

    return output;
}