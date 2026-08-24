import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import limits_declarations;
import bindless_declarations;
import hiz_common;

// =============== Instancing Step 2f: Gather total number of meshlets from final instance postfix sum ===============
[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeTotalMeshletCount(uniform TotalMeshletCountPushConstant pc)
{
    uint lastInstanceIndex = pc.instanceCount - 1;
    uint offset = pc.instanceMeshletOffsets[lastInstanceIndex].offset;
    uint count = pc.instanceMeshletOffsets[lastInstanceIndex].count;
    uint totalMeshlets = offset + count;

    pc.indirectDispatchBuffer.x = (totalMeshlets + 255) / INSTANCING_PREFIX_SUM_DISPATCH_X;
    pc.indirectDispatchBuffer.y = 1;
    pc.indirectDispatchBuffer.z = 1;
    pc.indirectDispatchBuffer.totalMeshlets = totalMeshlets;
}

// =============== Instancing Step 3: Expand Instance -> Meshlet through Binary Search. Also determine meshlet visibility. ===============
[shader("compute")]
[numthreads(INSTANCING_EXPAND_DISPATCH_X, 1, 1)]
void ComputeExpandInstancesToMeshlets(uniform ExpandMeshletsPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint globalMeshletIndex = dtid.x;
    if (globalMeshletIndex >= pc.indirectDispatchBuffer->totalMeshlets) {
        return;
    }

    // Binary search to find which instance owns this meshlet.
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

    // First cull in test order wins the tally
    bool bFrustumCulled = (pc.cullFlags & CULL_FLAG_MESHLET_FRUSTUM) != 0u && !IsSphereInFrustum(pc.sceneData[pc.sceneDataIndex].frustum, worldBounds);
    bool bConeCulled = !bFrustumCulled && (pc.cullFlags & CULL_FLAG_MESHLET_CONE) != 0u && !IsConeFrontFacing(mlet.coneAxis, mlet.coneCutoff, worldCenter, worldRadius, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, (float3x3)model.modelMatrix);
    bool bContributionCulled = !bFrustumCulled && !bConeCulled && (pc.cullFlags & CULL_FLAG_MESHLET_CONTRIBUTION) != 0u && IsContributionCulled(worldBounds, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, pc.sceneData[pc.sceneDataIndex].proj[1][1], pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize.y, CONTRIBUTION_CULL_MESHLET_MIN_PIXELS);
    bool bOccluded = !bFrustumCulled && !bConeCulled && !bContributionCulled && pc.bHiZ != 0u && IsOccludedHiZ(worldCenter, worldRadius, pc.sceneData[pc.sceneDataIndex].viewProj, pc.hizIndex, pc.hizMipCount, pc.hizExtent);
    bool bVisible = !bFrustumCulled && !bConeCulled && !bContributionCulled && !bOccluded;

    if (pc.cullStats != nullptr) {
        uint waveFrustum = WaveActiveCountBits(bFrustumCulled);
        uint waveCone = WaveActiveCountBits(bConeCulled);
        uint waveContribution = WaveActiveCountBits(bContributionCulled);
        uint waveOccluded = WaveActiveCountBits(bOccluded);
        if (WaveIsFirstLane()) {
            if (waveFrustum > 0) {
                InterlockedAdd(pc.cullStats[0], waveFrustum);
            }
            if (waveCone > 0) {
                InterlockedAdd(pc.cullStats[1], waveCone);
            }
            if (waveContribution > 0) {
                InterlockedAdd(pc.cullStats[2], waveContribution);
            }
            if (waveOccluded > 0) {
                InterlockedAdd(pc.cullStats[3], waveOccluded);
            }
        }
    }

    uint alphaMode = uint(pc.materialBuffer[inst.materialIndex].alphaProperties.y);
    // Blend has no consumer yet (OIT pending); drop before the scan so the partition stays dense
    if (alphaMode == MATERIAL_ALPHA_BLEND) {
        bVisible = false;
    }
    bool bCutout = alphaMode == MATERIAL_ALPHA_CUTOUT;

    // Bit 31 visibility, bit 30 cutout
    uint packedInstanceIndex = (instanceIndex & 0x3FFFFFFF) | (bCutout ? 0x40000000 : 0) | (bVisible ? 0x80000000 : 0);
    // Meshlet 2 greatest bit indicates which LOD to use,
    uint packedLocalMeshlet = (meshletIndexWithinLOD & 0x3FFFFFFF) | (lod << 30);

    if (globalMeshletIndex < pc.currentFrameBufferMeshletLimit) {
        pc.intermediateMeshlets[globalMeshletIndex].instanceIndex = packedInstanceIndex;
        pc.intermediateMeshlets[globalMeshletIndex].meshletIndexWithinLOD = packedLocalMeshlet;
    }
}

// =============== Instancing Step 4f: Generate indirect commands for the opaque and cutout meshlet draws ===============
static void MeshletCountToDispatch(uint count, out uint x, out uint y)
{
    x = min(count, (MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1);
    y = 1;
    if (count > (MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1) {
        y = (count + (MAX_MESH_WORK_GROUP_COUNT_X / 2)) / ((MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1);
    }
}

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeCompactedMeshletDispatch(uniform CompactedMeshletDispatchPushConstant pc)
{
    uint totalVisible = min(pc.compactedDispatchBuffer->totalVisibleMeshlets, pc.currentFrameBufferMeshletLimit);
    uint totalCutout = min(pc.compactedDispatchBuffer->totalCutoutMeshlets, totalVisible);
    uint totalOpaque = totalVisible - totalCutout;

    uint x, y;
    MeshletCountToDispatch(totalOpaque, x, y);
    pc.compactedDispatchBuffer->x = x;
    pc.compactedDispatchBuffer->y = y;
    pc.compactedDispatchBuffer->z = 1;

    uint cx, cy;
    MeshletCountToDispatch(totalCutout, cx, cy);
    pc.compactedDispatchBuffer->cutoutX = cx;
    pc.compactedDispatchBuffer->cutoutY = cy;
    pc.compactedDispatchBuffer->cutoutZ = 1;
    pc.compactedDispatchBuffer->meshletCapacity = pc.currentFrameBufferMeshletLimit;
}

// =============== Instancing Step 4g: Gather total number of visible meshlets from final meshlet postfix sum ===============
[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeMaxMeshletCount(uniform MaxMeshletCountPushConstant pc)
{
    pc.currentHighest[0] = max(pc.currentHighest[0], pc.indirectDispatchBuffer->totalMeshlets);
}
