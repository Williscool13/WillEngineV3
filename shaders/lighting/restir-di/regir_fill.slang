import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;
import restir_interop;

import shader_functions;
import packing_functions;
import restir_common;
import random_common;

[numthreads(64, 1, 1)]
void ComputeMain(uniform ReGIRFillPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint slot = threadID.x;
    uint totalSlots = REGIR_CELL_COUNT * REGIR_RESERVOIRS_PER_CELL;
    if (slot >= totalSlots) {
        return;
    }

    int lightCount = pc.lightData->lightCount;
    if (lightCount <= 0) {
        pc.gridBuffer[slot] = ReservoirEmpty();
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint cellIndex = slot / REGIR_RESERVOIRS_PER_CELL;
    float3 cellCenterWS = ReGIRCellCenter(cellIndex);
    float3 cellCenterVS = mul(scene.view, float4(cellCenterWS, 1.0)).xyz;

    uint h = (slot * 2654435761u) ^ (pc.frameIndex * 0x9E3779B9u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    Reservoir r = ReservoirEmpty();
    ReservoirAccum accum = ReservoirAccumEmpty();

    for (uint i = 0; i < REGIR_FILL_CANDIDATES; i++) {
        float uLight = HashStep(h);
        float uU = HashStep(h);
        float uV = HashStep(h);
        float uSelect = HashStep(h);

        int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
        LightVS vs = UnpackLightVS(pc.lightVS[lightIdx]);
        float3 Xi = SampleLightSurface(vs, cellCenterVS, uU, uV);
        LightSample s = EvalLightSample(vs, Xi, cellCenterVS, float3(0.0, 0.0, 0.0), vs.range);
        float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;

        float target = EvalCellTarget(s, lightColor, vs.intensity);
        float sourcePdf = LightSamplePdfArea(vs, cellCenterVS, Xi) / float(lightCount);
        float w_i = (sourcePdf > 0.0) ? (target / sourcePdf) : 0.0;
        if (ReservoirAddSample(accum, r, w_i, uSelect)) {
            r.lightIdx = uint(lightIdx);
            r.sampleOffsetPacked = EncodeSampleOffset(Xi, vs);
        }
    }

    r.M = REGIR_FILL_CANDIDATES;
    float targetFresh = 0.0;
    if (r.lightIdx != ~0u) {
        LightVS vs = UnpackLightVS(pc.lightVS[r.lightIdx]);
        float3 yStar = DecodeSamplePos(r, vs);
        LightSample s = EvalLightSample(vs, yStar, cellCenterVS, float3(0.0, 0.0, 0.0), vs.range);
        float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
        targetFresh = EvalCellTarget(s, lightColor, vs.intensity);
        ReservoirFinalise(r, accum, targetFresh, float(REGIR_FILL_CANDIDATES));
    }

    // Temporal grid reuse (RTG2 Ch.23): merge fresh with each prior-frame reservoir, renormalising to M=1 each round so confidence can't accumulate.
    Reservoir cur = r;
    for (uint g = 0; g < pc.historyCount; g++) {
        Reservoir hist = ReservoirEmpty();
        if (pc.gridHistory[g] != nullptr) {
            Reservoir c = pc.gridHistory[g][slot];
            if (c.lightIdx != ~0u && c.lightIdx < uint(lightCount)) { hist = c; }
        }

        ReservoirAccum accumM = ReservoirAccumEmpty();
        Reservoir merged = ReservoirEmpty();

        if (cur.lightIdx != ~0u) {
            LightVS vsCur = UnpackLightVS(pc.lightVS[cur.lightIdx]);
            float3 yCur = DecodeSamplePos(cur, vsCur);
            LightSample sCur = EvalLightSample(vsCur, yCur, cellCenterVS, float3(0.0, 0.0, 0.0), vsCur.range);
            float pCur = EvalCellTarget(sCur, UnpackUnorm4x8(vsCur.packedColor).rgb, vsCur.intensity);
            float wCur = pCur * cur.W * float(cur.M);
            if (ReservoirAddSample(accumM, merged, wCur, HashStep(h))) {
                merged.lightIdx = cur.lightIdx;
                merged.sampleOffsetPacked = cur.sampleOffsetPacked;
            }
        }

        uint mTotal = cur.M;
        if (hist.lightIdx != ~0u) {
            LightVS vsH = UnpackLightVS(pc.lightVS[hist.lightIdx]);
            float3 yH = DecodeSamplePos(hist, vsH);
            LightSample sH = EvalLightSample(vsH, yH, cellCenterVS, float3(0.0, 0.0, 0.0), vsH.range);
            float pH = EvalCellTarget(sH, UnpackUnorm4x8(vsH.packedColor).rgb, vsH.intensity);
            float wH = pH * hist.W * float(hist.M);
            if (ReservoirAddSample(accumM, merged, wH, HashStep(h))) {
                merged.lightIdx = hist.lightIdx;
                merged.sampleOffsetPacked = hist.sampleOffsetPacked;
            }
            mTotal += hist.M;
        }

        if (merged.lightIdx != ~0u) {
            LightVS vsW = UnpackLightVS(pc.lightVS[merged.lightIdx]);
            float3 yW = DecodeSamplePos(merged, vsW);
            LightSample sW = EvalLightSample(vsW, yW, cellCenterVS, float3(0.0, 0.0, 0.0), vsW.range);
            float pW = EvalCellTarget(sW, UnpackUnorm4x8(vsW.packedColor).rgb, vsW.intensity);
            ReservoirFinalise(merged, accumM, pW, float(mTotal));
        }
        // W already carries the 1/mTotal normalisation; resetting M=1 stops confidence from accumulating across frames.
        merged.M = 1u;
        cur = merged;
    }

    pc.gridBuffer[slot] = cur;
}
