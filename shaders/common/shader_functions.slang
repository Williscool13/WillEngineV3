import common_interop;
import model_interop;

// Returns false if the world-space AABB is fully outside any frustum plane
bool IsAABBInFrustum(Frustum frustum, float3 aabbMin, float3 aabbMax)
{
    for (int i = 0; i < 6; i++) {
        float4 plane = frustum.planes[i];
        // Positive vertex: component-wise max projection onto plane normal
        float3 pv = float3(
            plane.x >= 0.0 ? aabbMax.x : aabbMin.x,
            plane.y >= 0.0 ? aabbMax.y : aabbMin.y,
            plane.z >= 0.0 ? aabbMax.z : aabbMin.z
        );
        if (dot(plane.xyz, pv) + plane.w < 0.0) {
            return false;
        }
    }
    return true;
}

// Returns false if it has been culled
bool IsSphereInFrustum(Frustum frustum, float4 sphere) {
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
bool IsConeFrontFacing(float3 coneAxis, float coneCutoff, float3 center, float radius, float3 cameraPos, float3x3 modelMatrix) {
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

// World size of one pixel at the shading point. 1/NoV (capped 10x) covers the plane-distance error grazing surfaces add.
float RayOriginPixelFootprint(float viewZ, float2 ndcToViewMulXPixelSize, float NoV)
{
    return abs(viewZ) * max(abs(ndcToViewMulXPixelSize.x), abs(ndcToViewMulXPixelSize.y)) / max(NoV, 0.1);
}

// Lifts a gbuffer position off its surface. Footprint-scaled, which keeps the tangential slide the normal-mapped gbuffer normal introduces sub-pixel.
float3 OffsetGBufferRayOrigin(float3 worldPos, float3 worldN, float footprint, float scale)
{
    return worldPos + worldN * max(0.002, footprint * scale);
}

// Every reflection producer and reflection_shade must agree on this: hitT is measured from this origin and turned back into a world position there.
float3 ReflectionRayOrigin(float3 worldPos, float3 worldN, float viewZ, float2 ndcToViewMulXPixelSize, float NoV)
{
    return OffsetGBufferRayOrigin(worldPos, worldN, RayOriginPixelFootprint(viewZ, ndcToViewMulXPixelSize, NoV), 2.0);
}

// Secondary hits carry no pixel footprint; the offset only has to clear the interpolated-normal/triangle mismatch and float32 ulp at this coordinate magnitude.
float3 OffsetHitRayOrigin(float3 worldPos, float3 worldN)
{
    return worldPos + worldN * max(0.002, length(worldPos) * 1e-5);
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

// Interpolated surface attributes in view space, reconstructed from barycentrics + vertices.
// mv: model-view matrix (mul(view, modelMatrix))
// bary: float3(bary.x, bary.y, 1 - bary.x - bary.y) from the barycentric buffer
struct InterpolatedVertex
{
    float3 normalVS;
    float3 tangentVS;
    float tangentSign;
    float2 uv;
    float4 color;
};

float3 DecodePosition(VertexPosition v, float3 minBounds, float3 extent)
{
    return float3(float(v.pos0 & 0xFFFF), float(v.pos0 >> 16), float(v.pos1 & 0xFFFF)) / 65535.0 * extent + minBounds;
}

float3 DecodeNormal(VertexAttribute v)
{
    float nx = float(int((v.normalOct & 0xFF) << 24) >> 24) / 127.0;
    float ny = float(int((v.normalOct >> 8 & 0xFF) << 24) >> 24) / 127.0;
    return OctDecode(float2(nx, ny));
}

float3 DecodeTangent(VertexAttribute v)
{
    float tx = float(int((v.tangentOct & 0xFF) << 24) >> 24) / 127.0;
    float ty = float(int((v.tangentOct >> 8 & 0xFF) << 24) >> 24) / 127.0;
    return OctDecode(float2(tx, ty));
}

float DecodeTangentSign(VertexAttribute v)
{
    return bool(v.tangentOct >> 16 & 1) ? 1.0 : -1.0;
}

float2 DecodeUV(VertexAttribute v)
{
    return float2(f16tof32(v.texcoord & 0xFFFF), f16tof32(v.texcoord >> 16));
}

float4 DecodeColor(VertexAttribute v)
{
    return float4(float(v.color & 0xFF), float(v.color >> 8 & 0xFF), float(v.color >> 16 & 0xFF), float(v.color >> 24 & 0xFF)) / 255.0;
}

// Returns interpolated view-space position. For custom shading pipelines that need exact object-space
// position and already have the vertices in registers. mv: model-view matrix (mul(view, modelMatrix))
float3 InterpolatePosition(float3 bary, VertexPosition v0, VertexPosition v1, VertexPosition v2, float4x4 mv, float3 minBounds, float3 extent)
{
    float3 p0 = DecodePosition(v0, minBounds, extent);
    float3 p1 = DecodePosition(v1, minBounds, extent);
    float3 p2 = DecodePosition(v2, minBounds, extent);
    float3 objPos = bary.x * p0 + bary.y * p1 + bary.z * p2;
    return mul(mv, float4(objPos, 1.0f)).xyz;
}

InterpolatedVertex InterpolateVertex(float3 bary, VertexAttribute v0, VertexAttribute v1, VertexAttribute v2, float4x4 mv)
{
    // Normal: snorm8 packed as normalOct=[_:_:Y:X], sign-extended via shift
    float3 n0 = DecodeNormal(v0);
    float3 n1 = DecodeNormal(v1);
    float3 n2 = DecodeNormal(v2);

    // Tangent: snorm8 packed as tangentOct=[sign:_:Y:X], sign-extended via shift
    float3 t0 = DecodeTangent(v0);
    float3 t1 = DecodeTangent(v1);
    float3 t2 = DecodeTangent(v2);

    // UV: float16 packed as texcoord=[V:U]
    float2 uv0 = DecodeUV(v0);
    float2 uv1 = DecodeUV(v1);
    float2 uv2 = DecodeUV(v2);

    // Color: unorm8 RGBA packed into color
    float4 c0 = DecodeColor(v0);
    float4 c1 = DecodeColor(v1);
    float4 c2 = DecodeColor(v2);

    InterpolatedVertex interp;

    float3 normalOS  = normalize(bary.x * n0 + bary.y * n1 + bary.z * n2);
    float3 tangentOS = normalize(bary.x * t0 + bary.y * t1 + bary.z * t2);

    float s0 = DecodeTangentSign(v0);
    float s1 = DecodeTangentSign(v1);
    float s2 = DecodeTangentSign(v2);

    float3x3 normalMatrix = Adjugate((float3x3)mv);
    interp.normalVS = normalize(mul(normalMatrix, normalOS));
    interp.tangentVS  = normalize(mul((float3x3)mv, tangentOS));
    interp.tangentSign = sign(bary.x * s0 + bary.y * s1 + bary.z * s2);
    interp.uv         = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;
    interp.color      = bary.x * c0  + bary.y * c1  + bary.z * c2;

    return interp;
}



// Albedo: pack float4 RGBA into R32 (RGB8 + 8-bit alpha)
uint PackAlbedoRGBA8(float4 c)
{
    return uint(saturate(c.r) * 255.0 + 0.5)
         | (uint(saturate(c.g) * 255.0 + 0.5) << 8)
         | (uint(saturate(c.b) * 255.0 + 0.5) << 16)
         | (uint(saturate(c.a) * 255.0 + 0.5) << 24);
}

float4 UnpackAlbedoRGBA8(uint p)
{
    return float4(
        float(p & 0xFF),
        float((p >> 8) & 0xFF),
        float((p >> 16) & 0xFF),
        float((p >> 24) & 0xFF)
    ) / 255.0;
}

// Normal oct16: encodes a unit view-space normal into R32 as two packed uint16s
uint PackNormalOct16(float3 n)
{
    float2 oct = OctEncode(n) * 0.5 + 0.5; // remap [-1,1] -> [0,1]
    uint x = uint(saturate(oct.x) * 65535.0 + 0.5);
    uint y = uint(saturate(oct.y) * 65535.0 + 0.5);
    return x | (y << 16);
}

float3 UnpackNormalOct16(uint p)
{
    float2 oct = float2(float(p & 0xFFFF), float(p >> 16)) / 65535.0 * 2.0 - 1.0;
    return OctDecode(oct);
}

// Emissive: E5B9G9R9 shared-exponent (same bit layout as VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
uint PackEmissiveRGBE(float3 e)
{
    float maxC = max(max(e.r, e.g), max(e.b, 1e-5f));
    float exp_ = floor(log2(maxC)) + 1.0;
    float scale = pow(2.0, -(exp_ - 9.0));
    uint re = uint(clamp(e.r * scale, 0.0, 511.0) + 0.5);
    uint ge = uint(clamp(e.g * scale, 0.0, 511.0) + 0.5);
    uint be = uint(clamp(e.b * scale, 0.0, 511.0) + 0.5);
    uint biasedExp = uint(clamp(exp_ + 15.0, 0.0, 31.0));
    return re | (ge << 9) | (be << 18) | (biasedExp << 27);
}

float3 UnpackEmissiveRGBE(uint p)
{
    uint biasedExp = p >> 27;
    float scale = pow(2.0, float(biasedExp) - 15.0 - 9.0);
    return float3(float(p & 0x1FF), float((p >> 9) & 0x1FF), float((p >> 18) & 0x1FF)) * scale;
}

// PBR: roughness (15-bit) + metalness (1-bit, binary) packed into R32
uint PackPBR(float roughness, float metallic)
{
    uint r = uint(saturate(roughness) * 32767.0 + 0.5);
    uint m = metallic >= 0.5 ? 1u : 0u;
    return r | (m << 31);
}

void UnpackPBR(uint p, out float roughness, out float metallic)
{
    roughness = float(p & 0x7FFF) / 32767.0;
    metallic  = float(p >> 31);
}

// Motion vectors: float2 NDC delta packed as R16G16 half-float into R32
uint PackMotionVectors(float2 v)
{
    return f32tof16(v.x) | (f32tof16(v.y) << 16);
}

float2 UnpackMotionVectors(uint p)
{
    return float2(f16tof32(p & 0xFFFF), f16tof32(p >> 16));
}

// View-depth delta (viewZprev - viewZ) packed as float16 into bits 16-31 of gbuffer_one.w
uint PackViewZDelta(float delta)
{
    return f32tof16(delta) << 16;
}

float UnpackViewZDelta(uint p)
{
    return f16tof32(p >> 16);
}

float Luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Target-to-adapted luminance ratio, clamped to [0.25, 32]; scales noise thresholds with exposure
float ExposureNoiseScale(float adaptedLuminance, float targetLuminance)
{
    return clamp(targetLuminance / max(adaptedLuminance, 1e-3), 0.25, 32.0);
}

float MSDFScreenPxRange(Texture2D tex, float2 uv, float pxRange)
{
    uint2 atlasSize;
    uint mipCount;
    tex.GetDimensions(0, atlasSize.x, atlasSize.y, mipCount);
    float2 unitRange    = float2(pxRange) / float2(atlasSize);
    float2 screenTexSize = rsqrt(ddx(uv) * ddx(uv) + ddy(uv) * ddy(uv));
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}