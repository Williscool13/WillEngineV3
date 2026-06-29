/*
 * ReBLUR denoiser utility functions (hit-distance normalization, packing, blur kernel).
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

module reblur_utils;

import relax_utils;

// Poisson disk 8 samples (NRD g_Poisson8); z is the sample radius normalized to 1, used for the Gaussian weight.
public static const float3 REBLUR_POISSON8[8] = {
    float3(-0.4706069, -0.4427112, 0.6461146),
    float3(-0.9057375,  0.3003471, 0.9542373),
    float3(-0.3487388,  0.4037880, 0.5335386),
    float3( 0.1023042,  0.6439373, 0.6520134),
    float3( 0.5699277,  0.3513750, 0.6695386),
    float3( 0.2939128, -0.1131226, 0.3149309),
    float3( 0.7836658, -0.4208784, 0.8895339),
    float3( 0.1564120, -0.8198990, 0.8346850),
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
