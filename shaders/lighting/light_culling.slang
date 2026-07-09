import push_constant_interop;
import common_interop;
import lights_interop;
import cluster_common;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeLightCulling(uniform LightCullingPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    uint clusterIndex = threadID.x;
    if (clusterIndex >= CLUSTER_COUNT) { return; }

    uint cx = clusterIndex % CLUSTER_GRID_X;
    uint cy = (clusterIndex / CLUSTER_GRID_X) % CLUSTER_GRID_Y;
    uint cz = clusterIndex / (CLUSTER_GRID_X * CLUSTER_GRID_Y);

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float3 aabbMin;
    float3 aabbMax;
    ClusterViewAABB(uint3(cx, cy, cz), scene.ndcToViewMul, scene.ndcToViewAdd, pc.zNear, pc.zFar, aabbMin, aabbMax);

    uint indexOffset = clusterIndex * MAX_LIGHTS_PER_CLUSTER;
    uint count = 0u;

    // [0, analyticLightCount) are the analytic (area/sphere) lights this pass supports. [analyticLightCount, lightCount) are emissive triangles.
    // Triangle lights are unsupported by the punctual path clusters feed, so the loop stops before them entirely.
    int analyticCount = pc.lightData->analyticLightCount;
    for (int i = 0; i < analyticCount; i++)
    {
        LightInfo light = pc.lightData->lights[i];
        float3 centerVS = mul(scene.view, float4(light.position.xyz, 1.0)).xyz;
        float radius = light.range;
        if (SqDistPointAABB(centerVS, aabbMin, aabbMax) <= radius * radius)
        {
            pc.lightIndexList[indexOffset + count] = uint(i);
            count++;
            if (count >= MAX_LIGHTS_PER_CLUSTER) { break; }
        }
    }

    pc.lightGrid[clusterIndex] = uint2(indexOffset, count);
}
