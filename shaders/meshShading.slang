import shader_types;
import common_interop;
import model_interop;
import constants_interop;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation uint32_t materialIndex : TEXCOORD4;
};


struct PushConsts {
    SceneData* sceneData;

    // Statics
    Vertex* vertexBuffer;
    MeshletPrimitive* primitiveBuffer;
    uint32_t* meshletVerticesBuffer;
    uint32_t* meshletTrianglesBuffer;
    Meshlet* meshletBuffer;

    // Dynamics
    MaterialProperties* materialBuffer;
    Model* modelBuffer;
    Instance* instanceBuffer;

    uint32_t instanceIndex;
};

const static uint MAX_VERTICES = 64;
const static uint MAX_PRIMITIVES = 64;

struct MeshletPayload
{
    uint32_t meshletBaseIndex;
    uint32_t modelIndex;
    uint32_t materialIndex;
};

groupshared MeshletPayload sharedPayloads[TASK_SHADER_DISPATCH_X];

// ==================== Task Shader ====================
[shader("task")]
[numthreads(1, 1, 1)]
void taskMain(uniform PushConsts pushConstants, uint3 dispatchThreadId : SV_DispatchThreadID)
{

    Instance inst = pushConstants.instanceBuffer[pushConstants.instanceIndex];
    if (inst.bIsAllocated == 0) { return; }

    MeshletPrimitive prim = pushConstants.primitiveBuffer[inst.primitiveIndex];

    sharedPayloads[0].meshletBaseIndex = prim.meshletOffset;
    sharedPayloads[0].modelIndex = inst.modelIndex;
    sharedPayloads[0].materialIndex = inst.materialIndex;

    DispatchMesh(prim.meshletCount, 1, 1, sharedPayloads);
}

// ==================== Mesh Shader ====================
[shader("mesh")]
[outputtopology("triangle")]
[numthreads(MESH_SHADER_DISPATCH_X, 1, 1)]
void meshMain(
    in payload MeshletPayload sharedPayloads[TASK_SHADER_DISPATCH_X],
    out indices uint3 triangles[MAX_PRIMITIVES], out vertices VertexOutput vertices[MAX_VERTICES],
    uint3 groupId : SV_GroupID, uint3 gtid : SV_GroupThreadID,
    uniform PushConsts pushConstant)
{
    MeshletPayload payload = sharedPayloads[0];
    uint meshletIdx = payload.meshletBaseIndex + groupId.x;
    uint32_t threadIndex = gtid.x;

    Model m = pushConstant.modelBuffer[payload.modelIndex];
    Meshlet meshlet = pushConstant.meshletBuffer[meshletIdx];

    float4x4 viewProj = pushConstant.sceneData->viewProj;
    SetMeshOutputCounts(meshlet.meshletVerticesCount, meshlet.meshletTriangleCount);
    for (uint i = threadIndex; i < meshlet.meshletVerticesCount; i += MESH_SHADER_DISPATCH_X) {
        uint localVertexIndex = pushConstant.meshletVerticesBuffer[meshlet.meshletVerticesOffset + i];
        Vertex v = pushConstant.vertexBuffer[meshlet.vertexOffset + localVertexIndex];

        float4 worldPos = mul(m.modelMatrix, float4(v.position, 1.0));
        float4 clipPos = mul(viewProj, worldPos);

        vertices[i].position = clipPos;
        vertices[i].worldPos = worldPos.xyz;
        vertices[i].normal = mul((float3x3)m.modelMatrix, v.normal);
        vertices[i].uv = float2(v.texcoordU, v.texcoordV);
        vertices[i].tangent = v.tangent;

        uint meshletID = groupId.x;
        /*uint hash = meshletID * 747796405u + 2891336453u;
        float3 color = float3(
            (hash & 0xFF) / 255.0,
            ((hash >> 8) & 0xFF) / 255.0,
            ((hash >> 16) & 0xFF) / 255.0
        );
        vertices[i].color = float4(color, 1.0);*/
        vertices[i].color = v.color;
        vertices[i].materialIndex = payload.materialIndex;
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
    float4 color : SV_Target0;
};

[[vk::binding(0, 0)]]
SamplerState samplers[];
[[vk::binding(1, 0)]]
Texture2D textures[];

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput vertexInput, uniform PushConsts pushConstant)
{
    MaterialProperties material = pushConstant.materialBuffer[vertexInput.materialIndex];

    float4 color = vertexInput.color;
    color = color * textures[material.textureImageIndices[0]].Sample(samplers[material.textureSamplerIndices[0]], vertexInput.uv);

    float3 N = normalize(vertexInput.normal);
    float3 V = normalize(pushConstant.sceneData->cameraWorldPos.xyz - vertexInput.worldPos);
    float3 L = normalize(float3(1.0, 1.0, 1.0));

    float3 ambient = float3(0.1, 0.1, 0.1);
    float3 diffuse = max(dot(N, L), 0.0) * material.colorFactor.rgb;

    FragmentOutput output;
    output.color = color * float4(ambient + diffuse, material.colorFactor.a);

    output.color = textures[material.textureImageIndices[0]].Sample(samplers[material.textureSamplerIndices[0]], vertexInput.uv);
    return output;
}
