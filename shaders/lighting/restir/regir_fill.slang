import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;
import light_index_common;
import restir_interop;

import shader_functions;
import packing_functions;
import restir_common;
import random_common;
import regir_hash_common;

groupshared float gCellTargetReduce[64];
groupshared float gCellOccupancyReduce[64];
groupshared uint gKey[REGIR_RESERVOIRS_PER_CELL];
groupshared float gW[REGIR_RESERVOIRS_PER_CELL];
groupshared uint gRun[REGIR_RESERVOIRS_PER_CELL];
groupshared uint gFlag[REGIR_RESERVOIRS_PER_CELL];
groupshared float gSum[REGIR_RESERVOIRS_PER_CELL];
groupshared float gRunMean[REGIR_RESERVOIRS_PER_CELL];

/** Sorts the cell's reservoirs by lightIdx (empties last) and gives every reservoir the mean W of its light's run. */
static void SortAndShareW(uint tid)
{
    const uint N = REGIR_RESERVOIRS_PER_CELL;
    for (uint k = 2u; k <= N; k <<= 1u) {
        for (uint j = k >> 1u; j > 0u; j >>= 1u) {
            for (uint i = tid; i < N; i += 64u) {
                uint ixj = i ^ j;
                if (ixj > i) {
                    bool ascending = (i & k) == 0u;
                    uint a = gKey[i];
                    uint b = gKey[ixj];
                    if ((a > b) == ascending) {
                        gKey[i] = b; gKey[ixj] = a;
                        float wa = gW[i]; gW[i] = gW[ixj]; gW[ixj] = wa;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }

    for (uint i = tid; i < N; i += 64u) {
        uint head = (i == 0u || gKey[i] != gKey[i - 1u]) ? 1u : 0u;
        gRun[i] = head;
        gFlag[i] = head;
        gSum[i] = gW[i];
    }
    GroupMemoryBarrierWithGroupSync();

    // Inclusive prefix count of run heads (run id + 1) and a segmented inclusive sum of W per run
    for (uint d = 1u; d < N; d <<= 1u) {
        uint addRun[REGIR_RESERVOIRS_PER_CELL / 64u];
        float addSum[REGIR_RESERVOIRS_PER_CELL / 64u];
        uint addFlag[REGIR_RESERVOIRS_PER_CELL / 64u];
        for (uint e = 0u; e < REGIR_RESERVOIRS_PER_CELL / 64u; e++) {
            uint i = tid + e * 64u;
            bool take = i >= d;
            addRun[e] = take ? gRun[i - d] : 0u;
            bool open = take && gFlag[i] == 0u;
            addSum[e] = open ? gSum[i - d] : 0.0;
            addFlag[e] = open ? gFlag[i - d] : 0u;
        }
        GroupMemoryBarrierWithGroupSync();
        for (uint e = 0u; e < REGIR_RESERVOIRS_PER_CELL / 64u; e++) {
            uint i = tid + e * 64u;
            gRun[i] += addRun[e];
            gSum[i] += addSum[e];
            gFlag[i] |= addFlag[e];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    for (uint i = tid; i < N; i += 64u) {
        if (i == 0u || gRun[i] != gRun[i - 1u]) { gRunMean[gRun[i] - 1u] = float(i); }
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint i = tid; i < N; i += 64u) {
        if (i == N - 1u || gRun[i + 1u] != gRun[i]) {
            uint run = gRun[i] - 1u;
            float count = float(i) - gRunMean[run] + 1.0;
            gRunMean[run] = gSum[i] / count;
        }
    }
    GroupMemoryBarrierWithGroupSync();
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeReGIRFill(uniform ReGIRFillPushConstant pc, uint3 groupID : SV_GroupID, uint3 localID : SV_GroupThreadID) {
    uint cellIdx = groupID.x;
    if (cellIdx >= pc.activeCount[0]) {
        return;
    }

    int4 active = pc.activeCells[cellIdx];
    uint slot = ReGIRActiveSlot(uint(active.w));
    uint baseOut = slot * REGIR_RESERVOIRS_PER_CELL;
    int lightCount = pc.lightData->lightCount;

    if (lightCount <= 0) {
        for (uint r = localID.x; r < REGIR_RESERVOIRS_PER_CELL; r += 64u) {
            pc.reservoirs[baseOut + r] = ReGIRReservoirEmpty();
        }
        if (localID.x == 0u) { pc.cellData[slot] = float2(0.0, 0.0); }
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    ReGIRHashKey key;
    key.cellPos = active.xyz;
    key.level = ReGIRActiveLevel(uint(active.w));
    float3 cellCenterWS = ReGIRCellCenterFromKey(key);
    float3 cellCenterVS = mul(scene.view, float4(cellCenterWS, 1.0)).xyz;
    float targetMinDist2 = ReGIRTargetMinDist2(key.level);

    uint analyticCount = uint(pc.lightData->analyticLightCount);
    uint liveCount = LiveLightCount(lightCount, int(analyticCount));
    float localSum = 0.0;
    for (uint i = localID.x; i < liveCount; i += 64u) {
        uint li = LiveLightIndex(i, int(analyticCount));
        LightView vsl = UnpackLightVS(pc.lightVS[li]);
        localSum += EvalCellLightTarget(vsl, cellCenterVS, UnpackUnorm4x8(vsl.packedColor).rgb, targetMinDist2);
    }
    gCellTargetReduce[localID.x] = localSum;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (localID.x < stride) {
            gCellTargetReduce[localID.x] += gCellTargetReduce[localID.x + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    uint prevBase = 0u;
    bool hasPrev = false;
    if (pc.bHasPrev != 0u) {
        uint prevSlot = ReGIRHashFind(pc.hashEntriesPrev, key);
        if (prevSlot != REGIR_HASH_INVALID) {
            prevBase = prevSlot * REGIR_RESERVOIRS_PER_CELL;
            hasPrev = true;
        }
    }

    uint localNonEmpty = 0u;
    float cellSize = ReGIRCellSizeAtLevel(key.level);
    float prevRetention = pow(0.5, 1.0 / scene.framerateScale);
    float prevConfidence = prevRetention / (1.0 - prevRetention);
    for (uint r = localID.x; r < REGIR_RESERVOIRS_PER_CELL; r += 64u) {
            uint h = (slot * 2654435761u) ^ ((r + 1u) * 0x9E3779B9u) ^ (pc.frameIndex * 0x85EBCA6Bu);
        h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

        uint hj = h ^ 0x68bc21ebu; hj ^= hj >> 15u; hj *= 0x2c1b3c6du; hj ^= hj >> 12u;
        float3 jitterWS = (float3(HashStep(hj), HashStep(hj), HashStep(hj)) - 0.5) * cellSize;
        float3 sampleVS = mul(scene.view, float4(cellCenterWS + jitterWS, 1.0)).xyz;

        ReGIRReservoir res = ReGIRReservoirEmpty();
        ReservoirAccum accum = ReservoirAccumEmpty();
        float winnerTarget = 0.0;
        uint tile = (slot * 2654435761u) ^ (key.level * 0x9E3779B9u) ^ (pc.frameIndex * 0xB5297A4Du) ^ ((r + 1u) * 0x27d4eb2du);
        tile ^= tile >> 15u; tile *= 0x2c1b3c6du; tile ^= tile >> 12u;
        uint tileBase = (tile % REGIR_TILE_COUNT) * REGIR_TILE_SIZE;
        uint startSlot = h % REGIR_TILE_SIZE;
        for (uint i = 0; i < REGIR_FILL_CANDIDATES; i++) {
            uint slotInTile = (startSlot + i) % REGIR_TILE_SIZE;
            ReGIRTileSlot cand = pc.tiles[tileBase + slotInTile];
            float uSelect = HashStep(h);
            if (cand.lightIdx == ~0u || !IsLiveLight(cand.lightIdx, lightCount, int(analyticCount)) || cand.sourcePdf <= 0.0) { continue; }

            LightView vs = UnpackLightVS(pc.lightVS[cand.lightIdx]);
            float target = EvalCellLightTarget(vs, sampleVS, UnpackUnorm4x8(vs.packedColor).rgb, targetMinDist2);
            float w_i = target / cand.sourcePdf;
            if (ReservoirAddSample(accum, w_i, uSelect)) {
                res.lightIdx = cand.lightIdx;
                winnerTarget = target;
            }
        }
        FinalizeReservoirReGIR(res, accum, winnerTarget, float(REGIR_FILL_CANDIDATES));
        ReGIRClampW(res, pc.wClamp);

        if (hasPrev) {
            ReGIRReservoir prev = pc.reservoirsPrev[prevBase + r];
            // Empty input still counts toward merge M (else surviving side inflates un-halved); only an
            // externally invalidated prev index (light list shrank) drops out entirely.
            bool prevInvalidated = (prev.lightIdx != ~0u && !IsLiveLight(prev.lightIdx, lightCount, int(analyticCount)));
            if (!prevInvalidated) {
                ReservoirAccum accumM = ReservoirAccumEmpty();
                ReGIRReservoir merged = ReGIRReservoirEmpty();
                float mergedWinnerTarget = 0.0;
                if (res.lightIdx != ~0u) {
                    // winnerTarget was evaluated at this same sampleVS during candidate selection
                    float wCur = winnerTarget * ReGIRReservoirW(res);
                    if (ReservoirAddSample(accumM, wCur, HashStep(h))) {
                        merged.lightIdx = res.lightIdx;
                        mergedWinnerTarget = winnerTarget;
                    }
                }
                if (prev.lightIdx != ~0u) {
                    LightView vsH = UnpackLightVS(pc.lightVS[prev.lightIdx]);
                    float pH = EvalCellLightTarget(vsH, sampleVS, UnpackUnorm4x8(vsH.packedColor).rgb, targetMinDist2);
                    float wH = pH * ReGIRReservoirW(prev) * prevConfidence;
                    if (ReservoirAddSample(accumM, wH, HashStep(h))) {
                        merged.lightIdx = prev.lightIdx;
                        mergedWinnerTarget = pH;
                    }
                }
                FinalizeReservoirReGIR(merged, accumM, mergedWinnerTarget, 1.0 + prevConfidence);
                ReGIRClampW(merged, pc.wClamp);
                res = merged;
            }
        }

        gKey[r] = res.lightIdx;
        gW[r] = (res.lightIdx != ~0u) ? res.W : 0.0;
        if (res.lightIdx != ~0u) { localNonEmpty++; }
    }

    gCellOccupancyReduce[localID.x] = float(localNonEmpty);
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (localID.x < stride) {
            gCellOccupancyReduce[localID.x] += gCellOccupancyReduce[localID.x + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    SortAndShareW(localID.x);
    for (uint r = localID.x; r < REGIR_RESERVOIRS_PER_CELL; r += 64u) {
        ReGIRReservoir res;
        res.lightIdx = gKey[r];
        res.W = gW[r];
        res.sharedW = (res.lightIdx != ~0u) ? gRunMean[gRun[r] - 1u] : 0.0;
        pc.reservoirs[baseOut + r] = res;
    }
    if (localID.x == 0u) { pc.cellData[slot] = float2(gCellTargetReduce[0], gCellOccupancyReduce[0] / float(REGIR_RESERVOIRS_PER_CELL)); }
}
