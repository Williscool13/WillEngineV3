import common_interop;
import push_constant_interop;
import restir_interop;
import shader_functions;
import bindless_declarations;
import regir_hash_common;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeReGIRTouch(uint3 dtid : SV_DispatchThreadID, uniform ReGIRTouchPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(int2(pixel), 0)).r;
    if (depth <= 0.0) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 cameraPos = scene.cameraWorldPos.xyz;
    if (distance(worldPos, cameraPos) > REGIR_MAX_DIST) {
        return;
    }

    ReGIRHashKey key = ReGIRComputeKey(worldPos, cameraPos);
    uint cs = ReGIRChecksum(key);

    bool done = false;
    while (WaveActiveAnyTrue(!done)) {
        uint activeCs = WaveActiveMin(done ? 0xFFFFFFFFu : cs);
        if (!done && cs == activeCs) {
            int3 electedCell = WaveReadLaneFirst(key.cellPos);
            uint electedLevel = WaveReadLaneFirst(key.level);
            if (all(key.cellPos == electedCell) && key.level == electedLevel) {
                if (WaveIsFirstLane()) {
                    bool isNew;
                    uint slot = ReGIRHashInsert(pc.hashEntries, key, isNew);
                    if (slot != REGIR_HASH_INVALID && isNew) {
                        uint idx;
                        InterlockedAdd(pc.activeCount[0], 1u, idx);
                        pc.activeCells[idx] = int4(key.cellPos, int(ReGIRPackActive(key.level, slot)));
                    }
                }
                done = true;
            }
        }
    }
}
