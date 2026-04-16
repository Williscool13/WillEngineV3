import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import limits_declarations;
import bindless_declarations;

// =============== Step 1: Determine Instance Visibility and LOD (Shadow Pass) ===============
[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
void ComputeInstanceLODShadows(uniform InstanceLODShadowsPushConstant pc, uint3 dtid : SV_DispatchThreadID)
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

    bool bVisible = FrustumCullSphere(pc.shadowData.lightFrustums[pc.cascadeIndex], worldBounds);
    // todo: hiz occlusion culling
    if (!bVisible) {
        pc.instanceMeshletOffsets[instanceID].count = 0;
        pc.instanceMeshletOffsets[instanceID].lod = LOD_CULLED;
        pc.instanceMeshletOffsets[instanceID].primitiveIndex = inst.primitiveIndex;
        return;
    }

    uint lod = pc.cascadeIndex;

    pc.instanceMeshletOffsets[instanceID].count = prim.meshletCount[lod];
    pc.instanceMeshletOffsets[instanceID].lod = lod;
    pc.instanceMeshletOffsets[instanceID].primitiveIndex = inst.primitiveIndex;
}

// =============== Step 3: Expand Instance -> Meshlet through Binary Search. Also determine meshlet visibility. (Shadow Pass) ===============
[shader("compute")]
[numthreads(INSTANCING_EXPAND_DISPATCH_X, 1, 1)]
void ComputeExpandInstancesToMeshletsShadows(uniform ExpandMeshletsShadowsPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint globalMeshletIndex = dtid.x;
    if (globalMeshletIndex >= pc.indirectDispatchBuffer->totalMeshlets) {
        return;
    }

    // Binary search to find which instance owns this meshlet
    uint left = 0;
    uint right = pc.instanceCount - 1;
    uint instanceIndex = 0;

    while (left <= right) {
        uint mid = (left + right) / 2;
        uint offset = pc.instanceMeshletOffsets[mid].offset;
        uint count = pc.instanceMeshletOffsets[mid].count;
        uint endOffset = offset + count;

        if (globalMeshletIndex >= offset && globalMeshletIndex < endOffset) {
            instanceIndex = mid;
            break;
        }
        else if (globalMeshletIndex < offset) {
            right = mid - 1;
        }
        else {
            left = mid + 1;
        }
    }

    // Meshlet Visibility
    uint meshletOffset = pc.instanceMeshletOffsets[instanceIndex].offset;
    uint meshletIndexWithinLOD = globalMeshletIndex - meshletOffset;
    Instance inst = pc.instanceBuffer[instanceIndex];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model model = pc.modelBuffer[inst.modelIndex];

    uint lod = pc.instanceMeshletOffsets[instanceIndex].lod;
    Meshlet mlet = pc.meshletBuffer[prim.meshletOffset[lod] + meshletIndexWithinLOD];

    float3 scale = float3(
        length(model.modelMatrix[0].xyz),
        length(model.modelMatrix[1].xyz),
        length(model.modelMatrix[2].xyz)
    );
    float maxScale = max(max(scale.x, scale.y), scale.z);
    float worldRadius = mlet.meshletBoundingSphere.w * maxScale;
    float3 worldCenter = mul(model.modelMatrix, float4(mlet.meshletBoundingSphere.xyz, 1.0)).xyz;
    float4 worldBounds = float4(worldCenter, worldRadius);

    bool bVisible = FrustumCullSphere(pc.shadowData->lightFrustums[pc.cascadeIndex], worldBounds);

    // Instance greatest bit is visibility,
    uint packedInstanceIndex = (instanceIndex & 0x7FFFFFFF) | (bVisible ? 0x80000000 : 0);
    // Meshlet 2 greatest bit indicates which LOD to use,
    uint packedLocalMeshlet = (meshletIndexWithinLOD & 0x3FFFFFFF) | (lod << 30);

    if (globalMeshletIndex < pc.currentFrameBufferMeshletLimit) {
        pc.intermediateMeshlets[globalMeshletIndex].instanceIndex = packedInstanceIndex;
        pc.intermediateMeshlets[globalMeshletIndex].meshletIndexWithinLOD = packedLocalMeshlet;
    }
}