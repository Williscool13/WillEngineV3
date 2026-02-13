import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import limits_declarations;
import bindless_declarations;

// =============== Pass 0: Determine Instance LOD ===============
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeInstanceLOD(uniform InstanceLODPushConstant pc, uint3 dtid : SV_DispatchThreadID)
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

// =============== Pass 1: Upsweep Level 1 ===============
groupshared uint blockSums1[8];


[shader("compute")]
[numthreads(256, 1, 1)]
void ComputePrefixSumUpsweep1(uniform PrefixSumUpsweep1PushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;
    uint threadIdx = gtid.x;

    uint value = 0;
    if (globalIdx < pc.elementCount) {
        value = pc.instanceMeshletOffsets[globalIdx].count;
    }

    // Wave prefix sum
    uint localScan = WavePrefixSum(value);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (256 + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    // Last lane writes wave total
    if (waveIdx == WaveGetLaneCount() - 1) {
        blockSums1[waveID] = localScan + value;
    }

    GroupMemoryBarrierWithGroupSync();

    // Thread 0 scans wave totals
    if (threadIdx == 0) {
        uint runningSum = 0;
        for (uint i = 0; i < numWaves; i++) {
            uint temp = blockSums1[i];
            blockSums1[i] = runningSum;
            runningSum += temp;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Write local prefix sum
    uint blockLocalScan = localScan + blockSums1[waveID];
    if (globalIdx < pc.elementCount) {
        pc.level1Sums[globalIdx] = blockLocalScan;
    }

    // Last thread writes block total
    if (threadIdx == 255 && gid.x < pc.blockCount) {
        pc.level1BlockSums[gid.x] = blockLocalScan + value;
    }
}

// =============== Pass 2: Upsweep Level 2 (optional) ===============
groupshared uint blockSums2[8];

[shader("compute")]
[numthreads(256, 1, 1)]
void ComputePrefixSumUpsweep2(uniform PrefixSumUpsweep2PushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;
    uint threadIdx = gtid.x;

    uint value = 0;
    if (globalIdx < pc.elementCount) {
        value = pc.level1BlockSums[globalIdx];
    }

    uint localScan = WavePrefixSum(value);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (256 + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    if (waveIdx == WaveGetLaneCount() - 1) {
        blockSums2[waveID] = localScan + value;
    }

    GroupMemoryBarrierWithGroupSync();

    if (threadIdx == 0) {
        uint runningSum = 0;
        for (uint i = 0; i < numWaves; i++) {
            uint temp = blockSums2[i];
            blockSums2[i] = runningSum;
            runningSum += temp;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    uint blockLocalScan = localScan + blockSums2[waveID];
    if (globalIdx < pc.elementCount) {
        pc.level2Sums[globalIdx] = blockLocalScan;
    }

    if (threadIdx == 255 && gid.x < pc.blockCount) {
        pc.level2BlockSums[gid.x] = blockLocalScan + value;
    }
}

// =============== Pass 3: Scan Block Sums ===============
groupshared uint scanBlockSums[8];

[shader("compute")]
[numthreads(256, 1, 1)]
void ComputePrefixSumScanBlocks(uniform PrefixSumScanBlocksPushConstant pc, uint3 gtid : SV_GroupThreadID)
{
    uint threadIdx = gtid.x;

    uint value = 0;
    if (threadIdx < pc.blockCount) {
        value = pc.level2BlockSums[threadIdx];
    }

    // Wave prefix sum
    uint localScan = WavePrefixSum(value);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (256 + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    // Last lane writes wave total
    if (waveIdx == WaveGetLaneCount() - 1) {
        scanBlockSums[waveID] = localScan + value;
    }

    GroupMemoryBarrierWithGroupSync();

    // Thread 0 scans wave totals
    if (threadIdx == 0) {
        uint runningSum = 0;
        for (uint i = 0; i < numWaves; i++) {
            uint temp = scanBlockSums[i];
            scanBlockSums[i] = runningSum;
            runningSum += temp;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Write final scanned value
    uint blockLocalScan = localScan + scanBlockSums[waveID];
    if (threadIdx < pc.blockCount) {
        pc.scannedLevel2BlockSums[threadIdx] = blockLocalScan;
    }
}


// =============== Pass 4: Downsweep Level 1 (Optional) ===============
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputePrefixSumDownsweep1(uniform PrefixSumDownsweep1PushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    if (globalIdx >= pc.elementCount) {
        return;
    }

    uint blockOffset = pc.scannedLevel2BlockSums[gid.x];
    pc.level2Sums[globalIdx] += blockOffset;
}


// =============== Pass 5: Downsweep Level 2 ===============
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputePrefixSumDownsweep2(uniform PrefixSumDownsweep2PushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    if (globalIdx >= pc.elementCount) {
        return;
    }

    uint blockIdx = globalIdx / 256;
    uint blockOffset = pc.level2Sums[blockIdx];
    pc.instanceMeshletOffsets[globalIdx].offset = pc.level1Sums[globalIdx] + blockOffset;
}


// =============== Pass 6: Expand Instance -> Meshlet w/ Prefix Sum Values ===============
[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeTotalMeshletCount(uniform TotalMeshletCountPushConstant pc)
{
    uint lastInstanceIndex = pc.instanceCount - 1;
    uint offset = pc.instanceMeshletOffsets[lastInstanceIndex].offset;
    uint count = pc.instanceMeshletOffsets[lastInstanceIndex].count;
    uint totalMeshlets = offset + count;

    pc.indirectDispatchBuffer.x = (totalMeshlets + 255) / 256;
    pc.indirectDispatchBuffer.y = 1;
    pc.indirectDispatchBuffer.z = 1;
    pc.indirectDispatchBuffer.totalMeshlets = totalMeshlets;
}

[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeExpandInstancesToMeshlets(uniform ExpandMeshletsPushConstant pc, uint3 dtid : SV_DispatchThreadID)
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
    uint localMeshletIndex = globalMeshletIndex - meshletOffset;
    Instance inst = pc.instanceBuffer[instanceIndex];
    MeshletPrimitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model model = pc.modelBuffer[inst.modelIndex];

    uint lod = pc.instanceMeshletOffsets[instanceIndex].lod;
    Meshlet mlet = pc.meshletBuffer[prim.meshletOffset[lod] + localMeshletIndex];

    float3 scale = float3(
        length(model.modelMatrix[0].xyz),
        length(model.modelMatrix[1].xyz),
        length(model.modelMatrix[2].xyz)
    );
    float maxScale = max(max(scale.x, scale.y), scale.z);
    float worldRadius = mlet.meshletBoundingSphere.w * maxScale;
    float3 worldCenter = mul(model.modelMatrix, float4(mlet.meshletBoundingSphere.xyz, 1.0)).xyz;
    float4 worldBounds = float4(worldCenter, worldRadius);

    bool bVisible = true;
    if (!FrustumCullSphere(pc.sceneData[pc.sceneDataIndex].frustum, worldBounds)) {
        bVisible = false;
    }
    if (bVisible && !BackfaceCullCone(mlet.coneAxis, mlet.coneCutoff, worldCenter, worldRadius, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, (float3x3)model.modelMatrix)) {
        bVisible = false;
    }

    // Instance greatest bit is visibility,
    uint packedInstanceIndex = (instanceIndex & 0x7FFFFFFF) | (bVisible ? 0x80000000 : 0);
    // Meshlet 2 greatest bit indicates which LOD to use,
    uint packedLocalMeshlet = (localMeshletIndex & 0x3FFFFFFF) | (lod << 30);

    pc.intermediateMeshlets[globalMeshletIndex].instanceIndex = packedInstanceIndex;
    pc.intermediateMeshlets[globalMeshletIndex].localMeshletIndex = packedLocalMeshlet;
}

// =============== Meshlet Visibility Pass 1: Upsweep Level 1 ===============
groupshared uint meshletBlockSums1[8];

[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeMeshletVisibilityPrefixSumUpsweep1(uniform MeshletVisibilityPrefixSumUpsweep1PushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;
    uint threadIdx = gtid.x;

    uint value = 0;
    if (globalIdx < pc.indirectDispatchBuffer->totalMeshlets) {
        value = bool((pc.intermediateMeshlets[globalIdx].instanceIndex >> 31) & 1);
    }

    // Wave prefix sum
    uint localScan = WavePrefixSum(value);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (256 + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    // Last lane writes wave total
    if (waveIdx == WaveGetLaneCount() - 1) {
        meshletBlockSums1[waveID] = localScan + value;
    }

    GroupMemoryBarrierWithGroupSync();

    // Thread 0 scans wave totals
    if (threadIdx == 0) {
        uint runningSum = 0;
        for (uint i = 0; i < numWaves; i++) {
            uint temp = meshletBlockSums1[i];
            meshletBlockSums1[i] = runningSum;
            runningSum += temp;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Write local prefix sum
    uint blockLocalScan = localScan + meshletBlockSums1[waveID];
    if (globalIdx < pc.indirectDispatchBuffer->totalMeshlets) {
        pc.meshletLevel1Sums[globalIdx] = blockLocalScan;
    }

    // Last thread writes block total
    if (threadIdx == 255 && gid.x < pc.blockCount) {
        pc.meshletLevel1BlockSums[gid.x] = blockLocalScan + value;
    }
}

// =============== Meshlet Visibility Pass 5: Downsweep Level 2 ===============
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeMeshletVisibilityPrefixSumDownsweep2(uniform MeshletVisibilityPrefixSumDownsweep2PushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    if (globalIdx >= pc.indirectDispatchBuffer->totalMeshlets) {
        return;
    }

    uint blockIdx = globalIdx / 256;
    uint blockOffset = pc.meshletLevel2Sums[blockIdx];
    uint compactOffset = pc.meshletLevel1Sums[globalIdx] + blockOffset;

    uint packedInstance = pc.intermediateMeshlets[globalIdx].instanceIndex;
    bool visible = bool((packedInstance >> 31) & 1);

    if (visible) {
        pc.visibleMeshlets[compactOffset].instanceIndex = packedInstance & 0x7FFFFFFF;
        pc.visibleMeshlets[compactOffset].localMeshletIndex = pc.intermediateMeshlets[globalIdx].localMeshletIndex;
    }

    // Final thread does a postfix sum so we can get total visible meshlets
    if (globalIdx == pc.indirectDispatchBuffer->totalMeshlets - 1) {
        pc.compactedDispatchBuffer->totalVisibleMeshlets = compactOffset + (visible ? 1 : 0);
    }
}

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeCompactedMeshletDispatch(uniform CompactedMeshletDispatchPushConstant pc)
{
    uint totalVisible = pc.compactedDispatchBuffer->totalVisibleMeshlets;

    uint x = min(totalVisible, MAX_MESH_WORK_GROUP_COUNT_X + 1);
    uint y = 1;
    if (totalVisible > MAX_MESH_WORK_GROUP_COUNT_X + 1) {
        y = (totalVisible + MAX_MESH_WORK_GROUP_COUNT_X) / (MAX_MESH_WORK_GROUP_COUNT_X + 1);
    }

    pc.compactedDispatchBuffer->x = x;
    pc.compactedDispatchBuffer->y = y;
    pc.compactedDispatchBuffer->z = 1;
}





















































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
        InterlockedAdd(pc.primitiveCountersBuffer[primitiveRangeIndex].visibleCountPerLOD[lod], 1);
    }
}

[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
void ComputeVisibilityShadows(uniform VisibilityShadowsPushConstant pc, uint3 dtid : SV_DispatchThreadID)
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

    bool bVisible = FrustumCullSphere(pc.shadowData.lightFrustums[pc.cascadeLevel], worldBounds);
    // todo: hiz occlusion culling

    uint lod = 0;
    if (bVisible) {
        // Shadow specific geometry is preferred, but for now use cascade level directly as LOD
        lod = pc.cascadeLevel;
    }

    pc.instanceBuffer[instanceID].bIsVisible = bVisible ? 1u : 0u;
    pc.instanceBuffer[instanceID].lod = lod;

    if (bVisible) {
        uint32_t primitiveRangeIndex = pc.primitiveToPrimitiveRangeMapBuffer[inst.primitiveIndex];
        InterlockedAdd(pc.primitiveCountersBuffer[primitiveRangeIndex].visibleCountPerLOD[lod], 1);
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
        PrimitiveCounters counter = pc.primitiveCountersBuffer[primitiveRangeIndex];
        primitiveOffsets.instanceOffset = counter.visibleCountPerLOD[0] + counter.visibleCountPerLOD[1] + counter.visibleCountPerLOD[2] + counter.visibleCountPerLOD[3];
        primitiveOffsets.commandOffset = min(counter.visibleCountPerLOD[0], 1) + min(counter.visibleCountPerLOD[1], 1) + min(counter.visibleCountPerLOD[2], 1) + min(counter.visibleCountPerLOD[3], 1);
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

        uint32_t totalCommandCount = blockTotal.commandOffset;
        uint32_t totalInstanceCount = blockTotal.instanceOffset;

        pc.indirectCountBuffer.indirectCount = totalCommandCount;
        pc.indirectCountBuffer.totalInstanceCount = totalInstanceCount;
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
//  Input-Rate: Per-Primitive

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

// =============== Compute Shader (Compact Instances and Generate Indirect Commands) ===============
// - Writes visible instances for that LOD to the instance indirection buffer
// - Generates the indirect draw command for that LOD
//  Input Rate: Per-Instance

[shader("compute")]
[numthreads(INSTANCING_CONSTRUCTION_DISPATCH_X, 1, 1)]
void ComputeCompactAndGenerateIndirect(uniform CompactAndIndirectPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint instanceID = dtid.x;
    if (instanceID >= pc.instanceCount) {
        return;
    }

    Instance inst = pc.instanceBuffer[instanceID];
    if (inst.bIsVisible == 0) {
        return;
    }

    uint primitiveRangeIndex = pc.primitiveToPrimitiveRangeMapBuffer[inst.primitiveIndex];
    PrimitiveOffsets primitiveOffsets = pc.prefixSums[primitiveRangeIndex];
    PrimitiveCounters counter = pc.primitiveCountersBuffer[primitiveRangeIndex];
    uint lod = inst.lod;

    // Calculate LOD offset within this primitive's instances
    uint lodOffset = 0;
    for (uint i = 0; i < lod; i++) {
        lodOffset += counter.visibleCountPerLOD[i];
    }

    // Atomically get local index within this LOD
    uint localInstanceIdx = 0;
    uint commandIdx = primitiveOffsets.commandOffset;
    for (uint i = 0; i < lod; i++) {
        commandIdx += (counter.visibleCountPerLOD[i] > 0) ? 1 : 0;
    }

    InterlockedAdd(pc.primitiveCountersBuffer[primitiveRangeIndex].lodInstanceOffset[lod], 1, localInstanceIdx);

    // Write compacted instance
    uint globalInstanceIdx = primitiveOffsets.instanceOffset + lodOffset + localInstanceIdx;
    pc.instanceIndirectionBuffer[globalInstanceIdx] = instanceID;

    // First thread for this command initializes indirect draw params
    if (localInstanceIdx == 0) {
        MeshletPrimitive prim = pc.primitiveBuffer[inst.primitiveIndex];
        uint totalMeshlets = prim.meshletCount[lod];
        uint numChunks = (totalMeshlets + (TASK_SHADER_DISPATCH_X - 1)) / TASK_SHADER_DISPATCH_X;

        uint instanceCount = counter.visibleCountPerLOD[lod];
        uint yDispatches = min(instanceCount, MAX_TASK_WORK_GROUP_COUNT_Y);
        uint zDispatches = (instanceCount + MAX_TASK_WORK_GROUP_COUNT_Y - 1) / MAX_TASK_WORK_GROUP_COUNT_Y;

        pc.indirectBuffer[commandIdx].groupCountX = numChunks;
        pc.indirectBuffer[commandIdx].groupCountY = yDispatches;
        pc.indirectBuffer[commandIdx].groupCountZ = zDispatches;
        pc.indirectBuffer[commandIdx].compactedInstanceStart = primitiveOffsets.instanceOffset + lodOffset;
        pc.indirectBuffer[commandIdx].compactedInstanceCount = instanceCount;
        pc.indirectBuffer[commandIdx].meshletOffset = prim.meshletOffset[lod];
        pc.indirectBuffer[commandIdx].meshletCount = prim.meshletCount[lod];
    }
}