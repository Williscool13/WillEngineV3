module instancing_common;

import model_interop;
import constants_interop;

public float4 ComputeInstanceWorldBounds(Primitive prim, Model model)
{
    float3x3 axes = transpose((float3x3)model.modelMatrix);
    float3 scale = float3(length(axes[0]), length(axes[1]), length(axes[2]));
    float maxScale = max(max(scale.x, scale.y), scale.z);
    float worldRadius = prim.boundingSphere.w * maxScale;
    float3 worldCenter = mul(model.modelMatrix, float4(prim.boundingSphere.xyz, 1.0)).xyz;
    return float4(worldCenter, worldRadius);
}

public float4 ComputeMeshletWorldBounds(Meshlet mlet, Model model)
{
    float3x3 axes = transpose((float3x3)model.modelMatrix);
    float3 scale = float3(length(axes[0]), length(axes[1]), length(axes[2]));
    float maxScale = max(max(scale.x, scale.y), scale.z);
    float worldRadius = mlet.meshletBoundingSphere.w * maxScale;
    float3 worldCenter = mul(model.modelMatrix, float4(mlet.meshletBoundingSphere.xyz, 1.0)).xyz;
    return float4(worldCenter, worldRadius);
}

public uint SelectInstanceLOD(float4 worldBounds, float3 cameraPos, float lodScreenSizeScale, int lodBias)
{
    float distance = max(length(worldBounds.xyz - cameraPos), worldBounds.w);
    float screenSize = (worldBounds.w / distance) * lodScreenSizeScale;

    uint lod = 0;
    if (screenSize < 10.0) {
        lod = 3;
    }
    else if (screenSize < 50.0) {
        lod = 2;
    }
    else if (screenSize < 150.0) {
        lod = 1;
    }
    else {
        lod = 0;
    }

    return min(lod + lodBias, LOD_COUNT - 1);
}
