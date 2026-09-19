import push_constant_interop;
import common_interop;
import constants_interop;
import lights_interop;
import world_grid_interop;
import binning_common;
import bindless_declarations;
import shader_functions;

// =============== Shader Summary ============
// Editor only, one workgroup. Reads back the world-grid bin under the cursor: what binning kept, a recount of everything in range (kept < inRange = the strongest-K clip), and the top kept entries by power.

static const uint CURSOR_CELL_GROUP = 256u;
static const uint CURSOR_CELL_TOP = 8u;

[[vk::push_constant]]
DebugWorldGridCursorCellPushConstant pc;

groupshared uint gCount[CURSOR_CELL_GROUP];

static const uint MASK_WORDS = (MAX_EMISSIVE_MESHLETS_PER_WORLD_GRID_CELL > MAX_LIGHTS_PER_WORLD_GRID_CELL ? MAX_EMISSIVE_MESHLETS_PER_WORLD_GRID_CELL + 31u : MAX_LIGHTS_PER_WORLD_GRID_CELL + 31u) / 32u;

struct UsedMask
{
    uint words[MASK_WORDS];
    [mutating] void Clear() { for (uint w = 0u; w < MASK_WORDS; w++) { words[w] = 0u; } }
    bool Get(uint s) { return ((words[s >> 5u] >> (s & 31u)) & 1u) != 0u; }
    [mutating] void Set(uint s) { words[s >> 5u] |= 1u << (s & 31u); }
};

static uint ReduceCount(uint lane, uint value)
{
    gCount[lane] = value;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = CURSOR_CELL_GROUP >> 1u; stride > 0u; stride >>= 1u) {
        if (lane < stride) { gCount[lane] += gCount[lane + stride]; }
        GroupMemoryBarrierWithGroupSync();
    }
    uint total = gCount[0];
    GroupMemoryBarrierWithGroupSync();
    return total;
}

[shader("compute")]
[numthreads(CURSOR_CELL_GROUP, 1, 1)]
void ComputeDebugWorldGridCursorCell(uint3 localID : SV_GroupThreadID)
{
    uint lane = localID.x;
    if (lane == 0u) { pc.readback->wgCursorValid = 0u; }
    if (any(pc.cursorPixel >= pc.renderExtent)) { return; }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pc.cursorPixel) + 0.5) / float2(pc.renderExtent);
    float depth = rdgTexturesFloat[pc.depthTextureIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
    if (depth <= 0.0) { return; }
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 worldPos = mul(scene.invView, float4(viewPos, 1.0)).xyz;
    float3 camPos = scene.cameraWorldPos.xyz;

    WorldGridKey key = WorldGridResolveCell(worldPos, camPos);
    uint flatIndex = key.cellIndex;
    uint level = flatIndex / WORLD_GRID_CELLS_PER_CASCADE;
    uint local = flatIndex % WORLD_GRID_CELLS_PER_CASCADE;
    uint3 cell = uint3(local % WORLD_GRID_RES, (local / WORLD_GRID_RES) % WORLD_GRID_RES, local / (WORLD_GRID_RES * WORLD_GRID_RES));
    float cellSize = WorldGridCellSizeAtLevel(level);
    float3 center = WorldGridCellCenter(level, cell, camPos);
    float3 aabbMin = center - cellSize * 0.5;
    float3 aabbMax = center + cellSize * 0.5;

    // Same membership tests as world_grid_binning.slang, over every light instead of the kept K
    int analyticCount = pc.lightData->analyticLightCount;
    uint analyticInRange = 0u;
    for (int i = int(lane); i < analyticCount; i += int(CURSOR_CELL_GROUP)) {
        LightInfo light = pc.lightData->lights[i];
        if (SqDistPointAABB(light.position.xyz, aabbMin, aabbMax) <= light.range * light.range) { analyticInRange++; }
    }
    analyticInRange = ReduceCount(lane, analyticInRange);

    int meshletCount = pc.lightData->emissiveMeshletCount;
    uint meshletInRange = 0u;
    for (int g = int(lane); g < meshletCount; g += int(CURSOR_CELL_GROUP)) {
        EmissiveMeshlet meshlet = pc.lightData->emissiveMeshlets[g];
        if (!(any(meshlet.aabbMin > aabbMax) || any(meshlet.aabbMax < aabbMin))) { meshletInRange++; }
    }
    meshletInRange = ReduceCount(lane, meshletInRange);

    if (lane != 0u) { return; }

    uint2 kept = pc.worldGridBuffer[flatIndex];
    uint2 keptMeshlets = pc.worldGridEmissiveGrid[flatIndex];
    float2 cellPower = pc.worldGridCellPower[flatIndex];

    pc.readback->wgCursorLevel = level;
    pc.readback->wgCursorCell[0] = cell.x;
    pc.readback->wgCursorCell[1] = cell.y;
    pc.readback->wgCursorCell[2] = cell.z;
    pc.readback->wgCursorFlatIndex = flatIndex;
    pc.readback->wgCursorAabbMin[0] = aabbMin.x;
    pc.readback->wgCursorAabbMin[1] = aabbMin.y;
    pc.readback->wgCursorAabbMin[2] = aabbMin.z;
    pc.readback->wgCursorAabbMax[0] = aabbMax.x;
    pc.readback->wgCursorAabbMax[1] = aabbMax.y;
    pc.readback->wgCursorAabbMax[2] = aabbMax.z;
    pc.readback->wgCursorAnalyticKept = kept.y;
    pc.readback->wgCursorAnalyticInRange = analyticInRange;
    pc.readback->wgCursorAnalyticPower = cellPower.x;
    pc.readback->wgCursorMeshletKept = keptMeshlets.y;
    pc.readback->wgCursorMeshletInRange = meshletInRange;
    pc.readback->wgCursorMeshletPower = cellPower.y;

    // Strict > so equal-power ties keep list order, the same rule the bin's eviction uses
    UsedMask usedMask;
    usedMask.Clear();
    for (uint k = 0u; k < CURSOR_CELL_TOP; k++) {
        uint best = ~0u;
        float bestPower = -1.0;
        for (uint s = 0u; s < kept.y; s++) {
            if (usedMask.Get(s)) { continue; }
            float power = WorldGridLightPower(pc.lightData->lights[pc.worldGridIndexList[kept.x + s]]);
            if (power > bestPower) { bestPower = power; best = s; }
        }
        uint idx = ~0u;
        uint type = 0u;
        float range = 0.0;
        float3 pos = float3(0.0);
        if (best != ~0u) {
            usedMask.Set(best);
            idx = pc.worldGridIndexList[kept.x + best];
            LightInfo light = pc.lightData->lights[idx];
            type = light.type;
            range = light.range;
            pos = light.position.xyz;
        }
        pc.readback->wgCursorTopLightIdx[k] = idx;
        pc.readback->wgCursorTopLightType[k] = type;
        pc.readback->wgCursorTopLightPower[k] = max(bestPower, 0.0);
        pc.readback->wgCursorTopLightRange[k] = range;
        pc.readback->wgCursorTopLightPos[k * 3u + 0u] = pos.x;
        pc.readback->wgCursorTopLightPos[k * 3u + 1u] = pos.y;
        pc.readback->wgCursorTopLightPos[k * 3u + 2u] = pos.z;
    }

    usedMask.Clear();
    for (uint k = 0u; k < CURSOR_CELL_TOP; k++) {
        uint best = ~0u;
        float bestPower = -1.0;
        for (uint s = 0u; s < keptMeshlets.y; s++) {
            if (usedMask.Get(s)) { continue; }
            float power = pc.lightData->emissiveMeshlets[pc.worldGridEmissiveIndexList[keptMeshlets.x + s]].power;
            if (power > bestPower) { bestPower = power; best = s; }
        }
        uint idx = ~0u;
        uint lightCount = 0u;
        float3 meshletCenter = float3(0.0);
        if (best != ~0u) {
            usedMask.Set(best);
            idx = pc.worldGridEmissiveIndexList[keptMeshlets.x + best];
            EmissiveMeshlet meshlet = pc.lightData->emissiveMeshlets[idx];
            lightCount = meshlet.lightCount;
            meshletCenter = (meshlet.aabbMin + meshlet.aabbMax) * 0.5;
        }
        pc.readback->wgCursorTopMeshletIdx[k] = idx;
        pc.readback->wgCursorTopMeshletLightCount[k] = lightCount;
        pc.readback->wgCursorTopMeshletPower[k] = max(bestPower, 0.0);
        pc.readback->wgCursorTopMeshletCenter[k * 3u + 0u] = meshletCenter.x;
        pc.readback->wgCursorTopMeshletCenter[k * 3u + 1u] = meshletCenter.y;
        pc.readback->wgCursorTopMeshletCenter[k * 3u + 2u] = meshletCenter.z;
    }
    pc.readback->wgCursorValid = 1u;
}
