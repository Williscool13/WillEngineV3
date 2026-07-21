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
    // Weakest currently-held light, so an overflowing cell keeps the strongest K by power rather than the first K encountered.
    float minPower = 1e30;
    uint minSlot = 0u;

    // Analytic (area/sphere) only, matching FrustumBinning; triangle lights excluded.
    int analyticCount = pc.lightData->analyticLightCount;
    for (int i = 0; i < analyticCount; i++)
    {
        LightInfo light = pc.lightData->lights[i];
        float radius = light.range;
        if (SqDistPointAABB(light.position.xyz, aabbMin, aabbMax) > radius * radius) { continue; }

        float power = WorldGridLightPower(light);
        if (count < MAX_LIGHTS_PER_WORLD_GRID_CELL)
        {
            pc.worldGridIndexList[indexOffset + count] = uint(i);
            if (power < minPower) { minPower = power; minSlot = count; }
            count++;
        }
        else if (power > minPower)
        {
            // Evict the weakest, then rescan the K slots to find the new weakest. Rescan cost is paid only on overflow (rare in bounded analytic scenes).
            pc.worldGridIndexList[indexOffset + minSlot] = uint(i);
            minPower = 1e30;
            for (uint k = 0u; k < MAX_LIGHTS_PER_WORLD_GRID_CELL; k++)
            {
                float hp = WorldGridLightPower(pc.lightData->lights[pc.worldGridIndexList[indexOffset + k]]);
                if (hp < minPower) { minPower = hp; minSlot = k; }
            }
        }
    }

    pc.worldGridBuffer[flatIndex] = uint2(indexOffset, count);
}
