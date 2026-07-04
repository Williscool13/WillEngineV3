import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;
import bindless_declarations;

// =============== Shader Summary ============
/* Debug line renderer using mesh shader.
 * Generates quads from DebugLineSegments with variable width in view space.
 * No culling - assumes CPU has already culled primitives.
 */

struct VertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float cross : TEXCOORD0;
};

// =============== Fragment Shader ===============
[shader("fragment")]
float4 FragmentDebugRender(VertexOutput input) : SV_Target0
{
    float d = abs(input.cross);
    float aa = saturate((1.0 - d) / max(fwidth(d), 1e-5));
    float4 color = input.color;
    color.a *= aa;
    return color;
}

// Partial-compensation width scaling
static const float kRefDistance = 5.0;
static const float kDistanceExponent = 0.6;
static const float kMinScale = 0.35;
static const float kMaxScale = 3.0;

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void MeshDebugRender(
    out indices uint3 triangles[64], out vertices VertexOutput vertices[128],
    uint3 gid: SV_GroupID,
    uint3 gtid: SV_GroupThreadID,
    uint3 dtid: SV_DispatchThreadID,
    uniform DebugDrawPushConstant pc)
{
    uint validThreads = min(pc.totalLineSegments - gid.x * 32, 32);
    int32_t vertexCount = validThreads * 4;
    int32_t triangleCount = validThreads * 2;
    SetMeshOutputCounts(vertexCount, triangleCount);

    if (gtid.x >= validThreads) {
        return;
    }

    int32_t segmentIndex = gid.x * 32 + gtid.x;
    DebugLineSegment seg = pc.segmentBuffer[segmentIndex];

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float3 aView = mul(scene.view, float4(seg.a, 1.0)).xyz;
    float3 bView = mul(scene.view, float4(seg.b, 1.0)).xyz;

    float3 dir = normalize(bView - aView);
    float3 perp = normalize(cross(dir, float3(0, 0, 1)));

    float dist = length(0.5 * (aView + bView));
    float scale = clamp(pow(max(dist, 0.01) / kRefDistance, kDistanceExponent), kMinScale, kMaxScale);
    float halfWidth = seg.width * scale * 0.5f;

    // Expand to quad
    float3 v0 = aView - perp * halfWidth;
    float3 v1 = aView + perp * halfWidth;
    float3 v2 = bView - perp * halfWidth;
    float3 v3 = bView + perp * halfWidth;

    uint vBase = gtid.x * 4;
    vertices[vBase + 0].position = mul(scene.proj, float4(v0, 1.0));
    vertices[vBase + 1].position = mul(scene.proj, float4(v1, 1.0));
    vertices[vBase + 2].position = mul(scene.proj, float4(v2, 1.0));
    vertices[vBase + 3].position = mul(scene.proj, float4(v3, 1.0));
    vertices[vBase + 0].color = seg.color;
    vertices[vBase + 1].color = seg.color;
    vertices[vBase + 2].color = seg.color;
    vertices[vBase + 3].color = seg.color;
    vertices[vBase + 0].cross = -1.0;
    vertices[vBase + 1].cross = 1.0;
    vertices[vBase + 2].cross = -1.0;
    vertices[vBase + 3].cross = 1.0;

    uint pBase = gtid.x * 2;
    triangles[pBase + 0] = uint3(vBase + 0, vBase + 1, vBase + 2);
    triangles[pBase + 1] = uint3(vBase + 1, vBase + 3, vBase + 2);
}
