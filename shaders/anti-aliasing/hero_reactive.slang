/*
 * TAA reactivity mask for the hero sun shadow: |maskNow - maskPrev| at the same pixel (receiver is static, so no reprojection),
 * scaled and max-dilated. TAA snaps history to current where this is set, killing the moving contour's trail.
 */
import common_interop;
import push_constant_interop;
import bindless_declarations;

[[vk::push_constant]]
HeroReactivePushConstant pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeHeroReactive(uint3 threadID: SV_DispatchThreadID)
{
    if (threadID.x >= pc.renderExtent.x || threadID.y >= pc.renderExtent.y) {
        return;
    }

    int2 maxCoord = int2(pc.renderExtent) - 1;
    float reactive = 0.0;
    for (int y = -pc.dilationRadius; y <= pc.dilationRadius; ++y) {
        for (int x = -pc.dilationRadius; x <= pc.dilationRadius; ++x) {
            int2 coord = clamp(int2(threadID.xy) + int2(x, y), int2(0), maxCoord);
            float now = rdgTexturesFloat[pc.currentIndex].Load(int3(coord, 0)).r;
            float prev = rdgTexturesFloat[pc.historyIndex].Load(int3(coord, 0)).r;
            reactive = max(reactive, abs(now - prev));
        }
    }

    rdgStorageFloat[pc.outputIndex][threadID.xy] = saturate(reactive * pc.reactiveScale);
}
