import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import bindless_declarations;

// =============== Compute Shader (Instance Visibility and LOD calculation) ===============
// Evaluates the visibility and LOD of each instance and outputs to a packed visibility buffer
//  Input-Rate: Per-Instance
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
        float distance = max(length(worldCenter - cameraPos), worldRadius);
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

    if (bVisible) {
        uint32_t primitiveRangeIndex = pc.primitiveToPrimitiveRangeMapBuffer[inst.primitiveIndex];
        InterlockedAdd(pc.primitiveInstanceRangeBuffer[primitiveRangeIndex].visibleCountPerLOD[lod], 1);
    }
}

// =============== Compute Shader (Prefix Sum Local) ===============
//
//  Input-Rate: Per-Primitive

// Supports WaveLaneCount >= 32 (assuming dispatch x is 256)
groupshared PrimitiveOffsets subgroupTotals[8];
groupshared PrimitiveOffsets scannedOffsets[8];

[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_LOCAL_DISPATCH_X, 1, 1)]
void ComputePrefixSumLocal(uniform PrefixSumLocalPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID)
{
    uint blockIdx = gid.x;
    uint threadIdx = gtid.x;
    uint primitiveRangeIndex = dtid.x;

    PrimitiveOffsets primitiveOffsets = PrimitiveOffsets(0, 0);
    if (primitiveRangeIndex < pc.primitiveRangeCount) {
        PrimitiveInstanceRange range = pc.primitiveInstanceRangeBuffer[primitiveRangeIndex];
        primitiveOffsets.instanceOffset = range.visibleCountPerLOD[0] + range.visibleCountPerLOD[1] + range.visibleCountPerLOD[2] + range.visibleCountPerLOD[3];
        primitiveOffsets.commandOffset = min(range.visibleCountPerLOD[0], 1) + min(range.visibleCountPerLOD[1], 1) + min(range.visibleCountPerLOD[2], 1) + min(range.visibleCountPerLOD[3], 1);
    }

    // All-Wave Up-Sweep
    uint instanceLocalScan = WavePrefixSum(primitiveOffsets.instanceOffset);
    uint commandLocalScan = WavePrefixSum(primitiveOffsets.commandOffset);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (INSTANCING_PREFIX_SUM_LOCAL_DISPATCH_X + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    // Last lane in wave writes both postfix totals
    if (waveIdx == WaveGetLaneCount() - 1) {
        subgroupTotals[waveID].instanceOffset = instanceLocalScan + primitiveOffsets.instanceOffset;
        subgroupTotals[waveID].commandOffset = commandLocalScan + primitiveOffsets.commandOffset;
    }

    GroupMemoryBarrierWithGroupSync();
    // Thread 0 does single-threaded prefix sum of the wave upsweep
    if (threadIdx == 0) {
        scannedOffsets[0].instanceOffset = 0;
        scannedOffsets[0].commandOffset = 0;
        for (uint i = 1; i < numWaves; i++) {
            scannedOffsets[i].instanceOffset = scannedOffsets[i-1].instanceOffset + subgroupTotals[i-1].instanceOffset;
            scannedOffsets[i].commandOffset = scannedOffsets[i-1].commandOffset + subgroupTotals[i-1].commandOffset;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // Each thread writes prefix sum
    PrimitiveOffsets blockLocalScan;
    blockLocalScan.instanceOffset = instanceLocalScan + scannedOffsets[waveID].instanceOffset;
    blockLocalScan.commandOffset = commandLocalScan + scannedOffsets[waveID].commandOffset;
    if (primitiveRangeIndex < pc.primitiveRangeCount) {
        pc.prefixSums[primitiveRangeIndex] = blockLocalScan;
    }

    // Last thread writes block postfix total
    if (threadIdx == (INSTANCING_PREFIX_SUM_LOCAL_DISPATCH_X - 1)) {
        PrimitiveOffsets blockTotal;
        blockTotal.instanceOffset = blockLocalScan.instanceOffset + primitiveOffsets.instanceOffset;
        blockTotal.commandOffset = blockLocalScan.commandOffset + primitiveOffsets.commandOffset;
        pc.blockSums[blockIdx] = blockTotal;
    }
}

// =============== Compute Shader (Prefix Sum Block Totals) ===============
// Scans the 256 block totals from Sum Local
// Single workgroup, 256 threads

groupshared PrimitiveOffsets sumBlockSubgroupTotals[8];
groupshared PrimitiveOffsets sumBlockScannedOffsets[8];

[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_SUM_BLOCK_DISPATCH_X, 1, 1)]
void ComputePrefixSumBlocks(uniform PrefixSumBlocksPushConstant pc, uint3 gtid : SV_GroupThreadID)
{
    uint threadIdx = gtid.x;
    PrimitiveOffsets value = pc.blockSums[threadIdx];

    // All-Wave Up-Sweep
    uint instanceLocalScan = WavePrefixSum(value.instanceOffset);
    uint commandLocalScan = WavePrefixSum(value.commandOffset);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (INSTANCING_PREFIX_SUM_SUM_BLOCK_DISPATCH_X + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    // Last lane in wave writes both postfix totals
    if (waveIdx == WaveGetLaneCount() - 1) {
        sumBlockSubgroupTotals[waveID].instanceOffset = instanceLocalScan + value.instanceOffset;
        sumBlockSubgroupTotals[waveID].commandOffset = commandLocalScan + value.commandOffset;
    }

    GroupMemoryBarrierWithGroupSync();
    // Thread 0 does single-threaded prefix sum of the wave upsweep
    if (threadIdx == 0) {
        sumBlockScannedOffsets[0].instanceOffset = 0;
        sumBlockScannedOffsets[0].commandOffset = 0;
        for (uint i = 1; i < numWaves; i++) {
            sumBlockScannedOffsets[i].instanceOffset = sumBlockScannedOffsets[i-1].instanceOffset + sumBlockSubgroupTotals[i-1].instanceOffset;
            sumBlockScannedOffsets[i].commandOffset = sumBlockScannedOffsets[i-1].commandOffset + sumBlockSubgroupTotals[i-1].commandOffset;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // Final scanned value
    PrimitiveOffsets scannedValue;
    scannedValue.instanceOffset = instanceLocalScan + sumBlockScannedOffsets[waveID].instanceOffset;
    scannedValue.commandOffset = commandLocalScan + sumBlockScannedOffsets[waveID].commandOffset;
    pc.scannedBlockOffsets[threadIdx] = scannedValue;
}


// =============== Compute Shader (Prefix Sum Add Block Offsets) ===============
// Adds scanned block offsets to local prefix sums to produce final global prefix sums

[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_SCATTER_DISPATCH_X, 1, 1)]
void ComputePrefixSumScatter(uniform PrefixSumScatterPushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint blockIdx = gid.x;
    uint globalIdx = dtid.x;

    if (globalIdx >= pc.primitiveRangeCount) {
        return;
    }

    PrimitiveOffsets blockOffset = pc.scannedBlockOffsets[blockIdx];
    pc.prefixSums[globalIdx].instanceOffset += blockOffset.instanceOffset;
    pc.prefixSums[globalIdx].commandOffset += blockOffset.commandOffset;
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
        compacted.indexInPrimitiveRanges = i;

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

    PrimitiveInstanceRange range = pc.primitiveInstanceRangeBuffer[compacted.indexInPrimitiveRanges];
    uint writeOffset = compacted.lodIndirectionOffsets[lod];
    for (uint i = 0; i < range.count; i++) {
        uint instanceID = range.start + i;
        Instance inst = pc.instanceBuffer[instanceID];

        if (inst.bIsVisible != 0 && inst.lod == lod) {
            pc.instanceIndirectionBuffer[writeOffset++] = instanceID;
        }
    }

    MeshletPrimitive prim = pc.primitiveBuffer[range.primitiveIndex];
    uint totalMeshlets = prim.meshletCount[lod];
    uint numChunks = (totalMeshlets + (TASK_SHADER_DISPATCH_X - 1)) / TASK_SHADER_DISPATCH_X;

    uint indirectIdx = compacted.indirectCommandIndices[lod];
    pc.indirectBuffer[indirectIdx].groupCountX = numChunks;
    pc.indirectBuffer[indirectIdx].groupCountY = compacted.lodCounts[lod];
    pc.indirectBuffer[indirectIdx].groupCountZ = 1;
    pc.indirectBuffer[indirectIdx].compactedInstanceStart = compacted.lodIndirectionOffsets[lod];
    pc.indirectBuffer[indirectIdx].compactedInstanceCount = compacted.lodCounts[lod];
    pc.indirectBuffer[indirectIdx].meshletOffset = prim.meshletOffset[lod];
    pc.indirectBuffer[indirectIdx].meshletCount = prim.meshletCount[lod];
}