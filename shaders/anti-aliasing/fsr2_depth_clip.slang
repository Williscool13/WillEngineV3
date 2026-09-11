// =============================================================================
// fsr2_depth_clip.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2 (ffx_fsr2_depth_clip.h). No transparency and
// composition mask; the reactive mask is optional.
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
import flags_interop;
import shader_functions;
import push_constant_interop;
import bindless_declarations;
import fsr2_common;

[[vk::push_constant]] Fsr2DepthClipPushConstant pc;

float LoadReconstructedPrevDepth(int2 pos)
{
    return asfloat(rdgTexturesUInt[pc.reconstructedDepthIndex].Load(int3(Fsr2ClampCoord(pos, int2(pc.c.renderSize)), 0)).r);
}

float LoadDilatedDepth(int2 pos)
{
    return rdgTexturesFloat[pc.dilatedDepthIndex].Load(int3(Fsr2ClampCoord(pos, int2(pc.c.renderSize)), 0)).r;
}

float2 LoadDilatedMotionVector(int2 pos)
{
    return rdgTexturesFloat2[pc.dilatedMotionIndex].Load(int3(pos, 0)).xy;
}

float2 SamplePreviousDilatedMotionVector(float2 uv)
{
    return rdgTexturesFloat2[pc.previousDilatedMotionIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).xy;
}

float LoadReactiveMask(int2 pos)
{
    if (pc.reactiveMaskIndex == FSR2_INVALID_INDEX) {
        return 0.0;
    }
    return rdgTexturesFloat[pc.reactiveMaskIndex].Load(int3(pos, 0)).r;
}

float2 LoadInputMotionVector(int2 pos)
{
    return Fsr2LoadInputMotionVector(pc.sceneData, pc.gbufferOneIndex, pc.depthIndex, pos, pc.c.renderSize);
}

float ComputeDepthClip(float2 uvSample, float currentDepthSample)
{
    const int2 renderSize = int2(pc.c.renderSize);
    const float currentDepthViewSpace = Fsr2GetViewSpaceDepth(currentDepthSample, pc.c.depthToView);
    const Fsr2BilinearSamplingData bilinearInfo = Fsr2GetBilinearSamplingData(uvSample, renderSize);

    float depth = 0.0;
    float weightSum = 0.0;
    for (int i = 0; i < 4; i++) {
        const int2 samplePos = bilinearInfo.basePos + bilinearInfo.offsets[i];
        if (!Fsr2IsOnScreen(samplePos, renderSize)) {
            continue;
        }
        const float weight = bilinearInfo.weights[i];
        if (weight <= FSR2_RECONSTRUCTED_DEPTH_WEIGHT_THRESHOLD) {
            continue;
        }

        const float prevDepthSample = LoadReconstructedPrevDepth(samplePos);
        const float prevNearestDepthViewSpace = Fsr2GetViewSpaceDepth(prevDepthSample, pc.c.depthToView);
        const float depthDiff = currentDepthViewSpace - prevNearestDepthViewSpace;
        if (depthDiff <= 0.0) {
            continue;
        }

        const float planeDepth = min(prevDepthSample, currentDepthSample);
        const float3 center = Fsr2GetViewSpacePosition(int2(pc.c.renderSize * 0.5), renderSize, planeDepth, pc.c.depthToView, pc.c.tanHalfFov);
        const float3 corner = Fsr2GetViewSpacePosition(int2(0, 0), renderSize, planeDepth, pc.c.depthToView, pc.c.tanHalfFov);

        const float halfViewportWidth = length(pc.c.renderSize);
        const float depthThreshold = max(currentDepthViewSpace, prevNearestDepthViewSpace);

        const float Ksep = 1.37e-05;
        const float Kfov = length(corner) / length(center);
        const float requiredDepthSeparation = Ksep * Kfov * halfViewportWidth * depthThreshold;

        const float resolutionFactor = saturate(length(pc.c.renderSize) / length(float2(1920.0, 1080.0)));
        const float power = lerp(1.0, 3.0, resolutionFactor);
        depth += pow(saturate(requiredDepthSeparation / depthDiff), power) * weight;
        weightSum += weight;
    }

    return (weightSum > 0.0) ? saturate(1.0 - depth / weightSum) : 0.0;
}

float ComputeMotionDivergence(int2 pos, int2 motionVectorSize)
{
    float minConvergence = 1.0;

    const float2 motionVectorNucleus = LoadInputMotionVector(pos);
    const float nucleusVelocityLr = length(motionVectorNucleus * pc.c.renderSize);
    float maxVelocityUv = length(motionVectorNucleus);

    const float MOTION_VECTOR_VELOCITY_EPSILON = 1e-02;
    if (nucleusVelocityLr > MOTION_VECTOR_VELOCITY_EPSILON) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const int2 samplePos = Fsr2ClampLoad(pos, int2(x, y), motionVectorSize);
                const float2 motionVector = LoadInputMotionVector(samplePos);
                float velocityUv = length(motionVector);
                maxVelocityUv = max(velocityUv, maxVelocityUv);
                velocityUv = max(velocityUv, maxVelocityUv);
                minConvergence = min(minConvergence, dot(motionVector / velocityUv, motionVectorNucleus / velocityUv));
            }
        }
    }

    return saturate(1.0 - minConvergence) * saturate(maxVelocityUv / 0.01);
}

float ComputeDepthDivergence(int2 pos)
{
    const int2 renderSize = int2(pc.c.renderSize);
    const float maxDist = Fsr2GetViewSpaceDepth(0.0, pc.c.depthToView);
    float depthMax = 0.0;
    float depthMin = maxDist;
    bool bMaxDistFound = false;

    for (int y = -1; y < 2; y++) {
        for (int x = -1; x < 2; x++) {
            const int2 samplePos = pos + int2(x, y);
            const float onScreenFactor = Fsr2IsOnScreen(samplePos, renderSize) ? 1.0 : 0.0;
            const float depth = Fsr2GetViewSpaceDepth(LoadDilatedDepth(samplePos), pc.c.depthToView) * onScreenFactor;
            bMaxDistFound = bMaxDistFound || (maxDist == depth);
            depthMin = min(depthMin, depth);
            depthMax = max(depthMax, depth);
        }
    }

    return (1.0 - depthMin / depthMax) * (bMaxDistFound ? 0.0 : 1.0);
}

float ComputeTemporalMotionDivergence(int2 pos)
{
    const int2 renderSize = int2(pc.c.renderSize);
    const float2 uv = (float2(pos) + 0.5) / pc.c.renderSize;

    const float2 motionVector = LoadDilatedMotionVector(pos);
    float2 reprojectedUv = uv + motionVector;
    reprojectedUv = Fsr2ClampUv(reprojectedUv, renderSize, renderSize);
    const float2 prevMotionVector = SamplePreviousDilatedMotionVector(reprojectedUv);

    const float pxDistance = length(motionVector * pc.c.displaySize);
    return pxDistance > 1.0 ? lerp(0.0, 1.0 - saturate(length(prevMotionVector) / length(motionVector)), saturate(pow(pxDistance / 20.0, 3.0))) : 0.0;
}

// Mirror-like surfaces carry reflected content whose motion the surface MV does not describe; drop their history while the camera moves
float ComputeReflectionReactive(int2 pos)
{
    if (pc.reflectionReactive <= 0.0 || Fsr2LoadInputDepth(pc.depthIndex, pos) <= 0.0) {
        return 0.0;
    }
    const uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pos, 0));
    if ((gt1.w & GBUFFER1_LIT_BIT) == 0u) {
        return 0.0;
    }
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    const float mirror = 1.0 - smoothstep(pc.mirrorRoughnessMax, pc.tracedRoughnessMax, roughness);
    const float2 motionVector = UnpackMotionVectors(gt1.g) * 0.5;
    const float moving = saturate(length(motionVector * pc.c.displaySize) * 0.5);
    return mirror * moving * pc.reflectionReactive;
}

void PreProcessReactiveMasks(int2 pos, float motionDivergence)
{
    const int2 renderSize = int2(pc.c.renderSize);
    const float3 referenceColor = Fsr2LoadInputColor(pc.colorIndex, pos);
    float2 reactiveFactor = float2(ComputeReflectionReactive(pos), motionDivergence);

    float masksSum = 0.0;
    float3 colorSamples[9];
    float reactiveSamples[9];

    [unroll]
    for (int y = -1; y < 2; y++) {
        [unroll]
        for (int x = -1; x < 2; x++) {
            const int2 sampleCoord = Fsr2ClampLoad(pos, int2(x, y), renderSize);
            const int sampleIdx = (y + 1) * 3 + x + 1;
            const float reactiveSample = LoadReactiveMask(sampleCoord);
            colorSamples[sampleIdx] = Fsr2LoadInputColor(pc.colorIndex, sampleCoord);
            reactiveSamples[sampleIdx] = reactiveSample;
            masksSum += reactiveSample;
        }
    }

    if (masksSum > 0.0) {
        for (int sampleIdx = 0; sampleIdx < 9; sampleIdx++) {
            const float3 colorSample = colorSamples[sampleIdx];
            const float maxLenSq = max(dot(referenceColor, referenceColor), dot(colorSample, colorSample));
            const float similarity = dot(referenceColor, colorSample) / maxLenSq;

            const float POWER_BIAS_MAX = 6.0;
            const float similarityPower = 1.0 + (POWER_BIAS_MAX - similarity * POWER_BIAS_MAX);
            const float weightedReactiveSample = pow(reactiveSamples[sampleIdx], similarityPower);
            reactiveFactor = max(reactiveFactor, float2(weightedReactiveSample, 0.0));
        }
    }

    rdgStorageFloat2[pc.dilatedReactiveOutIndex][pos] = reactiveFactor;
}

float3 ComputePreparedInputColor(int2 pos)
{
    float3 rgb = max(float3(0.0), Fsr2LoadInputColor(pc.colorIndex, pos));
    rgb = Fsr2PrepareRgb(rgb, Fsr2Exposure(pc.exposureIndex, pc.c.preExposure), pc.c.preExposure);
    return Fsr2RGBToYCoCg(rgb);
}

float EvaluateSurface(int2 pos)
{
    const float d0 = Fsr2GetViewSpaceDepth(LoadReconstructedPrevDepth(pos + int2(0, -1)), pc.c.depthToView);
    const float d1 = Fsr2GetViewSpaceDepth(LoadReconstructedPrevDepth(pos + int2(0, 0)), pc.c.depthToView);
    const float d2 = Fsr2GetViewSpaceDepth(LoadReconstructedPrevDepth(pos + int2(0, 1)), pc.c.depthToView);
    return 1.0 - ((((d0 - d1) > (d1 * 0.01)) && ((d1 - d2) > (d2 * 0.01))) ? 1.0 : 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeFsr2DepthClip(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pos = int2(dispatchThreadId.xy);
    const int2 renderSize = int2(pc.c.renderSize);
    if (!Fsr2IsOnScreen(pos, renderSize)) {
        return;
    }

    const float2 depthUv = (float2(pos) + 0.5) / pc.c.renderSize;
    float2 motionVector = LoadDilatedMotionVector(pos);
    motionVector *= (length(motionVector * pc.c.displaySize) > 0.01) ? 1.0 : 0.0;

    const float2 dilatedUv = depthUv + motionVector;
    const float dilatedDepth = LoadDilatedDepth(pos);

    const float depthClip = ComputeDepthClip(dilatedUv, dilatedDepth) * EvaluateSurface(pos);
    const float3 preparedYCoCg = ComputePreparedInputColor(pos);
    rdgStorageFloat4[pc.preparedColorOutIndex][pos] = float4(preparedYCoCg, depthClip);

    const float motionDivergence = ComputeMotionDivergence(pos, renderSize);
    const float temporalMotionDifference = saturate(ComputeTemporalMotionDivergence(pos) - ComputeDepthDivergence(pos));

    PreProcessReactiveMasks(pos, max(temporalMotionDifference, motionDivergence));
}
