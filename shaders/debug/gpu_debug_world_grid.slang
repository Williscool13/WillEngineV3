import common_interop;
import model_interop;
import push_constant_interop;
import gpu_debug_draw_common;
import lights_interop;
import world_grid_interop;
import binning_common;
import packing_functions;
import world_cache_hash_common;
import shader_functions;
import world_cache_interop;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeGPUDebugWorldGrid(uint3 tid: SV_DispatchThreadID, uniform WorldGridDebugPushConstant pc)
{
    uint localIndex = tid.x;
    if (localIndex >= WORLD_GRID_CELLS_PER_CASCADE) { return; }

    uint3 cell = uint3(localIndex % WORLD_GRID_RES, (localIndex / WORLD_GRID_RES) % WORLD_GRID_RES, localIndex / (WORLD_GRID_RES * WORLD_GRID_RES));

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 camPos = scene.cameraWorldPos.xyz;

    float cellSize = WorldGridCellSizeAtLevel(pc.level);
    float3 center = WorldGridCellCenter(pc.level, cell, camPos);
    float he = cellSize * 0.5;

    float4 color = UnpackUnorm4x8(pc.packedTint);
    GPUDebugAddOBB(pc.args, pc.segmentBuffer, center, float3(he, 0.0, 0.0), float3(0.0, he, 0.0), float3(0.0, 0.0, he), 0.02, color);
}
