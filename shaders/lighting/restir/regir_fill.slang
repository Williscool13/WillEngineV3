import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;
import restir_interop;

import shader_functions;
import packing_functions;
import restir_common;
import random_common;
import regir_hash_common;

groupshared float gCellTargetReduce[64];
groupshared float gCellOccupancyReduce[64];

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

    float localSum = 0.0;
    for (uint li = localID.x; li < uint(lightCount); li += 64u) {
        LightVS vsl = UnpackLightVS(pc.lightVS[li]);
        localSum += EvalCellLightTarget(vsl, cellCenterVS, UnpackUnorm4x8(vsl.packedColor).rgb);
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
    for (uint r = localID.x; r < REGIR_RESERVOIRS_PER_CELL; r += 64u) {
            uint h = (slot * 2654435761u) ^ ((r + 1u) * 0x9E3779B9u) ^ (pc.frameIndex * 0x85EBCA6Bu);
        h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

        uint hj = h ^ 0x68bc21ebu; hj ^= hj >> 15u; hj *= 0x2c1b3c6du; hj ^= hj >> 12u;
        float3 jitterWS = (float3(HashStep(hj), HashStep(hj), HashStep(hj)) - 0.5) * cellSize;
        float3 sampleVS = mul(scene.view, float4(cellCenterWS + jitterWS, 1.0)).xyz;

        ReGIRReservoir res = ReGIRReservoirEmpty();
        ReservoirAccum accum = ReservoirAccumEmpty();
        float winnerTarget = 0.0;
        uint tile = (slot * 2654435761u) ^ (key.level * 0x9E3779B9u) ^ (pc.frameIndex * 0xB5297A4Du);
        tile ^= tile >> 15u; tile *= 0x2c1b3c6du; tile ^= tile >> 12u;
        uint tileBase = (tile % REGIR_TILE_COUNT) * REGIR_TILE_SIZE;
        uint startSlot = h % REGIR_TILE_SIZE;
        for (uint i = 0; i < REGIR_FILL_CANDIDATES; i++) {
            uint slotInTile = (startSlot + i) % REGIR_TILE_SIZE;
            ReGIRTileSlot cand = pc.tiles[tileBase + slotInTile];
            float uSelect = HashStep(h);
            if (cand.lightIdx == ~0u || cand.lightIdx >= uint(lightCount) || cand.sourcePdf <= 0.0) { continue; }

            LightVS vs = UnpackLightVS(pc.lightVS[cand.lightIdx]);
            float target = EvalCellLightTarget(vs, sampleVS, UnpackUnorm4x8(vs.packedColor).rgb);
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
            bool prevInvalidated = (prev.lightIdx != ~0u && prev.lightIdx >= uint(lightCount));
            if (!prevInvalidated) {
                ReservoirAccum accumM = ReservoirAccumEmpty();
                ReGIRReservoir merged = ReGIRReservoirEmpty();
                float mergedWinnerTarget = 0.0;
                if (res.lightIdx != ~0u) {
                    // winnerTarget was evaluated at this same sampleVS during candidate selection; res.targetPdf == winnerTarget.
                    float wCur = winnerTarget * ReGIRReservoirW(res);
                    if (ReservoirAddSample(accumM, wCur, HashStep(h))) {
                        merged.lightIdx = res.lightIdx;
                        mergedWinnerTarget = winnerTarget;
                    }
                }
                if (prev.lightIdx != ~0u) {
                    LightVS vsH = UnpackLightVS(pc.lightVS[prev.lightIdx]);
                    float pH = EvalCellLightTarget(vsH, sampleVS, UnpackUnorm4x8(vsH.packedColor).rgb);
                    float wH = pH * ReGIRReservoirW(prev);
                    if (ReservoirAddSample(accumM, wH, HashStep(h))) {
                        merged.lightIdx = prev.lightIdx;
                        mergedWinnerTarget = pH;
                    }
                }
                FinalizeReservoirReGIR(merged, accumM, mergedWinnerTarget, 2.0);
                ReGIRClampW(merged, pc.wClamp);
                res = merged;
            }
        }

        pc.reservoirs[baseOut + r] = res;
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
    if (localID.x == 0u) { pc.cellData[slot] = float2(gCellTargetReduce[0], gCellOccupancyReduce[0] / float(REGIR_RESERVOIRS_PER_CELL)); }
}
