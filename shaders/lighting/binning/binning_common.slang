module binning_common;

import lights_interop;
import shader_functions;
import packing_functions;
import world_grid_interop;

// FrustumBinning: screen-frustum-tied clusters (froxels)

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

// WorldGridBinning: camera-centered cascaded world-space grid

/** Light power for strongest-K ranking; matches the CPU alias metric (intensity * area * maxColor). Triangles are excluded from the bin. */
public float WorldGridLightPower(LightInfo light)
{
    float area;
    if (light.type == LIGHT_TYPE_SPHERE) {
        float r = light.right.w;
        area = 4.0 * 3.14159265358979 * r * r;
    } else {
        area = 4.0 * light.right.w * light.up.w;
    }
    float3 color = UnpackUnorm4x8(light.packedColor).rgb;
    float colorMax = max(color.r, max(color.g, color.b));
    return light.intensity * area * colorMax;
}

public float WorldGridCellSizeAtLevel(uint level)
{
    // Doubles at every level
    return WORLD_GRID_BASE_CELL_SIZE * exp2(float(level));
}

public float3 WorldGridCascadeOrigin(float3 camPos, uint level)
{
    float cellSize = WorldGridCellSizeAtLevel(level);
    float3 snapped = floor(camPos / cellSize) * cellSize;
    return snapped - (float(WORLD_GRID_RES) * 0.5) * cellSize;
}

public uint WorldGridFlatIndex(uint level, uint3 cell)
{
    return level * WORLD_GRID_CELLS_PER_CASCADE + (cell.z * WORLD_GRID_RES + cell.y) * WORLD_GRID_RES + cell.x;
}

public float3 WorldGridCellCenter(uint level, uint3 cell, float3 camPos)
{
    float cellSize = WorldGridCellSizeAtLevel(level);
    float3 origin = WorldGridCascadeOrigin(camPos, level);
    return origin + (float3(cell) + 0.5) * cellSize;
}

public struct WorldGridKey
{
    public int level;
    public uint cellIndex;
    public bool bValid; // false = beyond the outermost cascade
};

/**
 * Resolves a world position to its cascade + cell.
 * Reserved: decal/probe ids (idTech8 packs 3 id types/cell).
 */
public WorldGridKey WorldGridResolveCell(float3 worldPos, float3 camPos)
{
    for (uint l = 0u; l < WORLD_GRID_CASCADES; l++)
    {
        float cellSize = WorldGridCellSizeAtLevel(l);
        float3 origin = WorldGridCascadeOrigin(camPos, l);
        int3 cell = int3(floor((worldPos - origin) / cellSize));
        if (all(cell >= int3(0)) && all(cell < int3(WORLD_GRID_RES)))
        {
            WorldGridKey key;
            key.level = int(l);
            key.cellIndex = WorldGridFlatIndex(l, uint3(cell));
            key.bValid = true;
            return key;
        }
    }

    // Clamp to farthest cascade
    uint outer = WORLD_GRID_CASCADES - 1u;
    float cellSize = WorldGridCellSizeAtLevel(outer);
    float3 origin = WorldGridCascadeOrigin(camPos, outer);
    int3 cell = clamp(int3(floor((worldPos - origin) / cellSize)), int3(0), int3(WORLD_GRID_RES - 1u));
    WorldGridKey key;
    key.level = int(outer);
    key.cellIndex = WorldGridFlatIndex(outer, uint3(cell));
    key.bValid = false;
    return key;
}

public struct WorldGridPunctualLights
{
    public float4x4 view;
    public float3 camPos;
    public uint2* grid;
    public uint* indexList;
};
