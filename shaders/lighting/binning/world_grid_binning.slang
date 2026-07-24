import push_constant_interop;
import common_interop;
import lights_interop;
import world_grid_interop;
import binning_common;
import reflection_probe_common;
import reflection_probe_interop;

// Probe influence volume vs cell AABB. Sphere: point-AABB distance. Box: 15-axis OBB/AABB SAT (RTCD 4.4.1), OBB derived from worldToLocal (rows have length 1/extent_i; center solves Linear*c + t = 0).
static bool ReflectionProbeIntersectsCellAABB(ReflectionProbeGPU probe, float3 aabbMin, float3 aabbMax)
{
    float4x4 rows = transpose(probe.worldToLocal);
    float3 extent = ReflectionProbeWorldExtent(probe);
    float3 axis0 = normalize(rows[0].xyz);
    float3 axis1 = normalize(rows[1].xyz);
    float3 axis2 = normalize(rows[2].xyz);
    float3 t = mul(probe.worldToLocal, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 obbCenter = -(axis0 * extent.x * t.x + axis1 * extent.y * t.y + axis2 * extent.z * t.z);

    if ((probe.flags & REFLECTION_PROBE_FLAG_SPHERE) != 0u)
    {
        float radius = extent.x;
        return SqDistPointAABB(obbCenter, aabbMin, aabbMax) <= radius * radius;
    }

    float3 aabbCenter = (aabbMin + aabbMax) * 0.5;
    float3 ae = (aabbMax - aabbMin) * 0.5;
    float3 be = extent;
    float3 d = obbCenter - aabbCenter;

    float3 bu[3] = { axis0, axis1, axis2 };
    float R[3][3];
    float AbsR[3][3];
    [unroll]
    for (uint i = 0u; i < 3u; i++)
    {
        [unroll]
        for (uint j = 0u; j < 3u; j++)
        {
            R[i][j] = bu[j][i];
            AbsR[i][j] = abs(R[i][j]) + 1e-6;
        }
    }

    [unroll]
    for (uint i = 0u; i < 3u; i++)
    {
        float ra = ae[i];
        float rb = be[0] * AbsR[i][0] + be[1] * AbsR[i][1] + be[2] * AbsR[i][2];
        if (abs(d[i]) > ra + rb) { return false; }
    }

    [unroll]
    for (uint j = 0u; j < 3u; j++)
    {
        float ra = ae[0] * AbsR[0][j] + ae[1] * AbsR[1][j] + ae[2] * AbsR[2][j];
        float rb = be[j];
        if (abs(dot(d, bu[j])) > ra + rb) { return false; }
    }

    if (abs(d[2] * R[1][0] - d[1] * R[2][0]) > ae[1] * AbsR[2][0] + ae[2] * AbsR[1][0] + be[1] * AbsR[0][2] + be[2] * AbsR[0][1]) { return false; }
    if (abs(d[2] * R[1][1] - d[1] * R[2][1]) > ae[1] * AbsR[2][1] + ae[2] * AbsR[1][1] + be[0] * AbsR[0][2] + be[2] * AbsR[0][0]) { return false; }
    if (abs(d[2] * R[1][2] - d[1] * R[2][2]) > ae[1] * AbsR[2][2] + ae[2] * AbsR[1][2] + be[0] * AbsR[0][1] + be[1] * AbsR[0][0]) { return false; }
    if (abs(d[0] * R[2][0] - d[2] * R[0][0]) > ae[0] * AbsR[2][0] + ae[2] * AbsR[0][0] + be[1] * AbsR[1][2] + be[2] * AbsR[1][1]) { return false; }
    if (abs(d[0] * R[2][1] - d[2] * R[0][1]) > ae[0] * AbsR[2][1] + ae[2] * AbsR[0][1] + be[0] * AbsR[1][2] + be[2] * AbsR[1][0]) { return false; }
    if (abs(d[0] * R[2][2] - d[2] * R[0][2]) > ae[0] * AbsR[2][2] + ae[2] * AbsR[0][2] + be[0] * AbsR[1][1] + be[1] * AbsR[1][0]) { return false; }
    if (abs(d[1] * R[0][0] - d[0] * R[1][0]) > ae[0] * AbsR[1][0] + ae[1] * AbsR[0][0] + be[1] * AbsR[2][2] + be[2] * AbsR[2][1]) { return false; }
    if (abs(d[1] * R[0][1] - d[0] * R[1][1]) > ae[0] * AbsR[1][1] + ae[1] * AbsR[0][1] + be[0] * AbsR[2][2] + be[2] * AbsR[2][0]) { return false; }
    if (abs(d[1] * R[0][2] - d[0] * R[1][2]) > ae[0] * AbsR[1][2] + ae[1] * AbsR[0][2] + be[0] * AbsR[2][1] + be[1] * AbsR[2][0]) { return false; }

    return true;
}

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

    // Smallest-volume probe whose influence volume intersects this cell; ascending-index strict-less tie-break. Written unconditionally (aliased RDG buffers are not zeroed).
    uint bestProbe = WORLD_GRID_PROBE_SENTINEL;
    float bestProbeVolume = 1e30;
    for (uint p = 0u; p < pc.reflectionProbeCount; p++)
    {
        ReflectionProbeGPU probe = pc.reflectionProbes[p];
        if (!ReflectionProbeIntersectsCellAABB(probe, aabbMin, aabbMax)) { continue; }
        float volume = ReflectionProbeVolume(probe);
        if (volume < bestProbeVolume)
        {
            bestProbeVolume = volume;
            bestProbe = p;
        }
    }
    pc.worldGridProbeGrid[flatIndex] = bestProbe;
}
