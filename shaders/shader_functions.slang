import common_interop;
import model_interop;

// Returns false if it has been culled
bool FrustumCullSphere(Frustum frustum, float4 sphere) {
    // sphere.xyz = center, sphere.w = radius
    for (int i = 0; i < 6; i++) {
        float4 plane = frustum.planes[i];
        float dist = dot(plane.xyz, sphere.xyz) + plane.w;
        if (dist < -sphere.w) {
            return false;
        }
    }
    return true;
}

// Returns false if backface culled
bool BackfaceCullCone(float3 coneAxis, float coneCutoff, float3 center, float radius, float3 cameraPos, float3x3 modelMatrix) {
    float3 worldConeAxis = normalize(mul(modelMatrix, coneAxis));
    float3 cameraToCenter = center - cameraPos;
    float centerDist = length(cameraToCenter);

    // Cone is backfacing if: dot(center - camera, axis) >= cutoff * dist + radius
    return dot(cameraToCenter, worldConeAxis) < coneCutoff * centerDist + radius;
}

float3x3 Adjugate(float3x3 m) {
    return float3x3(
        cross(m[1], m[2]),
        cross(m[2], m[0]),
        cross(m[0], m[1])
    );
}

float ScreenToViewSpaceDepth(float screenDepth, float depthLinearizeMul, float depthLinearizeAdd) {
    // Optimization by XeGTAO
    // https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.hlsli#L112
    return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

// Reconstructs position of a pixel based on inverse project and depth buffer. Doesn't work in all cases.
float3 CheapReconstructViewSpacePosition(float2 ndcToViewMul, float2 ndcToViewAdd, float2 uv, float viewSpaceDepth)
{
    float3 ret;
    ret.xy = (ndcToViewMul * uv + ndcToViewAdd) * viewSpaceDepth;
    ret.y = -ret.y;
    ret.z = -viewSpaceDepth;
    return ret;
}

// Reconstructs position of a pixel based on inverse project and depth buffer
float3 ReconstructViewPosition(float4x4 invProj, float2 uv, float depth) {
    float2 ndc = uv * 2.0 - 1.0;
    float4 positionVS = mul(invProj, float4(ndc, depth, 1.0));
    return positionVS.xyz / positionVS.w;
}

// Reconstructs position of a pixel based on inverse project and depth buffer
float3 ReconstructWorldPosition(float4x4 invViewProj, float2 uv, float depth) {
    float2 ndc = uv * 2.0 - 1.0;
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float3 RGBToYCoCg(float3 rgb) {
    float Y  = dot(rgb, float3( 0.25,  0.5,   0.25));
    float Co = dot(rgb, float3( 0.5,   0.0,  -0.5));
    float Cg = dot(rgb, float3(-0.25,  0.5,  -0.25));
    return float3(Y, Co, Cg);
}

float3 YCoCgToRGB(float3 ycocg) {
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;

    float tmp = Y - Cg;
    float r = tmp + Co;
    float g = Y + Cg;
    float b = tmp - Co;

    return float3(r, g, b);
}

// Interpolated surface attributes in view space, reconstructed from barycentrics + vertices.
// mv: model-view matrix (mul(view, modelMatrix))
// bary: float3(bary.x, bary.y, 1 - bary.x - bary.y) from the barycentric buffer
struct InterpolatedVertex
{
    float3 normalVS;
    float2 uv;
    float4 color;
};

InterpolatedVertex InterpolateVertex(float3 bary, Vertex v0, Vertex v1, Vertex v2, float4x4 mv)
{
    InterpolatedVertex result;

    float3 objNormal = normalize(bary.x * v0.normal + bary.y * v1.normal + bary.z * v2.normal);
    result.normalVS = normalize(mul(Adjugate((float3x3)mv), objNormal));

    result.uv = bary.x * float2(v0.texcoordU, v0.texcoordV)
              + bary.y * float2(v1.texcoordU, v1.texcoordV)
              + bary.z * float2(v2.texcoordU, v2.texcoordV);

    result.color = bary.x * v0.color + bary.y * v1.color + bary.z * v2.color;

    return result;
}

// Returns interpolated view-space position. For custom shading pipelines that need exact object-space
// position and already have the vertices in registers. mv: model-view matrix (mul(view, modelMatrix))
float3 InterpolatePosition(float3 bary, Vertex v0, Vertex v1, Vertex v2, float4x4 mv)
{
    float3 objPos = bary.x * v0.position + bary.y * v1.position + bary.z * v2.position;
    return mul(mv, float4(objPos, 1.0f)).xyz;
}

// Octahedral encoding: maps a unit float3 normal to float2 in [-1,1]^2.
float2 OctEncode(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    if (n.z < 0.0f) {
        n.xy = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    }
    return n.xy;
}

// Decodes an octahedral-encoded normal back to a unit float3.
float3 OctDecode(float2 f)
{
    float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}