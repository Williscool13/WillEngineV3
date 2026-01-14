import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;

import bindless_declarations;

import shader_functions;

// =============== Task Shader ===============
struct VertexOutput
{
    float4 position : SV_Position;
    float3 viewNormal : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation uint32_t materialIndex : TEXCOORD4;
    float4 currentClipPos : TEXCOORD5;
    float4 previousClipPos : TEXCOORD6;
};

// Per https://developer.nvidia.com/blog/using-mesh-shaders-for-professional-graphics/
// Task shader output should ideally be below 236/108. We can hit 108 if we turn uint32_t to uint8_t and store offsets in the payload.
// 4 + 4 + 4 + 64 = 86
struct MeshletPayload {
    uint32_t modelIndex;
    uint32_t materialIndex;
    uint32_t groupMeshletOffset;

    uint8_t meshletIndices[TASK_SHADER_DISPATCH_X];
};

groupshared MeshletPayload sharedPayload;
groupshared uint32_t visibleMeshletCount;

[shader("task")]
[numthreads(TASK_SHADER_DISPATCH_X, 1, 1)]
void TaskMain(uniform InstancedMeshShadingPushConstant pushConstant,
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint drawIndex : SV_DrawIndex)
{
    InstancedMeshIndirectDrawParameters indirectParameters = pushConstant.indirectBuffer[drawIndex];

    uint32_t instanceIndex = indirectParameters.compactedInstanceStart + gid.y;
    Instance inst = pushConstant.compactedInstanceBuffer[instanceIndex];

    uint32_t groupMeshletOffset = indirectParameters.meshletOffset + (gid.x * TASK_SHADER_DISPATCH_X);
    uint32_t totalMeshlets = indirectParameters.meshletCount;

    // thread 0 populates shared information for mesh shader
    if (gtid.x == 0) {
        visibleMeshletCount = 0;

        sharedPayload.modelIndex = inst.modelIndex;
        sharedPayload.materialIndex = inst.materialIndex;
        sharedPayload.groupMeshletOffset = groupMeshletOffset;
    }

    GroupMemoryBarrierWithGroupSync();


    Model model = pushConstant.modelBuffer[inst.modelIndex];

    // group thread count is only 64, will never exceed uint8
    uint8_t meshletID = (uint8_t)gtid.x;
    uint localMeshletCount = min(TASK_SHADER_DISPATCH_X, totalMeshlets - (gid.x * TASK_SHADER_DISPATCH_X));

    if (meshletID < localMeshletCount) {
        Meshlet mlet = pushConstant.meshletBuffer[groupMeshletOffset + meshletID];

        // Transform to world space
        float3 scale = float3(
            length(model.modelMatrix[0].xyz),
            length(model.modelMatrix[1].xyz),
            length(model.modelMatrix[2].xyz)
        );
        float maxScale = max(max(scale.x, scale.y), scale.z);
        float worldRadius = mlet.meshletBoundingSphere.w * maxScale;
        float3 worldCenter = mul(model.modelMatrix, float4(mlet.meshletBoundingSphere.xyz, 1.0)).xyz;
        float4 worldBounds = float4(worldCenter, worldRadius);

        bool visible = true;
        if (!FrustumCullSphere(pushConstant.sceneData->frustum, worldBounds)) {
            visible = false;
        }

        if (visible && !BackfaceCullCone(mlet.coneAxis, mlet.coneCutoff, worldCenter, worldRadius, pushConstant.sceneData->cameraWorldPos.xyz, (float3x3)model.modelMatrix)) {
            visible = false;
        }

        if (visible) {
            uint index;
            InterlockedAdd(visibleMeshletCount, 1, index);
            sharedPayload.meshletIndices[index] = meshletID;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (gtid.x == 0) {
        uint count = visibleMeshletCount;
        DispatchMesh(count, 1, 1, sharedPayload);
    }
}

// ==================== Mesh Shader ====================
[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void MeshMain(
    in payload MeshletPayload inPayload, out indices uint3 triangles[MESHLET_MAX_TRIANGLES], out vertices VertexOutput vertices[MESHLET_MAX_VERTICES],
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform InstancedMeshShadingPushConstant pushConstant)
{
    uint32_t payloadMeshletIndex = gid.x;
    uint32_t meshletIndex = inPayload.groupMeshletOffset + inPayload.meshletIndices[payloadMeshletIndex];
    Meshlet meshlet = pushConstant.meshletBuffer[meshletIndex];

    Model m = pushConstant.modelBuffer[inPayload.modelIndex];

    float4x4 jitteredViewProj = pushConstant.sceneData->viewProj;
    float4x4 viewProj = pushConstant.sceneData->unjitteredViewProj;
    float4x4 jitteredPrevViewProj = pushConstant.sceneData->prevViewProj;
    float4x4 prevViewProj = pushConstant.sceneData->unjitteredPrevViewProj;

    float3x3 viewRotation = (float3x3)pushConstant.sceneData.view;

    SetMeshOutputCounts(meshlet.meshletVertexCount, meshlet.meshletTriangleCount);
    uint32_t threadIndex = gtid.x;
    for (uint i = threadIndex; i < meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        uint localVertexIndex = pushConstant.meshletVerticesBuffer[meshlet.meshletVertexOffset + i];
        Vertex v = pushConstant.vertexBuffer[meshlet.vertexOffset + localVertexIndex];
        float4 clipPos = mul(jitteredViewProj, mul(m.modelMatrix, float4(v.position, 1.0)));
        vertices[i].position = clipPos;
        vertices[i].viewNormal = normalize(mul(viewRotation, mul(Adjugate((float3x3)m.modelMatrix), v.normal)));
        vertices[i].tangent = float4(normalize(mul(Adjugate((float3x3)m.modelMatrix), v.tangent.xyz)), v.tangent.w);
        vertices[i].uv = float2(v.texcoordU, v.texcoordV);
        /*uint meshletID = gid.x;
        uint hash = meshletID * 747796405u + 2891336453u;
        float3 color = float3(
            (hash & 0xFF) / 255.0,
            ((hash >> 8) & 0xFF) / 255.0,
            ((hash >> 16) & 0xFF) / 255.0
        );
        vertices[i].color = float4(color * v.color.xyz, 1.0);*/
        vertices[i].color = v.color;
        vertices[i].materialIndex = inPayload.materialIndex;
        vertices[i].currentClipPos = mul(viewProj, mul(m.modelMatrix, float4(v.position, 1.0)));
        vertices[i].previousClipPos = mul(prevViewProj, mul(m.prevModelMatrix, float4(v.position, 1.0)));
    }

    for (uint i = threadIndex; i < meshlet.meshletTriangleCount; i += MESH_SHADER_DISPATCH_X) {
        uint triIndex = meshlet.meshletTriangleOffset + i;
        uint packed = pushConstant.meshletTrianglesBuffer[triIndex];

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
    float2 velocity : SV_Target3;
};

[shader("fragment")]
FragmentOutput FragmentMain(VertexOutput vertexInput, uniform InstancedMeshShadingPushConstant pushConstant)
{
    MaterialProperties material = pushConstant.materialBuffer[vertexInput.materialIndex];

    // float4 albedoSample = textures[material.textureImageIndices[0]].Sample(samplers[material.textureSamplerIndices[0]], vertexInput.uv);
    float4 albedoSample = assetTextures[material.textureImageIndices[0]].SampleBias(assetSamplers[material.textureSamplerIndices[0]], vertexInput.uv, -1.0);
    float4 albedo = vertexInput.color * albedoSample * material.colorFactor;

    float3 viewNormal = normalize(vertexInput.viewNormal);

    FragmentOutput output;
    output.albedo = albedo;
    output.normal = float4(viewNormal * 0.5 + 0.5, 1.0);
    output.pbr = float4(material.metalRoughFactors.x, material.metalRoughFactors.y, 0.0, 0.0);

    float2 currNdc = vertexInput.currentClipPos.xy / vertexInput.currentClipPos.w;
    float2 prevNdc = vertexInput.previousClipPos.xy / vertexInput.previousClipPos.w;
    output.velocity = currNdc - prevNdc;

    return output;
}
