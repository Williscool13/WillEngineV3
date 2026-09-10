// =============================================================================
// fsr2_exposure.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2 (ffx_fsr2_compute_luminance_pyramid.h,
// SpdStore 1x1 level). Single group reduces the 64x64-tile log-luma mip to the
// frame average and runs the auto-exposure smoothing.
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

[[vk::push_constant]] Fsr2ExposurePushConstant pc;

static const int GROUP_SIZE = 256;
static const int TILE_SIZE = 64;

groupshared float gsSum[GROUP_SIZE];
groupshared float gsWeight[GROUP_SIZE];

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
void ComputeFsr2Exposure(uint groupIndex : SV_GroupIndex)
{
    const int2 renderSize = int2(pc.c.renderSize);
    const int2 tileCount = (renderSize + TILE_SIZE - 1) / TILE_SIZE;
    const int totalTiles = tileCount.x * tileCount.y;

    float sum = 0.0;
    float weight = 0.0;
    for (int i = int(groupIndex); i < totalTiles; i += GROUP_SIZE) {
        const int2 tile = int2(i % tileCount.x, i / tileCount.x);
        const int2 validTexels = min(int2(TILE_SIZE), renderSize - tile * TILE_SIZE);
        const float tileWeight = float(validTexels.x * validTexels.y);
        sum += rdgTexturesFloat[pc.lumaMip5Index].Load(int3(tile, 0)).r * tileWeight;
        weight += tileWeight;
    }
    gsSum[groupIndex] = sum;
    gsWeight[groupIndex] = weight;
    GroupMemoryBarrierWithGroupSync();

    for (int stride = GROUP_SIZE / 2; stride > 0; stride >>= 1) {
        if (groupIndex < uint(stride)) {
            gsSum[groupIndex] += gsSum[groupIndex + stride];
            gsWeight[groupIndex] += gsWeight[groupIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        const float logLumaAverage = gsSum[0] / max(gsWeight[0], 1.0);

        float previousAverage = FSR2_RESET_AUTO_EXPOSURE_AVERAGE_SMOOTHING;
        if (pc.c.frameIndex != 0 && pc.previousExposureIndex != FSR2_INVALID_INDEX) {
            previousAverage = rdgTexturesFloat2[pc.previousExposureIndex].Load(int3(0, 0, 0)).y;
        }

        float result = logLumaAverage;
        if (previousAverage < FSR2_RESET_AUTO_EXPOSURE_AVERAGE_SMOOTHING) {
            const float rate = 1.0;
            result = previousAverage + (result - previousAverage) * (1.0 - exp(-pc.c.deltaTime * rate));
        }

        rdgStorageFloat2[pc.exposureOutIndex][int2(0, 0)] = float2(Fsr2ComputeAutoExposureFromLavg(result), result);
    }
}
