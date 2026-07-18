/*
 * RELAX denoiser utility functions.
 * This software contains source code provided by NVIDIA Corporation.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

module relax_utils;

import bindless_declarations;
import constants_interop;
import relax_interop;
import shader_functions;

// =====================================================================
// Constants
// =====================================================================

public static const float NRD_EPS = 1e-6;
public static const float NRD_INF = 1e6;
public static const float RELAX_NORMAL_ULP = 1.5 / 255.0;
public static const float RELAX_MAX_ACCUM_FRAME_NUM = 255.0;
public static const float RELAX_ANTILAG_ACCELERATION_AMOUNT_SCALE = 10.0;
public static const float NRD_CATROM_SHARPNESS = 0.5;
public static const float NRD_ROUGHNESS_SENSITIVITY = 0.01;
public static const float NRD_EXP_WEIGHT_DEFAULT_SCALE = 3.0;
public static const float NRD_CURVATURE_HIGH_PARALLAX_DISOCCLUSION_THRESHOLD = 0.04;
public static const float NRD_MAX_ALLOWED_VIRTUAL_MOTION_ACCELERATION = 5.0;

// =====================================================================
// Math
// =====================================================================

public float MLSmoothStep01(float x) { x = saturate(x); return x * x * (3.0 - x * 2.0); }
public float MLSmoothStep(float a, float b, float x) { x = saturate((x - a) / (b - a)); return x * x * (3.0 - x * 2.0); }
public float MLLinearStep(float a, float b, float x) { return saturate((x - a) / (b - a)); }
public float MLLengthSquared(float2 v) { return dot(v, v); }
public float MLLengthSquared(float3 v) { return dot(v, v); }
public float MLRsqrt(float x) { return 1.0 / sqrt(max(x, 1e-15)); }
public float MLSign(float x) { return step(0.0, x) * 2.0 - 1.0; }
public float MLPi(float x) { return radians(x * 180.0); }

public float MLAcosApproxPositive(float x)
{
    return lerp(1.567589, 1.399331, saturate(x)) * sqrt(saturate(1.0 - x));
}

// =====================================================================
// Geometry
// =====================================================================

public float3 MLAffineTransform(float4x4 m, float3 p) { return mul(m, float4(p, 1.0)).xyz; }
public float3 MLRotateVector(float4x4 m, float3 v) { return mul((float3x3)m, v); }
public float3 MLRotateVector(float3x3 m, float3 v) { return mul(m, v); }

/** @brief Rotate 2D vector by a packed (cos, sin, -sin, cos) rotator. */
public float2 MLRotateVector(float4 rotator, float2 v) { return v.x * rotator.xz + v.y * rotator.yw; }

public float4 MLGetRotator(float angle) { float ca = cos(angle); float sa = sin(angle); return float4(ca, sa, -sa, ca); }
public float4 MLCombineRotators(float4 r1, float4 r2) { return float4(r1.xyxy) * r2.xxzz + float4(r1.zwzw) * r2.yyww; }

public float2 MLGetScreenUv(float4x4 worldToClip, float3 X)
{
    float4 clip = mul(worldToClip, float4(X, 1.0));
    return (float2(clip.xy) / clip.w) * float2(0.5, 0.5) + 0.5;
}

public float3x3 MLGetBasis(float3 N)
{
    float sz = MLSign(N.z);
    float a = 1.0 / (sz + N.z);
    float ya = N.y * a;
    float b = N.x * ya;
    float c = N.x * sz;
    float3 T = float3(c * N.x * a - 1.0, sz * b, c);
    float3 B = float3(b, N.y * ya - sz, N.y);
    return float3x3(T, B, N);
}

// =====================================================================
// Color
// =====================================================================

public float MLLuminance(float3 x) { return dot(x, float3(0.2126, 0.7152, 0.0722)); }

public float3 MLRgbToYCoCg(float3 x)
{
    return float3(dot(x, float3(0.25, 0.5, 0.25)), dot(x, float3(0.5, 0.0, -0.5)), dot(x, float3(-0.25, 0.5, -0.25)));
}

public float3 MLYCoCgToRgb(float3 x)
{
    float t = x.x - x.z;
    return float3(t + x.y, x.x + x.z, t - x.y);
}

// =====================================================================
// Filtering
// =====================================================================

public struct MLBilinear
{
    public float2 origin;
    public float2 weights;
}

public MLBilinear MLGetBilinearFilter(float2 uv, float2 texSize)
{
    float2 t = uv * texSize - 0.5;
    MLBilinear result;
    result.origin = floor(t);
    result.weights = saturate(t - result.origin);
    return result;
}

public float4 MLGetBilinearCustomWeights(MLBilinear f, float4 customWeights)
{
    float2 oneMinusWeights = saturate(1.0 - f.weights);
    float4 w = customWeights;
    w.x *= oneMinusWeights.x * oneMinusWeights.y;
    w.y *= f.weights.x * oneMinusWeights.y;
    w.z *= oneMinusWeights.x * f.weights.y;
    w.w *= f.weights.x * f.weights.y;
    return w;
}

public float MLGetModifiedRoughnessFromNormalVariance(float roughness, float3 nonNormalizedAverageNormal)
{
    float l = length(nonNormalizedAverageNormal);
    float kappa = saturate(1.0 - l * l) / max(l * (3.0 - l * l), NRD_EPS);
    return sqrt(saturate(roughness * roughness + kappa));
}

/** Bicubic (Catmull-Rom, no corners) with fallback to custom bilinear. Samples rdgTexturesFloat4. */
public void BicubicFilterDiffSpec(
    float2 samplePos, float2 invResourceSize,
    float4 bilinearCustomWeights, bool useBicubic,
    uint specIdx, out float4 spec,
    uint diffIdx, out float4 diff)
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
    float w4;
    if (useBicubic) {
        w = float4(w12.x * w0.y, w0.x * w12.y, w12.x * w12.y, w3.x * w12.y);
        w4 = w12.x * w3.y;
    } else {
        w = bilinearCustomWeights;
        w4 = 0.0;
    }
    float sum = dot(w, 1.0) + w4;

    float4 uv01 = centerPos.xyxy + (useBicubic ? float4(tc.x, -1.0, -1.0, tc.y) : float4(0, 0, 1, 0));
    float4 uv23 = centerPos.xyxy + (useBicubic ? float4(tc.x, tc.y, 2.0, tc.y) : float4(0, 1, 1, 1));
    float2 uv4 = centerPos + (useBicubic ? float2(tc.x, 2.0) : f);
    uv01 *= invResourceSize.xyxy;
    uv23 *= invResourceSize.xyxy;
    uv4 *= invResourceSize;

    SamplerState lin = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];

    spec  = rdgTexturesFloat4[specIdx].SampleLevel(lin, uv01.xy, 0) * w.x;
    spec += rdgTexturesFloat4[specIdx].SampleLevel(lin, uv01.zw, 0) * w.y;
    spec += rdgTexturesFloat4[specIdx].SampleLevel(lin, uv23.xy, 0) * w.z;
    spec += rdgTexturesFloat4[specIdx].SampleLevel(lin, uv23.zw, 0) * w.w;
    spec += rdgTexturesFloat4[specIdx].SampleLevel(lin, uv4, 0) * w4;

    diff  = rdgTexturesFloat4[diffIdx].SampleLevel(lin, uv01.xy, 0) * w.x;
    diff += rdgTexturesFloat4[diffIdx].SampleLevel(lin, uv01.zw, 0) * w.y;
    diff += rdgTexturesFloat4[diffIdx].SampleLevel(lin, uv23.xy, 0) * w.z;
    diff += rdgTexturesFloat4[diffIdx].SampleLevel(lin, uv23.zw, 0) * w.w;
    diff += rdgTexturesFloat4[diffIdx].SampleLevel(lin, uv4, 0) * w4;

    if (sum < 0.0001) { spec = 0; diff = 0; }
    else { spec /= sum; diff /= sum; }
}

/** Single-texture variant. */
public void BicubicFilterSingle(
    float2 samplePos, float2 invResourceSize,
    float4 bilinearCustomWeights, bool useBicubic,
    uint texIdx, out float4 c)
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
    float w4;
    if (useBicubic) {
        w = float4(w12.x * w0.y, w0.x * w12.y, w12.x * w12.y, w3.x * w12.y);
        w4 = w12.x * w3.y;
    } else {
        w = bilinearCustomWeights;
        w4 = 0.0;
    }
    float sum = dot(w, 1.0) + w4;

    float4 uv01 = centerPos.xyxy + (useBicubic ? float4(tc.x, -1.0, -1.0, tc.y) : float4(0, 0, 1, 0));
    float4 uv23 = centerPos.xyxy + (useBicubic ? float4(tc.x, tc.y, 2.0, tc.y) : float4(0, 1, 1, 1));
    float2 uv4 = centerPos + (useBicubic ? float2(tc.x, 2.0) : f);
    uv01 *= invResourceSize.xyxy;
    uv23 *= invResourceSize.xyxy;
    uv4 *= invResourceSize;

    SamplerState lin = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];
    c  = rdgTexturesFloat4[texIdx].SampleLevel(lin, uv01.xy, 0) * w.x;
    c += rdgTexturesFloat4[texIdx].SampleLevel(lin, uv01.zw, 0) * w.y;
    c += rdgTexturesFloat4[texIdx].SampleLevel(lin, uv23.xy, 0) * w.z;
    c += rdgTexturesFloat4[texIdx].SampleLevel(lin, uv23.zw, 0) * w.w;
    c += rdgTexturesFloat4[texIdx].SampleLevel(lin, uv4, 0) * w4;
    c = sum < 0.0001 ? 0 : c / sum;
}

// =====================================================================
// Sequence / RNG
// =====================================================================

public uint MLSequenceHash(uint x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}

public uint MLHashCombine(uint seed, uint value) { return seed ^ (MLSequenceHash(value) + 0x9E3779B9u + (seed << 6) + (seed >> 2)); }

public uint MLBayer4x4ui(uint2 samplePos, uint frameIndex)
{
    uint2 p = samplePos & 3;
    uint b = ((p.y & 1) << 2) | ((p.x & 1) << 3) | ((p.y & 2) >> 1) | (p.x & 2);
    uint sampleOffset = reversebits(frameIndex) >> 28;
    return (b + sampleOffset) & 0xF;
}

public float MLBayer4x4(uint2 samplePos, uint frameIndex)
{
    return (float(MLBayer4x4ui(samplePos, frameIndex)) + 0.5) / 16.0;
}

public uint MLCheckerBoard(uint2 samplePos, uint frameIndex)
{
    return ((samplePos.x ^ samplePos.y) ^ frameIndex) & 0x1;
}

public float2 RngHashGetFloat2(uint2 pixelPos, uint frameIndex)
{
    uint s = MLHashCombine(MLSequenceHash(pixelPos.x + pixelPos.y * 65536u), frameIndex);
    uint s2 = MLSequenceHash(s);
    return float2(float(s >> 8) / float(1u << 24), float(s2 >> 8) / float(1u << 24));
}

// =====================================================================
// ImportanceSampling / BRDF
// =====================================================================

public float MLGetSpecularLobeTanHalfAngle(float roughness, float percentOfVolume = 0.75)
{
    percentOfVolume = saturate(percentOfVolume);
    return saturate(roughness) * sqrt(percentOfVolume / (1.0 - percentOfVolume + NRD_EPS));
}

public float4 MLGetSpecularDominantDirection(float3 N, float3 V, float linearRoughness)
{
    float NoV = abs(dot(N, V));
    float a = 0.298475 * log(39.4115 - 39.0029 * saturate(linearRoughness));
    float dominantFactor = saturate(pow(saturate(1.0 - NoV), 10.8649) * (1.0 - a) + a);
    float3 R = reflect(-V, N);
    float3 D = normalize(lerp(N, R, dominantFactor));
    return float4(D, dominantFactor);
}

public float BRDFPow5(float x) { return pow(saturate(1.0 - x), 5.0); }

// =====================================================================
// NRD front-end helpers
// =====================================================================

public float3 NRDSafeNormalize(float3 v) { return v * rsqrt(dot(v, v) + 1e-9); }

/** Decode normal+roughness from GBuffer Target One (uint4). Normal is view-space oct16 in .r, roughness in .b. Rotated to world-space via viewToWorld. */
public float4 RelaxUnpackNormalAndRoughness(uint4 gt1, float3x3 viewToWorld)
{
    float4 r;
    float roughness, metallic;
    r.xyz = normalize(mul(viewToWorld, UnpackNormalOct16(gt1.r)));
    UnpackPBR(gt1.b, roughness, metallic);
    r.w = roughness;
    return r;
}

public float4 RelaxUnpackPrevNormalRoughness(float4 p)
{
    float4 r;
    r.rgb = NRDSafeNormalize(p.rgb * 2.0 - 1.0);
    r.a = p.a;
    return r;
}

public float4 RelaxPackPrevNormalRoughness(float4 nr)
{
    float4 r;
    r.rgb = nr.xyz * 0.5 + 0.5;
    r.a = nr.a;
    return r;
}

// =====================================================================
// Common NRD helpers (from Common.hlsli / RELAX_Common.hlsli)
// =====================================================================

public bool RelaxIsInDenoisingRange(float z, float denoisingRange) { return z < denoisingRange; }

public float RelaxPixelRadiusToWorld(float unproject, float orthoMode, float pixelRadius, float viewZ)
{
    return pixelRadius * unproject * lerp(viewZ, 1.0, abs(orthoMode));
}

public float3 RelaxGetCurrentWorldPosFromPixelPos(int2 pixelPos, float viewZ, float2 rectSizeInv, float4 frustumForward, float4 frustumRight, float4 frustumUp, float orthoMode)
{
    float2 clipXY = ((float2)pixelPos + 0.5) * rectSizeInv * 2.0 - 1.0;
    if (orthoMode == 0)
        return viewZ * (frustumForward.xyz + frustumRight.xyz * clipXY.x + frustumUp.xyz * clipXY.y);
    else
        return viewZ * frustumForward.xyz + frustumRight.xyz * clipXY.x + frustumUp.xyz * clipXY.y;
}

public float3 RelaxGetCurrentWorldPosFromClipXY(float2 clipXY, float viewZ, float4 frustumForward, float4 frustumRight, float4 frustumUp, float orthoMode)
{
    if (orthoMode == 0)
        return viewZ * (frustumForward.xyz + frustumRight.xyz * clipXY.x + frustumUp.xyz * clipXY.y);
    else
        return viewZ * frustumForward.xyz + frustumRight.xyz * clipXY.x + frustumUp.xyz * clipXY.y;
}

public float3 RelaxGetPreviousWorldPosFromPixelPos(int2 pixelPos, float viewZ, float2 rectSizePrev, float4 prevFrustumForward, float4 prevFrustumRight, float4 prevFrustumUp, float orthoMode)
{
    float2 clipXY = ((float2)pixelPos + 0.5) * (1.0 / rectSizePrev) * 2.0 - 1.0;
    if (orthoMode == 0)
        return viewZ * (prevFrustumForward.xyz + prevFrustumRight.xyz * clipXY.x + prevFrustumUp.xyz * clipXY.y);
    else
        return viewZ * prevFrustumForward.xyz + prevFrustumRight.xyz * clipXY.x + prevFrustumUp.xyz * clipXY.y;
}

public float RelaxGetPlaneDistanceWeight(float3 centerWorldPos, float3 centerNormal, float centerViewZ, float3 sampleWorldPos, float threshold)
{
    float d = abs(dot(sampleWorldPos - centerWorldPos, centerNormal));
    return d / centerViewZ > threshold ? 0.0 : 1.0;
}

public float RelaxGetPlaneDistanceWeightAtrous(float3 centerWorldPos, float3 centerNormal, float3 sampleWorldPos, float threshold)
{
    return abs(dot(sampleWorldPos - centerWorldPos, centerNormal)) < threshold ? 1.0 : 0.0;
}

public float RelaxGetSpecLobeTanHalfAngle(float roughness, float percentOfVolume = 0.75)
{
    roughness = saturate(roughness);
    percentOfVolume = saturate(percentOfVolume);
    return roughness * roughness * percentOfVolume / (1.0 - percentOfVolume + NRD_EPS);
}

public float2 RelaxGetNormalWeightParamsATrous(float roughness, float numFramesInHistory, float specularReprojectionConfidence, float normalEdgeStoppingRelaxation, float specularLobeAngleFraction, float specularLobeAngleSlack)
{
    float relaxation = saturate(numFramesInHistory / 5.0);
    relaxation *= lerp(1.0, specularReprojectionConfidence, normalEdgeStoppingRelaxation);
    float f = 0.9 + 0.1 * relaxation;
    float angle = atan(RelaxGetSpecLobeTanHalfAngle(roughness, specularLobeAngleFraction));
    angle *= 10.0 - 9.0 * relaxation;
    angle += specularLobeAngleSlack;
    angle = min(MLPi(0.5), angle);
    return float2(angle, f);
}

public float RelaxGetSpecularNormalWeightATrous(float2 params0, float3 n0, float3 n, float3 v0, float3 v)
{
    float cosaN = dot(n0, n);
    float cosaV = dot(v0, v);
    float cosa = min(cosaN, cosaV);
    float a = MLAcosApproxPositive(cosa);
    a = MLSmoothStep(0.0, params0.x, a);
    return saturate(1.0 - a * params0.y);
}

public float RelaxGetNormalWeightParam2(float roughness, float angleFraction)
{
    float angle = atan(RelaxGetSpecLobeTanHalfAngle(roughness, angleFraction));
    angle = 1.0 / max(angle, RELAX_NORMAL_ULP);
    return angle;
}

public float RelaxBilinearWeightedFloat(float s00, float s10, float s01, float s11, float4 w)
{
    float output = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;
    float sumW = dot(w, 1.0);
    return sumW < 0.0001 ? 0 : output / sumW;
}

public float4 RelaxBilinearWeightedFloat4(float4 s00, float4 s10, float4 s01, float4 s11, float4 w)
{
    float4 output = s00 * w.x + s10 * w.y + s01 * w.z + s11 * w.w;
    float sumW = dot(w, 1.0);
    return sumW < 0.0001 ? 0 : output / sumW;
}

public float RelaxGetBilateralWeight(float z, float zc)
{
    return MLLinearStep(0.03, 0.0, abs(z - zc) / max(z, zc));
}

public float4 RelaxIsInScreenBilinear(float2 footprintOrigin, float2 rectSize)
{
    float4 p = footprintOrigin.xyxy + float4(0, 0, 1, 1);
    float4 r = float4(p >= 0.0);
    r *= float4(p < rectSize.xyxy);
    return r.xzxz * r.yyww;
}

public float RelaxIsInScreenNearest(float2 uv) { return float(all(uv > 0.0) && all(uv < 1.0)); }

public float2 RelaxMirrorUv(float2 uv)
{
    float2 mirrorUv = 1.0 - abs(1.0 - frac(uv * 0.5) * 2.0);
    return min(mirrorUv, 0.99999);
}

public float RelaxComputeParallaxInPixels(float3 X, float2 uvForZeroParallax, float4x4 mWorldToClip, float2 rectSize)
{
    float2 uv = MLGetScreenUv(mWorldToClip, X);
    return length((uv - uvForZeroParallax) * rectSize);
}

public float RelaxGetSpecMagicCurve(float roughness, float power = 0.25)
{
    float f = 1.0 - exp2(-200.0 * roughness * roughness);
    return f * pow(saturate(roughness), power);
}

public float2 RelaxGetRoughnessWeightParams(float roughness, float fraction)
{
    float a = 1.0 / lerp(NRD_ROUGHNESS_SENSITIVITY, 1.0, saturate(roughness * fraction));
    return float2(a, -roughness * a);
}

public float2 RelaxGetRelaxedRoughnessWeightParams(float m, float fraction = 1.0)
{
    float a = 1.0 / lerp(NRD_ROUGHNESS_SENSITIVITY, 1.0, lerp(m * m, m, saturate(fraction)));
    return float2(a, -m * a);
}

public float RelaxComputeWeight(float x, float px, float py)
{
    return MLSmoothStep(1.0, 0.0, abs(x * px + py));
}

/** NRD ExpApprox: cheap exp() approximation for non-positive arguments. */
public float MLExpApprox(float x)
{
    return 1.0 / (x * x - x + 1.0);
}

/** Exponential weight variant of RelaxComputeWeight, for noisy data (NRD ComputeExponentialWeight). */
public float RelaxComputeExponentialWeight(float x, float px, float py)
{
    return MLExpApprox(-NRD_EXP_WEIGHT_DEFAULT_SCALE * abs(x * px + py));
}

public float RelaxGetHitDistFactor(float hitDist, float frustumSize)
{
    return saturate(hitDist / frustumSize);
}

public float2 RelaxGetHitDistanceWeightParams(float hitDist, float nonLinearAccumSpeed)
{
    float a = 1.0 / nonLinearAccumSpeed;
    float b = hitDist * a;
    return float2(a, -b);
}

/** @param r kernel sample radius normalized to 1 */
public float RelaxGetGaussianWeight(float r)
{
    return exp(-0.66 * r * r);
}

public float2 RelaxGetGeometryWeightParams(float planeDistSensitivity, float frustumSize, float3 Xv, float3 Nv)
{
    float norm = planeDistSensitivity * frustumSize;
    float a = 1.0 / norm;
    float b = dot(Nv, Xv) * a;
    return float2(a, -b);
}

public float RelaxApplyGeometryWeightLast(float w, float z, float NoX, float2 geomWeightParams, float denoisingRange)
{
    w *= RelaxComputeWeight(NoX, geomWeightParams.x, geomWeightParams.y);
    return !RelaxIsInDenoisingRange(z, denoisingRange) ? 0.0 : w;
}

public float RelaxGetEncodingAwareNormalWeight(float3 Ncurr, float3 Nprev, float maxAngle, float curvatureAngle, float thresholdAngle)
{
    float cosa = dot(Ncurr, Nprev);
    float angle = MLAcosApproxPositive(cosa);
    float w = MLSmoothStep01(1.0 - (angle - curvatureAngle - thresholdAngle) / maxAngle);
    return MLSmoothStep(0.05, 0.95, w);
}

public float3 RelaxGetXvirtual(float hitDist, float curvature, float3 X, float3 Xprev, float3 N, float3 V, float roughness)
{
    float4 D = MLGetSpecularDominantDirection(N, V, roughness);
    float3 reflectionRay = D.xyz * hitDist;
    float3x3 reflectorBasis = MLGetBasis(N);
    float3 O = MLRotateVector(reflectorBasis, reflectionRay);
    O.z = -O.z;
    float mag = 1.0 / (2.0 * curvature * O.z - 1.0);
    float NoV = abs(dot(N, V));
    float f = length(X);
    f *= saturate(1.0 - NoV);
    f *= max(curvature, 0.0);
    f = 1.0 / (1.0 + f);
    mag *= f;
    float3 I = O * mag;
    D.w *= length(I);
    float closenessToSurface = saturate(D.w / (hitDist + NRD_EPS));
    float3 x = lerp(Xprev, X, closenessToSurface);
    return x + V * D.w * MLSign(mag);
}

public float RelaxLoadViewZ(uint texIdx, int2 pixel, float depthLinearizeMult, float depthLinearizeAdd)
{
    float raw = rdgTexturesFloat[texIdx].Load(int3(pixel, 0)).r;
    return ScreenToViewSpaceDepth(raw, depthLinearizeMult, depthLinearizeAdd);
}

// Packed geometry guide for the RELAX chain: one 8-byte texel per pixel replaces the per-tap gbuffer uint4 + raw depth loads and their decode (view-to-world mul, PBR unpack, depth linearize).
// x = oct12 world normal | unorm8 roughness (>= stock NRD's 8-10 bit normal-roughness encodings), y = f32 viewZ bits.
public uint2 RelaxPackGuide(float3 worldNormal, float roughness, float viewZ)
{
    float3 p = worldNormal / (abs(worldNormal.x) + abs(worldNormal.y) + abs(worldNormal.z));
    float2 e = p.z >= 0.0 ? p.xy : (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    uint2 q = uint2((e * 0.5 + 0.5) * 4095.0 + 0.5);
    uint r8 = uint(saturate(roughness) * 255.0 + 0.5);
    return uint2(q.x | (q.y << 12) | (r8 << 24), asuint(viewZ));
}

public float4 RelaxUnpackGuideNormalRoughness(uint packed)
{
    float2 e = float2(packed & 0xFFFu, (packed >> 12) & 0xFFFu) / 4095.0 * 2.0 - 1.0;
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return float4(normalize(n), float(packed >> 24) / 255.0);
}

public void RelaxLoadGuide(uint guideIndex, int2 pixel, out float3 normal, out float roughness, out float viewZ)
{
    uint2 g = rdgTexturesUInt2[guideIndex].Load(int3(pixel, 0)).xy;
    float4 nr = RelaxUnpackGuideNormalRoughness(g.x);
    normal = nr.xyz;
    roughness = nr.w;
    viewZ = asfloat(g.y);
}

public float RelaxLoadGuideViewZ(uint guideIndex, int2 pixel)
{
    return asfloat(rdgTexturesUInt2[guideIndex].Load(int3(pixel, 0)).y);
}

// f16x4 packing for groupshared arrays holding RGBA16F-resident, spatially-consumed values.
public uint2 RelaxPackF16x4(float4 v)
{
    return uint2(f32tof16(v.x) | (f32tof16(v.y) << 16), f32tof16(v.z) | (f32tof16(v.w) << 16));
}

public float4 RelaxUnpackF16x4(uint2 p)
{
    return float4(f16tof32(p.x & 0xFFFFu), f16tof32(p.x >> 16), f16tof32(p.y & 0xFFFFu), f16tof32(p.y >> 16));
}

/** Gather from a texture that already stores linearized viewZ (no conversion needed). */
public float4 RelaxGatherLinearViewZ(uint texIdx, float2 uv)
{
    return rdgTexturesFloat[texIdx].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv);
}
