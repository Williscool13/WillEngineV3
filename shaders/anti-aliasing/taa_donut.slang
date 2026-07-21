// =============================================================================
// donut_taa.slang
//
// TAA/TAAU resolve, ported to WillEngineV3/Slang from NVIDIA Donut
// (shaders/passes/taa_cs.hlsl, include/donut/shaders/taa_cb.h). At resolution
// ratio 1 the input and output views coincide and this is native-res TAA.
//
// ATTRIBUTION & LICENSE (MIT) -- Copyright (c) 2014-2021, NVIDIA CORPORATION.
// Full LICENSE/README + upstream sources vendored at extern/donut/. MIT notice
// below MUST be preserved.
//
//   ------------------------------------------------------------------------
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the "Software"),
//   to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense,
//   and/or sell copies of the Software, and to permit persons to whom the
//   Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
//   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
//   ------------------------------------------------------------------------
// =============================================================================

// DISCLAIMER: This port was done by AI.
//      In addition to the copyright attribution above, the shader file and license for Donut TAA is available in extern/donut

import common_interop;
import constants_interop;
import flags_interop;
import shader_functions;
import push_constant_interop;
import bindless_declarations;

[[vk::push_constant]] DonutTaaPushConstant pc;

static const int GROUP_X = 16;
static const int GROUP_Y = 16;
static const int BUFFER_X = GROUP_X + 3;
static const int BUFFER_Y = GROUP_Y + 3;
static const int RENAMED_GROUP_Y = (GROUP_X * GROUP_Y) / BUFFER_X;

groupshared float4 s_ColorsAndLengths[BUFFER_Y][BUFFER_X]; // .rgb = PQ color, .a = dot(pixelMV, pixelMV)
groupshared float2 s_MotionVectors[BUFFER_Y][BUFFER_X];
groupshared uint s_Hero[BUFFER_Y][BUFFER_X]; // hero flag from the same gbuffer load, for the silhouette rim test

static const float pq_m1 = 0.1593017578125;
static const float pq_m2 = 78.84375;
static const float pq_c1 = 0.8359375;
static const float pq_c2 = 18.8515625;
static const float pq_c3 = 18.6875;

float3 PQDecode(float3 image)
{
    float3 Np = pow(max(image, 0.0), 1.0 / pq_m2);
    float3 L = Np - pq_c1;
    L = L / (pq_c2 - pq_c3 * Np);
    L = pow(max(L, 0.0), 1.0 / pq_m1);
    return L * pc.pqC;
}

// pqC too low -> saturate() clips highlights into history and TAA darkens speculars.
float3 PQEncode(float3 image)
{
    float3 L = image * pc.invPqC;
    float3 Lm = pow(max(L, 0.0), pq_m1);
    float3 N = (pq_c1 + pq_c2 * Lm) / (1.0 + pq_c3 * Lm);
    return saturate(pow(N, pq_m2));
}

float3 BicubicSampleCatmullRom(uint texIndex, float2 samplePos, float2 invTextureSize)
{
    float2 tc = floor(samplePos - 0.5) + 0.5;
    float2 f = saturate(samplePos - tc);
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w3 = 0.5 * (f3 - f2);
    float2 w2 = 1.0 - w0 - w1 - w3;

    float2 w12 = w1 + w2;

    float2 tc0 = (tc - 1.0) * invTextureSize;
    float2 tc12 = (tc + w2 / w12) * invTextureSize;
    float2 tc3 = (tc + 2.0) * invTextureSize;

    Texture2D<float4> tex = rdgTexturesFloat4[texIndex];
    SamplerState samp = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];

    float3 result =
        tex.SampleLevel(samp, float2(tc0.x,  tc0.y),  0).rgb * (w0.x  * w0.y)  +
        tex.SampleLevel(samp, float2(tc0.x,  tc12.y), 0).rgb * (w0.x  * w12.y) +
        tex.SampleLevel(samp, float2(tc0.x,  tc3.y),  0).rgb * (w0.x  * w3.y)  +
        tex.SampleLevel(samp, float2(tc12.x, tc0.y),  0).rgb * (w12.x * w0.y)  +
        tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rgb * (w12.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc3.y),  0).rgb * (w12.x * w3.y)  +
        tex.SampleLevel(samp, float2(tc3.x,  tc0.y),  0).rgb * (w3.x  * w0.y)  +
        tex.SampleLevel(samp, float2(tc3.x,  tc12.y), 0).rgb * (w3.x  * w12.y) +
        tex.SampleLevel(samp, float2(tc3.x,  tc3.y),  0).rgb * (w3.x  * w3.y);

    return max(0.0, result);
}

// clamp compensates for Donut relying on OOB Load()==0.
void Preload(int2 sharedID, int2 globalID, int2 inputMax)
{
    int2 loadCoord = clamp(globalID, int2(0, 0), inputMax);

    float3 color = PQEncode(rdgTexturesFloat4[pc.colorInputIndex].Load(int3(loadCoord, 0)).rgb);

    uint4 gbufferOne = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(loadCoord, 0));
    float2 velocityNDC = UnpackMotionVectors(gbufferOne.g);
    float2 motion = velocityNDC * 0.5 * pc.inputViewSize; // NDC delta -> input-pixel delta
    float motionLength = dot(motion, motion);

    s_ColorsAndLengths[sharedID.y][sharedID.x] = float4(color, motionLength);
    s_MotionVectors[sharedID.y][sharedID.x] = motion;
    s_Hero[sharedID.y][sharedID.x] = gbufferOne.w & GBUFFER1_HERO_BIT;
}

float2 OutputToInput(int2 pixelPosRelativeToOrigin)
{
    float2 inputPixelOffset = -pc.sceneData.jitter * 0.5 * pc.inputViewSize;
    return (float2(pixelPosRelativeToOrigin) + 0.5) * pc.inputOverOutputViewSize
        - 0.5 + pc.inputViewOrigin + inputPixelOffset;
}

[shader("compute")]
[numthreads(GROUP_X, GROUP_Y, 1)]
void ComputeDonutTaa(
    uint3 i_groupIdx  : SV_GroupID,
    uint3 i_threadIdx : SV_GroupThreadID,
    uint3 i_globalIdx : SV_DispatchThreadID)
{
    int2 inputMax = int2(pc.inputViewSize) - 1;

    // rename 16x16 group -> 19-wide apron layout
    int2 newID;
    float linearID = float(i_threadIdx.y) * float(GROUP_X) + float(i_threadIdx.x);
    linearID = (linearID + 0.5) / float(BUFFER_X);
    newID.y = int(floor(linearID));
    newID.x = int(floor(frac(linearID) * float(BUFFER_X)));

    int2 groupOutputBase = int2(i_groupIdx.xy) * int2(GROUP_X, GROUP_Y);
    int2 groupBase = int2(OutputToInput(groupOutputBase) - 1.0);

    if (newID.y < RENAMED_GROUP_Y) {
        Preload(newID, groupBase + newID, inputMax);
    }

    newID.y += RENAMED_GROUP_Y;

    if (newID.y < BUFFER_Y) {
        Preload(newID, groupBase + newID, inputMax);
    }

    GroupMemoryBarrierWithGroupSync();

    int2 outputPixelLocal = int2(i_globalIdx.xy);
    if (any(outputPixelLocal >= int2(pc.outputViewSize))) {
        return;
    }

    int2   outputPixelPos = outputPixelLocal + int2(pc.outputViewOrigin);
    float2 inputPos       = OutputToInput(outputPixelLocal);
    int2   inputPosInt    = int2(round(inputPos));
    int2   inputPosShared = inputPosInt - groupBase - 1;

    // 3x3 moments + MV dilation
    float3 colorMoment1 = float3(0.0);
    float3 colorMoment2 = float3(0.0);
    float  longestMVLength = -1.0;
    int2   longestMVPos = int2(0, 0);
    float3 thisPixelColor = float3(0.0);

    [unroll]
    for (int dy = 0; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = 0; dx <= 2; dx++)
        {
            int2 pos = inputPosShared + int2(dx, dy);

            float4 colorAndLength = s_ColorsAndLengths[pos.y][pos.x];
            float3 color = colorAndLength.rgb;
            float motionLength = colorAndLength.a;

            if (dx == 1 && dy == 1) {
                thisPixelColor = color;
            }

            colorMoment1 += color;
            colorMoment2 += color * color;

            if (motionLength > longestMVLength)
            {
                longestMVPos = pos;
                longestMVLength = motionLength;
            }
        }
    }

    float clampingFactor = pc.clampingFactor;
    float newFrameWeight = pc.newFrameWeight;
    if (pc.useHistoryClampRelax != 0)
    {
        float2 centerUV = (float2(outputPixelPos) + 0.5) * pc.outputTextureSizeInv;
        float2 offset = pc.outputTextureSizeInv * 0.4;
        Texture2D<float> mask = rdgTexturesFloat[pc.historyClampRelaxIndex];
        SamplerState samp = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];

        float historyClampRelax = saturate(
              (mask.SampleLevel(samp, centerUV + offset * float2(-1, -1), 0)
             + mask.SampleLevel(samp, centerUV + offset * float2( 1, -1), 0)
             + mask.SampleLevel(samp, centerUV + offset * float2(-1,  1), 0)
             + mask.SampleLevel(samp, centerUV + offset * float2( 1,  1), 0)) * 0.4);

        clampingFactor *= (1.0 + historyClampRelax * 1.5);
        newFrameWeight /= (1.0 + historyClampRelax * 0.5);
    }

    float2 longestMV = s_MotionVectors[longestMVPos.y][longestMVPos.x];

    colorMoment1 /= 9.0;
    colorMoment2 /= 9.0;
    float3 colorVariance = colorMoment2 - colorMoment1 * colorMoment1;
    float3 colorSigma = sqrt(max(0.0, colorVariance)) * clampingFactor;
    float3 colorMin = colorMoment1 - colorSigma;
    float3 colorMax = colorMoment1 + colorSigma;

    longestMV *= pc.outputOverInputViewSize; // input pixels -> output pixels
    float2 sourcePos = float2(outputPixelPos) + longestMV + 0.5;

    float3 resultPQ;
    if (newFrameWeight < 1.0 &&
        all(sourcePos > pc.outputViewOrigin) &&
        all(sourcePos < pc.outputViewOrigin + pc.outputViewSize))
    {
        float3 history;
        if (pc.useCatmullRom != 0) {
            history = BicubicSampleCatmullRom(pc.feedbackInputIndex, sourcePos, pc.outputTextureSizeInv);
        } else {
            history = rdgTexturesFloat4[pc.feedbackInputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], sourcePos * pc.outputTextureSizeInv, 0).rgb;
        }

        float3 historyClamped = history;
        if (clampingFactor >= 0.0) {
            historyClamped = min(colorMax, max(colorMin, history));
        }

        float motionWeight = smoothstep(0.0, 1.0, length(longestMV));
        float2 distanceToLowResPixel = inputPos - float2(inputPosInt);
        float upscalingFactor = pc.outputOverInputViewSize.x;
        float sampleWeight = saturate(1.0 - upscalingFactor * dot(distanceToLowResPixel, distanceToLowResPixel));
        float blendWeight = saturate(max(motionWeight, sampleWeight) * newFrameWeight);

        // Snap to current on the hero shadow's moving contour; render-res mask sampled at the resolved input pixel.
        if (pc.heroReactiveIndex != ~0x0u) {
            float heroReactive = rdgTexturesFloat[pc.heroReactiveIndex].Load(int3(clamp(inputPosInt, int2(0, 0), inputMax), 0)).r;
            blendWeight = lerp(blendWeight, 1.0, heroReactive);
        }

        resultPQ = lerp(historyClamped, thisPixelColor, blendWeight);
    }
    else
    {
        resultPQ = thisPixelColor;
    }

    if (pc.heroRimStrength > 0.0) {
        // The full 3x3 s_Hero is already in the apron this thread read for its moments; no extra gbuffer taps.
        int2 sc = inputPosShared + int2(1, 1);
        uint heroCount = 0;
        [unroll]
        for (int ry = -1; ry <= 1; ry++)
        {
            [unroll]
            for (int rx = -1; rx <= 1; rx++)
            {
                heroCount += (s_Hero[sc.y + ry][sc.x + rx] != 0u) ? 1u : 0u;
            }
        }
        // Coverage feathers the rim across the 3x3 (peaks at the 50/50 boundary, 0 in pure hero/background), so the treated
        // band is a smooth ramp instead of a render-res staircase of blurred-vs-sharp mask edges.
        float frac = float(heroCount) / 9.0;
        float coverage = 1.0 - abs(2.0 * frac - 1.0);

        // Uniform mode gates the whole silhouette on the hero's own (camera-excluded) motion; else per-pixel on the local dilated MV.
        float rimMotion = (pc.heroRimUniformGate >= 0.0) ? pc.heroRimUniformGate : smoothstep(0.5, 2.0, length(longestMV));
        float rimWeight = pc.heroRimStrength * rimMotion * coverage;

        if (rimWeight > 0.0) {
            // Isotropic 3x3 mean: an axis-quantized 2-tap can't follow a curved (mostly diagonal) silhouette and reinforces the staircase.
            resultPQ = lerp(resultPQ, colorMoment1, rimWeight);
        }

        // Debug: R = applied weight, G = rim coverage. Coloured in debug_visualize (HeroRim); only bound when that view is active.
        if (pc.heroRimDebugIndex != ~0x0u) {
            rdgStorageFloat4[pc.heroRimDebugIndex][outputPixelPos] = float4(rimWeight, coverage, 0.0, 1.0);
        }
    }

    float3 result = PQDecode(resultPQ);

    rdgStorageFloat4[pc.colorOutputIndex][outputPixelPos]    = float4(result, 1.0);
    rdgStorageFloat4[pc.feedbackOutputIndex][outputPixelPos] = float4(resultPQ, 0.0);
}
