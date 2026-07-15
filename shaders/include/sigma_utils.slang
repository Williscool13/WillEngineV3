/*
 * SIGMA shadow denoiser utility functions.
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

module sigma_utils;

import bindless_declarations;
import constants_interop;
import relax_utils;

public static const float SIGMA_FP16_MAX = 65504.0;
public static const int SIGMA_BORDER = 2;             // 5x5 dense kernel half-width
public static const float SIGMA_TS_SIGMA_SCALE = 3.0; // variance clamp width in temporal stabilization
public static const float SIGMA_MAX_ACCUM_FRAME_NUM = 7.0;
public static const float SIGMA_PLANE_DIST_SENSITIVITY = 0.005;
public static const float SIGMA_DISOCCLUSION_THRESHOLD = 0.02;

/** NRD g_Special8: 8-tap sparse Poisson kernel, .xy in [-1,1], .z gaussian weight. */
public static const float3 SIGMA_SPECIAL8[8] = {
    float3(-1.0, 0.0, 1.0),
    float3(0.0, 1.0, 1.0),
    float3(1.0, 0.0, 1.0),
    float3(0.0, -1.0, 1.0),
    float3(-0.25 * 1.41421356, 0.25 * 1.41421356, 0.5),
    float3(0.25 * 1.41421356, 0.25 * 1.41421356, 0.5),
    float3(0.25 * 1.41421356, -0.25 * 1.41421356, 0.5),
    float3(-0.25 * 1.41421356, -0.25 * 1.41421356, 0.5),
};

public bool SigmaIsLit(float penumbra) { return penumbra >= SIGMA_FP16_MAX; }

/** Storage encoding: shadow is held as sqrt() in textures and squared back to linear on load (matches NRD). */
public float SigmaPackShadow(float s) { return sqrt(saturate(s)); }
public float SigmaUnpackShadow(float s) { return s * s; }

public float SigmaAreBothLitOrUnlit(float penumbra1, float penumbra2)
{
    bool noL1 = penumbra1 == 0.0;
    bool noL2 = penumbra2 == 0.0;
    return float(noL1 == noL2);
}

/** World-space penumbra hitDist -> blur radius in pixels. unprojectZ is world size of one pixel at this depth. */
public float SigmaGetKernelRadiusInPixels(float hitDist, float unprojectZ, float maxPixelRadius, float scale = 1.0)
{
    float unclampedRadius = (hitDist / unprojectZ) * scale;
    float minRadius = min(unclampedRadius, 2.0);
    return clamp(unclampedRadius, minRadius, maxPixelRadius);
}

/** NRD FilterBicubic + TextureCubic for a 2-channel texture (smooth per-tile blur-radius read). */
public float2 SigmaTextureCubic(uint texIdx, float2 uv)
{
    uint texW, texH;
    rdgTexturesFloat2[texIdx].GetDimensions(texW, texH);
    float2 size = float2(texW, texH);

    const float4 c1 = float4(3.0, 0.0, 1.0, 4.0);
    const float4 c2 = float4(-1.0, 3.0, -3.0, 1.0);
    const float4 c3 = float4(3.0, -6.0, -3.0, 0.0);
    const float k = 1.0 / 6.0;

    float4 dxdy = -c1.zyyz / size.xyxy;
    float2 f = frac(uv * size - 0.5);
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float3 xw, yw;
    float4 phi;

    phi = k * (c2.xyzw * f3.xxxx + c3.xyxw * f2.xxxx + c3.zwxw * f.xxxx + c1.zwzy);
    xw.xy = c2.ww + c2.wx * f.xx + c2.xw * phi.yw / (phi.xz + phi.yw);
    xw.z = phi.x + phi.y;

    phi = k * (c2.xyzw * f3.yyyy + c3.xyxw * f2.yyyy + c3.zwxw * f.yyyy + c1.zwzy);
    yw.xy = c2.ww + c2.wx * f.yy + c2.xw * phi.yw / (phi.xz + phi.yw);
    yw.z = phi.x + phi.y;

    float4 uv_10_00 = uv.xyxy + c2.wwxx * xw.xxyy * dxdy.xyxy;
    float4 uv_11_01 = uv_10_00 + yw.xxxx * dxdy.zwzw;
    uv_10_00 -= yw.yyyy * dxdy.zwzw;
    float2 t = float2(yw.z, xw.z);

    SamplerState lin = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];
    float2 c00 = rdgTexturesFloat2[texIdx].SampleLevel(lin, uv_10_00.zw, 0);
    float2 c10 = rdgTexturesFloat2[texIdx].SampleLevel(lin, uv_10_00.xy, 0);
    float2 c01 = rdgTexturesFloat2[texIdx].SampleLevel(lin, uv_11_01.zw, 0);
    float2 c11 = rdgTexturesFloat2[texIdx].SampleLevel(lin, uv_11_01.xy, 0);

    c00 = lerp(c00, c01, t.x);
    c10 = lerp(c10, c11, t.x);
    return lerp(c00, c10, t.y);
}

/** NRD BicubicFilterNoCornersWithFallbackToBilinearFilterWithCustomWeights (12-tap CatRom), single channel .r. */
public float SigmaBicubicHistory(uint texIdx, float2 samplePos, float2 invResourceSize, float4 bilinearCustomWeights, bool useBicubic)
{
    float2 centerPos = floor(samplePos - 0.5) + 0.5;
    float2 f = saturate(samplePos - centerPos);
    float2 w0 = f * (f * (-NRD_CATROM_SHARPNESS * f + 2.0 * NRD_CATROM_SHARPNESS) - NRD_CATROM_SHARPNESS);
    float2 w1 = f * (f * ((2.0 - NRD_CATROM_SHARPNESS) * f - (3.0 - NRD_CATROM_SHARPNESS))) + 1.0;
    float2 w2 = f * (f * (-(2.0 - NRD_CATROM_SHARPNESS) * f + (3.0 - 2.0 * NRD_CATROM_SHARPNESS)) + NRD_CATROM_SHARPNESS);
    float2 w3 = f * (f * (NRD_CATROM_SHARPNESS * f - NRD_CATROM_SHARPNESS));
    float2 w12 = w1 + w2;
    float2 tc = w2 / w12;
    float4 w;
    w.x = w12.x * w0.y;
    w.y = w0.x * w12.y;
    w.z = w12.x * w12.y;
    w.w = w3.x * w12.y;
    float w4 = w12.x * w3.y;
    w = useBicubic ? w : bilinearCustomWeights;
    w4 = useBicubic ? w4 : 0.0;
    float sumW = dot(w, 1.0) + w4;
    float4 uv01 = centerPos.xyxy + (useBicubic ? float4(tc.x, -1.0, -1.0, tc.y) : float4(0, 0, 1, 0));
    float4 uv23 = centerPos.xyxy + (useBicubic ? float4(tc.x, tc.y, 2.0, tc.y) : float4(0, 1, 1, 1));
    float2 uv4 = centerPos + (useBicubic ? float2(tc.x, 2.0) : f);
    uv01 *= invResourceSize.xyxy;
    uv23 *= invResourceSize.xyxy;
    uv4 *= invResourceSize;

    SamplerState lin = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];
    float color = rdgTexturesFloat2[texIdx].SampleLevel(lin, uv01.xy, 0).r * w.x;
    color += rdgTexturesFloat2[texIdx].SampleLevel(lin, uv01.zw, 0).r * w.y;
    color += rdgTexturesFloat2[texIdx].SampleLevel(lin, uv23.xy, 0).r * w.z;
    color += rdgTexturesFloat2[texIdx].SampleLevel(lin, uv23.zw, 0).r * w.w;
    color += rdgTexturesFloat2[texIdx].SampleLevel(lin, uv4, 0).r * w4;
    return sumW < 0.0001 ? 0.0 : color / sumW;
}

/** NRD per-frame blur rotator: GetRotator(Weyl1D(1/sqrt(3), frame)*90deg [+22.5deg post-blur]). */
public float4 SigmaBlurRotator(uint frameIndex, uint passIndex)
{
    float weyl = frac(0.57735026919 + float(frameIndex * 10368889u) / 16777216.0);
    float angle = weyl * 1.57079632679 + float(passIndex) * 0.39269908169;
    return MLGetRotator(angle);
}

/** World size of one pixel at a given linear view depth (NRD PixelRadiusToWorld for perspective). */
public float SigmaPixelSize(float2 ndcToViewMul, float viewZ, uint renderExtentY)
{
    return abs(ndcToViewMul.y) * viewZ / float(renderExtentY);
}

/** NRD frustum size at a given depth: min screen dimension expressed in world units. */
public float SigmaFrustumSize(float2 ndcToViewMul, float viewZ, uint2 renderExtent)
{
    float pixelSize = SigmaPixelSize(ndcToViewMul, viewZ, renderExtent.y);
    return float(min(renderExtent.x, renderExtent.y)) * pixelSize;
}

/** Disocclusion threshold with the REBLUR/RELAX 1/NoV slope scale (stock SIGMA uses NoV=1). */
public float SigmaDisocclusionThreshold(float frustumSize, float NoV)
{
    return frustumSize * saturate(SIGMA_DISOCCLUSION_THRESHOLD / max(0.05, NoV));
}
