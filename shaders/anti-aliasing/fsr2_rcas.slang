// =============================================================================
// fsr2_rcas.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2 (ffx_fsr2_rcas.h, ffx_fsr1.h FsrRcasF with
// FSR_RCAS_DENOISE). One pixel per thread instead of the quad remap.
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

[[vk::push_constant]] Fsr2RcasPushConstant pc;

static const float FSR_RCAS_LIMIT = 0.25 - (1.0 / 16.0);

float ApproximateReciprocalMedium(float a)
{
    float b = asfloat(0x7ef19fff - asuint(a));
    return b * (-b * a + 2.0);
}

float Max3(float a, float b, float c)
{
    return max(a, max(b, c));
}

float Min3(float a, float b, float c)
{
    return min(a, min(b, c));
}

float3 Max3(float3 a, float3 b, float3 c)
{
    return max(a, max(b, c));
}

float3 Min3(float3 a, float3 b, float3 c)
{
    return min(a, min(b, c));
}

float3 LoadRcasInput(int2 pos, float exposure)
{
    const float3 color = rdgTexturesFloat4[pc.inputIndex].Load(int3(Fsr2ClampCoord(pos, int2(pc.c.displaySize)), 0)).rgb;
    return Fsr2PrepareRgb(color, exposure, pc.c.preExposure);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeFsr2Rcas(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pos = int2(dispatchThreadId.xy);
    if (!Fsr2IsOnScreen(pos, int2(pc.c.displaySize))) {
        return;
    }
    const float exposure = Fsr2Exposure(pc.exposureIndex, pc.c.preExposure);

    //    b
    //  d e f
    //    h
    const float3 b = LoadRcasInput(pos + int2(0, -1), exposure);
    const float3 d = LoadRcasInput(pos + int2(-1, 0), exposure);
    const float3 e = LoadRcasInput(pos, exposure);
    const float3 f = LoadRcasInput(pos + int2(1, 0), exposure);
    const float3 h = LoadRcasInput(pos + int2(0, 1), exposure);

    // Luma times 2.
    const float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    const float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    const float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    const float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    const float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

    // Noise detection.
    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
    nz = saturate(abs(nz) * ApproximateReciprocalMedium(Max3(Max3(bL, dL, eL), fL, hL) - Min3(Min3(bL, dL, eL), fL, hL)));
    nz = -0.5 * nz + 1.0;

    const float3 mn4 = min(Min3(b, d, f), h);
    const float3 mx4 = max(Max3(b, d, f), h);

    const float2 peakC = float2(1.0, -1.0 * 4.0);

    const float3 hitMin = mn4 / (4.0 * mx4);
    const float3 hitMax = (peakC.x - mx4) / (4.0 * mn4 + peakC.y);
    const float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT, min(Max3(lobeRGB.r, lobeRGB.g, lobeRGB.b), 0.0)) * pc.sharpness;

    lobe *= nz;

    const float rcpL = ApproximateReciprocalMedium(4.0 * lobe + 1.0);
    float3 color = (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;

    color = Fsr2UnprepareRgb(color, exposure, pc.c.preExposure);
    rdgStorageFloat4[pc.outputIndex][pos] = float4(color, 1.0);
}
