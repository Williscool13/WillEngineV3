import push_constant_interop;
import shader_functions;
import constants_interop;
import model_interop;
import pbr_functions;
import visibility_lighting_common;

import bindless_declarations;

[numthreads(16, 16, 1)]
void ComputeMain(uniform VisibilityLightingPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    LightingDispatchParameters lightParams = pc.lightDispatchBuffer[pc.lightingIndex];
    uint2 pixel = threadID.xy + uint2(lightParams.minX, lightParams.minY);
    if (any(pixel >= uint2(lightParams.maxX, lightParams.maxY))) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        return;
    }

    float3 finalColor = li.albedo.xyz + li.emissiveRGB;
    rdgStorageFloat4[pc.primaryOutputImageIndex][pixel] = float4(finalColor, 1.0);
}
