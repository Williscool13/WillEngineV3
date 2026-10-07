import common_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;
import constants_interop;
import flags_interop;
import shadow_interop;

import shader_functions;
import instancing_common;

// Elements are cascade-major: element = cascade * instanceCount + instance.
[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
void ComputeCSMInstanceCull(uniform CSMInstanceCullPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint element = dtid.x;
    if (element >= pc.instanceCount * pc.cascadeCount) {
        return;
    }
    uint instanceID = element % pc.instanceCount;
    uint cascadeIndex = element / pc.instanceCount;

    uint count = 0;
    uint lod = LOD_CULLED;
    Instance inst = pc.instanceBuffer[instanceID];
    if (inst.primitiveIndex != DEAD_SLOT_PRIMITIVE_INDEX) {
        Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
        Model model = pc.modelBuffer[inst.modelIndex];
        float4 worldBounds = ComputeInstanceWorldBounds(prim, model);
        CSMCascadeGPU cascade = pc.csmData->cascades[cascadeIndex];
        bool bVisible = IsSphereInFrustum(cascade.frustum, worldBounds) && 2.0 * worldBounds.w >= cascade.texelWorldSize;
        if (bVisible) {
            lod = SelectInstanceLOD(worldBounds, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, pc.sceneData[pc.sceneDataIndex].lodScreenSizeScale, pc.lodBias);
            count = prim.meshletCount[lod];
        }
    }

    pc.instanceMeshletOffsets[element].count = count;
    pc.instanceMeshletOffsets[element].lod = lod;
    pc.instanceMeshletOffsets[element].primitiveIndex = inst.primitiveIndex;
}

// meshletIndexWithinLOD packs the cascade into bits 28..29 below the LOD bits.
[shader("compute")]
[numthreads(INSTANCING_EXPAND_DISPATCH_X, 1, 1)]
void ComputeCSMExpandMeshlets(uniform CSMExpandMeshletsPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint globalMeshletIndex = dtid.x;
    if (globalMeshletIndex >= pc.indirectDispatchBuffer->totalMeshlets) {
        return;
    }

    uint left = 0;
    uint right = pc.elementCount - 1;
    uint element = 0;
    while (left <= right) {
        uint mid = (left + right) / 2;
        uint offset = pc.instanceMeshletOffsets[mid].offset;
        uint endOffset = offset + pc.instanceMeshletOffsets[mid].count;
        if (globalMeshletIndex >= offset && globalMeshletIndex < endOffset) {
            element = mid;
            break;
        }
        else if (globalMeshletIndex < offset) {
            right = mid - 1;
        }
        else {
            left = mid + 1;
        }
    }

    uint instanceIndex = element % pc.instanceCount;
    uint cascadeIndex = element / pc.instanceCount;
    uint meshletIndexWithinLOD = globalMeshletIndex - pc.instanceMeshletOffsets[element].offset;
    uint lod = pc.instanceMeshletOffsets[element].lod;

    Instance inst = pc.instanceBuffer[instanceIndex];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model model = pc.modelBuffer[inst.modelIndex];
    MaterialProperties material = pc.materialBuffer[inst.materialIndex];
    uint alphaMode = uint(material.alphaProperties.y);
    bool bTwoSided = material.alphaProperties.z != 0.0;

    Meshlet mlet = pc.meshletBuffer[prim.meshletOffset[lod] + meshletIndexWithinLOD];
    float4 worldBounds = ComputeMeshletWorldBounds(mlet, model);
    CSMCascadeGPU cascade = pc.csmData->cascades[cascadeIndex];

    bool bVisible = IsSphereInFrustum(cascade.frustum, worldBounds) && 2.0 * worldBounds.w >= cascade.texelWorldSize && alphaMode != MATERIAL_ALPHA_BLEND;
    bool bCutout = alphaMode == MATERIAL_ALPHA_CUTOUT && (inst.flags & INSTANCE_FLAG_ALPHA_CUTOUT) != 0u;
    uint region = (bCutout ? MESHLET_REGION_CUTOUT : MESHLET_REGION_OPAQUE) | (bTwoSided ? 1u : 0u);

    uint packedInstanceIndex = (instanceIndex & 0x1FFFFFFF) | (region << 29) | (bVisible ? 0x80000000 : 0);
    uint packedLocalMeshlet = (meshletIndexWithinLOD & 0x0FFFFFFF) | (cascadeIndex << 28) | (lod << 30);

    if (globalMeshletIndex < pc.currentFrameBufferMeshletLimit) {
        pc.intermediateMeshlets[globalMeshletIndex].instanceIndex = packedInstanceIndex;
        pc.intermediateMeshlets[globalMeshletIndex].meshletIndexWithinLOD = packedLocalMeshlet;
    }
}
