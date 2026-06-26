import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;
import restir_interop;

import shader_functions;
import packing_functions;
import restir_common;
import random_common;
import regir_hash;
import light_tree;

groupshared float gCellTargetReduce[64];
groupshared float gCellOccupancyReduce[64];

[numthreads(64, 1, 1)]
void ComputeMain(uniform ReGIRFillPushConstant pc, uint3 groupID : SV_GroupID, uint3 localID : SV_GroupThreadID) {
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

    // Cell target sum S_cell = sum over all lights of the cell target. The shading pass divides each light's cell target by this to recover the analytic grid selection pdf p_grid(L) = target(L)/S_cell, used for balance-heuristic MIS.
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
    for (uint r = localID.x; r < REGIR_RESERVOIRS_PER_CELL; r += 64u) {
            uint h = (slot * 2654435761u) ^ ((r + 1u) * 0x9E3779B9u) ^ (pc.frameIndex * 0x85EBCA6Bu);
        h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

        ReGIRReservoir res = ReGIRReservoirEmpty();
        ReservoirAccum accum = ReservoirAccumEmpty();
        float winnerTarget = 0.0;
        if (pc.useTileFeeder != 0u) {
            // Presampled-tile feeder
            uint tile = (slot * 2654435761u) ^ (key.level * 0x9E3779B9u);
            tile ^= tile >> 15u; tile *= 0x2c1b3c6du; tile ^= tile >> 12u;
            uint tileBase = (tile % REGIR_TILE_COUNT) * REGIR_TILE_SIZE;
            uint startSlot = h % REGIR_TILE_SIZE;
            for (uint i = 0; i < REGIR_FILL_CANDIDATES; i++) {
                uint slotInTile = (startSlot + i) % REGIR_TILE_SIZE;
                ReGIRTileSlot cand = pc.tiles[tileBase + slotInTile];
                float uSelect = HashStep(h);
                if (cand.lightIdx == ~0u || cand.lightIdx >= uint(lightCount) || cand.sourcePdf <= 0.0) { continue; }

                LightVS vs = UnpackLightVS(pc.lightVS[cand.lightIdx]);
                float target = EvalCellLightTarget(vs, cellCenterVS, UnpackUnorm4x8(vs.packedColor).rgb);
                float w_i = target / cand.sourcePdf;
                if (ReservoirAddSample(accum, w_i, uSelect)) {
                    res.lightIdx = cand.lightIdx;
                    winnerTarget = target;
                }
            }
        } else {
        for (uint i = 0; i < REGIR_FILL_CANDIDATES; i++) {
            // Candidate selection via the world-space light BVH (analytic selection pdf).
            LightTreeSample ts = SampleLightTree(pc.bvhNodes, pc.bvhNumLeaves, cellCenterWS, h);
            float uSelect = HashStep(h);
            if (ts.lightIdx == ~0u || ts.lightIdx >= uint(lightCount) || ts.pdf <= 0.0) { continue; }

            int lightIdx = int(ts.lightIdx);
            LightVS vs = UnpackLightVS(pc.lightVS[lightIdx]);
            float target = EvalCellLightTarget(vs, cellCenterVS, UnpackUnorm4x8(vs.packedColor).rgb);
            float w_i = target / ts.pdf;
            if (ReservoirAddSample(accum, w_i, uSelect)) {
                res.lightIdx = uint(lightIdx);
                winnerTarget = target;
            }
        }
        }
        FinalizeReservoirReGIR(res, accum, winnerTarget, float(REGIR_FILL_CANDIDATES));
        ReGIRClampW(res, pc.wClamp);

        // Temporal: merge fresh with the previous frame's reservoir, renormalising to M=1 so confidence can't accumulate.
        if (hasPrev) {
            ReGIRReservoir prev = pc.reservoirsPrev[prevBase + r];
            if (prev.lightIdx != ~0u && prev.lightIdx < uint(lightCount)) {
                ReservoirAccum accumM = ReservoirAccumEmpty();
                ReGIRReservoir merged = ReGIRReservoirEmpty();
                float mergedWinnerTarget = 0.0;
                float mTotal = 0.0;
                if (res.lightIdx != ~0u) {
                    LightVS vsCur = UnpackLightVS(pc.lightVS[res.lightIdx]);
                    float pCur = EvalCellLightTarget(vsCur, cellCenterVS, UnpackUnorm4x8(vsCur.packedColor).rgb);
                    float wCur = pCur * ReGIRReservoirW(res);
                    if (ReservoirAddSample(accumM, wCur, HashStep(h))) {
                        merged.lightIdx = res.lightIdx;
                        mergedWinnerTarget = pCur;
                    }
                    mTotal += 1.0;
                }
                {
                    LightVS vsH = UnpackLightVS(pc.lightVS[prev.lightIdx]);
                    float pH = EvalCellLightTarget(vsH, cellCenterVS, UnpackUnorm4x8(vsH.packedColor).rgb);
                    float wH = pH * ReGIRReservoirW(prev);
                    if (ReservoirAddSample(accumM, wH, HashStep(h))) {
                        merged.lightIdx = prev.lightIdx;
                        mergedWinnerTarget = pH;
                    }
                    mTotal += 1.0;
                }
                if (merged.lightIdx != ~0u) {
                    FinalizeReservoirReGIR(merged, accumM, mergedWinnerTarget, mTotal);
                    ReGIRClampW(merged, pc.wClamp);
                    res = merged;
                }
            }
        }

        pc.reservoirs[baseOut + r] = res;
        if (res.lightIdx != ~0u) { localNonEmpty++; }
    }

    // Per-cell occupancy = fraction of non-empty reservoirs. The shading pass mixes (1-occupancy) of uniform selection into the grid pdf so sparse cells (few in-range lights -> mostly-empty reservoirs) stay lit and correctly weighted.
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
