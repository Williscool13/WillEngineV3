// =============================================================================
// fsr2_accumulate.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2 (ffx_fsr2_accumulate.h, ffx_fsr2_upsample.h,
// ffx_fsr2_reproject.h, ffx_fsr2_postprocess_lock_status.h). Reproject uses the
// reference Lanczos2, upsample the FSR1 polynomial approximation.
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

import common_interop;
import constants_interop;
import shader_functions;
import push_constant_interop;
import bindless_declarations;
import fsr2_common;

[[vk::push_constant]] Fsr2AccumulatePushConstant pc;

struct AccumulationParams
{
    int2 pxHrPos;
    float2 hrUv;
    float2 lrUvHwSampler;
    float2 motionVector;
    float2 reprojectedHrUv;
    float hrVelocity;
    float depthClipFactor;
    float dilatedReactiveFactor;
    float accumulationMask;
    bool bIsResetFrame;
    bool bIsExistingSample;
    bool bIsNewSample;
};

struct LockState
{
    bool bNewLock;
    bool bWasLockedPrevFrame;
};

// ---------------------------------------------------------------- resource access

float4 LoadHistory(int2 pos)
{
    return rdgTexturesFloat4[pc.historyColorIndex].Load(int3(pos, 0));
}

float2 SampleLockStatus(float2 uv)
{
    return rdgTexturesFloat2[pc.lockStatusIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).xy;
}

float SampleDepthClip(float2 uv)
{
    return rdgTexturesFloat4[pc.preparedColorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).w;
}

float2 SampleDilatedReactiveMasks(float2 uv)
{
    return rdgTexturesFloat2[pc.dilatedReactiveIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).xy;
}

float2 LoadDilatedMotionVector(int2 pos)
{
    return rdgTexturesFloat2[pc.dilatedMotionIndex].Load(int3(pos, 0)).xy;
}

float3 LoadPreparedInputColor(int2 pos)
{
    return rdgTexturesFloat4[pc.preparedColorIndex].Load(int3(pos, 0)).xyz;
}

float4 SampleLumaHistory(float2 uv)
{
    return rdgTexturesFloat4[pc.lumaHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
}

float SampleMipLuma(float2 uv)
{
    return rdgTexturesFloat[pc.lumaMip4Index].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
}

float LoadRwNewLocks(int2 pos)
{
    return rdgStorageFloat[pc.newLocksIndex][pos];
}

// ---------------------------------------------------------------- reproject

// Reference Lanczos2 4x4 with deringing to the inner 2x2.
float4 HistorySample(float2 uv, int2 textureSize)
{
    float2 pxSample = (uv * float2(textureSize)) - float2(0.5, 0.5);
    pxSample.x = max(0.0, min(float(textureSize.x), pxSample.x));
    pxSample.y = max(0.0, min(float(textureSize.y), pxSample.y));
    const int2 iPxSample = int2(floor(pxSample));
    const float2 pxFrac = frac(pxSample);

    float4 samples[4][4];
    [unroll]
    for (int row = 0; row < 4; row++) {
        [unroll]
        for (int col = 0; col < 4; col++) {
            samples[row][col] = LoadHistory(Fsr2ClampCoord(iPxSample + int2(col - 1, row - 1), textureSize));
        }
    }

    const float4 colorX0 = Fsr2Lanczos2Row(samples[0][0], samples[0][1], samples[0][2], samples[0][3], pxFrac.x);
    const float4 colorX1 = Fsr2Lanczos2Row(samples[1][0], samples[1][1], samples[1][2], samples[1][3], pxFrac.x);
    const float4 colorX2 = Fsr2Lanczos2Row(samples[2][0], samples[2][1], samples[2][2], samples[2][3], pxFrac.x);
    const float4 colorX3 = Fsr2Lanczos2Row(samples[3][0], samples[3][1], samples[3][2], samples[3][3], pxFrac.x);
    float4 colorXY = Fsr2Lanczos2Row(colorX0, colorX1, colorX2, colorX3, pxFrac.y);

    const float4 deringingMin = min(min(samples[1][1], samples[1][2]), min(samples[2][1], samples[2][2]));
    const float4 deringingMax = max(max(samples[1][1], samples[1][2]), max(samples[2][1], samples[2][2]));
    return clamp(colorXY, deringingMin, deringingMax);
}

void ReprojectHistoryColor(AccumulationParams params, out float3 historyColor, out float temporalReactiveFactor, out bool bInMotionLastFrame)
{
    const float4 history = HistorySample(params.reprojectedHrUv, int2(pc.c.displaySize));

    historyColor = Fsr2PrepareRgb(history.rgb, Fsr2Exposure(pc.exposureIndex), pc.c.previousPreExposure);
    historyColor = Fsr2RGBToYCoCg(historyColor);

    temporalReactiveFactor = saturate(abs(history.w));
    bInMotionLastFrame = (history.w < 0.0);
}

LockState ReprojectHistoryLockStatus(AccumulationParams params, out float2 reprojectedLockStatus)
{
    LockState state;
    state.bNewLock = false;
    state.bWasLockedPrevFrame = false;

    const float newLockIntensity = LoadRwNewLocks(params.pxHrPos);
    state.bNewLock = newLockIntensity > (127.0 / 255.0);

    reprojectedLockStatus = SampleLockStatus(params.reprojectedHrUv);
    if (reprojectedLockStatus[LOCK_LIFETIME_REMAINING] != 0.0) {
        state.bWasLockedPrevFrame = true;
    }
    return state;
}

// ---------------------------------------------------------------- lock status

float GetShadingChangeLuma(float2 uv)
{
    uv = Fsr2ClampUv(uv, int2(pc.c.lumaMipClampSize), int2(pc.c.lumaMipSize));
    float shadingChangeLuma = Fsr2Exposure(pc.exposureIndex) * exp(SampleMipLuma(uv));
    return pow(shadingChangeLuma, 1.0 / 6.0);
}

void UpdateLockStatus(AccumulationParams params, inout float reactiveFactor, LockState state, inout float2 lockStatus, out float lockContributionThisFrame, out float luminanceDiff)
{
    const float shadingChangeLuma = GetShadingChangeLuma(params.hrUv);

    lockStatus[LOCK_TEMPORAL_LUMA] = (lockStatus[LOCK_TEMPORAL_LUMA] == 0.0) ? shadingChangeLuma : lockStatus[LOCK_TEMPORAL_LUMA];

    const float previousShadingChangeLuma = lockStatus[LOCK_TEMPORAL_LUMA];
    luminanceDiff = 1.0 - Fsr2MinDividedByMax(previousShadingChangeLuma, shadingChangeLuma);

    if (state.bNewLock) {
        lockStatus[LOCK_TEMPORAL_LUMA] = shadingChangeLuma;
        lockStatus[LOCK_LIFETIME_REMAINING] = (lockStatus[LOCK_LIFETIME_REMAINING] != 0.0) ? 2.0 : 1.0;
    } else if (lockStatus[LOCK_LIFETIME_REMAINING] <= 1.0) {
        lockStatus[LOCK_TEMPORAL_LUMA] = lerp(lockStatus[LOCK_TEMPORAL_LUMA], shadingChangeLuma, 0.5);
    } else if (luminanceDiff > 0.1) {
        lockStatus[LOCK_LIFETIME_REMAINING] = 0.0;
    }

    reactiveFactor = max(reactiveFactor, saturate((luminanceDiff - 0.1) * 10.0));
    lockStatus[LOCK_LIFETIME_REMAINING] *= (1.0 - reactiveFactor);
    lockStatus[LOCK_LIFETIME_REMAINING] *= saturate(1.0 - params.accumulationMask);
    lockStatus[LOCK_LIFETIME_REMAINING] *= (params.depthClipFactor < 0.1) ? 1.0 : 0.0;

    const float lifetimeContribution = saturate(lockStatus[LOCK_LIFETIME_REMAINING] - 1.0);
    const float shadingChangeContribution = saturate(Fsr2MinDividedByMax(lockStatus[LOCK_TEMPORAL_LUMA], shadingChangeLuma));
    lockContributionThisFrame = saturate(saturate(lifetimeContribution * 4.0) * shadingChangeContribution);
}

void FinalizeLockStatus(AccumulationParams params, float2 lockStatus, float upsampledWeight)
{
    const float2 estimatedUvNextFrame = params.hrUv - params.motionVector;
    if (!Fsr2IsUvInside(estimatedUvNextFrame)) {
        lockStatus[LOCK_LIFETIME_REMAINING] = 0.0;
    } else {
        const float lifetimeDecreaseLanczosMax = pc.c.jitterPhaseCount * FSR2_AVERAGE_LANCZOS_WEIGHT_PER_FRAME;
        const float lifetimeDecrease = upsampledWeight / lifetimeDecreaseLanczosMax;
        lockStatus[LOCK_LIFETIME_REMAINING] = max(0.0, lockStatus[LOCK_LIFETIME_REMAINING] - lifetimeDecrease);
    }
    rdgStorageFloat2[pc.lockStatusOutIndex][params.pxHrPos] = lockStatus;
}

// ---------------------------------------------------------------- upsample

float GetUpsampleLanczosWeight(float2 srcSampleOffset, float kernelWeight)
{
    const float2 srcSampleOffsetBiased = srcSampleOffset * kernelWeight;
    return Fsr2Lanczos2ApproxSq(dot(srcSampleOffsetBiased, srcSampleOffsetBiased));
}

float ComputeMaxKernelWeight()
{
    const float KERNEL_SIZE_BIAS = 1.0;
    const float kernelWeight = 1.0 + (1.0 / pc.c.downscaleFactor.x - 1.0) * KERNEL_SIZE_BIAS;
    return min(1.99, kernelWeight);
}

float4 ComputeUpsampledColorAndWeight(AccumulationParams params, inout Fsr2RectificationBox clippingBox, float reactiveFactor)
{
    const int2 renderSize = int2(pc.c.renderSize);
    const float2 dstOutputPos = float2(params.pxHrPos) + 0.5;
    const float2 srcOutputPos = dstOutputPos * pc.c.downscaleFactor;
    const int2 srcInputPos = int2(floor(srcOutputPos));

    float3 samples[9];

    const float2 srcUnjitteredPos = (float2(srcInputPos) + 0.5) - pc.c.jitter;

    int2 offsetTL;
    offsetTL.x = (srcUnjitteredPos.x > srcOutputPos.x) ? -2 : -1;
    offsetTL.y = (srcUnjitteredPos.y > srcOutputPos.y) ? -2 : -1;

    const bool bFlipRow = srcUnjitteredPos.y > srcOutputPos.y;
    const bool bFlipCol = srcUnjitteredPos.x > srcOutputPos.x;
    const float2 offsetTLf = float2(offsetTL);

    [unroll]
    for (int row = 0; row < 3; row++) {
        [unroll]
        for (int col = 0; col < 3; col++) {
            const int2 sampleColRow = int2(bFlipCol ? (3 - col) : col, bFlipRow ? (3 - row) : row);
            const int2 srcSamplePos = srcInputPos + offsetTL + sampleColRow;
            const int2 sampleCoord = Fsr2ClampLoad(srcSamplePos, int2(0, 0), renderSize);
            samples[row * 3 + col] = LoadPreparedInputColor(sampleCoord);
        }
    }

    float4 colorAndWeight = float4(0.0);
    const float2 baseSampleOffset = srcUnjitteredPos - srcOutputPos;

    const float kernelReactiveFactor = max(reactiveFactor, params.bIsNewSample ? 1.0 : 0.0);
    const float kernelBiasMax = ComputeMaxKernelWeight() * (1.0 - kernelReactiveFactor);
    const float kernelBiasMin = max(1.0, ((1.0 + kernelBiasMax) * 0.3));
    const float kernelBiasFactor = max(0.0, max(0.25 * params.depthClipFactor, kernelReactiveFactor));
    const float kernelBias = lerp(kernelBiasMax, kernelBiasMin, kernelBiasFactor);

    const float rectificationCurveBias = lerp(-2.0, -3.0, saturate(params.hrVelocity / 50.0));

    [unroll]
    for (int row = 0; row < 3; row++) {
        [unroll]
        for (int col = 0; col < 3; col++) {
            const int sampleIndex = row * 3 + col;
            const int2 sampleColRow = int2(bFlipCol ? (3 - col) : col, bFlipRow ? (3 - row) : row);
            const float2 offset = offsetTLf + float2(sampleColRow);
            const float2 srcSampleOffset = baseSampleOffset + offset;
            const int2 srcSamplePos = srcInputPos + offsetTL + sampleColRow;

            const float onScreenFactor = Fsr2IsOnScreen(srcSamplePos, renderSize) ? 1.0 : 0.0;
            const float sampleWeight = onScreenFactor * GetUpsampleLanczosWeight(srcSampleOffset, kernelBias);

            colorAndWeight += float4(samples[sampleIndex] * sampleWeight, sampleWeight);

            const float srcSampleOffsetSq = dot(srcSampleOffset, srcSampleOffset);
            const float boxSampleWeight = exp(rectificationCurveBias * srcSampleOffsetSq);
            const bool bInitialSample = (row == 0) && (col == 0);
            Fsr2RectificationBoxAddSample(bInitialSample, clippingBox, samples[sampleIndex], boxSampleWeight);
        }
    }

    Fsr2RectificationBoxComputeVarianceBoxData(clippingBox);

    colorAndWeight.w *= (colorAndWeight.w > FSR2_EPSILON) ? 1.0 : 0.0;

    if (colorAndWeight.w > FSR2_EPSILON) {
        colorAndWeight.xyz = colorAndWeight.xyz / colorAndWeight.w;
        colorAndWeight.w *= FSR2_UPSAMPLE_LANCZOS_WEIGHT_SCALE;
        colorAndWeight.xyz = clamp(colorAndWeight.xyz, clippingBox.aabbMin, clippingBox.aabbMax);
    }

    return colorAndWeight;
}

// ---------------------------------------------------------------- accumulate

float GetPxHrVelocity(float2 motionVector)
{
    return length(motionVector * pc.c.displaySize);
}

void AccumulateColor(inout float3 historyColor, float3 accumulation, float4 upsampledColorAndWeight)
{
    accumulation = max(float3(FSR2_EPSILON), accumulation + upsampledColorAndWeight.www);

    upsampledColorAndWeight.xyz = Fsr2RGBToYCoCg(Fsr2Tonemap(Fsr2YCoCgToRGB(upsampledColorAndWeight.xyz)));
    historyColor = Fsr2RGBToYCoCg(Fsr2Tonemap(Fsr2YCoCgToRGB(historyColor)));

    const float3 alpha = upsampledColorAndWeight.www / accumulation;
    historyColor = lerp(historyColor, upsampledColorAndWeight.xyz, alpha);

    historyColor = Fsr2YCoCgToRGB(historyColor);
    historyColor = Fsr2InverseTonemap(historyColor);
}

void RectifyHistory(AccumulationParams params, Fsr2RectificationBox clippingBox, inout float3 historyColor, inout float3 accumulation, float lockContributionThisFrame, float temporalReactiveFactor, float lumaInstabilityFactor)
{
    const float scaleFactorInfluence = min(20.0, pow(1.0 / abs(pc.c.downscaleFactor.x * pc.c.downscaleFactor.y), 3.0));

    const float velocityFactor = saturate(params.hrVelocity / 20.0);
    const float boxScaleT = max(params.depthClipFactor, max(params.accumulationMask, velocityFactor));
    const float boxScale = lerp(scaleFactorInfluence, 1.0, boxScaleT);

    const float3 scaledBoxVec = clippingBox.boxVec * boxScale;
    float3 boxMin = clippingBox.boxCenter - scaledBoxVec;
    float3 boxMax = clippingBox.boxCenter + scaledBoxVec;

    boxMin = max(clippingBox.aabbMin, boxMin);
    boxMax = min(clippingBox.aabbMax, boxMax);

    if (any(boxMin > historyColor) || any(historyColor > boxMax)) {
        const float3 clampedHistoryColor = clamp(historyColor, boxMin, boxMax);

        float3 historyContribution = float3(max(lumaInstabilityFactor, lockContributionThisFrame));

        const float reactiveFactor = params.dilatedReactiveFactor;
        const float reactiveContribution = 1.0 - pow(reactiveFactor, 1.0 / 2.0);
        historyContribution *= reactiveContribution;

        historyColor = lerp(clampedHistoryColor, historyColor, saturate(historyContribution));

        const float3 accumulationMin = min(accumulation, float3(0.1));
        accumulation = lerp(accumulationMin, accumulation, saturate(historyContribution));
    }
}

float3 ComputeBaseAccumulationWeight(AccumulationParams params, float thisFrameReactiveFactor, bool bInMotionLastFrame, float upsampledWeight)
{
    float baseAccumulation = FSR2_MAX_ACCUMULATION_LANCZOS_WEIGHT * (params.bIsExistingSample ? 1.0 : 0.0) * (1.0 - thisFrameReactiveFactor) * (1.0 - params.depthClipFactor);

    baseAccumulation = min(baseAccumulation, lerp(baseAccumulation, upsampledWeight * 10.0, max(bInMotionLastFrame ? 1.0 : 0.0, saturate(params.hrVelocity * 10.0))));
    baseAccumulation = min(baseAccumulation, lerp(baseAccumulation, upsampledWeight, saturate(params.hrVelocity / 20.0)));

    return float3(baseAccumulation);
}

float ComputeLumaInstabilityFactor(AccumulationParams params, Fsr2RectificationBox clippingBox, float thisFrameReactiveFactor, float luminanceDiff)
{
    const float UNORM_THRESHOLD = 1.0 / 255.0;
    const int N_MINUS_1 = 0;
    const int N_MINUS_2 = 1;
    const int N_MINUS_3 = 2;
    const int N_MINUS_4 = 3;

    float currentFrameLuma = clippingBox.boxCenter.x;
    currentFrameLuma = currentFrameLuma / (1.0 + max(0.0, currentFrameLuma));
    currentFrameLuma = round(currentFrameLuma * 255.0) / 255.0;

    const bool bSampleLumaHistory = (max(max(params.depthClipFactor, params.accumulationMask), luminanceDiff) < 0.1) && (params.bIsNewSample == false);
    float4 currentFrameLumaHistory = bSampleLumaHistory ? SampleLumaHistory(params.reprojectedHrUv) : float4(0.0);

    float lumaInstability = 0.0;
    const float diffs0 = (currentFrameLuma - currentFrameLumaHistory[N_MINUS_1]);

    float minDiff = abs(diffs0);

    if (minDiff >= UNORM_THRESHOLD) {
        for (int i = N_MINUS_2; i <= N_MINUS_4; i++) {
            const float diffs1 = (currentFrameLuma - currentFrameLumaHistory[i]);
            if (sign(diffs0) == sign(diffs1)) {
                const float MIN_BIAS = 1.0;
                minDiff = min(minDiff, abs(diffs1) * MIN_BIAS);
            }
        }

        const float boxSize = clippingBox.boxVec.x;
        const float boxSizeFactor = pow(saturate(boxSize / 0.1), 6.0);

        lumaInstability = ((minDiff != abs(diffs0)) ? 1.0 : 0.0) * boxSizeFactor;
        lumaInstability = (lumaInstability > UNORM_THRESHOLD) ? 1.0 : 0.0;

        lumaInstability *= 1.0 - max(params.accumulationMask, pow(thisFrameReactiveFactor, 1.0 / 6.0));
    }

    currentFrameLumaHistory[N_MINUS_4] = currentFrameLumaHistory[N_MINUS_3];
    currentFrameLumaHistory[N_MINUS_3] = currentFrameLumaHistory[N_MINUS_2];
    currentFrameLumaHistory[N_MINUS_2] = currentFrameLumaHistory[N_MINUS_1];
    currentFrameLumaHistory[N_MINUS_1] = currentFrameLuma;

    rdgStorageFloat4[pc.lumaHistoryOutIndex][params.pxHrPos] = currentFrameLumaHistory;

    return lumaInstability * ((currentFrameLumaHistory[N_MINUS_4] != 0.0) ? 1.0 : 0.0);
}

float ComputeTemporalReactiveFactor(AccumulationParams params, float temporalReactiveFactor)
{
    float newFactor = min(0.99, temporalReactiveFactor);

    newFactor = max(newFactor, lerp(newFactor, 0.4, saturate(params.hrVelocity)));
    newFactor = max(newFactor * newFactor, max(params.depthClipFactor * 0.1, params.dilatedReactiveFactor));

    newFactor = params.bIsNewSample ? 1.0 : newFactor;

    if (saturate(params.hrVelocity * 10.0) >= 1.0) {
        newFactor = max(FSR2_EPSILON, newFactor) * -1.0;
    }
    return newFactor;
}

AccumulationParams InitParams(int2 pxHrPos)
{
    AccumulationParams params;
    const int2 renderSize = int2(pc.c.renderSize);

    params.pxHrPos = pxHrPos;
    const float2 hrUv = (float2(pxHrPos) + 0.5) / pc.c.displaySize;
    params.hrUv = hrUv;

    const float2 lrUvJittered = hrUv + pc.c.jitter / pc.c.renderSize;
    params.lrUvHwSampler = Fsr2ClampUv(lrUvJittered, renderSize, renderSize);

    params.motionVector = LoadDilatedMotionVector(Fsr2ClampCoord(int2(hrUv * pc.c.renderSize), renderSize));
    params.hrVelocity = GetPxHrVelocity(params.motionVector);

    params.reprojectedHrUv = params.hrUv + params.motionVector;
    params.bIsExistingSample = Fsr2IsUvInside(params.reprojectedHrUv);

    params.depthClipFactor = saturate(SampleDepthClip(params.lrUvHwSampler));

    const float2 dilatedReactiveMasks = SampleDilatedReactiveMasks(params.lrUvHwSampler);
    params.dilatedReactiveFactor = dilatedReactiveMasks.x;
    params.accumulationMask = dilatedReactiveMasks.y;
    params.bIsResetFrame = (pc.c.frameIndex == 0);
    params.bIsNewSample = (params.bIsExistingSample == false || params.bIsResetFrame);

    return params;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeFsr2Accumulate(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pxHrPos = int2(dispatchThreadId.xy);
    if (!Fsr2IsOnScreen(pxHrPos, int2(pc.c.displaySize))) {
        return;
    }

    const AccumulationParams params = InitParams(pxHrPos);

    float3 historyColor = float3(0.0);
    float2 lockStatus = float2(0.0, 0.0);

    float temporalReactiveFactor = 0.0;
    bool bInMotionLastFrame = false;
    LockState lockState;
    lockState.bNewLock = false;
    lockState.bWasLockedPrevFrame = false;
    if (params.bIsExistingSample && !params.bIsResetFrame) {
        ReprojectHistoryColor(params, historyColor, temporalReactiveFactor, bInMotionLastFrame);
        lockState = ReprojectHistoryLockStatus(params, lockStatus);
    }

    float thisFrameReactiveFactor = max(params.dilatedReactiveFactor, temporalReactiveFactor);

    float luminanceDiff = 0.0;
    float lockContributionThisFrame = 0.0;
    UpdateLockStatus(params, thisFrameReactiveFactor, lockState, lockStatus, lockContributionThisFrame, luminanceDiff);

    Fsr2RectificationBox clippingBox = Fsr2RectificationBoxReset();
    const float4 upsampledColorAndWeight = ComputeUpsampledColorAndWeight(params, clippingBox, thisFrameReactiveFactor);

    const float lumaInstabilityFactor = ComputeLumaInstabilityFactor(params, clippingBox, thisFrameReactiveFactor, luminanceDiff);

    float3 accumulation = ComputeBaseAccumulationWeight(params, thisFrameReactiveFactor, bInMotionLastFrame, upsampledColorAndWeight.w);

    if (params.bIsNewSample) {
        historyColor = Fsr2YCoCgToRGB(upsampledColorAndWeight.xyz);
    } else {
        RectifyHistory(params, clippingBox, historyColor, accumulation, lockContributionThisFrame, thisFrameReactiveFactor, lumaInstabilityFactor);
        AccumulateColor(historyColor, accumulation, upsampledColorAndWeight);
    }

    historyColor = Fsr2UnprepareRgb(historyColor, Fsr2Exposure(pc.exposureIndex), pc.c.preExposure);

    FinalizeLockStatus(params, lockStatus, upsampledColorAndWeight.w);

    temporalReactiveFactor = ComputeTemporalReactiveFactor(params, thisFrameReactiveFactor);

    rdgStorageFloat4[pc.historyColorOutIndex][pxHrPos] = float4(historyColor, temporalReactiveFactor);

    if (pc.outputIndex != FSR2_INVALID_INDEX) {
        rdgStorageFloat4[pc.outputIndex][pxHrPos] = float4(historyColor, 1.0);
    }
    rdgStorageFloat[pc.newLocksIndex][pxHrPos] = 0.0;
}
