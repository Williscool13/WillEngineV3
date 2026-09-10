// =============================================================================
// fsr2_lock.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2 (ffx_fsr2_lock.h). The reconstructed depth
// clear is not needed here: that texture is transient and cleared by the graph.
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

[[vk::push_constant]] Fsr2LockPushConstant pc;

static const uint REJECTION_MASKS[4] = {
    (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4),
    (1u << 1) | (1u << 2) | (1u << 4) | (1u << 5),
    (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7),
    (1u << 4) | (1u << 5) | (1u << 7) | (1u << 8),
};

float LoadLockInputLuma(int2 pos)
{
    return rdgTexturesFloat[pc.lockInputLumaIndex].Load(int3(pos, 0)).r;
}

bool ComputeThinFeatureConfidence(int2 pos)
{
    const int2 renderSize = int2(pc.c.renderSize);
    const float nucleus = LoadLockInputLuma(pos);

    const float SIMILAR_THRESHOLD = 1.05;
    float dissimilarLumaMin = FSR2_FLT_MAX;
    float dissimilarLumaMax = 0.0;

    uint mask = 1u << 4;

    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) {
                continue;
            }
            const int idx = (y + 1) * 3 + (x + 1);
            const int2 samplePos = Fsr2ClampLoad(pos, int2(x, y), renderSize);
            const float sampleLuma = LoadLockInputLuma(samplePos);
            const float difference = max(sampleLuma, nucleus) / min(sampleLuma, nucleus);
            if (difference > 0.0 && (difference < SIMILAR_THRESHOLD)) {
                mask |= 1u << idx;
            } else {
                dissimilarLumaMin = min(dissimilarLumaMin, sampleLuma);
                dissimilarLumaMax = max(dissimilarLumaMax, sampleLuma);
            }
        }
    }

    const bool bIsRidge = nucleus > dissimilarLumaMax || nucleus < dissimilarLumaMin;
    if (!bIsRidge) {
        return false;
    }

    [unroll]
    for (int i = 0; i < 4; i++) {
        if ((mask & REJECTION_MASKS[i]) == REJECTION_MASKS[i]) {
            return false;
        }
    }
    return true;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeFsr2Lock(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pos = int2(dispatchThreadId.xy);
    if (!Fsr2IsOnScreen(pos, int2(pc.c.renderSize))) {
        return;
    }

    if (ComputeThinFeatureConfidence(pos)) {
        const int2 hrPos = Fsr2ComputeHrPosFromLrPos(pos, pc.c.jitter, pc.c.renderSize, pc.c.displaySize);
        if (Fsr2IsOnScreen(hrPos, int2(pc.c.displaySize))) {
            rdgStorageFloat[pc.newLocksOutIndex][hrPos] = 1.0;
        }
    }
}
