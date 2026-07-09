module cluster_common;

import lights_interop;
import shader_functions;

public uint ClusterZSlice(float viewDepth, float zNear, float zFar)
{
    float numSlices = float(CLUSTER_GRID_Z);
    float slice = log(max(viewDepth, zNear) / zNear) * numSlices / log(zFar / zNear);
    return uint(clamp(slice, 0.0, numSlices - 1.0));
}

public uint ClusterIndexFromPixel(uint2 pixel, uint2 renderExtent, float viewDepth, float zNear, float zFar)
{
    uint cx = min(uint(pixel.x * CLUSTER_GRID_X / max(renderExtent.x, 1u)), CLUSTER_GRID_X - 1u);
    uint cy = min(uint(pixel.y * CLUSTER_GRID_Y / max(renderExtent.y, 1u)), CLUSTER_GRID_Y - 1u);
    uint cz = ClusterZSlice(viewDepth, zNear, zFar);
    return cx + CLUSTER_GRID_X * (cy + CLUSTER_GRID_Y * cz);
}

// Reconstructed via the engine's own uv/depth unprojection so froxel bounds match shading's cluster assignment.
public void ClusterViewAABB(uint3 c, float2 ndcToViewMul, float2 ndcToViewAdd, float zNear, float zFar, out float3 aabbMin, out float3 aabbMax)
{
    float numSlices = float(CLUSTER_GRID_Z);
    float zn = zNear * pow(zFar / zNear, float(c.z) / numSlices);
    float zf = zNear * pow(zFar / zNear, float(c.z + 1u) / numSlices);

    float2 uvMin = float2(c.xy) / float2(CLUSTER_GRID_X, CLUSTER_GRID_Y);
    float2 uvMax = float2(c.xy + uint2(1u, 1u)) / float2(CLUSTER_GRID_X, CLUSTER_GRID_Y);

    aabbMin = float3(1e30);
    aabbMax = float3(-1e30);
    [unroll]
    for (uint i = 0u; i < 4u; i++)
    {
        float2 uv = float2((i & 1u) != 0u ? uvMax.x : uvMin.x, (i & 2u) != 0u ? uvMax.y : uvMin.y);
        float3 pNear = CheapReconstructViewSpacePosition(ndcToViewMul, ndcToViewAdd, uv, zn);
        float3 pFar = CheapReconstructViewSpacePosition(ndcToViewMul, ndcToViewAdd, uv, zf);
        aabbMin = min(aabbMin, min(pNear, pFar));
        aabbMax = max(aabbMax, max(pNear, pFar));
    }
}

public float SqDistPointAABB(float3 p, float3 mn, float3 mx)
{
    float3 d = max(max(mn - p, p - mx), float3(0.0));
    return dot(d, d);
}

public struct ClusterPunctualLights
{
    public float4x4 view;
    public uint2* grid;
    public uint* indexList;
    public uint clusterIndex;
};
