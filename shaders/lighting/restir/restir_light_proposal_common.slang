module restir_light_proposal_common;

import restir_interop;
import restir_common;
import regir_hash_common;
import binning_common;
import random_common;
import packing_functions;

/**
 * Candidate light proposal for ReSTIR DI initial sampling. Two backends conform:
 * ReGIRProposal (world-space reservoir hash grid) and WorldGridProposal (cascaded strongest-K light bin).
 * The generic base kernel ReSTIRDIBase is specialized per backend; only these three ops differ, everything downstream is shared.
 */
public interface ILightProposal
{
    /** Resolve the shading point into this backend's cell; populates internal cell state. Advances h only if the backend jitters. */
    [mutating] void Acquire(float3 worldPos, float3 cameraPos, float4x4 view, float camDist, inout uint h);

    /** Draw candidate i of m. Returns false when the cell cannot supply one (caller does the shared uniform fallback). Advances h. */
    bool Draw(uint i, uint m, int lightCount, inout uint h, out int lightIdx, out float pSelect);

    /** Marginal light-selection pdf for an arbitrary (BRDF-hit) light; feeds the Talbot-MIS denominator. */
    float Marginal(int lightIdx, LightView vs, int lightCount);
}

/** ReGIR: per-cell RIS reservoirs from the hash grid. Grid-tap source pdf is the exact per-reservoir targetPdf/totalWeight. */
public struct ReGIRProposal : ILightProposal
{
    public uint* hashEntries;
    public ReGIRReservoir* reservoirs;
    public float2* cellData;

    bool useGrid;
    uint gridBase;
    float3 cellCenterVS;
    float cellTargetSum;
    float cellOccupancy;

    [mutating] public void Acquire(float3 worldPos, float3 cameraPos, float4x4 view, float camDist, inout uint h)
    {
        useGrid = false;
        gridBase = 0u;
        cellCenterVS = float3(0.0);
        cellTargetSum = 0.0;
        cellOccupancy = 0.0;
        if (camDist <= REGIR_MAX_DIST) {
            float regirJitter = ReGIRCellSizeAtLevel(ReGIRLevelFromDistance(camDist));
            float3 jitter = (float3(HashStep(h), HashStep(h), HashStep(h)) - 0.5) * regirJitter;

            [unroll]
            for (int attempt = 0; attempt < 2 && !useGrid; attempt++)
            {
                float3 queryPos = (attempt == 0) ? (worldPos + jitter) : worldPos;
                ReGIRHashKey key = ReGIRComputeKey(queryPos, cameraPos);
                uint cellSlot = ReGIRHashFind(hashEntries, key);
                if (cellSlot != REGIR_HASH_INVALID) {
                    float2 cd = cellData[cellSlot];
                    if (cd.x > 0.0) {
                        cellTargetSum = cd.x;
                        gridBase = ReGIRCellReservoirBase(cellSlot);
                        cellCenterVS = mul(view, float4(ReGIRCellCenterFromKey(key), 1.0)).xyz;
                        cellOccupancy = cd.y;
                        useGrid = true;
                    }
                }
            }
        }
    }

    public bool Draw(uint i, uint m, int lightCount, inout uint h, out int lightIdx, out float pSelect)
    {
        lightIdx = -1;
        pSelect = 0.0;
        if (!useGrid) { return false; }
        // Stratified over the candidate loop so the taps hit distinct cell regions (RTXDI parity).
        float uStrat = (float(i) + HashStep(h)) / float(m);
        uint slotInCell = min(uint(uStrat * float(REGIR_RESERVOIRS_PER_CELL)), REGIR_RESERVOIRS_PER_CELL - 1u);
        ReGIRReservoir gr = reservoirs[gridBase + slotInCell];
        if (gr.lightIdx != ~0u && gr.lightIdx < uint(lightCount) && gr.totalWeight > 0.0) {
            lightIdx = int(gr.lightIdx);
            pSelect = gr.targetPdf / gr.totalWeight;
            return true;
        }
        return false;
    }

    public float Marginal(int lightIdx, LightView vs, int lightCount)
    {
        // Modeled from cell-average occupancy, not the exact per-reservoir pdf, so it does not partition unity when mBrdf > 0.
        if (!useGrid) { return 1.0 / float(lightCount); }
        float3 color = UnpackUnorm4x8(vs.packedColor).rgb;
        return cellOccupancy * (EvalCellLightTarget(vs, cellCenterVS, color) / cellTargetSum) + (1.0 - cellOccupancy) * (1.0 / float(lightCount));
    }
}

// Fraction of each light-sampling candidate budget routed to a uniformly-random emissive triangle when the bin holds only analytic lights.
static const float WORLD_GRID_EMISSIVE_MIX_PROB = 0.5;

/** WorldGridBin: cascaded strongest-K analytic light list. Uniform selection over the cell; RIS applies importance at the real shading point.
 *  Emissive triangles are never binned, so when they are present each candidate draw is a mixture: with WORLD_GRID_EMISSIVE_MIX_PROB pick a uniform emissive, else draw from the analytic bin. Marginal() returns the exact mixture pdf so the Talbot-MIS denominator stays unbiased. */
public struct WorldGridProposal : ILightProposal
{
    public uint2* grid;
    public uint* indexList;
    public int analyticCount;

    uint cellOffset;
    uint cellCount;

    [mutating] public void Acquire(float3 worldPos, float3 cameraPos, float4x4 view, float camDist, inout uint h)
    {
        cellOffset = 0u;
        cellCount = 0u;
        WorldGridKey key = WorldGridResolveCell(worldPos, cameraPos);
        uint2 cell = grid[key.cellIndex];
        cellOffset = cell.x;
        cellCount = cell.y;
    }

    public bool Draw(uint i, uint m, int lightCount, inout uint h, out int lightIdx, out float pSelect)
    {
        lightIdx = -1;
        pSelect = 0.0;
        // Empty cell: let the caller's uniform-over-all-lights fallback handle it (already covers emissives).
        if (cellCount == 0u) { return false; }
        uint eCount = uint(max(lightCount - analyticCount, 0));
        float beta = (eCount > 0u) ? WORLD_GRID_EMISSIVE_MIX_PROB : 0.0;

        float u = (float(i) + HashStep(h)) / float(m);
        if (u < beta) {
            uint e = min(uint(u / beta * float(eCount)), eCount - 1u);
            lightIdx = analyticCount + int(e);
            pSelect = beta / float(eCount);
            return true;
        }
        uint j = min(uint((u - beta) / (1.0 - beta) * float(cellCount)), cellCount - 1u);
        uint idx = indexList[cellOffset + j];
        if (idx >= uint(lightCount)) { return false; }
        lightIdx = int(idx);
        pSelect = (1.0 - beta) / float(cellCount);
        return true;
    }

    public float Marginal(int lightIdx, LightView vs, int lightCount)
    {
        // Exact: light is a candidate iff it survived the cell's top-K, at selection pdf 1/count; truncated lights are owned solely by the BRDF technique.
        if (cellCount == 0u) { return 1.0 / float(lightCount); }
        uint eCount = uint(max(lightCount - analyticCount, 0));
        float beta = (eCount > 0u) ? WORLD_GRID_EMISSIVE_MIX_PROB : 0.0;
        // Emissives are never binned; their only selection mass is the mixture's emissive component.
        if (lightIdx >= analyticCount) { return (eCount > 0u) ? (beta / float(eCount)) : 0.0; }
        for (uint k = 0u; k < cellCount; k++) {
            if (int(indexList[cellOffset + k]) == lightIdx) { return (1.0 - beta) / float(cellCount); }
        }
        return 0.0;
    }
}
