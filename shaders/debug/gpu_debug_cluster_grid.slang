import common_interop;
import model_interop;
import push_constant_interop;
import gpu_debug_draw_common;
import lights_interop;
import world_grid_interop;
import binning_common;
import packing_functions;
import radiance_cache_hash_common;
import shader_functions;
import radiance_cache_interop;

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
