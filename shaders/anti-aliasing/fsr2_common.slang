// =============================================================================
// fsr2_common.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2 (ffx_fsr2_common.h, ffx_fsr2_sample.h,
// ffx_fsr2_callbacks_hlsl.h). fp32 only; the fp16 twins are dropped.
//
// ATTRIBUTION & LICENSE (MIT) -- Copyright (c) 2022-2023 Advanced Micro Devices, Inc.
// All rights reserved. Full LICENSE + README at extern/fsr2/. MIT notice below
// MUST be preserved.
//
//   ------------------------------------------------------------------------
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the "Software"),
//   to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense,
//   and/or sell copies of the Software, and to permit persons to whom the
//   Software is furnished to do so, subject to the following conditions:
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
//   ------------------------------------------------------------------------
// =============================================================================

// DISCLAIMER: This port was done by AI.

module fsr2_common;

import common_interop;
import constants_interop;
import shader_functions;
import bindless_declarations;

public static const float FSR2_FP16_MAX = 65504.0;
public static const float FSR2_EPSILON = 1e-3;
public static const float FSR2_TONEMAP_EPSILON = 1.0 / 65504.0;
public static const float FSR2_FLT_MAX = 3.402823466e+38;
public static const float FSR2_RECONSTRUCTED_DEPTH_WEIGHT_THRESHOLD = 0.01;
public static const float FSR2_UPSAMPLE_LANCZOS_WEIGHT_SCALE = 1.0 / 12.0;
public static const float FSR2_MAX_ACCUMULATION_LANCZOS_WEIGHT = 1.0;
public static const float FSR2_AVERAGE_LANCZOS_WEIGHT_PER_FRAME = 0.74 / 12.0;
public static const float FSR2_RESET_AUTO_EXPOSURE_AVERAGE_SMOOTHING = 1e8;
public static const uint FSR2_INVALID_INDEX = 0xFFFFFFFF;
public static const int LOCK_LIFETIME_REMAINING = 0;
public static const int LOCK_TEMPORAL_LUMA = 1;
// Shading-change luma is mip 4 of a render/2 chain, i.e. 32x32 render pixels per texel.
public static const int FSR2_LUMA_MIP_DIV = 32;

public float3 Fsr2YCoCgToRGB(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

public float3 Fsr2RGBToYCoCg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

public float Fsr2RGBToLuma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

public float Fsr2RGBToPerceivedLuma(float3 c)
{
    float luminance = Fsr2RGBToLuma(c);
    float perceived;
    if (luminance <= 216.0 / 24389.0) {
        perceived = luminance * (24389.0 / 27.0);
    } else {
        perceived = pow(luminance, 1.0 / 3.0) * 116.0 - 16.0;
    }
    return perceived * 0.01;
}

public float3 Fsr2Tonemap(float3 c)
{
    return c / (max(max(0.0, c.r), max(c.g, c.b)) + 1.0);
}

public float3 Fsr2InverseTonemap(float3 c)
{
    return c / max(FSR2_TONEMAP_EPSILON, 1.0 - max(c.r, max(c.g, c.b)));
}

public int2 Fsr2ClampLoad(int2 pos, int2 offset, int2 size)
{
    int2 result = pos + offset;
    result.x = (offset.x < 0) ? max(result.x, 0) : result.x;
    result.x = (offset.x > 0) ? min(result.x, size.x - 1) : result.x;
    result.y = (offset.y < 0) ? max(result.y, 0) : result.y;
    result.y = (offset.y > 0) ? min(result.y, size.y - 1) : result.y;
    return result;
}

public int2 Fsr2ClampCoord(int2 pos, int2 size)
{
    return clamp(pos, int2(0, 0), size - int2(1, 1));
}

public float2 Fsr2ClampUv(float2 uv, int2 textureSize, int2 resourceSize)
{
    float2 sampleLocation = uv * float2(textureSize);
    float2 clampedLocation = max(float2(0.5, 0.5), min(sampleLocation, float2(textureSize) - float2(0.5, 0.5)));
    return clampedLocation / float2(resourceSize);
}

public bool Fsr2IsOnScreen(int2 pos, int2 size)
{
    return all(uint2(pos) < uint2(size));
}

public bool Fsr2IsUvInside(float2 uv)
{
    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

public float Fsr2ComputeAutoExposureFromLavg(float Lavg)
{
    Lavg = exp(Lavg);
    const float S = 100.0;
    const float K = 12.5;
    float exposureISO100 = log2((Lavg * S) / K);
    const float q = 0.65;
    float Lmax = (78.0 / (q * S)) * pow(2.0, exposureISO100);
    return 1.0 / Lmax;
}

public int2 Fsr2ComputeHrPosFromLrPos(int2 lrPos, float2 jitter, float2 renderSize, float2 displaySize)
{
    float2 srcJitteredPos = float2(lrPos) + 0.5 - jitter;
    float2 lrPosInHr = (srcJitteredPos / renderSize) * displaySize;
    return int2(floor(lrPosInHr));
}

// depthToView = (0, near) for reverse-Z infinite: viewZ = near / d. Sky (d = 0) is clamped to a huge finite distance instead of inf.
public float Fsr2GetViewSpaceDepth(float deviceDepth, float2 depthToView)
{
    return depthToView.y / max(deviceDepth - depthToView.x, 1e-9);
}

public float3 Fsr2GetViewSpacePosition(int2 viewportPos, int2 viewportSize, float deviceDepth, float2 depthToView, float2 tanHalfFov)
{
    float z = Fsr2GetViewSpaceDepth(deviceDepth, depthToView);
    float2 ndc = float2(viewportPos) / float2(viewportSize) * float2(2.0, -2.0) + float2(-1.0, 1.0);
    return float3(tanHalfFov.x * ndc.x * z, tanHalfFov.y * ndc.y * z, z);
}

public float3 Fsr2PrepareRgb(float3 rgb, float exposure, float preExposure)
{
    rgb /= preExposure;
    rgb *= exposure;
    return clamp(rgb, 0.0, FSR2_FP16_MAX);
}

public float3 Fsr2UnprepareRgb(float3 rgb, float exposure, float preExposure)
{
    rgb /= exposure;
    rgb *= preExposure;
    return rgb;
}

public struct Fsr2BilinearSamplingData
{
    public int2 offsets[4];
    public float weights[4];
    public int2 basePos;
}

public Fsr2BilinearSamplingData Fsr2GetBilinearSamplingData(float2 uv, int2 size)
{
    Fsr2BilinearSamplingData data;
    float2 pxSample = (uv * float2(size)) - float2(0.5, 0.5);
    data.basePos = int2(floor(pxSample));
    float2 pxFrac = frac(pxSample);

    data.offsets[0] = int2(0, 0);
    data.offsets[1] = int2(1, 0);
    data.offsets[2] = int2(0, 1);
    data.offsets[3] = int2(1, 1);

    data.weights[0] = (1.0 - pxFrac.x) * (1.0 - pxFrac.y);
    data.weights[1] = pxFrac.x * (1.0 - pxFrac.y);
    data.weights[2] = (1.0 - pxFrac.x) * pxFrac.y;
    data.weights[3] = pxFrac.x * pxFrac.y;
    return data;
}

public float Fsr2Lanczos2(float x)
{
    x = min(abs(x), 2.0);
    if (x < FSR2_EPSILON) {
        return 1.0;
    }
    const float PI = 3.141592653589793;
    return (sin(PI * x) / (PI * x)) * (sin(0.5 * PI * x) / (0.5 * PI * x));
}

// FSR1 Lanczos approximation. Input is x*x, clamped to 4.
public float Fsr2Lanczos2ApproxSq(float x2)
{
    x2 = min(x2, 4.0);
    float a = (2.0 / 5.0) * x2 - 1.0;
    float b = (1.0 / 4.0) * x2 - 1.0;
    return ((25.0 / 16.0) * a * a - (25.0 / 16.0 - 1.0)) * (b * b);
}

public float4 Fsr2Lanczos2Row(float4 c0, float4 c1, float4 c2, float4 c3, float t)
{
    float w0 = Fsr2Lanczos2(-1.0 - t);
    float w1 = Fsr2Lanczos2(-0.0 - t);
    float w2 = Fsr2Lanczos2(1.0 - t);
    float w3 = Fsr2Lanczos2(2.0 - t);
    return (w0 * c0 + w1 * c1 + w2 * c2 + w3 * c3) / (w0 + w1 + w2 + w3);
}

public struct Fsr2RectificationBox
{
    public float3 boxCenter;
    public float3 boxVec;
    public float3 aabbMin;
    public float3 aabbMax;
    public float boxCenterWeight;
}

public Fsr2RectificationBox Fsr2RectificationBoxReset()
{
    Fsr2RectificationBox box;
    box.boxCenterWeight = 0.0;
    box.boxCenter = float3(0.0);
    box.boxVec = float3(0.0);
    box.aabbMin = float3(FSR2_FLT_MAX);
    box.aabbMax = float3(-FSR2_FLT_MAX);
    return box;
}

public void Fsr2RectificationBoxAddSample(bool bInitialSample, inout Fsr2RectificationBox box, float3 colorSample, float sampleWeight)
{
    float3 weightedSample = colorSample * sampleWeight;
    if (bInitialSample) {
        box.aabbMin = colorSample;
        box.aabbMax = colorSample;
        box.boxCenter = weightedSample;
        box.boxVec = colorSample * weightedSample;
        box.boxCenterWeight = sampleWeight;
    } else {
        box.aabbMin = min(box.aabbMin, colorSample);
        box.aabbMax = max(box.aabbMax, colorSample);
        box.boxCenter += weightedSample;
        box.boxVec += colorSample * weightedSample;
        box.boxCenterWeight += sampleWeight;
    }
}

public void Fsr2RectificationBoxComputeVarianceBoxData(inout Fsr2RectificationBox box)
{
    box.boxCenterWeight = (abs(box.boxCenterWeight) > FSR2_EPSILON) ? box.boxCenterWeight : 1.0;
    box.boxCenter /= box.boxCenterWeight;
    box.boxVec /= box.boxCenterWeight;
    box.boxVec = sqrt(abs(box.boxVec - box.boxCenter * box.boxCenter));
}

public float Fsr2MinDividedByMax(float v0, float v1)
{
    float m = max(v0, v1);
    return m != 0.0 ? min(v0, v1) / m : 0.0;
}

// Exposure from the auto-exposure texture (exposure, log Lavg); without one the input is already exposed by preExposure.
public float Fsr2Exposure(uint exposureIndex, float preExposure)
{
    if (exposureIndex == FSR2_INVALID_INDEX) {
        return preExposure;
    }
    float exposure = rdgTexturesFloat2[exposureIndex].Load(int3(0, 0, 0)).x;
    return exposure == 0.0 ? preExposure : exposure;
}

public float Fsr2LoadInputDepth(uint depthIndex, int2 pos)
{
    return rdgTexturesFloat[depthIndex].Load(int3(pos, 0)).r;
}

public float3 Fsr2LoadInputColor(uint colorIndex, int2 pos)
{
    return rdgTexturesFloat4[colorIndex].Load(int3(pos, 0)).rgb;
}

// Gbuffer NDC delta current->previous as a UV delta (motionVectorScale 0.5); sky gets the camera-only vector.
public float2 Fsr2LoadInputMotionVector(SceneData* scene, uint gbufferOneIndex, uint depthIndex, int2 pos, float2 renderSize)
{
    float2 ndcDelta;
    if (Fsr2LoadInputDepth(depthIndex, pos) > 0.0) {
        ndcDelta = UnpackMotionVectors(rdgTexturesUInt4[gbufferOneIndex].Load(int3(pos, 0)).g);
    } else {
        ndcDelta = SkyMotionVectorNDC(scene->clipToPrevClip, scene->jitter, scene->prevJitter, (float2(pos) + 0.5) / renderSize);
    }
    return ndcDelta * 0.5;
}
