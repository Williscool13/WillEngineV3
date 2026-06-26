// =============================================================================
// donut_taa.slang
//
// Native-res TAA resolve, ported to WillEngineV3/Slang from NVIDIA Donut
// (shaders/passes/taa_cs.hlsl, include/donut/shaders/taa_cb.h). Native-res
// specialization: Donut's upscaling view-remap is stripped, only the sub-pixel
// jitter offset survives. PQ-space accumulation, 3x3 variance clip, MV dilation,
// Catmull-Rom history resample.
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

import common_interop;        // SceneData (mainRenderTargetSize, texelSize, jitter)
import constants_interop;     // RDG_LINEAR_SAMPLER_INDEX
import shader_functions;      // UnpackMotionVectors
import push_constant_interop; // DonutTaaPushConstant (shared host/device interop)
import bindless_declarations; // rdgTexturesFloat4 / rdgTexturesUInt4 / rdgTexturesFloat / rdgStorageFloat4 / rdgSamplers

// DonutTaaPushConstant lives in the shared interop module (push_constant_interop), mirroring the engine's TemporalAntialiasingPushConstant; the C++ pass fills it.
[[vk::push_constant]] DonutTaaPushConstant pc;

// -----------------------------------------------------------------------------
// Tile / LDS configuration (identical to Donut: 16x16 group, +3 apron).
// -----------------------------------------------------------------------------
static const int GROUP_X = 16;
static const int GROUP_Y = 16;
static const int BUFFER_X = GROUP_X + 3;                 // 19
static const int BUFFER_Y = GROUP_Y + 3;                 // 19
static const int RENAMED_GROUP_Y = (GROUP_X * GROUP_Y) / BUFFER_X; // 13

groupshared float4 s_ColorsAndLengths[BUFFER_Y][BUFFER_X]; // .rgb = PQ color, .a = dot(pixelMV, pixelMV)
groupshared float2 s_MotionVectors[BUFFER_Y][BUFFER_X];    // pixel-space reprojection vector

// PQ (SMPTE-2084) encode/decode; constants bit-exact to Donut. History is PQ-space.
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
    return L * pc.pqC; // returns cd/m^2
}

// pqC=clamp(maxRadiance,1e-4,1e8): too low and saturate() clips highlights into history (TAA darkens speculars).
float3 PQEncode(float3 image)
{
    float3 L = image * pc.invPqC;
    float3 Lm = pow(max(L, 0.0), pq_m1);
    float3 N = (pq_c1 + pq_c2 * Lm) / (1.0 + pq_c3 * Lm);
    return saturate(pow(N, pq_m2));
}

// Catmull-Rom (bicubic, 9-tap) history resample. samplePos in texel coords.
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

// Preload one LDS texel: PQ color + pixel-space MV. Clamp-to-edge (Donut relied on OOB Load()==0).
void Preload(int2 sharedID, int2 globalID, int2 renderMax)
{
    int2 loadCoord = clamp(globalID, int2(0, 0), renderMax);

    float3 color = PQEncode(rdgTexturesFloat4[pc.colorInputIndex].Load(int3(loadCoord, 0)).rgb);

    uint packedMV = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(loadCoord, 0)).g; // MV in .g -- identical to temporal_antialiasing.slang
    float2 velocityNDC = UnpackMotionVectors(packedMV);
    // NDC delta -> pixel delta. *0.5 (NDC->UV, the engine's TAA convention) then *renderSize (UV->pixels).
    float2 motion = velocityNDC * 0.5 * pc.sceneData.mainRenderTargetSize;
    float motionLength = dot(motion, motion);

    s_ColorsAndLengths[sharedID.y][sharedID.x] = float4(color, motionLength);
    s_MotionVectors[sharedID.y][sharedID.x] = motion;
}

// Native-res OutputToInput: only the sub-pixel jitter offset survives.
float2 OutputToInput(int2 pixelPos, float2 inputPixelOffset)
{
    return float2(pixelPos) + inputPixelOffset;
}

[numthreads(GROUP_X, GROUP_Y, 1)]
void ComputeMain(
    uint3 i_groupIdx  : SV_GroupID,
    uint3 i_threadIdx : SV_GroupThreadID,
    uint3 i_globalIdx : SV_DispatchThreadID)
{
    float2 renderSize  = pc.sceneData.mainRenderTargetSize;
    int2   renderSizeI = int2(renderSize);
    int2   renderMax   = renderSizeI - 1;
    float2 texelSize   = pc.sceneData.texelSize; // == 1.0 / renderSize

    // jitter in pixels (sign-insensitive here)
    float2 inputPixelOffset = pc.sceneData.jitter * 0.5 * renderSize;

    // rename 16x16 group -> 19-wide apron layout
    int2 newID;
    float linearID = float(i_threadIdx.y) * float(GROUP_X) + float(i_threadIdx.x);
    linearID = (linearID + 0.5) / float(BUFFER_X);
    newID.y = int(floor(linearID));
    newID.x = int(floor(frac(linearID) * float(BUFFER_X)));

    int2 groupOutputBase = int2(i_groupIdx.xy) * int2(GROUP_X, GROUP_Y);
    int2 groupBase = int2(OutputToInput(groupOutputBase, inputPixelOffset) - 1.0);

    // preload colors + MVs into LDS (two passes)
    if (newID.y < RENAMED_GROUP_Y)
        Preload(newID, groupBase + newID, renderMax);

    newID.y += RENAMED_GROUP_Y;

    if (newID.y < BUFFER_Y)
        Preload(newID, groupBase + newID, renderMax);

    GroupMemoryBarrierWithGroupSync();

    int2 outputPixelPos = int2(i_globalIdx.xy);
    if (any(outputPixelPos >= renderSizeI))
        return; // tail overrun

    float2 inputPos       = OutputToInput(outputPixelPos, inputPixelOffset);
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

            if (dx == 1 && dy == 1)
                thisPixelColor = color;

            colorMoment1 += color;
            colorMoment2 += color * color;

            if (motionLength > longestMVLength)
            {
                longestMVPos = pos;
                longestMVLength = motionLength;
            }
        }
    }

    // optional history-clamp-relax mask
    float clampingFactor = pc.clampingFactor;
    float newFrameWeight = pc.newFrameWeight;
    if (pc.useHistoryClampRelax != 0)
    {
        float2 centerUV = (float2(outputPixelPos) + 0.5) * texelSize;
        float2 offset = texelSize * 0.4; // hand-tuned filter width (Donut)
        Texture2D<float> mask = rdgTexturesFloat[pc.historyClampRelaxIndex];
        SamplerState samp = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];

        float historyClampRelax = saturate(
              (mask.SampleLevel(samp, centerUV + offset * float2(-1, -1), 0)
             + mask.SampleLevel(samp, centerUV + offset * float2( 1, -1), 0)
             + mask.SampleLevel(samp, centerUV + offset * float2(-1,  1), 0)
             + mask.SampleLevel(samp, centerUV + offset * float2( 1,  1), 0)) * 0.4); // hand-tuned mult

        clampingFactor *= (1.0 + historyClampRelax * 1.5); // extend the clamp range
        newFrameWeight /= (1.0 + historyClampRelax * 0.5); // reduce new-sample weight
    }

    float2 longestMV = s_MotionVectors[longestMVPos.y][longestMVPos.x]; // pixel space

    // variance-clip bounds (PQ space)
    colorMoment1 /= 9.0;
    colorMoment2 /= 9.0;
    float3 colorVariance = colorMoment2 - colorMoment1 * colorMoment1;
    float3 colorSigma = sqrt(max(0.0, colorVariance)) * clampingFactor;
    float3 colorMin = colorMoment1 - colorSigma;
    float3 colorMax = colorMoment1 + colorSigma;

    // reproject with dilated MV; sourcePos in texel coords (+0.5 = texel center)
    float2 sourcePos = float2(outputPixelPos) + longestMV + 0.5;

    float3 resultPQ;
    if (newFrameWeight < 1.0 &&
        all(sourcePos > float2(0.0)) &&
        all(sourcePos < renderSize))
    {
        float3 history;
        if (pc.useCatmullRom != 0)
            history = BicubicSampleCatmullRom(pc.feedbackInputIndex, sourcePos, texelSize);
        else
            history = rdgTexturesFloat4[pc.feedbackInputIndex]
                        .SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], sourcePos * texelSize, 0).rgb;

        // clip history to neighborhood
        float3 historyClamped = history;
        if (clampingFactor >= 0.0)
            historyClamped = min(colorMax, max(colorMin, history));

        // Motion/subsample-aware blend weight.
        float motionWeight = smoothstep(0.0, 1.0, length(longestMV)); // ramps over ~1 px of motion
        float2 distanceToLowResPixel = inputPos - float2(inputPosInt); // fractional jitter
        float sampleWeight = saturate(1.0 - dot(distanceToLowResPixel, distanceToLowResPixel));
        float blendWeight = saturate(max(motionWeight, sampleWeight) * newFrameWeight);

        resultPQ = lerp(historyClamped, thisPixelColor, blendWeight);
    }
    else
    {
        // first frame / invalid history / off-screen
        resultPQ = thisPixelColor;
    }

    float3 result = PQDecode(resultPQ);

    rdgStorageFloat4[pc.colorOutputIndex][outputPixelPos]    = float4(result, 1.0);   // linear HDR
    rdgStorageFloat4[pc.feedbackOutputIndex][outputPixelPos] = float4(resultPQ, 0.0); // PQ history (next frame's input)
}

// Host contract (jitter, RGBA16F PQ history ping-pong, first-frame newFrameWeight=1) is owned by the C++ pass SetupDonutTemporalAntiAliasing in anti_aliasing_passes.cpp.
