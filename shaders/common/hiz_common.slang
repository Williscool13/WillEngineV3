module hiz_common;

import bindless_declarations;

// Screen rect + nearest depth vs the min (farthest) pyramid value over the rect. Mip chosen so the rect spans <= 1 texel per axis and 4 loads cover it.
bool IsRectOccluded(float2 uvMin, float2 uvMax, float maxDepth, uint hizIndex, uint hizMipCount, uint2 hizExtent)
{
    if (uvMax.x <= 0.0 || uvMax.y <= 0.0 || uvMin.x >= 1.0 || uvMin.y >= 1.0) {
        return false;
    }
    uvMin = clamp(uvMin, 0.0, 1.0);
    uvMax = clamp(uvMax, 0.0, 1.0);

    float2 sizeTexels = (uvMax - uvMin) * float2(hizExtent);
    float maxSize = max(sizeTexels.x, sizeTexels.y);
    uint mip = uint(clamp(ceil(log2(max(maxSize, 1.0))), 0.0, float(hizMipCount - 1)));
    uint2 mipExtent = max(hizExtent >> mip, uint2(1u, 1u));
    uint2 t0 = min(uint2(uvMin * float2(mipExtent)), mipExtent - 1u);
    uint2 t1 = min(uint2(uvMax * float2(mipExtent)), mipExtent - 1u);
    float hiz = min(min(rdgTexturesFloat[hizIndex].Load(int3(t0.x, t0.y, mip)), rdgTexturesFloat[hizIndex].Load(int3(t1.x, t0.y, mip))),
                    min(rdgTexturesFloat[hizIndex].Load(int3(t0.x, t1.y, mip)), rdgTexturesFloat[hizIndex].Load(int3(t1.x, t1.y, mip))));
    return maxDepth < hiz;
}

// Conservative sphere test: projects the 8 corners of the sphere's world AABB.
// Returns true only when provably occluded; any untestable case (near-plane crossing) is visible.
public bool IsOccludedHiZ(float3 worldCenter, float worldRadius, float4x4 viewProj, uint hizIndex, uint hizMipCount, uint2 hizExtent)
{
    float2 uvMin = float2(1e9, 1e9);
    float2 uvMax = float2(-1e9, -1e9);
    float maxDepth = 0.0;
    for (uint i = 0; i < 8; i++) {
        float3 corner = worldCenter + worldRadius * float3(
            (i & 1) != 0 ? 1.0 : -1.0,
            (i & 2) != 0 ? 1.0 : -1.0,
            (i & 4) != 0 ? 1.0 : -1.0);
        float4 clip = mul(viewProj, float4(corner, 1.0));
        if (clip.w <= 1e-6) {
            return false;
        }
        float3 ndc = clip.xyz / clip.w;
        float2 uv = ndc.xy * 0.5 + 0.5;
        uvMin = min(uvMin, uv);
        uvMax = max(uvMax, uv);
        maxDepth = max(maxDepth, ndc.z);
    }

    return IsRectOccluded(uvMin, uvMax, maxDepth, hizIndex, hizMipCount, hizExtent);
}

// Tighter box test: projects the 8 corners of the model-space AABB through mvp = viewProj * modelMatrix (the oriented box, not a world AABB).
public bool IsOccludedHiZBox(float3 boxMin, float3 boxMax, float4x4 mvp, uint hizIndex, uint hizMipCount, uint2 hizExtent)
{
    float2 uvMin = float2(1e9, 1e9);
    float2 uvMax = float2(-1e9, -1e9);
    float maxDepth = 0.0;
    for (uint i = 0; i < 8; i++) {
        float3 corner = float3(
            (i & 1) != 0 ? boxMax.x : boxMin.x,
            (i & 2) != 0 ? boxMax.y : boxMin.y,
            (i & 4) != 0 ? boxMax.z : boxMin.z);
        float4 clip = mul(mvp, float4(corner, 1.0));
        if (clip.w <= 1e-6) {
            return false;
        }
        float3 ndc = clip.xyz / clip.w;
        float2 uv = ndc.xy * 0.5 + 0.5;
        uvMin = min(uvMin, uv);
        uvMax = max(uvMax, uv);
        maxDepth = max(maxDepth, ndc.z);
    }

    return IsRectOccluded(uvMin, uvMax, maxDepth, hizIndex, hizMipCount, hizExtent);
}
