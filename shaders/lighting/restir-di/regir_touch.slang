import common_interop;
import push_constant_interop;
import restir_interop;
import shader_functions;
import bindless_declarations;
import regir_hash;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dtid : SV_DispatchThreadID, uniform ReGIRTouchPushConstant pc)
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
    // Must match the consume pass's reconstruction exactly, or the key won't resolve to the touched cell.
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 cameraPos = scene.cameraWorldPos.xyz;
    if (distance(worldPos, cameraPos) > REGIR_MAX_DIST) {
        return;
    }

    ReGIRHashKey key = ReGIRComputeKey(worldPos, cameraPos);
    uint cs = ReGIRChecksum(key);

    // Peel one unique cell per wave iteration: the elected lane does the single CAS + append for the whole same-cell group, so a coherent tile costs one atomic per cell, not per pixel.
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
