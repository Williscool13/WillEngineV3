import push_constant_interop;
import constants_interop;
import restir_common;

import bindless_declarations;
import bucket_dispatch_common;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeLightingBlack(uniform VisibilityLightingPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID) {
    uint2 pixel = BucketPixel(pc.tileListBuffer, pc.tileCapacity, pc.lightingIndex, gid.x, gtid.xy);
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    if (pc.secondaryOutputImageIndex != ~0x0u) {
        WriteZeroDemodulatedLighting(pc.primaryOutputImageIndex, pc.secondaryOutputImageIndex, pc.diffuseRatioIndex, pixel, pc.renderExtent,
                                     pc.activeCheckerboardField, pc.bFullRateResolve != 0u, pc.bCheckerboardPacked != 0u);
        return;
    }
    rdgStorageFloat4[pc.primaryOutputImageIndex][pixel] = float4(0.0, 0.0, 0.0, 1.0);
}
