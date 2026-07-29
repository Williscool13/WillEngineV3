import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;
import bindless_declarations;

// =============== Shader Summary ============
/* Debug line renderer using mesh shader.
 * Expands DebugLineSegments to pixel-space quads shaded as round-capped capsules.
 * No culling - assumes CPU has already culled primitives.
 */

struct VertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float4 capsule : TEXCOORD0; // x = axial, y = lateral, z = segment length, w = local radius, all in pixels
};

// Partial-compensation width scaling
static const float kRefDistance = 5.0;
static const float kDistanceExponent = 0.6;
static const float kMinScale = 0.35;
static const float kMaxScale = 3.0;
static const float kMinClipW = 1e-4;

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

    uint vBase = gtid.x * 4;
    uint pBase = gtid.x * 2;
    triangles[pBase + 0] = uint3(vBase + 0, vBase + 1, vBase + 2);
    triangles[pBase + 1] = uint3(vBase + 1, vBase + 3, vBase + 2);

    float4 clipA = mul(scene.proj, float4(aView, 1.0));
    float4 clipB = mul(scene.proj, float4(bView, 1.0));
    if (clipA.w < kMinClipW && clipB.w < kMinClipW) {
        [unroll]
        for (uint i = 0; i < 4; i++) {
            vertices[vBase + i].position = float4(0.0, 0.0, 2.0, 1.0);
            vertices[vBase + i].color = seg.color;
            vertices[vBase + i].capsule = float4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    // Per-endpoint width so caps of segments sharing a vertex have identical radius
    float scaleA = clamp(pow(max(length(aView), 0.01) / kRefDistance, kDistanceExponent), kMinScale, kMaxScale);
    float scaleB = clamp(pow(max(length(bView), 0.01) / kRefDistance, kDistanceExponent), kMinScale, kMaxScale);
    float wA = seg.width * scaleA * 0.5f;
    float wB = seg.width * scaleB * 0.5f;

    // Manual near clip; screen-space construction breaks at w <= 0
    if (clipA.w < kMinClipW) {
        float t = (kMinClipW - clipA.w) / (clipB.w - clipA.w);
        clipA = lerp(clipA, clipB, t);
        wA = lerp(wA, wB, t);
    }
    if (clipB.w < kMinClipW) {
        float t = (kMinClipW - clipB.w) / (clipA.w - clipB.w);
        clipB = lerp(clipB, clipA, t);
        wB = lerp(wB, wA, t);
    }

    // Build the quad in pixel space so width and caps stay round on screen at any 3D angle
    float2 halfRes = scene.mainRenderTargetSize * 0.5;
    float projScaleY = abs(scene.proj[1][1]);
    float2 pA = clipA.xy / clipA.w * halfRes;
    float2 pB = clipB.xy / clipB.w * halfRes;
    float rA = wA * projScaleY / clipA.w * halfRes.y;
    float rB = wB * projScaleY / clipB.w * halfRes.y;

    float2 abS = pB - pA;
    float lenS = length(abS);
    float2 dirS = lenS > 1e-3 ? abS / lenS : float2(1.0, 0.0);
    float2 perpS = float2(-dirS.y, dirS.x);

    float2 c0 = pA - dirS * rA - perpS * rA;
    float2 c1 = pA - dirS * rA + perpS * rA;
    float2 c2 = pB + dirS * rB - perpS * rB;
    float2 c3 = pB + dirS * rB + perpS * rB;
    float zA = clipA.z / clipA.w;
    float zB = clipB.z / clipB.w;

    // w = 1 keeps interpolation screen-linear to match the pixel-space capsule coords
    vertices[vBase + 0].position = float4(c0 / halfRes, zA, 1.0);
    vertices[vBase + 1].position = float4(c1 / halfRes, zA, 1.0);
    vertices[vBase + 2].position = float4(c2 / halfRes, zB, 1.0);
    vertices[vBase + 3].position = float4(c3 / halfRes, zB, 1.0);
    vertices[vBase + 0].color = seg.color;
    vertices[vBase + 1].color = seg.color;
    vertices[vBase + 2].color = seg.color;
    vertices[vBase + 3].color = seg.color;
    vertices[vBase + 0].capsule = float4(-rA, -rA, lenS, rA);
    vertices[vBase + 1].capsule = float4(-rA, rA, lenS, rA);
    vertices[vBase + 2].capsule = float4(lenS + rB, -rB, lenS, rB);
    vertices[vBase + 3].capsule = float4(lenS + rB, rB, lenS, rB);
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void MeshDebugRenderGPU(
    out indices uint3 triangles[64], out vertices VertexOutput vertices[128],
    uint3 gid: SV_GroupID,
    uint3 gtid: SV_GroupThreadID,
    uniform GPUDebugDrawPushConstant pc)
{
    uint totalSegments = min(pc.args->segmentCount, pc.args->capacity);
    uint validThreads = min(totalSegments - gid.x * 32, 32);
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

    uint vBase = gtid.x * 4;
    uint pBase = gtid.x * 2;
    triangles[pBase + 0] = uint3(vBase + 0, vBase + 1, vBase + 2);
    triangles[pBase + 1] = uint3(vBase + 1, vBase + 3, vBase + 2);

    float4 clipA = mul(scene.proj, float4(aView, 1.0));
    float4 clipB = mul(scene.proj, float4(bView, 1.0));
    if (clipA.w < kMinClipW && clipB.w < kMinClipW) {
        [unroll]
        for (uint i = 0; i < 4; i++) {
            vertices[vBase + i].position = float4(0.0, 0.0, 2.0, 1.0);
            vertices[vBase + i].color = seg.color;
            vertices[vBase + i].capsule = float4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    // Per-endpoint width so caps of segments sharing a vertex have identical radius
    float scaleA = clamp(pow(max(length(aView), 0.01) / kRefDistance, kDistanceExponent), kMinScale, kMaxScale);
    float scaleB = clamp(pow(max(length(bView), 0.01) / kRefDistance, kDistanceExponent), kMinScale, kMaxScale);
    float wA = seg.width * scaleA * 0.5f;
    float wB = seg.width * scaleB * 0.5f;

    // Manual near clip; screen-space construction breaks at w <= 0
    if (clipA.w < kMinClipW) {
        float t = (kMinClipW - clipA.w) / (clipB.w - clipA.w);
        clipA = lerp(clipA, clipB, t);
        wA = lerp(wA, wB, t);
    }
    if (clipB.w < kMinClipW) {
        float t = (kMinClipW - clipB.w) / (clipA.w - clipB.w);
        clipB = lerp(clipB, clipA, t);
        wB = lerp(wB, wA, t);
    }

    // Build the quad in pixel space so width and caps stay round on screen at any 3D angle
    float2 halfRes = scene.mainRenderTargetSize * 0.5;
    float projScaleY = abs(scene.proj[1][1]);
    float2 pA = clipA.xy / clipA.w * halfRes;
    float2 pB = clipB.xy / clipB.w * halfRes;
    float rA = wA * projScaleY / clipA.w * halfRes.y;
    float rB = wB * projScaleY / clipB.w * halfRes.y;

    float2 abS = pB - pA;
    float lenS = length(abS);
    float2 dirS = lenS > 1e-3 ? abS / lenS : float2(1.0, 0.0);
    float2 perpS = float2(-dirS.y, dirS.x);

    float2 c0 = pA - dirS * rA - perpS * rA;
    float2 c1 = pA - dirS * rA + perpS * rA;
    float2 c2 = pB + dirS * rB - perpS * rB;
    float2 c3 = pB + dirS * rB + perpS * rB;
    float zA = clipA.z / clipA.w;
    float zB = clipB.z / clipB.w;

    // w = 1 keeps interpolation screen-linear to match the pixel-space capsule coords
    vertices[vBase + 0].position = float4(c0 / halfRes, zA, 1.0);
    vertices[vBase + 1].position = float4(c1 / halfRes, zA, 1.0);
    vertices[vBase + 2].position = float4(c2 / halfRes, zB, 1.0);
    vertices[vBase + 3].position = float4(c3 / halfRes, zB, 1.0);
    vertices[vBase + 0].color = seg.color;
    vertices[vBase + 1].color = seg.color;
    vertices[vBase + 2].color = seg.color;
    vertices[vBase + 3].color = seg.color;
    vertices[vBase + 0].capsule = float4(-rA, -rA, lenS, rA);
    vertices[vBase + 1].capsule = float4(-rA, rA, lenS, rA);
    vertices[vBase + 2].capsule = float4(lenS + rB, -rB, lenS, rB);
    vertices[vBase + 3].capsule = float4(lenS + rB, rB, lenS, rB);
}

// =============== Fragment Shader ===============
[shader("fragment")]
float4 FragmentDebugRender(VertexOutput input) : SV_Target0
{
    float t = clamp(input.capsule.x, 0.0, input.capsule.z);
    float d = length(float2(input.capsule.x - t, input.capsule.y)) / max(input.capsule.w, 1e-6);
    float aa = saturate((1.0 - d) / max(fwidth(d), 1e-5));
    float4 color = input.color;
    color.a *= aa;
    return color;
}
