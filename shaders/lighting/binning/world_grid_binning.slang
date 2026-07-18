import push_constant_interop;
import common_interop;
import lights_interop;
import world_grid_interop;
import binning_common;

// One thread per cell; membership must match WorldGridResolveCell exactly (lighting_pbr.slang reads it back).
[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeWorldGridBinning(uniform WorldGridBinningPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    uint flatIndex = threadID.x;
    if (flatIndex >= WORLD_GRID_CELL_COUNT) { return; }

    uint level = flatIndex / WORLD_GRID_CELLS_PER_CASCADE;
    uint local = flatIndex % WORLD_GRID_CELLS_PER_CASCADE;
    uint3 cell = uint3(local % WORLD_GRID_RES, (local / WORLD_GRID_RES) % WORLD_GRID_RES, local / (WORLD_GRID_RES * WORLD_GRID_RES));

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 camPos = scene.cameraWorldPos.xyz;

    float cellSize = WorldGridCellSizeAtLevel(level);
    float3 center = WorldGridCellCenter(level, cell, camPos);
    float3 aabbMin = center - cellSize * 0.5;
    float3 aabbMax = center + cellSize * 0.5;

    uint indexOffset = flatIndex * MAX_LIGHTS_PER_WORLD_GRID_CELL;
    uint count = 0u;

    // Analytic (area/sphere) only, matching FrustumBinning; triangle lights excluded.
    int analyticCount = pc.lightData->analyticLightCount;
    for (int i = 0; i < analyticCount; i++)
    {
        LightInfo light = pc.lightData->lights[i];
        float radius = light.range;
        if (SqDistPointAABB(light.position.xyz, aabbMin, aabbMax) <= radius * radius)
        {
            pc.worldGridIndexList[indexOffset + count] = uint(i);
            count++;
            if (count >= MAX_LIGHTS_PER_WORLD_GRID_CELL) { break; }
        }
    }

    pc.worldGridBuffer[flatIndex] = uint2(indexOffset, count);
}
