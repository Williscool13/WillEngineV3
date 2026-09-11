// =============================================================================
// fsr2_luminance.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2 (ffx_fsr2_compute_luminance_pyramid.h).
// One 64x64 tile per group; writes the shading-change mip (32x32 blocks)
// directly instead of running SPD.
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

[[vk::push_constant]] Fsr2LuminancePushConstant pc;

static const int TILE_SIZE = 64;
static const int GROUP_DIM = 16;
static const int TEXELS_PER_THREAD = TILE_SIZE / GROUP_DIM;

groupshared float gsSum[GROUP_DIM * GROUP_DIM];
groupshared float gsCount[GROUP_DIM * GROUP_DIM];

[shader("compute")]
[numthreads(GROUP_DIM, GROUP_DIM, 1)]
void ComputeFsr2Luminance(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    const int2 renderSize = int2(pc.c.renderSize);
    const int2 tileBase = int2(groupId.xy) * TILE_SIZE;
    const int2 threadBase = tileBase + int2(groupThreadId.xy) * TEXELS_PER_THREAD;

    float sum = 0.0;
    float count = 0.0;
    for (int y = 0; y < TEXELS_PER_THREAD; y++) {
        for (int x = 0; x < TEXELS_PER_THREAD; x++) {
            const int2 pos = threadBase + int2(x, y);
            if (!Fsr2IsOnScreen(pos, renderSize)) {
                continue;
            }
            float2 uv = (float2(pos) + 0.5 + pc.c.jitter) / pc.c.renderSize;
            uv = Fsr2ClampUv(uv, renderSize, renderSize);
            float3 rgb = rdgTexturesFloat4[pc.colorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rgb;
            sum += log(max(FSR2_EPSILON, Fsr2RGBToLuma(rgb)));
            count += 1.0;
        }
    }
    gsSum[groupIndex] = sum;
    gsCount[groupIndex] = count;
    GroupMemoryBarrierWithGroupSync();

    if (groupThreadId.x < 2 && groupThreadId.y < 2) {
        const int2 quadrant = int2(groupThreadId.xy);
        float quadrantSum = 0.0;
        float quadrantCount = 0.0;
        for (int y = 0; y < GROUP_DIM / 2; y++) {
            for (int x = 0; x < GROUP_DIM / 2; x++) {
                const int index = (quadrant.y * (GROUP_DIM / 2) + y) * GROUP_DIM + quadrant.x * (GROUP_DIM / 2) + x;
                quadrantSum += gsSum[index];
                quadrantCount += gsCount[index];
            }
        }
        const int2 mip4Pos = int2(groupId.xy) * 2 + quadrant;
        if (Fsr2IsOnScreen(mip4Pos, int2(pc.c.lumaMipSize))) {
            rdgStorageFloat[pc.lumaMip4OutIndex][mip4Pos] = quadrantCount > 0.0 ? quadrantSum / quadrantCount : 0.0;
        }
    }
}
