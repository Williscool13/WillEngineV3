import common_interop;
import model_interop;
import push_constant_interop;
import gpu_debug_draw;
import lights_interop;
import world_grid_interop;
import binning_common;
import packing_functions;

// =============== Shader Summary ============
// GPU debug draw support: converts the appended segment count into mesh-task indirect args, plus a test pattern producer (reference for real producers).

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeGPUDebugBuildIndirect(uniform GPUDebugBuildIndirectPushConstant pc)
{
    uint count = min(pc.args->segmentCount, pc.args->capacity);
    pc.args->segmentCount = count;
    pc.args->groupCountX = (count + 31) / 32;
    pc.args->groupCountY = 1;
    pc.args->groupCountZ = 1;

    pc.sphereArgs->instanceCount = min(pc.sphereArgs->instanceCount, pc.sphereArgs->capacity);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeGPUDebugTestPattern(uint3 dtid: SV_DispatchThreadID, uniform GPUDebugTestPatternPushConstant pc)
{
    float phase = float(pc.frameIndex) * 0.03 + float(dtid.x + dtid.y) * 0.4;
    float3 center = float3((float(dtid.x) - 3.5) * 3.0, 2.0 + sin(phase), (float(dtid.y) - 3.5) * 3.0);
    float4 color = float4(float(dtid.x) / 7.0, float(dtid.y) / 7.0, 1.0 - float(dtid.x) / 7.0, 1.0);
    GPUDebugAddAABB(pc.args, pc.segmentBuffer, center - 0.5, center + 0.5, 0.02, color);

    float3 axis = normalize(float3(cos(phase), 0.4, sin(phase))) * 0.5;
    GPUDebugAddSphere(pc.sphereArgs, pc.sphereBuffer, center + float3(0.0, 1.6, 0.0), 0.35, float4(0.5, axis), float4(0.5, -axis), float4(0.2, 0.0, 0.0, 0.0));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeGPUDebugClusterGrid(uint3 tid: SV_DispatchThreadID, uniform ClusterGridDebugPushConstant pc)
{
    uint clusterIndex = tid.x;
    if (clusterIndex >= CLUSTER_COUNT) { return; }

    uint cx = clusterIndex % CLUSTER_GRID_X;
    uint cy = (clusterIndex / CLUSTER_GRID_X) % CLUSTER_GRID_Y;
    uint cz = clusterIndex / (CLUSTER_GRID_X * CLUSTER_GRID_Y);

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float3 mn;
    float3 mx;
    ClusterViewAABB(uint3(cx, cy, cz), scene.ndcToViewMul, scene.ndcToViewAdd, pc.zNear, pc.zFar, mn, mx);

    float3 centerVS = 0.5 * (mn + mx);
    float3 he = 0.5 * (mx - mn);
    float3 centerWS = mul(scene.invView, float4(centerVS, 1.0)).xyz;
    float3 ax = mul((float3x3)scene.invView, float3(he.x, 0.0, 0.0));
    float3 ay = mul((float3x3)scene.invView, float3(0.0, he.y, 0.0));
    float3 az = mul((float3x3)scene.invView, float3(0.0, 0.0, he.z));

    float t = float(cz) / float(CLUSTER_GRID_Z - 1u);
    float4 color = float4(t, 1.0 - t, 0.5, 1.0);
    GPUDebugAddOBB(pc.args, pc.segmentBuffer, centerWS, ax, ay, az, 0.02, color);
}

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
