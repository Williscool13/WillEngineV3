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
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    ReGIRHashKey key;
    key.cellPos = active.xyz;
    key.level = ReGIRActiveLevel(uint(active.w));
    float3 cellCenterVS = mul(scene.view, float4(ReGIRCellCenterFromKey(key), 1.0)).xyz;

    uint prevBase = 0u;
    bool hasPrev = false;
    if (pc.bHasPrev != 0u) {
        uint prevSlot = ReGIRHashFind(pc.hashEntriesPrev, key);
        if (prevSlot != REGIR_HASH_INVALID) {
            prevBase = prevSlot * REGIR_RESERVOIRS_PER_CELL;
            hasPrev = true;
        }
    }

    for (uint r = localID.x; r < REGIR_RESERVOIRS_PER_CELL; r += 64u) {
            uint h = (slot * 2654435761u) ^ ((r + 1u) * 0x9E3779B9u) ^ (pc.frameIndex * 0x85EBCA6Bu);
        h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

        ReGIRReservoir res = ReGIRReservoirEmpty();
        ReservoirAccum accum = ReservoirAccumEmpty();
        float winnerTarget = 0.0;
        for (uint i = 0; i < REGIR_FILL_CANDIDATES; i++) {
            float uLight = HashStep(h);
            float uSelect = HashStep(h);

            int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
            LightVS vs = UnpackLightVS(pc.lightVS[lightIdx]);
            float target = EvalCellLightTarget(vs, cellCenterVS, UnpackUnorm4x8(vs.packedColor).rgb);
            float w_i = target * float(lightCount);
            if (ReservoirAddSample(accum, w_i, uSelect)) {
                res.lightIdx = uint(lightIdx);
                winnerTarget = target;
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
    }
}
