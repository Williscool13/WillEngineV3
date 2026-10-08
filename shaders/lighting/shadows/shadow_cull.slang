import common_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;
import constants_interop;
import flags_interop;
import shadow_interop;

import shader_functions;
import instancing_common;

static float ShadowTexelSize(ShadowViewGPU view, float3 center)
{
    return view.eye.w == 0.0 ? view.texelSize : view.texelSize * distance(center, view.eye.xyz);
}

static bool IsShadowConeFrontFacing(ShadowViewGPU view, Meshlet mlet, float4 worldBounds, Model model)
{
    if (view.eye.w == 0.0) {
        return dot(-view.eye.xyz, normalize(mul((float3x3)model.modelMatrix, mlet.coneAxis))) < mlet.coneCutoff;
    }
    return IsConeFrontFacing(mlet.coneAxis, mlet.coneCutoff, worldBounds.xyz, worldBounds.w, view.eye.xyz, (float3x3)model.modelMatrix);
}

// Elements are view-major: element = view * instanceCount + instance.
[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
void ComputeShadowInstanceCull(uniform ShadowInstanceCullPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint element = dtid.x;
    if (element >= pc.instanceCount * pc.viewCount) {
        return;
    }
    uint instanceID = element % pc.instanceCount;
    uint viewIndex = element / pc.instanceCount;

    uint count = 0;
    uint lod = LOD_CULLED;
    Instance inst = pc.instanceBuffer[instanceID];
    if (inst.primitiveIndex != DEAD_SLOT_PRIMITIVE_INDEX && (inst.flags & pc.requiredInstanceFlags) == pc.requiredInstanceFlags) {
        Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
        Model model = pc.modelBuffer[inst.modelIndex];
        float4 worldBounds = ComputeInstanceWorldBounds(prim, model);
        ShadowViewGPU view = pc.views[viewIndex];
        bool bVisible = IsSphereInFrustum(view.frustum, worldBounds) && 2.0 * worldBounds.w >= ShadowTexelSize(view, worldBounds.xyz);
        if (bVisible) {
            // Perspective views pick LOD from the light so a cached map stays valid as the camera moves.
            lod = view.eye.w == 0.0
                      ? SelectInstanceLOD(worldBounds, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, pc.sceneData[pc.sceneDataIndex].lodScreenSizeScale, pc.lodBias)
                      : SelectInstanceLOD(worldBounds, view.eye.xyz, 1.0 / view.texelSize, pc.lodBias);
            count = prim.meshletCount[lod];
        }
    }

    pc.instanceMeshletOffsets[element].count = count;
    pc.instanceMeshletOffsets[element].lod = lod;
    pc.instanceMeshletOffsets[element].primitiveIndex = inst.primitiveIndex;
}

[shader("compute")]
[numthreads(INSTANCING_EXPAND_DISPATCH_X, 1, 1)]
void ComputeShadowExpandMeshlets(uniform ShadowExpandMeshletsPushConstant pc, uint3 dtid : SV_DispatchThreadID)
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
    uint viewIndex = element / pc.instanceCount;
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
    ShadowViewGPU view = pc.views[viewIndex];

    bool bConeCulled = !bTwoSided && !IsShadowConeFrontFacing(view, mlet, worldBounds, model);
    bool bVisible = !bConeCulled && IsSphereInFrustum(view.frustum, worldBounds) && 2.0 * worldBounds.w >= ShadowTexelSize(view, worldBounds.xyz) && alphaMode != MATERIAL_ALPHA_BLEND;
    bool bCutout = alphaMode == MATERIAL_ALPHA_CUTOUT && (inst.flags & INSTANCE_FLAG_ALPHA_CUTOUT) != 0u;
    uint region = (bCutout ? MESHLET_REGION_CUTOUT : MESHLET_REGION_OPAQUE) | (bTwoSided ? 1u : 0u);

    uint packedInstanceIndex = (instanceIndex & 0x1FFFFFFF) | (region << 29) | (bVisible ? 0x80000000 : 0);
    uint packedLocalMeshlet = (meshletIndexWithinLOD & SHADOW_MESHLET_MASK) | (viewIndex << SHADOW_VIEW_SHIFT) | (lod << SHADOW_LOD_SHIFT);

    if (globalMeshletIndex < pc.currentFrameBufferMeshletLimit) {
        pc.intermediateMeshlets[globalMeshletIndex].instanceIndex = packedInstanceIndex;
        pc.intermediateMeshlets[globalMeshletIndex].meshletIndexWithinLOD = packedLocalMeshlet;
    }
}
