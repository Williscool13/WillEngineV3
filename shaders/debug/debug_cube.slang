import common_interop;
import model_interop;
import push_constant_interop;

// =============== Shader Summary ============
// Instanced procedural debug cubes, launched via draw indirect (mirrors debug_sphere.slang). Axis-aligned box generated from SV_VertexID; a fixed per-face shade multiplier gives it a 3D corner-shaded look with no lighting.

static const uint2 kQuadCorners[6] = {
    uint2(0, 0), uint2(1, 0), uint2(1, 1),
    uint2(0, 0), uint2(1, 1), uint2(0, 1)
};

// vertexCount = 6 faces * 6 verts, mirrored by GPU_DEBUG_CUBE_VERTEX_COUNT
static const float3 kCubeFaceNormal[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
static const float3 kCubeFaceRight[6] = {float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1), float3(1, 0, 0), float3(-1, 0, 0)};
static const float3 kCubeFaceUp[6] = {float3(0, 0, 1), float3(0, 0, 1), float3(1, 0, 0), float3(1, 0, 0), float3(0, 1, 0), float3(0, 1, 0)};
static const float kCubeFaceShade[6] = {1.0, 0.7, 0.95, 0.55, 0.85, 0.65};

struct CubeVertexOutput {
    float4 position : SV_Position;
    nointerpolation float4 color : TEXCOORD0;
};

[shader("vertex")]
CubeVertexOutput VertexDebugCube(uint vid: SV_VertexID, uint iid: SV_InstanceID, uniform GPUDebugCubeDrawPushConstant pc)
{
    uint faceIndex = vid / 6;
    uint2 corner = kQuadCorners[vid % 6];
    float3 localPos = kCubeFaceNormal[faceIndex]
        + kCubeFaceRight[faceIndex] * (float(corner.x) * 2.0 - 1.0)
        + kCubeFaceUp[faceIndex] * (float(corner.y) * 2.0 - 1.0);

    DebugCubeInstance inst = pc.instanceBuffer[iid];
    float3 worldPos = inst.center + localPos * inst.halfExtent;

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 viewPos = mul(scene.view, float4(worldPos, 1.0)).xyz;

    CubeVertexOutput output;
    output.position = mul(scene.proj, float4(viewPos, 1.0));
    output.color = float4(inst.color.rgb * kCubeFaceShade[faceIndex], inst.color.a);
    return output;
}

[shader("fragment")]
float4 FragmentDebugCube(CubeVertexOutput input) : SV_Target0
{
    return input.color;
}
