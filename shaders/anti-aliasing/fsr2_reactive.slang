// =============================================================================
// fsr2_reactive.slang
//
// AMD FidelityFX Super Resolution 2.2.1, ported to WillEngineV3/Slang from
// GPUOpen-Effects/FidelityFX-FSR2 (ffx_fsr2_autogen_reactive_pass.hlsl).
// The pre-overlay colour snapshot plays the role of the opaque-only input.
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

[[vk::push_constant]] Fsr2ReactivePushConstant pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeFsr2Reactive(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const int2 pos = int2(dispatchThreadId.xy);
    const int2 renderSize = int2(pc.c.renderSize);
    if (!Fsr2IsOnScreen(pos, renderSize)) {
        return;
    }

    float3 colorPreOverlay = Fsr2Tonemap(max(float3(0.0), Fsr2LoadInputColor(pc.preOverlayColorIndex, pos)));
    float3 colorPostOverlay = Fsr2Tonemap(max(float3(0.0), Fsr2LoadInputColor(pc.colorIndex, pos)));

    float3 delta = abs(colorPostOverlay - colorPreOverlay);
    float reactive = max(delta.x, max(delta.y, delta.z)) * pc.scale;
    reactive = reactive < pc.threshold ? 0.0 : pc.binaryValue;

    rdgStorageFloat[pc.reactiveOutIndex][pos] = reactive;
}
