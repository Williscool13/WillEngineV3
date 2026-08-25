import common_interop;
import model_interop;
import shader_functions;
import instancing_interop;
import push_constant_interop;
import constants_interop;

import limits_declarations;
import bindless_declarations;

groupshared uint gsScanWaveSums[8];

// Exclusive prefix sum of `value` across the workgroup; call from all threads (contains barriers)
static uint BlockExclusiveScan(uint value, uint threadIdx)
{
    uint localScan = WavePrefixSum(value);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (INSTANCING_PREFIX_SUM_DISPATCH_X + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    // Last lane writes wave total
    if (waveIdx == WaveGetLaneCount() - 1) {
        gsScanWaveSums[waveID] = localScan + value;
    }

    GroupMemoryBarrierWithGroupSync();

    // Thread 0 scans wave totals
    if (threadIdx == 0) {
        uint runningSum = 0;
        for (uint i = 0; i < numWaves; i++) {
            uint temp = gsScanWaveSums[i];
            gsScanWaveSums[i] = runningSum;
            runningSum += temp;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    return localScan + gsScanWaveSums[waveID];
}

groupshared uint4 gsRegionScanWaveSums[8];

// Region variant for the meshlet visibility scan: one visible-count lane per draw region
static uint4 BlockExclusiveScan4(uint4 value, uint threadIdx)
{
    uint4 localScan = WavePrefixSum(value);
    uint waveIdx = WaveGetLaneIndex();
    uint waveID = threadIdx / WaveGetLaneCount();
    uint numWaves = (INSTANCING_PREFIX_SUM_DISPATCH_X + WaveGetLaneCount() - 1) / WaveGetLaneCount();

    if (waveIdx == WaveGetLaneCount() - 1) {
        gsRegionScanWaveSums[waveID] = localScan + value;
    }

    GroupMemoryBarrierWithGroupSync();

    if (threadIdx == 0) {
        uint4 runningSum = uint4(0, 0, 0, 0);
        for (uint i = 0; i < numWaves; i++) {
            uint4 temp = gsRegionScanWaveSums[i];
            gsRegionScanWaveSums[i] = runningSum;
            runningSum += temp;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    return localScan + gsRegionScanWaveSums[waveID];
}

// =============== Instancing Step 2a: Upsweep Level 1 ===============
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

    uint blockLocalScan = BlockExclusiveScan(value, threadIdx);
    if (globalIdx < pc.elementCount) {
        pc.level1Sums[globalIdx] = blockLocalScan;
    }

    // Last thread writes block total
    if (threadIdx == 255 && gid.x < pc.blockCount) {
        pc.level1BlockSums[gid.x] = blockLocalScan + value;
    }
}

// =============== Instancing Step 2b: Upsweep Level 2 (optional) ===============
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

    uint blockLocalScan = BlockExclusiveScan(value, threadIdx);
    if (globalIdx < pc.elementCount) {
        pc.level2Sums[globalIdx] = blockLocalScan;
    }

    if (threadIdx == 255 && gid.x < pc.blockCount) {
        pc.level2BlockSums[gid.x] = blockLocalScan + value;
    }
}

// =============== Instancing Step 2c: Scan Block Sums ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputePrefixSumScanBlocks(uniform PrefixSumScanBlocksPushConstant pc, uint3 gtid : SV_GroupThreadID)
{
    uint threadIdx = gtid.x;

    uint value = 0;
    if (threadIdx < pc.blockCount) {
        value = pc.level2BlockSums[threadIdx];
    }

    uint blockLocalScan = BlockExclusiveScan(value, threadIdx);
    if (threadIdx < pc.blockCount) {
        pc.scannedLevel2BlockSums[threadIdx] = blockLocalScan;
    }
}

// =============== Instancing Step 2d: Downsweep Level 1 (Optional) ===============
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

// =============== Instancing Step 2e: Downsweep Level 2 ===============
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

// Note: The meshlet visibility chain scans one visible-count lane per draw region (uint4); its middle steps are the region variants below.
// =============== Instancing Step 4a: Meshlet Visibility Upsweep Level 1 ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputeMeshletVisibilityPrefixSumUpsweep1(uniform MeshletVisibilityPrefixSumUpsweep1PushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;
    uint threadIdx = gtid.x;

    uint4 value = uint4(0, 0, 0, 0);
    uint limit = min(pc.currentFrameBufferMeshletLimit, pc.indirectDispatchBuffer->totalMeshlets);
    if (globalIdx < limit) {
        uint packed = pc.intermediateMeshlets[globalIdx].instanceIndex;
        uint visible = (packed >> 31) & 1;
        uint region = (packed >> 29) & 3;
        value[region] = visible;
    }

    uint4 blockLocalScan = BlockExclusiveScan4(value, threadIdx);
    if (globalIdx < limit) {
        pc.meshletLevel1Sums[globalIdx] = blockLocalScan;
    }

    if (threadIdx == 255 && gid.x < pc.blockCount) {
        pc.meshletLevel1BlockSums[gid.x] = blockLocalScan + value;
    }
}

// =============== Instancing Step 4b: Region Upsweep Level 2 (optional) ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputeRegionPrefixSumUpsweep2(uniform RegionPrefixSumUpsweep2PushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;
    uint threadIdx = gtid.x;

    uint4 value = uint4(0, 0, 0, 0);
    if (globalIdx < pc.elementCount) {
        value = pc.level1BlockSums[globalIdx];
    }

    uint4 blockLocalScan = BlockExclusiveScan4(value, threadIdx);
    if (globalIdx < pc.elementCount) {
        pc.level2Sums[globalIdx] = blockLocalScan;
    }

    if (threadIdx == 255 && gid.x < pc.blockCount) {
        pc.level2BlockSums[gid.x] = blockLocalScan + value;
    }
}

// =============== Instancing Step 4c: Region Scan Block Sums; single group, so the last block's inclusive scan is the grand total ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputeRegionPrefixSumScanBlocks(uniform RegionPrefixSumScanBlocksPushConstant pc, uint3 gtid : SV_GroupThreadID)
{
    uint threadIdx = gtid.x;

    uint4 value = uint4(0, 0, 0, 0);
    if (threadIdx < pc.blockCount) {
        value = pc.level2BlockSums[threadIdx];
    }

    uint4 blockLocalScan = BlockExclusiveScan4(value, threadIdx);
    if (threadIdx < pc.blockCount) {
        pc.scannedLevel2BlockSums[threadIdx] = blockLocalScan;
    }

    if (threadIdx == pc.blockCount - 1) {
        uint4 totals = blockLocalScan + value;
        uint4 bases = uint4(0, totals.x, totals.x + totals.y, totals.x + totals.y + totals.z);
        for (uint r = 0; r < MESHLET_REGION_COUNT; r++) {
            pc.compactedDispatchBuffer->regionArgs[r].w = totals[r];
        }
        pc.compactedDispatchBuffer->regionBase = bases;
        pc.compactedDispatchBuffer->totalVisibleMeshlets = bases.w + totals.w;
    }
}

// =============== Instancing Step 4d: Region Downsweep Level 1 (Optional) ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputeRegionPrefixSumDownsweep1(uniform RegionPrefixSumDownsweep1PushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    if (globalIdx >= pc.elementCount) {
        return;
    }

    uint4 blockOffset = pc.scannedLevel2BlockSums[gid.x];
    pc.level2Sums[globalIdx] += blockOffset;
}

// =============== Instancing Step 4e: Meshlet Visibility Downsweep Level 2 ===============
[shader("compute")]
[numthreads(INSTANCING_PREFIX_SUM_DISPATCH_X, 1, 1)]
void ComputeMeshletVisibilityPrefixSumDownsweep2(uniform MeshletVisibilityPrefixSumDownsweep2PushConstant pc, uint3 gid : SV_GroupID, uint3 dtid : SV_DispatchThreadID)
{
    uint globalIdx = dtid.x;

    uint limit = min(pc.currentFrameBufferMeshletLimit, pc.indirectDispatchBuffer->totalMeshlets);
    if (globalIdx >= limit) {
        return;
    }

    uint packedInstance = pc.intermediateMeshlets[globalIdx].instanceIndex;
    bool visible = bool((packedInstance >> 31) & 1);
    if (!visible) {
        return;
    }
    uint region = (packedInstance >> 29) & 3;

    uint blockIdx = globalIdx / INSTANCING_PREFIX_SUM_DISPATCH_X;
    uint4 ranks = pc.meshletLevel1Sums[globalIdx] + pc.meshletLevel2Sums[blockIdx];
    uint compactOffset = pc.compactedDispatchBuffer->regionBase[region] + ranks[region];

    if (compactOffset < limit) {
        pc.visibleMeshlets[compactOffset].instanceIndex = packedInstance & 0x1FFFFFFF;
        pc.visibleMeshlets[compactOffset].meshletIndexWithinLOD = pc.intermediateMeshlets[globalIdx].meshletIndexWithinLOD;
    }
}
