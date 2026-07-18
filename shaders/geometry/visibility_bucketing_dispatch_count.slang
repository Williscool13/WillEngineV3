import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;

import shader_constants;
import shader_functions;
import bindless_declarations;

// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeBucketDispatchCount(uint3 dtid : SV_DispatchThreadID, uniform BucketDispatchCountPushConstant pc)
{
    uint32_t shadeActive = 0;
    if (dtid.x < pc.materialCount) {
        shadeActive = (pc.shadeDispatchBuffer[dtid.x].xDispatch > 0) ? 1 : 0;
    }

    uint32_t lightActive = 0;
    if (dtid.x < pc.lightingCount) {
        lightActive = (pc.lightDispatchBuffer[dtid.x].xDispatch > 0) ? 1 : 0;
    }

    uint32_t waveShadeCount = WaveActiveCountBits(shadeActive != 0);
    uint32_t waveLightCount = WaveActiveCountBits(lightActive != 0);

    if (WaveIsFirstLane()) {
        if (waveShadeCount > 0) {
            InterlockedAdd(pc.countBuffer[0], waveShadeCount);
        }
        if (waveLightCount > 0) {
            InterlockedAdd(pc.countBuffer[1], waveLightCount);
        }
    }
}
