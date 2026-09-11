// =============================================================================
// fsr2_reconstruct.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2
// (ffx_fsr2_reconstruct_dilated_velocity_and_previous_depth.h).
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

[[vk::push_constant]] Fsr2ReconstructPushConstant pc;

static const int2 NEAREST_DEPTH_OFFSETS[9] = {
    int2(0, 0), int2(1, 0), int2(0, 1), int2(0, -1), int2(-1, 0), int2(-1, 1), int2(1, 1), int2(-1, -1), int2(1, -1),
};

void FindNearestDepth(int2 pos, int2 size, out float nearestDepth, out int2 nearestDepthCoord)
{
    float depth[9];
    [unroll]
    for (int i = 0; i < 9; ++i) {
        depth[i] = Fsr2LoadInputDepth(pc.depthIndex, Fsr2ClampCoord(pos + NEAREST_DEPTH_OFFSETS[i], size));
    }

    nearestDepthCoord = pos;
    nearestDepth = depth[0];
    [unroll]
    for (int i = 1; i < 9; ++i) {
        const int2 samplePos = pos + NEAREST_DEPTH_OFFSETS[i];
        if (Fsr2IsOnScreen(samplePos, size) && depth[i] > nearestDepth) {
            nearestDepthCoord = samplePos;
            nearestDepth = depth[i];
        }
    }
}

void ReconstructPrevDepth(int2 pos, float depth, float2 motionVector, int2 size)
{
    motionVector *= (length(motionVector * pc.c.displaySize) > 0.1) ? 1.0 : 0.0;

    const float2 uv = (float2(pos) + 0.5) / float2(size);
    const float2 reprojectedUv = uv + motionVector;
    const Fsr2BilinearSamplingData bilinearInfo = Fsr2GetBilinearSamplingData(reprojectedUv, size);

    const uint depthBits = asuint(depth);
    [unroll]
    for (int i = 0; i < 4; i++) {
        if (bilinearInfo.weights[i] > FSR2_RECONSTRUCTED_DEPTH_WEIGHT_THRESHOLD) {
            const int2 storePos = bilinearInfo.basePos + bilinearInfo.offsets[i];
            if (Fsr2IsOnScreen(storePos, size)) {
                InterlockedMax(rdgStorageUInt[pc.reconstructedDepthOutIndex][storePos], depthBits);
            }
        }
    }
}

float ComputeLockInputLuma(int2 pos)
{
    float3 rgb = max(float3(0.0), Fsr2LoadInputColor(pc.colorIndex, pos));
    rgb = Fsr2Tonemap(rgb);
    return pow(Fsr2RGBToPerceivedLuma(rgb), 1.0 / 6.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeFsr2Reconstruct(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pos = int2(dispatchThreadId.xy);
    const int2 renderSize = int2(pc.c.renderSize);
    if (!Fsr2IsOnScreen(pos, renderSize)) {
        return;
    }

    float dilatedDepth;
    int2 nearestDepthCoord;
    FindNearestDepth(pos, renderSize, dilatedDepth, nearestDepthCoord);

    const float2 dilatedMotionVector = Fsr2LoadInputMotionVector(pc.sceneData, pc.gbufferOneIndex, pc.depthIndex, nearestDepthCoord, pc.c.renderSize);

    rdgStorageFloat[pc.dilatedDepthOutIndex][pos] = dilatedDepth;
    rdgStorageFloat2[pc.dilatedMotionOutIndex][pos] = dilatedMotionVector;

    ReconstructPrevDepth(pos, dilatedDepth, dilatedMotionVector, renderSize);

    rdgStorageFloat[pc.lockInputLumaOutIndex][pos] = ComputeLockInputLuma(pos);
}
