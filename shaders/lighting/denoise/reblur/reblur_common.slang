/*
 * ReBLUR shared helpers: faithful port of NRD REBLUR_Common.hlsli (DIFFUSE_SPECULAR, YCoCg variant).
 * Internal-data packing, accumulation-speed model, history mixing, anisotropic kernel basis, antilag,
 * hit-distance normalization, front/back-end signal packing, blur kernel.
 * Globals in NRD (g*) are passed as explicit parameters here so this stays a pure math module (like denoiser_common).
 * Config baked from REBLUR_Config.hlsli: ACCUMSPEED_BITS=6 (MAX_ACCUM=63), MATERIALID_BITS=4 (MAX=15),
 * USE_YCOCG=1, USE_CONFIDENCE_NON_LINEARLY=1, ANTILAG_MODE=2, SHOW=0.
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

module reblur_common;

import denoiser_common;

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

// =====================================================================
// Hit-distance normalization, signal packing, blur kernel (merged from reblur_utils).
// =====================================================================

// NRD g_Special8: structured cross + diagonal ring; z = sample radius normalized to 1 (Gaussian weight).
public static const float3 REBLUR_POISSON8[8] = {
    float3(-1.0, 0.0, 1.0),
    float3( 0.0, 1.0, 1.0),
    float3( 1.0, 0.0, 1.0),
    float3( 0.0, -1.0, 1.0),
    float3(-0.35355339, 0.35355339, 0.5),
    float3( 0.35355339, 0.35355339, 0.5),
    float3( 0.35355339, -0.35355339, 0.5),
    float3(-0.35355339, -0.35355339, 0.5),
};

/** @brief NRD hit-distance normalization curve: (A + |viewZ|*B) * lerp(1, C, exp2(D*roughness^2)). */
public float ReblurGetHitDistNormalization(float viewZ, float4 hitDistParams, float roughness)
{
    return (hitDistParams.x + abs(viewZ) * hitDistParams.y) * lerp(1.0, hitDistParams.z, saturate(exp2(hitDistParams.w * roughness * roughness)));
}

/** @brief World hit distance -> normalized [0,1] (REBLUR_FrontEnd_GetNormHitDist). */
public float ReblurGetNormHitDist(float hitDist, float viewZ, float4 hitDistParams, float roughness)
{
    float f = ReblurGetHitDistNormalization(viewZ, hitDistParams, roughness);
    return saturate(hitDist / max(f, NRD_EPS));
}

/** @brief Normalized [0,1] -> world hit distance. */
public float ReblurDenormHitDist(float normHitDist, float viewZ, float4 hitDistParams, float roughness)
{
    return normHitDist * ReblurGetHitDistNormalization(viewZ, hitDistParams, roughness);
}

/** @brief Pack radiance (linear RGB) + raw world hit distance into the ReBLUR working signal (YCoCg + hitDist). */
public float4 ReblurFrontEndPack(float3 radiance, float hitDist)
{
    radiance = clamp(radiance, 0.0, NRD_INF);
    return float4(MLRgbToYCoCg(radiance), max(hitDist, 0.0));
}

/** @brief Unpack the ReBLUR working signal back to linear RGB (+ hitDist in .w). */
public float4 ReblurBackEndUnpack(float4 data)
{
    return float4(max(MLYCoCgToRgb(data.xyz), 0.0), data.w);
}

/** @brief Non-linear accumulation speed from a linear accumulated-frame count: f = 1 / (1 + N). */
public float ReblurGetNonLinearAccumSpeed(float framesNum) { return 1.0 / (1.0 + framesNum); }

/** @brief Spec magic curve with power 0.5, as used by the NRD spatial filters (fixes roughness < 0.1). */
public float ReblurGetSpecMagicCurve05(float roughness) { return RelaxGetSpecMagicCurve(roughness, 0.5); }

/** @brief Lobe tolerance tightens as history accumulates. 1.5/255 = normal-encoding error floor (oct16). */
public float ReblurGetNormalWeightParam(float nonLinearAccumSpeed, float lobeAngleFraction, float roughness)
{
    float percentOfVolume = 0.75 * lerp(lobeAngleFraction, 1.0, nonLinearAccumSpeed);
    float tanHalfAngle = MLGetSpecularLobeTanHalfAngle(roughness, percentOfVolume);
    float angle = max(atan(tanHalfAngle), 1.5 / 255.0);
    return 1.0 / angle;
}

/** @brief Exponential weight sharpens as history converges. Normalized domain. */
public float2 ReblurGetHitDistanceWeightParams(float hitDist, float nonLinearAccumSpeed)
{
    float a = 1.0 / nonLinearAccumSpeed;
    return float2(a, -hitDist * a);
}
