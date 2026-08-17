import push_constant_interop;
import constants_interop;
import flags_interop;
import common_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

static const float DOF_SKY_VIEWZ = 3.0e4;

// Signed circle of confusion in full-res pixels
float DofSignedCoc(float viewZ, DofCocPushConstant pc) {
    float delta = viewZ - pc.focusDistance;
    float dist = abs(delta) - pc.sharpHalfRange;
    if (dist <= 0.0) { return 0.0; }
    if (delta < 0.0) { return -saturate(dist * pc.nearTransitionInv) * pc.nearRadiusPx; }
    return saturate(dist * pc.farTransitionInv) * pc.farRadiusPx;
}

[shader("compute")]
[numthreads(POST_PROCESS_DOF_DISPATCH_X, POST_PROCESS_DOF_DISPATCH_Y, 1)]
void ComputeDofCoc(uniform DofCocPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.outputExtent)) { return; }

    float3 color = float3(0.0, 0.0, 0.0);
    float coc = 0.0;
    float maxMagnitude = -1.0;

    for (int i = 0; i < 4; ++i) {
        int2 fullCoord = min(int2(pixel) * 2 + int2(i & 1, i >> 1), int2(pc.inputExtent) - 1);
        color += rdgTexturesFloat4[pc.sceneColorIndex].Load(int3(fullCoord, 0)).rgb;

        int2 depthCoord = min(int2((float2(fullCoord) + 0.5) * float2(pc.renderExtent) / float2(pc.inputExtent)), int2(pc.renderExtent) - 1);
        float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(depthCoord, 0)).r;
        float viewZ = depth > 0.0 ? ScreenToViewSpaceDepth(depth, pc.sceneData.depthLinearizeMult, pc.sceneData.depthLinearizeAdd) : DOF_SKY_VIEWZ;

        float sampleCoc = DofSignedCoc(viewZ, pc);
        if (abs(sampleCoc) > maxMagnitude) {
            maxMagnitude = abs(sampleCoc);
            coc = sampleCoc;
        }
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(color * 0.25, coc);
}
