import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import limits_declarations;
import bindless_declarations;

// =============== Instancing Step 1: Determine Instance Visibility and LOD ===============
[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
void ComputeInstanceLOD(uniform InstanceLODPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint instanceID = dtid.x;
    if (instanceID >= pc.instanceCount) {
        return;
    }

    Instance inst = pc.instanceBuffer[instanceID];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model model = pc.modelBuffer[inst.modelIndex];

    float4 boundingSphere = prim.boundingSphere;
    float3 scale = float3(
        length(model.modelMatrix[0].xyz),
        length(model.modelMatrix[1].xyz),
        length(model.modelMatrix[2].xyz)
    );
    float maxScale = max(max(scale.x, scale.y), scale.z);
    float worldRadius = boundingSphere.w * maxScale;
    float3 worldCenter = mul(model.modelMatrix, float4(boundingSphere.xyz, 1.0)).xyz;
    float4 worldBounds = float4(worldCenter, worldRadius);

    bool bVisible = FrustumCullSphere(pc.sceneData[pc.sceneDataIndex].frustum, worldBounds);
    // todo: hiz occlusion culling
    if (!bVisible) {
        pc.instanceMeshletOffsets[instanceID].count = 0;
        pc.instanceMeshletOffsets[instanceID].lod = LOD_CULLED;
        pc.instanceMeshletOffsets[instanceID].primitiveIndex = inst.primitiveIndex;
        return;
    }

    float3 cameraPos = pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz;
    float distance = max(length(worldCenter - cameraPos), worldRadius);
    float screenSize = (worldRadius / distance) * pc.sceneData[pc.sceneDataIndex].lodScreenSizeScale;


    uint lod = 0;
    if (screenSize < 10.0) lod = 3;
    else if (screenSize < 50.0) lod = 2;
    else if (screenSize < 150.0) lod = 1;
    else lod = 0;

    lod = min(lod + pc.lodBias, LOD_COUNT - 1);

    pc.instanceMeshletOffsets[instanceID].count = prim.meshletCount[lod];
    pc.instanceMeshletOffsets[instanceID].lod = lod;
    pc.instanceMeshletOffsets[instanceID].primitiveIndex = inst.primitiveIndex;
}
