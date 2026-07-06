import common_interop;
import model_interop;
import push_constant_interop;

// =============== Shader Summary ============
// Instanced procedural debug spheres, launched via draw indirect. UV-sphere generated from SV_VertexID (no vertex/index buffer); per-instance L1 SH evaluated at the surface normal in the fragment shader.

static const float kPi = 3.14159265359;
static const uint kSphereStacks = 16;
static const uint kSphereSlices = 32;
// vertexCount = kSphereStacks * kSphereSlices * 6, mirrored by GPU_DEBUG_SPHERE_VERTEX_COUNT

static const uint2 kQuadCorners[6] = {
    uint2(0, 0), uint2(1, 0), uint2(1, 1),
    uint2(0, 0), uint2(1, 1), uint2(0, 1)
};

struct SphereVertexOutput {
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    nointerpolation float4 shR : TEXCOORD0;
    nointerpolation float4 shG : TEXCOORD1;
    nointerpolation float4 shB : TEXCOORD2;
};

[shader("vertex")]
SphereVertexOutput VertexDebugSphere(uint vid: SV_VertexID, uint iid: SV_InstanceID, uniform GPUDebugSphereDrawPushConstant pc)
{
    uint quadIndex = vid / 6;
    uint2 corner = kQuadCorners[vid % 6];
    uint stack = quadIndex / kSphereSlices;
    uint slice = quadIndex % kSphereSlices;

    float phi = (float(slice + corner.x) / float(kSphereSlices)) * 2.0 * kPi;
    float theta = (float(stack + corner.y) / float(kSphereStacks)) * kPi;
    float sinTheta = sin(theta);
    float3 normal = float3(sinTheta * cos(phi), cos(theta), sinTheta * sin(phi));

    DebugSphereInstance inst = pc.instanceBuffer[iid];
    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 viewPos = mul(scene.view, float4(inst.center + normal * inst.radius, 1.0)).xyz;

    SphereVertexOutput output;
    output.position = mul(scene.proj, float4(viewPos, 1.0));
    output.normal = normal;
    output.shR = inst.shR;
    output.shG = inst.shG;
    output.shB = inst.shB;
    return output;
}

[shader("fragment")]
float4 FragmentDebugSphere(SphereVertexOutput input) : SV_Target0
{
    float3 n = normalize(input.normal);
    float3 color = float3(
        input.shR.x + dot(input.shR.yzw, n),
        input.shG.x + dot(input.shG.yzw, n),
        input.shB.x + dot(input.shB.yzw, n));
    return float4(max(color, 0.0), 1.0);
}
