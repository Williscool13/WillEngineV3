import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import limits_declarations;
import bindless_declarations;

// =============== Step 1: Determine Instance Visibility and LOD ===============
[shader("compute")]
[numthreads(INSTANCING_VISIBILITY_DISPATCH_X, 1, 1)]
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

// =============== Step 2a: Upsweep Level 1 ===============
groupshared uint blockSums1[8];


[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
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
    uint numWaves = (INSTANCING_PREFIX_SUM_DISPATCH_X + WaveGetLaneCount() - 1) / WaveGetLaneCount();

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

// =============== Step 2b: Upsweep Level 2 (optional) ===============
groupshared uint blockSums2[8];

[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
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
    uint numWaves = (INSTANCING_PREFIX_SUM_DISPATCH_X + WaveGetLaneCount() - 1) / WaveGetLaneCount();

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

// =============== Step 2c: Scan Block Sums ===============
groupshared uint scanBlockSums[8];

[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
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
    uint numWaves = (INSTANCING_PREFIX_SUM_DISPATCH_X + WaveGetLaneCount() - 1) / WaveGetLaneCount();

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


// =============== Step 2d: Downsweep Level 1 (Optional) ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputePrefixSumDownsweep1(uniform PrefixSumDownsweep1PushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    if (globalIdx >= pc.elementCount) {
        return;
    }

    uint blockOffset = pc.scannedLevel2BlockSums[gid.x];
    pc.level2Sums[globalIdx] += blockOffset;
}


// =============== Step 2e: Downsweep Level 2 ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputePrefixSumDownsweep2(uniform PrefixSumDownsweep2PushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    if (globalIdx >= pc.elementCount) {
        return;
    }

    uint blockIdx = globalIdx / INSTANCING_PREFIX_SUM_DISPATCH_X;
    uint blockOffset = pc.level2Sums[blockIdx];
    pc.instanceMeshletOffsets[globalIdx].offset = pc.level1Sums[globalIdx] + blockOffset;
}


// =============== Step 2f: Gather total number of meshlets from final instance postfix sum ===============
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

// =============== Step 3: Expand Instance -> Meshlet through Binary Search. Also determine meshlet visibility. ===============
[shader("compute")]
[numthreads(INSTANCING_EXPAND_DISPATCH_X, 1, 1)]
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

    if (globalMeshletIndex < pc.currentFrameBufferMeshletLimit) {
        pc.intermediateMeshlets[globalMeshletIndex].instanceIndex = packedInstanceIndex;
        pc.intermediateMeshlets[globalMeshletIndex].localMeshletIndex = packedLocalMeshlet;
    }
}

// =============== Step 4a: Upsweep Level 1 ===============
groupshared uint meshletBlockSums1[8];

[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputeMeshletVisibilityPrefixSumUpsweep1(uniform MeshletVisibilityPrefixSumUpsweep1PushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;
    uint threadIdx = gtid.x;

    uint value = 0;
    uint limit = min(pc.currentFrameBufferMeshletLimit, pc.indirectDispatchBuffer->totalMeshlets);
    if (globalIdx < limit) {
        value = bool((pc.intermediateMeshlets[globalIdx].instanceIndex >> 31) & 1);
    }

    // Wave prefix sum
    uint localScan = WavePrefixSum(value);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (INSTANCING_PREFIX_SUM_DISPATCH_X + WaveGetLaneCount() - 1) / WaveGetLaneCount();

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
    if (globalIdx < limit) {
        pc.meshletLevel1Sums[globalIdx] = blockLocalScan;
    }

    // Last thread writes block total
    if (threadIdx == 255 && gid.x < pc.blockCount) {
        pc.meshletLevel1BlockSums[gid.x] = blockLocalScan + value;
    }
}

// Note: Reuse b, c, d; from the other prefix sum because the code is exactly the same.
// =============== Step 4e: Downsweep Level 2 ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputeMeshletVisibilityPrefixSumDownsweep2(uniform MeshletVisibilityPrefixSumDownsweep2PushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    uint limit = min(pc.currentFrameBufferMeshletLimit, pc.indirectDispatchBuffer->totalMeshlets);
    if (globalIdx >= limit) {
        return;
    }

    uint blockIdx = globalIdx / INSTANCING_PREFIX_SUM_DISPATCH_X;
    uint blockOffset = pc.meshletLevel2Sums[blockIdx];
    uint compactOffset = pc.meshletLevel1Sums[globalIdx] + blockOffset;

    uint packedInstance = pc.intermediateMeshlets[globalIdx].instanceIndex;
    bool visible = bool((packedInstance >> 31) & 1);

    if (visible && compactOffset < limit) {
        pc.visibleMeshlets[compactOffset].instanceIndex = packedInstance & 0x7FFFFFFF;
        pc.visibleMeshlets[compactOffset].localMeshletIndex = pc.intermediateMeshlets[globalIdx].localMeshletIndex;
    }

    // Final thread does a postfix sum so we can get total visible meshlets
    if (globalIdx == limit - 1) {
        pc.compactedDispatchBuffer->totalVisibleMeshlets = compactOffset + (visible ? 1 : 0);
    }
}

// =============== Step 4f: Generate an indirect command to draw all visible meshlets ===============
[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeCompactedMeshletDispatch(uniform CompactedMeshletDispatchPushConstant pc)
{
    uint totalVisible = min(pc.compactedDispatchBuffer->totalVisibleMeshlets, pc.currentFrameBufferMeshletLimit);

    uint x = min(totalVisible, (MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1);
    uint y = 1;
    if (totalVisible > (MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1) {
        y = (totalVisible + (MAX_MESH_WORK_GROUP_COUNT_X / 2)) / ((MAX_MESH_WORK_GROUP_COUNT_X / 2) + 1);
    }

    pc.compactedDispatchBuffer->x = x;
    pc.compactedDispatchBuffer->y = y;
    pc.compactedDispatchBuffer->z = 1;
}

// =============== Step 4g: Gather total number of visible meshlets from final meshlet postfix sum ===============
[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeMaxMeshletCount(uniform MaxMeshletCountPushConstant pc)
{
    pc.currentHighest[0] = max(pc.currentHighest[0], pc.indirectDispatchBuffer->totalMeshlets);
}
