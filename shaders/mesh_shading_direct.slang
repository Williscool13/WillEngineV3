import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;

import bindless_declarations;
import shader_functions;

// =============== Shader Summary ============
/*  This shader is for direct (non-instanced) geometry passes.
*   Used for custom draws with small instance counts (portals, special effects, stencil marking).
*
*   Unlike the main instanced path, this skips GPU instance culling and compaction.
*   Instead, all submitted instances are drawn, but meshlet-level culling still occurs in the task shader.
*
*   Flow:
*   1. Compute shader: Builds indirect draw commands by reading primitive meshlet counts
*   2. Task shader: Performs frustum and backface culling on meshlets, dispatches visible meshlets
*   3. Mesh shader: Assembles geometry for visible meshlets
*   4. Fragment shader: Outputs to G-buffers (same as main instanced path)
*/

// =============== Compute Shader (Build Indirect Commands) ===============
/*  Compute shader that generates VkDrawMeshTasksIndirectCommandEXT for each instance.
*   Each instance gets one indirect command with the correct number of task groups.
*
*   One thread per instance, reads primitive meshlet count and calculates required task groups.
*   Outputs 1 indirect command per instance. Not terribly efficient, but this is designed for low instance count special cases.
*/

struct VkDrawMeshTasksIndirectCommandEXT
{
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
};

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeMain(uniform BuildDirectIndirectPushConstant pc, uint3 gid : SV_DispatchThreadID)
{
    uint32_t instanceIndex = gid.x;
    if (instanceIndex >= pc.instanceCount) {
        return;
    }

    Instance inst = pc.instanceBuffer[instanceIndex];
    MeshletPrimitive prim = pc.primitiveBuffer[inst.primitiveIndex];

    uint32_t taskGroupCount = (prim.meshletCount + TASK_SHADER_DISPATCH_X - 1) / TASK_SHADER_DISPATCH_X;

    pc.indirectCommandBuffer[instanceIndex].groupCountX = taskGroupCount;
    pc.indirectCommandBuffer[instanceIndex].groupCountY = 1;
    pc.indirectCommandBuffer[instanceIndex].groupCountZ = 1;
}

// =============== Task Shader ===============
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
};

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
void TaskMain(uniform DirectMeshShadingPushConstant pc,
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint drawIndex : SV_DrawIndex)
{
    Instance inst = pc.instanceBuffer[drawIndex];
    MeshletPrimitive prim = pc.primitiveBuffer[inst.primitiveIndex];

    uint32_t groupMeshletOffset = prim.meshletOffset + (gid.x * TASK_SHADER_DISPATCH_X);
    uint32_t totalMeshlets = prim.meshletCount;

    if (gtid.x == 0) {
        visibleMeshletCount = 0;
        sharedPayload.modelIndex = inst.modelIndex;
        sharedPayload.materialIndex = inst.materialIndex;
        sharedPayload.groupMeshletOffset = groupMeshletOffset;
    }

    GroupMemoryBarrierWithGroupSync();

    Model model = pc.modelBuffer[inst.modelIndex];

    uint8_t meshletID = (uint8_t)gtid.x;
    uint localMeshletCount = min(TASK_SHADER_DISPATCH_X, totalMeshlets - (gid.x * TASK_SHADER_DISPATCH_X));

    if (meshletID < localMeshletCount) {
        Meshlet mlet = pc.meshletBuffer[groupMeshletOffset + meshletID];

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
        if (!FrustumCullSphere(pc.sceneData[pc.sceneDataIndex].frustum, worldBounds)) {
            visible = false;
        }
        if (visible && !BackfaceCullCone(mlet.coneAxis, mlet.coneCutoff, worldCenter, worldRadius, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, (float3x3)model.modelMatrix)) {
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
    in payload MeshletPayload inPayload,
    out indices uint3 triangles[MESHLET_MAX_TRIANGLES],
    out vertices VertexOutput vertices[MESHLET_MAX_VERTICES],
    uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform DirectMeshShadingPushConstant pc)
{
    uint32_t payloadMeshletIndex = gid.x;
    uint32_t meshletIndex = inPayload.groupMeshletOffset + inPayload.meshletIndices[payloadMeshletIndex];
    Meshlet meshlet = pc.meshletBuffer[meshletIndex];

    Model m = pc.modelBuffer[inPayload.modelIndex];

    float4x4 jitteredViewProj = pc.sceneData[pc.sceneDataIndex].viewProj;
    float4x4 jitteredPrevViewProj = pc.sceneData[pc.sceneDataIndex].prevViewProj;

    float3x3 viewRotation = (float3x3)pc.sceneData[pc.sceneDataIndex].view;

    SetMeshOutputCounts(meshlet.meshletVertexCount, meshlet.meshletTriangleCount);
    uint32_t threadIndex = gtid.x;
    for (uint i = threadIndex; i < meshlet.meshletVertexCount; i += MESH_SHADER_DISPATCH_X) {
        uint localVertexIndex = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + i];
        Vertex v = pc.vertexBuffer[meshlet.vertexOffset + localVertexIndex];
        float4 clipPos = mul(jitteredViewProj, mul(m.modelMatrix, float4(v.position, 1.0)));
        vertices[i].position = clipPos;
        vertices[i].viewNormal = normalize(mul(viewRotation, mul(Adjugate((float3x3)m.modelMatrix), v.normal)));
        vertices[i].tangent = float4(normalize(mul(Adjugate((float3x3)m.modelMatrix), v.tangent.xyz)), v.tangent.w);
        vertices[i].uv = float2(v.texcoordU, v.texcoordV);
        vertices[i].color = v.color;
        vertices[i].materialIndex = inPayload.materialIndex;
        vertices[i].currentClipPos = clipPos;
        vertices[i].previousClipPos = mul(jitteredPrevViewProj, mul(m.prevModelMatrix, float4(v.position, 1.0)));
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
FragmentOutput FragmentMain(VertexOutput vertexInput, uniform DirectMeshShadingPushConstant pc)
{
    MaterialProperties material = pc.materialBuffer[vertexInput.materialIndex];

    float4 albedoSample = assetTextures[material.textureImageIndices[0]].SampleBias(assetSamplers[material.textureSamplerIndices[0]], vertexInput.uv, -1.0);
    float4 albedo = vertexInput.color * albedoSample * material.colorFactor;

    float3 viewNormal = normalize(vertexInput.viewNormal);

    FragmentOutput output;
    output.albedo = albedo;
    output.normal = float4(viewNormal * 0.5 + 0.5, 1.0);
    output.pbr = float4(material.metalRoughFactors.x, material.metalRoughFactors.y, 0.0, 0.0);
    output.emissive = material.emissiveFactor;

    float2 currNdc = vertexInput.currentClipPos.xy / vertexInput.currentClipPos.w;
    float2 prevNdc = vertexInput.previousClipPos.xy / vertexInput.previousClipPos.w;
    currNdc += pc.sceneData[pc.sceneDataIndex].jitter;
    prevNdc += pc.sceneData[pc.sceneDataIndex].prevJitter;
    float2 totalVelocity = currNdc - prevNdc;

    float2 objectOnlyPrevNdc = vertexInput.objectOnlyPrevClipPos.xy / vertexInput.objectOnlyPrevClipPos.w;
    objectOnlyPrevNdc += pc.sceneData[pc.sceneDataIndex].jitter;
    float2 objectVelocity = currNdc - objectOnlyPrevNdc;
    output.velocity = float4(totalVelocity, objectVelocity);

    return output;
}