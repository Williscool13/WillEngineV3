import push_constant_interop;
import shader_functions;
import constants_interop;
import model_interop;
import pbr_functions;
import visibility_lighting_common;

import bindless_declarations;
import bucket_dispatch_common;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeLightingUnlit(uniform VisibilityLightingPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID) {
    uint2 pixel = BucketPixel(pc.tileListBuffer, pc.tileCapacity, pc.lightingIndex, gid.x, gtid.xy);
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.materialBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        return;
    }

    float3 finalColor = li.albedo.xyz + li.emissiveRGB * pc.sceneData[pc.sceneDataIndex].preExposure;
    rdgStorageFloat4[pc.primaryOutputImageIndex][pixel] = float4(finalColor, 1.0);
}
