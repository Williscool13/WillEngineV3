import common_interop;
import constants_interop;
import push_constant_interop;
import model_interop;
import instancing_interop;

import shader_constants;

import bindless_declarations;


// ==================== Mesh Shader ====================
struct VertexOutput
{
    float4 position : SV_Position;
    float3 viewNormal : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation uint32_t materialIndex : TEXCOORD4;
    noperspective float4 currentClipPos : TEXCOORD5;
    noperspective float4 previousClipPos : TEXCOORD6;
    noperspective float4 objectOnlyPrevClipPos : TEXCOORD7;
    float clipDistance : SV_ClipDistance0;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void MeshPortalRender(
    out indices uint3 triangles[MESHLET_MAX_TRIANGLES],
    out vertices VertexOutput vertices[MESHLET_MAX_VERTICES],
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint drawIndex : SV_DrawIndex,
    uniform DirectMeshShadingPushConstant pc)
{
//    uint32_t payloadMeshletIndex = gid.x;
//    uint32_t meshletIndex = inPayload.groupMeshletOffset + inPayload.meshletIndices[payloadMeshletIndex];
//    Meshlet meshlet = pc.meshletBuffer[meshletIndex];
//
//    Model m = pc.modelBuffer[inPayload.modelIndex];
//
//    float4x4 jitteredViewProj = pc.sceneData[pc.sceneDataIndex].viewProj;
//    float4x4 jitteredPrevViewProj = pc.sceneData[pc.sceneDataIndex].prevViewProj;
//
//    float3x3 viewRotation = (float3x3)pc.sceneData[pc.sceneDataIndex].view;
//
//    SetMeshOutputCounts(meshlet.meshletVertexCount, meshlet.meshletTriangleCount);
//    uint32_t threadIndex = gtid.x;
//    for (uint i = threadIndex; i < meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
//        uint localVertexIndex = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + i];
//        Vertex v = pc.vertexBuffer[meshlet.vertexOffset + localVertexIndex];
//        float4 worldPos = mul(m.modelMatrix, float4(v.position, 1.0));
//        vertices[i].clipDistance = dot(worldPos.xyz, pc.sceneData[pc.sceneDataIndex].clipPlane.xyz) + pc.sceneData[pc.sceneDataIndex].clipPlane.w;
//        float4 clipPos =     mul(jitteredViewProj,      worldPos);
//        vertices[i].position = clipPos;
//        vertices[i].viewNormal = normalize(mul(viewRotation, mul(Adjugate((float3x3)m.modelMatrix), v.normal)));
//        vertices[i].tangent = float4(normalize(mul(Adjugate((float3x3)m.modelMatrix), v.tangent.xyz)), v.tangent.w);
//        vertices[i].uv = float2(v.texcoordU, v.texcoordV);
//        vertices[i].color = v.color;
//        vertices[i].materialIndex = inPayload.materialIndex;
//        vertices[i].currentClipPos = clipPos;
//        vertices[i].previousClipPos = mul(jitteredPrevViewProj, mul(m.prevModelMatrix, float4(v.position, 1.0)));
//        vertices[i].objectOnlyPrevClipPos = mul(jitteredViewProj, mul(m.prevModelMatrix, float4(v.position, 1.0)));
//    }
//
//    for (uint i = threadIndex; i < meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) {
//        uint triIndex = meshlet.meshletTriangleOffset + i;
//        uint packed = pc.meshletTrianglesBuffer[triIndex];
//
//        uint idx0 = (packed >> 0) & 0xFF;
//        uint idx1 = (packed >> 8) & 0xFF;
//        uint idx2 = (packed >> 16) & 0xFF;
//
//        triangles[i] = uint3(idx0, idx1, idx2);
//    }
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
//FragmentOutput FragmentPortalRender(VertexOutput vertexInput, uniform DirectMeshShadingPushConstant pc)
FragmentOutput FragmentPortalRender(uniform DirectMeshShadingPushConstant pc)
{
//     MaterialProperties material = pc.materialBuffer[vertexInput.materialIndex];
//
//     float4 albedoSample = assetTextures[material.textureImageIndices[0]].SampleBias(assetSamplers[material.textureSamplerIndices[0]], vertexInput.uv, -1.0);
//     float4 albedo = vertexInput.color * albedoSample * material.colorFactor;
//
//     float3 viewNormal = normalize(vertexInput.viewNormal);
//
//     FragmentOutput output;
//     output.albedo = albedo;
//     output.normal = float4(viewNormal * 0.5 + 0.5, 1.0);
//     output.pbr = float4(material.metalRoughFactors.x, material.metalRoughFactors.y, 0.0, 0.0);
//     output.emissive = material.emissiveFactor;
//
//     float2 currNdc = vertexInput.currentClipPos.xy / vertexInput.currentClipPos.w;
//     float2 prevNdc = vertexInput.previousClipPos.xy / vertexInput.previousClipPos.w;
//     currNdc += pc.sceneData[pc.sceneDataIndex].jitter;
//     prevNdc += pc.sceneData[pc.sceneDataIndex].prevJitter;
//     float2 totalVelocity = currNdc - prevNdc;
//
//     float2 objectOnlyPrevNdc = vertexInput.objectOnlyPrevClipPos.xy / vertexInput.objectOnlyPrevClipPos.w;
//     objectOnlyPrevNdc += pc.sceneData[pc.sceneDataIndex].jitter;
//     float2 objectVelocity = currNdc - objectOnlyPrevNdc;
//     output.velocity = float4(totalVelocity, objectVelocity);
    FragmentOutput output = FragmentOutput();
    return output;
}


// ==================== Fragment Shader (Composite) ====================
struct CompositeOutput {
    float4 color : SV_Target0;
    float4 velocity : SV_Target1;
    float depth : SV_Depth;
};

[shader("fragment")]
CompositeOutput FragmentPortalComposite(uniform PortalCompositePushConstant pc, FullscreenPassVertexOutput input) {
    CompositeOutput output;

    output.color = rdgTextures[pc.portalColorIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv);
    output.velocity = rdgTextures[pc.portalVelocityIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv);
    output.depth = rdgTextures[pc.portalDepthIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv).r;

    return output;
}