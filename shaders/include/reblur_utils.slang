/*
 * ReBLUR denoiser utility functions (hit-distance normalization, packing, blur kernel).
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

module reblur_utils;

import relax_utils;

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

// ReblurGetLuma(float4) lives in reblur_common (canonical); do not duplicate here (ambiguous import).

/** @brief Non-linear accumulation speed from a linear accumulated-frame count: f = 1 / (1 + N). */
public float ReblurGetNonLinearAccumSpeed(float framesNum) { return 1.0 / (1.0 + framesNum); }

/** @brief NRD blur radius: maxBlurRadius * sqrt01(areaFactor) * radiusScale, clamped to minBlurRadius.
 *  areaFactor (= hitDistFactor[*roughness] * nonLinearAccumSpeed) drives AREA not radius, hence the sqrt;
 *  this keeps a meaningful radius at convergence instead of collapsing linearly to ~minBlurRadius. */
public float ReblurGetBlurRadius(float maxBlurRadius, float minBlurRadius, float areaFactor, float radiusScale)
{
    float r = maxBlurRadius * sqrt(saturate(areaFactor)) * radiusScale;
    return max(r, minBlurRadius);
}

/** @brief Lobe tolerance tightens as history accumulates. 1.5/255 = normal-encoding error floor (oct16). */
public float ReblurGetNormalWeightParam(float nonLinearAccumSpeed, float lobeAngleFraction, float roughness)
{
    float percentOfVolume = 0.75 * lerp(lobeAngleFraction, 1.0, nonLinearAccumSpeed);
    float tanHalfAngle = MLGetSpecularLobeTanHalfAngle(roughness, percentOfVolume);
    float angle = max(atan(tanHalfAngle), 1.5 / 255.0);
    return 1.0 / angle;
}

/** @brief Exponential weight sharpens toward 0.0005 as history converges (clamped by the spec magic
 *  curve so smooth specular stays strict). Normalized domain. */
public float2 ReblurGetHitDistanceWeightParams(float hitDist, float nonLinearAccumSpeed, float roughness)
{
    float smc = RelaxGetSpecMagicCurve(roughness);
    float norm = lerp(0.0005, 1.0, min(nonLinearAccumSpeed, smc));
    float a = 1.0 / norm;
    return float2(a, -hitDist * a);
}
