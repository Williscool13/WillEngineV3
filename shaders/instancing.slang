import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import bindless_declarations;

// =============== Compute Shader (Instance Visibility and LOD calculation) ===============
// Evaluates the visibility of each instance and outputs to a packed visibility buffer
[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
void ComputeVisibility(uniform VisibilityPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint instanceID = dtid.x;
    if (instanceID >= pc.instanceCount) {
        return;
    }
    Instance inst = pc.instanceBuffer[instanceID];
    MeshletPrimitive prim = pc.primitiveBuffer[inst.primitiveIndex];
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

    uint lod = 0;
    if (bVisible) {
        // Calculate LOD based on screen space size
        float3 cameraPos = pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz;
        float distance = length(worldCenter - cameraPos);
        float screenSize = (worldRadius / distance) * pc.sceneData[pc.sceneDataIndex].lodScreenSizeScale;

        // LOD thresholds (adjust as needed)
        if (screenSize < 10.0) lod = 3;
        else if (screenSize < 50.0) lod = 2;
        else if (screenSize < 150.0) lod = 1;
        else lod = 0;

        lod = min(lod + pc.lodBias, LOD_COUNT - 1);
    }

    pc.instanceBuffer[instanceID].bIsVisible = bVisible ? 1u : 0u;
    pc.instanceBuffer[instanceID].lod = lod;
}

// =============== Compute Shader (Visible Primitive Count Sum) ===============
// Each thread processes one PrimitiveInstanceRange and counts how many instances are visible by iterating through its instance range
// Writes the visible count back to the primitiveInstanceRangeBuffer's visibleCount field.

[shader("compute")]
[numthreads(INSTANCING_VISIBLE_PRIMITIVE_COUNTER_DISPATCH_X, 1, 1)]
void ComputeVisiblePrimitiveCounter(uniform VisiblePrimitiveCounterPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint primitiveRangeID = dtid.x;
    if (primitiveRangeID >= pc.primitiveRangeCount) {
        return;
    }

    PrimitiveInstanceRange range = pc.primitiveInstanceRangeBuffer[primitiveRangeID];

    uint lodCounts[LOD_COUNT] = {0, 0, 0, 0};
    for (uint i = 0; i < range.count; ++i) {
        Instance inst = pc.instanceBuffer[range.start + i];
        if (inst.bIsVisible != 0) {
            lodCounts[inst.lod]++;
        }
    }

    for (uint lod = 0; lod < LOD_COUNT; lod++) {
        pc.primitiveInstanceRangeBuffer[primitiveRangeID].visibleCountPerLOD[lod] = lodCounts[lod];
    }
}


// =============== Compute Shader (Prefix Sum for Compacted Instance Offsets) ===============
// Single-threaded prefix sum that:
// 1. Culls primitives with no visible instances
// 2. Assigns per-LOD offsets in the instance indirection buffer
// 3. Assigns indices for indirect commands (one per non-empty LOD)
// Input: primitiveInstanceRangeBuffer with visibleCountPerLOD filled by Pass 1.5
// Output: compactedPrimitiveDataBuffer with only non-empty primitives

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputePrefixSum(uniform InstancingPrefixSumPushConstant pc) {
    uint indirectionOffset = 0;
    uint indirectCommandCount = 0;
    uint compactedPrimitiveCount = 0;

    for (uint i = 0; i < pc.primitiveRangeCount; i++) {
        PrimitiveInstanceRange range = pc.primitiveInstanceRangeBuffer[i];

        uint totalVisible = range.visibleCountPerLOD[0] + range.visibleCountPerLOD[1] +range.visibleCountPerLOD[2] + range.visibleCountPerLOD[3];
        if (totalVisible == 0) { continue; }

        CompactedPrimitiveData compacted;
        compacted.primitiveIndex = range.primitiveIndex;

        compacted.lodIndirectionOffsets[0] = indirectionOffset;
        compacted.lodIndirectionOffsets[1] = indirectionOffset + range.visibleCountPerLOD[0];
        compacted.lodIndirectionOffsets[2] = indirectionOffset + range.visibleCountPerLOD[0] + range.visibleCountPerLOD[1];
        compacted.lodIndirectionOffsets[3] = indirectionOffset + range.visibleCountPerLOD[0] + range.visibleCountPerLOD[1] + range.visibleCountPerLOD[2];

        compacted.lodCounts[0] = range.visibleCountPerLOD[0];
        compacted.lodCounts[1] = range.visibleCountPerLOD[1];
        compacted.lodCounts[2] = range.visibleCountPerLOD[2];
        compacted.lodCounts[3] = range.visibleCountPerLOD[3];

        if (range.visibleCountPerLOD[0] > 0) {
            compacted.indirectCommandIndices[0] = indirectCommandCount++;
        } else {
            compacted.indirectCommandIndices[0] = 0xFFFFFFFF;
        }

        if (range.visibleCountPerLOD[1] > 0) {
            compacted.indirectCommandIndices[1] = indirectCommandCount++;
        } else {
            compacted.indirectCommandIndices[1] = 0xFFFFFFFF;
        }

        if (range.visibleCountPerLOD[2] > 0) {
            compacted.indirectCommandIndices[2] = indirectCommandCount++;
        } else {
            compacted.indirectCommandIndices[2] = 0xFFFFFFFF;
        }

        if (range.visibleCountPerLOD[3] > 0) {
            compacted.indirectCommandIndices[3] = indirectCommandCount++;
        } else {
            compacted.indirectCommandIndices[3] = 0xFFFFFFFF;
        }

        pc.compactedPrimitiveDataBuffer[compactedPrimitiveCount++] = compacted;
        indirectionOffset += totalVisible;
    }

    pc.indirectCountBuffer[0].packedPrimitiveCountDispatchX = (compactedPrimitiveCount * LOD_COUNT + INSTANCING_CONSTRUCTION_DISPATCH_X - 1) / INSTANCING_CONSTRUCTION_DISPATCH_X;
    pc.indirectCountBuffer[0].packedPrimitiveCountDispatchY = 1;
    pc.indirectCountBuffer[0].packedPrimitiveCountDispatchZ = 1;
    pc.indirectCountBuffer[0].packedPrimitiveCount = compactedPrimitiveCount;
    pc.indirectCountBuffer[0].indirectCount = indirectCommandCount;

}

// =============== Compute Shader (Compact Instances and Generate Indirect Commands) ===============
// Each thread processes one (primitive, LOD) pair
// - Writes visible instances for that LOD to the instance indirection buffer
// - Generates the indirect draw command for that LOD

[shader("compute")]
[numthreads(INSTANCING_CONSTRUCTION_DISPATCH_X, 1, 1)]
void ComputeCompactAndGenerateIndirect(uniform CompactAndIndirectPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint flatID = dtid.x;
    uint compactedPrimitiveID = flatID / LOD_COUNT;
    uint lod = flatID % LOD_COUNT;

    if (compactedPrimitiveID >= pc.indirectCountBuffer.packedPrimitiveCount) {
        return;
    }

    CompactedPrimitiveData compacted = pc.compactedPrimitiveDataBuffer[compactedPrimitiveID];
    if (compacted.lodCounts[lod] == 0) {
        return;
    }

    PrimitiveInstanceRange range = pc.primitiveInstanceRangeBuffer[compacted.primitiveIndex];
    uint writeOffset = compacted.lodIndirectionOffsets[lod];
    for (uint i = 0; i < range.count; i++) {
        uint instanceID = range.start + i;
        Instance inst = pc.instanceBuffer[instanceID];

        if (inst.bIsVisible != 0 && inst.lod == lod) {
            pc.instanceIndirectionBuffer[writeOffset++] = instanceID;
        }
    }

    MeshletPrimitive prim = pc.primitiveBuffer[compacted.primitiveIndex];
    uint totalMeshlets = prim.meshletCount[lod];
    uint numChunks = (totalMeshlets + (TASK_SHADER_DISPATCH_X - 1)) / TASK_SHADER_DISPATCH_X;

    uint indirectIdx = compacted.indirectCommandIndices[lod];
    pc.indirectBuffer[indirectIdx].groupCountX = numChunks;
    pc.indirectBuffer[indirectIdx].groupCountY = compacted.lodCounts[lod];
    pc.indirectBuffer[indirectIdx].groupCountZ = 1;
    pc.indirectBuffer[indirectIdx].compactedInstanceStart = compacted.lodIndirectionOffsets[lod];
    pc.indirectBuffer[indirectIdx].meshletOffset = prim.meshletOffset[lod];
    pc.indirectBuffer[indirectIdx].meshletCount = prim.meshletCount[lod];
}