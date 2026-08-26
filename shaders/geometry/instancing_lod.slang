import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import limits_declarations;
import bindless_declarations;
import hiz_common;

float4 ComputeInstanceWorldBounds(Primitive prim, Model model)
{
    float3x3 axes = transpose((float3x3)model.modelMatrix);
    float3 scale = float3(length(axes[0]), length(axes[1]), length(axes[2]));
    float maxScale = max(max(scale.x, scale.y), scale.z);
    float worldRadius = prim.boundingSphere.w * maxScale;
    float3 worldCenter = mul(model.modelMatrix, float4(prim.boundingSphere.xyz, 1.0)).xyz;
    return float4(worldCenter, worldRadius);
}

uint SelectInstanceLOD(float4 worldBounds, float3 cameraPos, float lodScreenSizeScale, int lodBias)
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

// =============== Instancing Step 1, phase 1: frustum + contribution + last-frame visibility bit ===============
[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
void ComputeInstanceLOD(uniform InstanceLODPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint instanceID = dtid.x;
    bool bInRange = instanceID < pc.instanceCount;

    uint count = 0;
    uint lod = LOD_CULLED;
    uint primitiveIndex = DEAD_SLOT_PRIMITIVE_INDEX;
    bool bFrustumCulled = false;
    bool bContributionCulled = false;

    if (bInRange) {
        Instance inst = pc.instanceBuffer[instanceID];
        primitiveIndex = inst.primitiveIndex;
        if (inst.primitiveIndex != DEAD_SLOT_PRIMITIVE_INDEX) {
            Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
            Model model = pc.modelBuffer[inst.modelIndex];
            float4 worldBounds = ComputeInstanceWorldBounds(prim, model);

            bFrustumCulled = (pc.cullFlags & CULL_FLAG_INSTANCE_FRUSTUM) != 0u && !IsSphereInFrustum(pc.sceneData[pc.sceneDataIndex].frustum, worldBounds);
            bContributionCulled = !bFrustumCulled && (pc.cullFlags & CULL_FLAG_INSTANCE_CONTRIBUTION) != 0u && IsContributionCulled(worldBounds, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, pc.sceneData[pc.sceneDataIndex].proj[1][1], pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize.y, CONTRIBUTION_CULL_INSTANCE_MIN_PIXELS);
            if (!bFrustumCulled && !bContributionCulled) {
                lod = SelectInstanceLOD(worldBounds, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, pc.sceneData[pc.sceneDataIndex].lodScreenSizeScale, pc.lodBias);

                // Occlusion gate: bit-clear instances keep their LOD but draw nothing in phase 1; the phase-2 pass re-derives them from count == 0
                bool bPrevVisible = true;
                if (pc.visBits != nullptr) {
                    bPrevVisible = ((pc.visBits[instanceID >> 5] >> (instanceID & 31u)) & 1u) != 0u;
                }
                count = bPrevVisible ? prim.meshletCount[lod] : 0;
            }
        }
    }

    if (pc.cullStats != nullptr) {
        uint waveFrustum = WaveActiveCountBits(bFrustumCulled);
        uint waveContribution = WaveActiveCountBits(bContributionCulled);
        if (WaveIsFirstLane()) {
            if (waveFrustum > 0) {
                InterlockedAdd(pc.cullStats[0], waveFrustum);
            }
            if (waveContribution > 0) {
                InterlockedAdd(pc.cullStats[1], waveContribution);
            }
        }
    }

    if (bInRange) {
        pc.instanceMeshletOffsets[instanceID].count = count;
        pc.instanceMeshletOffsets[instanceID].lod = lod;
        pc.instanceMeshletOffsets[instanceID].primitiveIndex = primitiveIndex;
    }
}

// =============== Instancing Step 1, phase 2: frustum + Hi-Z against the phase-1 pyramid ===============
// Emits meshlets only for instances phase 1 skipped (count == 0) that are actually visible.
// Also writes next frame's visibility bits (buffer pre-cleared to 0) and the occluded-instance stat.
[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
void ComputeInstanceLODOcclusion(uniform InstanceLODOcclusionPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint instanceID = dtid.x;
    bool bInRange = instanceID < pc.instanceCount;

    uint newCount = 0;
    uint lod = LOD_CULLED;
    uint primitiveIndex = DEAD_SLOT_PRIMITIVE_INDEX;
    bool bOccluded = false;

    if (bInRange) {
        Instance inst = pc.instanceBuffer[instanceID];
        primitiveIndex = inst.primitiveIndex;
        if (inst.primitiveIndex != DEAD_SLOT_PRIMITIVE_INDEX) {
            Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
            Model model = pc.modelBuffer[inst.modelIndex];
            float4 worldBounds = ComputeInstanceWorldBounds(prim, model);

            bool bFrustumCulled = (pc.cullFlags & CULL_FLAG_INSTANCE_FRUSTUM) != 0u && !IsSphereInFrustum(pc.sceneData[pc.sceneDataIndex].frustum, worldBounds);
            bool bContributionCulled = !bFrustumCulled && (pc.cullFlags & CULL_FLAG_INSTANCE_CONTRIBUTION) != 0u && IsContributionCulled(worldBounds, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, pc.sceneData[pc.sceneDataIndex].proj[1][1], pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize.y, CONTRIBUTION_CULL_INSTANCE_MIN_PIXELS);
            if (!bFrustumCulled && !bContributionCulled) {
                lod = SelectInstanceLOD(worldBounds, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, pc.sceneData[pc.sceneDataIndex].lodScreenSizeScale, pc.lodBias);
                float4x4 mvp = mul(pc.sceneData[pc.sceneDataIndex].viewProj, model.modelMatrix);
                bOccluded = IsOccludedHiZBox(prim.boundingBoxMin, prim.boundingBoxMax, mvp, pc.hizIndex, pc.hizMipCount, pc.hizExtent);
                if (!bOccluded) {
                    InterlockedOr(pc.visBits[instanceID >> 5], 1u << (instanceID & 31u));
                    // Phase-1 count read before the overwrite below: count > 0 means it was already drawn
                    if (pc.instanceMeshletOffsets[instanceID].count == 0) {
                        newCount = prim.meshletCount[lod];
                    }
                }
            }
        }
    }

    if (pc.occludedCounter != nullptr) {
        uint waveOccluded = WaveActiveCountBits(bOccluded);
        if (WaveIsFirstLane() && waveOccluded > 0) {
            InterlockedAdd(pc.occludedCounter[0], waveOccluded);
        }
    }

    if (bInRange) {
        pc.instanceMeshletOffsets[instanceID].count = newCount;
        pc.instanceMeshletOffsets[instanceID].lod = lod;
        pc.instanceMeshletOffsets[instanceID].primitiveIndex = primitiveIndex;
    }
}
