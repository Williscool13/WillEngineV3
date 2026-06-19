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
        pc.gridBuffer[slot] = ReGIRReservoirEmpty();
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint cellIndex = slot / REGIR_RESERVOIRS_PER_CELL;
    float3 cellCenterWS = ReGIRCellCenter(cellIndex, pc.gridOffset);
    float3 cellCenterVS = mul(scene.view, float4(cellCenterWS, 1.0)).xyz;

    uint h = (slot * 2654435761u) ^ (pc.frameIndex * 0x9E3779B9u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    ReGIRReservoir r = ReGIRReservoirEmpty();
    ReservoirAccum accum = ReservoirAccumEmpty();
    float winnerPHat = 0.0;
    for (uint i = 0; i < REGIR_FILL_CANDIDATES; i++) {
        float uLight = HashStep(h);
        float uU = HashStep(h);
        float uV = HashStep(h);
        float uSelect = HashStep(h);

        int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
        LightVS vs = UnpackLightVS(pc.lightVS[lightIdx]);
        // Cone (solid-angle) sampling for spheres, uniform area for rects; anchored at the cell center. Pair with LightSamplePdfArea.
        float3 Xi = SampleLightSurface(vs, cellCenterVS, uU, uV);
        LightSample s = EvalLightSample(vs, Xi, cellCenterVS, float3(0.0, 0.0, 0.0), vs.range);
        float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;

        // w_i = (1/M_light) · p̂(y_i) · (1 / p_light_area)
        // 1 / M_light (omitted, we do it later in ReservoirFinalize)
        // p̂(y_i): light contribution at the cell center.
        float target = EvalCellTarget(s, lightColor, vs.intensity);
        // Joint source pdf of picking this (light, surface point): P(light) · p(point | light).
        float sourcePdf = LightSamplePdfArea(vs, cellCenterVS, Xi) / float(lightCount);
        float w_i = (sourcePdf > 0.0) ? (target / sourcePdf) : 0.0;
        if (ReservoirAddSample(accum, w_i, uSelect)) {
            r.lightIdx = uint(lightIdx);
            r.sampleOffsetPacked = EncodeSampleOffset(Xi, vs);
            winnerPHat = target;
        }
    }

    // Store the RIS pieces (totalWeight = wSum/M, targetPdf = winnerPHat) rather than a pre-divided W;
    // the shading pass forms sourcePdf = targetPdf / cellAverageWeight (RTG2 Ch.23, Section 23.3.2).
    FinalizeReservoirReGIR(r, accum, winnerPHat, float(REGIR_FILL_CANDIDATES));
    ReGIRClampW(r, pc.wClamp);

    // Temporal grid reuse (RTG2 Ch.23): merge fresh with each prior-frame reservoir, renormalising to M=1 each round so confidence can't accumulate.
    // newest -> oldest. I'm not sure if this is the correct approach (this will positively weight older "good" samples.
    ReGIRReservoir cur = r;
    for (uint g = pc.historyCount; g-- > 0; ) {
    //for (uint g = 0; g < pc.historyCount; g++) {
        ReGIRReservoir hist = ReGIRReservoirEmpty();
        if (pc.gridHistory[g] != nullptr) {
            ReGIRReservoir c = pc.gridHistory[g][slot];
            if (c.lightIdx != ~0u && c.lightIdx < uint(lightCount)) { hist = c; }
        }

        ReservoirAccum accumM = ReservoirAccumEmpty();
        ReGIRReservoir merged = ReGIRReservoirEmpty();
        float mergedWinnerPHat = 0.0;
        // Each stored grid reservoir is normalised to M=1, so the merge counts one candidate per non-empty reservoir.
        float mTotal = 0.0;

        if (cur.lightIdx != ~0u) {
            LightVS vsCur = UnpackLightVS(pc.lightVS[cur.lightIdx]);
            float3 yCur = DecodeSamplePos(cur.sampleOffsetPacked, vsCur);
            LightSample sCur = EvalLightSample(vsCur, yCur, cellCenterVS, float3(0.0, 0.0, 0.0), vsCur.range);
            float pCur = EvalCellTarget(sCur, UnpackUnorm4x8(vsCur.packedColor).rgb, vsCur.intensity);
            // Reuse multiplies by the reservoir's effective W (totalWeight/targetPdf) instead of dividing by a source pdf.
            float wCur = pCur * ReGIRReservoirW(cur);
            if (ReservoirAddSample(accumM, wCur, HashStep(h))) {
                merged.lightIdx = cur.lightIdx;
                merged.sampleOffsetPacked = cur.sampleOffsetPacked;
                mergedWinnerPHat = pCur;
            }
            mTotal += 1.0;
        }

        if (hist.lightIdx != ~0u) {
            LightVS vsH = UnpackLightVS(pc.lightVS[hist.lightIdx]);
            float3 yH = DecodeSamplePos(hist.sampleOffsetPacked, vsH);
            LightSample sH = EvalLightSample(vsH, yH, cellCenterVS, float3(0.0, 0.0, 0.0), vsH.range);
            float pH = EvalCellTarget(sH, UnpackUnorm4x8(vsH.packedColor).rgb, vsH.intensity);
            float wH = pH * ReGIRReservoirW(hist);
            if (ReservoirAddSample(accumM, wH, HashStep(h))) {
                merged.lightIdx = hist.lightIdx;
                merged.sampleOffsetPacked = hist.sampleOffsetPacked;
                mergedWinnerPHat = pH;
            }
            mTotal += 1.0;
        }

        if (merged.lightIdx != ~0u) {
            // Renormalising to M=1 (M_total folded into totalWeight here) stops confidence from accumulating across frames.
            FinalizeReservoirReGIR(merged, accumM, mergedWinnerPHat, mTotal);
            cur = merged;
        }
    }

    ReGIRClampW(cur, pc.wClamp);
    pc.gridBuffer[slot] = cur;
}
