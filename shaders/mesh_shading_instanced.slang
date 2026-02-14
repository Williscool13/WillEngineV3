import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;

import limits_declarations;
import bindless_declarations;

import shader_functions;

// =============== Shader Summary ============
/*  This shader is the main geometry pass for the majority of objects in this engine.
*   It is executed through indirect commands generated from instancing compute shaders in `instancing.slang`
*
*   The instancing pass determines instance and meshlet visibility by culling and does a prefix sum and indirect generation pass.
*   The resulting indirect buffer will have X, Y, 1 task dispatches. Where: TotalMeshlets = Y * MeshHardwareLimit + X
*/

// ==================== Mesh Shader ====================
struct VertexOutput
{
    float4 position : SV_Position;
    float3 viewNormal : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation uint32_t materialIndex : TEXCOORD4;
    float4 currentClipPos : COLOR1;
    float4 previousClipPos : COLOR2;
    float4 objectOnlyPrevClipPos : COLOR3;
    float clipDistance[1] : SV_ClipDistance;
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
    float3x3 viewRotation = (float3x3)pc.sceneData[pc.sceneDataIndex].view;

    SetMeshOutputCounts(meshlet.meshletVertexCount, meshlet.meshletTriangleCount);

    uint32_t threadIndex = gtid.x;
    for (uint i = threadIndex; i < meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        uint localVertexIndex = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + i];
        Vertex v = pc.vertexBuffer[meshlet.vertexOffset + localVertexIndex];
        float4 worldPos = mul(m.modelMatrix, float4(v.position, 1.0));
        vertices[i].clipDistance[0] = dot(worldPos.xyz, pc.sceneData[pc.sceneDataIndex].clipPlane.xyz) + pc.sceneData[pc.sceneDataIndex].clipPlane.w;
        float4 clipPos = mul(jitteredViewProj, worldPos);
        float4 prevClipPos = mul(jitteredPrevViewProj, mul(m.prevModelMatrix, float4(v.position, 1.0)));
        vertices[i].position = clipPos;
        vertices[i].viewNormal = normalize(mul(viewRotation, mul(Adjugate((float3x3)m.modelMatrix), v.normal)));
        vertices[i].tangent = float4(normalize(mul(Adjugate((float3x3)m.modelMatrix), v.tangent.xyz)), v.tangent.w);
        vertices[i].uv = float2(v.texcoordU, v.texcoordV);
        vertices[i].color = v.color;
        vertices[i].materialIndex = inst.materialIndex;
        vertices[i].currentClipPos = clipPos;
        vertices[i].previousClipPos = prevClipPos;
        vertices[i].objectOnlyPrevClipPos = mul(jitteredViewProj, mul(m.prevModelMatrix, float4(v.position, 1.0)));
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
    MaterialProperties material = pc.materialBuffer[vertexInput.materialIndex];

    float4 albedoSample = assetTextures[NonUniformResourceIndex(material.textureImageIndices[0])].SampleBias(assetSamplers[NonUniformResourceIndex(material.textureSamplerIndices[0])], vertexInput.uv, -1.0);
    float4 albedo = vertexInput.color * albedoSample * material.colorFactor;

    float3 viewNormal = normalize(vertexInput.viewNormal);

    FragmentOutput output;
    output.albedo = albedo;
    output.normal = float4(viewNormal * 0.5 + 0.5, 1.0); // add normal texture
    output.pbr = float4(material.metalRoughFactors.x, material.metalRoughFactors.y, 0.0, 0.0); // add pbr texture
    output.emissive = material.emissiveFactor; // add emissive texture

    float2 currNdc = vertexInput.currentClipPos.xy / vertexInput.currentClipPos.w;
    float2 prevNdc = vertexInput.previousClipPos.xy / vertexInput.previousClipPos.w;
    float2 currJitter = pc.sceneData[pc.sceneDataIndex].jitter;
    float2 prevJitter = pc.sceneData[pc.sceneDataIndex].prevJitter;
    float2 totalVelocity = (currNdc + currJitter) - (prevNdc + prevJitter);
    float2 objectOnlyPrevNdc = vertexInput.objectOnlyPrevClipPos.xy / vertexInput.objectOnlyPrevClipPos.w;
    // float2 objectVelocity = (currNdc + currJitter) - (objectOnlyPrevNdc + currJitter);
    float2 objectVelocity = currNdc - objectOnlyPrevNdc;
    output.velocity = float4(totalVelocity, objectVelocity);

    return output;
}
