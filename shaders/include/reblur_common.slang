/*
 * ReBLUR core helpers: faithful port of NRD REBLUR_Common.hlsli (DIFFUSE_SPECULAR, YCoCg variant).
 * Internal-data packing, accumulation-speed model, history mixing, anisotropic kernel basis, antilag.
 * Globals in NRD (g*) are passed as explicit parameters here so this stays a pure math module (like relax_utils).
 * Config baked from REBLUR_Config.hlsli: ACCUMSPEED_BITS=6 (MAX_ACCUM=63), MATERIALID_BITS=4 (MAX=15),
 * USE_YCOCG=1, USE_CONFIDENCE_NON_LINEARLY=1, ANTILAG_MODE=2, SHOW=0.
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

module reblur_common;

import relax_utils;

public static const float REBLUR_MAX_ACCUM_FRAME_NUM = 63.0;
public static const float REBLUR_MAX_MATERIALID_NUM = 15.0;

// =====================================================================
// Internal data ( PREV_INTERNAL_DATA, R16_UINT ): diff accum (6b) | spec accum (6b) | materialID (4b)
// =====================================================================

public uint ReblurPackInternalData(float diffAccumSpeed, float specAccumSpeed, float materialID, float maxAccumulatedFrameNum)
{
    diffAccumSpeed = min(diffAccumSpeed + 1.0, maxAccumulatedFrameNum);
    specAccumSpeed = min(specAccumSpeed + 1.0, maxAccumulatedFrameNum);

    uint d = uint(round(diffAccumSpeed)) & 0x3Fu;
    uint s = uint(round(specAccumSpeed)) & 0x3Fu;
    uint m = uint(round(materialID)) & 0xFu;
    return d | (s << 6) | (m << 12);
}

/** @return x = diff accum frames, y = spec accum frames, z = materialID. */
public float3 ReblurUnpackInternalData(uint p)
{
    float d = float(p & 0x3Fu);
    float s = float((p >> 6) & 0x3Fu);
    float m = float((p >> 12) & 0xFu);
    return float3(d, s, m);
}

// =====================================================================
// Intermediate data 1 ( DATA1, RG8_UNORM ): diff accum, spec accum (normalized frame counts)
// =====================================================================

public float2 ReblurPackData1(float diffAccumSpeed, float specAccumSpeed)
{
    float2 r;
    r.x = saturate(round(diffAccumSpeed) / REBLUR_MAX_ACCUM_FRAME_NUM);
    r.y = saturate(round(specAccumSpeed) / REBLUR_MAX_ACCUM_FRAME_NUM);
    return r;
}

public float2 ReblurUnpackData1(float2 p)
{
    return round(p * REBLUR_MAX_ACCUM_FRAME_NUM);
}

// =====================================================================
// Intermediate data 2 ( DATA2, R32_UINT ): occlusion bits | virtualHistoryAmount | smbAllowCatRom | curvature
// =====================================================================

public uint ReblurPackData2(float fbits, float curvature, float virtualHistoryAmount, bool smbAllowCatRom)
{
    uint p = uint(fbits + 0.5);
    p |= uint(saturate(virtualHistoryAmount) * 127.0 + 0.5) << 8;
    p |= smbAllowCatRom ? (1u << 15) : 0u;
    p |= uint(f32tof16(curvature)) << 16;
    return p;
}

/** @return x = virtualHistoryAmount, y = curvature. */
public float2 ReblurUnpackData2(uint p, out uint bits, out bool smbAllowCatRom)
{
    bits = p & 0xFFu;
    smbAllowCatRom = (p & (1u << 15)) != 0u;
    float virtualHistoryAmount = float((p >> 8) & 127u) / 127.0;
    float curvature = f16tof32(p >> 16);
    return float2(virtualHistoryAmount, curvature);
}

// =====================================================================
// Accumulation-speed model
// =====================================================================

/** Lower bound on the hit-distance non-linear accumulation speed; prevents color banding from the
 *  exponential hit-dist weight + feedback loop. */
public float ReblurGetMinAllowedLimitForHitDistNonLinearAccumSpeed(float roughness, float maxAccumulatedFrameNum)
{
    float frameNum = 0.5 * RelaxGetSpecMagicCurve(roughness) * maxAccumulatedFrameNum;
    return 1.0 / (1.0 + frameNum);
}

public float ReblurRemapRoughnessToResponsiveFactor(float roughness, float invRoughnessThreshold)
{
    float amount = max(roughness, 1e-3) * invRoughnessThreshold;
    return MLSmoothStep01(amount);
}

/** Core ReBLUR accumulation speed (REBLUR_USE_CONFIDENCE_NON_LINEARLY = 1). */
public float ReblurGetNonLinearAccumSpeed(float accumSpeed, float maxAccumSpeed, float confidence, bool hasData, float checkerboardResolveAccumSpeed)
{
    float nonLinearAccumSpeed = max(1.0 - confidence, 1.0 / (1.0 + min(accumSpeed, maxAccumSpeed)));
    if (!hasData) {
        nonLinearAccumSpeed *= lerp(1.0 - checkerboardResolveAccumSpeed, 1.0, nonLinearAccumSpeed);
    }
    return nonLinearAccumSpeed;
}

/** Convergence-curve accumulation speed: f = 1/(1 + s*lerp(b,1,N/(1+p*maxAccum))*N). gConvergence = (s,b,p). */
public float ReblurGetAdvancedNonLinearAccumSpeed(float accumSpeed, float maxAccumulatedFrameNum, float3 convergence)
{
    float f = saturate(accumSpeed / (1.0 + maxAccumulatedFrameNum * convergence.z));
    float e = convergence.x * lerp(convergence.y, 1.0, f);
    return 1.0 / (1.0 + e * accumSpeed);
}

// =====================================================================
// History mixing (YCoCg radiance in .xyz, normalized hit distance in .w)
// =====================================================================

public float ReblurClampNegativeHitDistToZero(float hitDist) { return saturate(hitDist); }

public float ReblurGetLumaScale(float currLuma, float newLuma) { return (newLuma + NRD_EPS) / (currLuma + NRD_EPS); }

public float ReblurExtractHitDist(float4 v) { return v.w; }

/** YCoCg luma is the .x channel. */
public float ReblurGetLuma(float4 v) { return v.x; }

public float4 ReblurChangeLuma(float4 v, float newLuma)
{
    v.xyz *= ReblurGetLumaScale(ReblurGetLuma(v), newLuma);
    return v;
}

public float4 ReblurClampNegativeToZero(float4 v)
{
    // YCoCg round-trip clamps RGB >= 0 while keeping the encoding consistent (chroma stays signed).
    v.xyz = MLRgbToYCoCg(max(MLYCoCgToRgb(v.xyz), 0.0));
    v.w = ReblurClampNegativeHitDistToZero(v.w);
    return v;
}

/** Mix history->current; hit distance (.w) uses a floored alpha to avoid banding. */
public float4 ReblurMixHistoryAndCurrent(float4 history, float4 current, float f, float minHitDistLimit)
{
    float4 r;
    r.xyz = lerp(history.xyz, current.xyz, f);
    r.w = lerp(history.w, current.w, max(f, minHitDistLimit));
    return r;
}

// =====================================================================
// Antilag (REBLUR_ANTILAG_MODE = 2). antilag = (luminanceSigmaScale, luminanceSensitivity).
// Returns a [0,1] factor (1 = trust history, lower = suppress on luminance deviation).
// =====================================================================

public float ReblurComputeAntilag(float h, float a, float sigma, float accumSpeed, float2 antilag, float framerateScale)
{
    float s = sigma * antilag.x;
    float magic = antilag.y * framerateScale * framerateScale;

    float hc = clamp(h, a - s, a + s);
    float d = abs(h - hc) / (max(h, hc) + NRD_EPS);
    d = 1.0 / (1.0 + d * accumSpeed / max(magic, NRD_EPS));

    return d;
}

// =====================================================================
// Anisotropic kernel basis: stretch the blur footprint along the specular reflection direction.
// =====================================================================

public float2x3 ReblurGetKernelBasis(float3 D, float3 N)
{
    float3x3 basis = MLGetBasis(N);
    float3 T = basis[0];
    float3 B = basis[1];

    if (abs(dot(D, N)) < 0.999) {
        float3 R = reflect(-D, N);
        T = normalize(cross(N, R));
        B = cross(R, T);
    }

    return float2x3(T, B);
}
