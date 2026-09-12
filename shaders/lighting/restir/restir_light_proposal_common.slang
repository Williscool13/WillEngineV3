module restir_light_proposal_common;

import restir_interop;
import restir_common;
import light_index_common;
import regir_hash_common;
import binning_common;
import lights_interop;
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
    bool Draw(uint i, uint m, int liveCount, inout uint h, out int lightIdx, out float pSelect);

    /** Marginal light-selection pdf for an arbitrary (BRDF-hit) light; feeds the Talbot-MIS denominator. */
    float Marginal(int lightIdx, LightView vs, int liveCount);
}

/** ReGIR: per-cell RIS reservoirs from the hash grid, sorted by light with empties last. A tap draws uniformly from the occupied
 *  prefix and uses the light's cell-shared mean W, so pSelect = 1 / (occupancy * sharedW) is one value per light per cell. */
public struct ReGIRProposal : ILightProposal
{
    public uint* hashEntries;
    public ReGIRReservoir* reservoirs;
    public float2* cellData;
    public int analyticCount;
    public int realLightCount;

    bool useGrid;
    uint gridBase;
    uint cellOccupied;
    float cellOccupancy;

    [mutating] public void Acquire(float3 worldPos, float3 cameraPos, float4x4 view, float camDist, inout uint h)
    {
        useGrid = false;
        gridBase = 0u;
        cellOccupied = 0u;
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
                    // Gate on occupancy, not the centre target sum: a cell whose centre sits in or behind a panel's plane sums to zero yet its jittered fill points still populate reservoirs
                    if (cd.y > 0.0) {
                        gridBase = ReGIRCellReservoirBase(cellSlot);
                        cellOccupancy = cd.y;
                        cellOccupied = uint(cd.y * float(REGIR_RESERVOIRS_PER_CELL) + 0.5);
                        useGrid = true;
                    }
                }
            }
        }
    }

    public bool Draw(uint i, uint m, int liveCount, inout uint h, out int lightIdx, out float pSelect)
    {
        lightIdx = -1;
        pSelect = 0.0;
        if (!useGrid) { return false; }
        // Stratified over the occupied prefix: the sorted layout spreads the taps across the cell's lights in proportion to their runs
        float uStrat = (float(i) + HashStep(h)) / float(m);
        uint slotInCell = min(uint(uStrat * float(cellOccupied)), cellOccupied - 1u);
        ReGIRReservoir gr = reservoirs[gridBase + slotInCell];
        if (gr.lightIdx != ~0u && IsLiveLight(gr.lightIdx, realLightCount, analyticCount) && gr.sharedW > 0.0) {
            lightIdx = int(gr.lightIdx);
            pSelect = 1.0 / (cellOccupancy * gr.sharedW);
            return true;
        }
        return false;
    }

    public float Marginal(int lightIdx, LightView vs, int liveCount)
    {
        if (!useGrid) { return 1.0 / float(liveCount); }
        // Binary search the sorted occupied prefix: a light the cell does not hold cannot come from the grid, so its light-strategy pdf is 0
        uint target = uint(lightIdx);
        uint lo = 0u;
        uint hi = cellOccupied;
        while (lo < hi) {
            uint mid = (lo + hi) >> 1u;
            if (reservoirs[gridBase + mid].lightIdx < target) { lo = mid + 1u; } else { hi = mid; }
        }
        if (lo < cellOccupied) {
            ReGIRReservoir gr = reservoirs[gridBase + lo];
            if (gr.lightIdx == target && gr.sharedW > 0.0) { return 1.0 / (cellOccupancy * gr.sharedW); }
        }
        return 0.0;
    }
}

/** WorldGridBin: cascaded strongest-K light list. The cell holds analytic lights and emissive groups (whole mesh instances) in two
 *  parallel top-K lists. A draw splits between the two lists proportional to their kept-set power sums (so low-power emissive groups
 *  cannot starve the analytic candidate budget), then picks uniformly among analytics, or a group by its power share and a triangle
 *  uniformly within it. All terms are exact, so Marginal() keeps the Talbot-MIS denominator unbiased. */
public struct WorldGridProposal : ILightProposal
{
    public uint2* grid;
    public uint* indexList;
    public uint2* emissiveGrid;
    public uint* emissiveIndexList;
    public float2* cellPower;
    public LightData* lightData;
    public int analyticCount;

    uint cellOffset;
    uint cellCount;
    uint groupOffset;
    uint groupCount;
    float analyticPow;
    float groupPow;

    [mutating] public void Acquire(float3 worldPos, float3 cameraPos, float4x4 view, float camDist, inout uint h)
    {
        cellOffset = 0u;
        cellCount = 0u;
        groupOffset = 0u;
        groupCount = 0u;
        analyticPow = 0.0;
        groupPow = 0.0;
        WorldGridKey key = WorldGridResolveCell(worldPos, cameraPos);
        uint2 cell = grid[key.cellIndex];
        cellOffset = cell.x;
        cellCount = cell.y;
        if (emissiveGrid != nullptr) {
            uint2 g = emissiveGrid[key.cellIndex];
            groupOffset = g.x;
            groupCount = g.y;
        }
        if (cellPower != nullptr) {
            float2 p = cellPower[key.cellIndex];
            analyticPow = p.x;
            groupPow = p.y;
        }
    }

    bool UsePowerSplit()
    {
        if (cellCount > 0u && analyticPow <= 0.0) { return false; }
        if (groupCount > 0u && groupPow <= 0.0) { return false; }
        return analyticPow + groupPow > 0.0;
    }

    public bool Draw(uint i, uint m, int liveCount, inout uint h, out int lightIdx, out float pSelect)
    {
        lightIdx = -1;
        pSelect = 0.0;
        uint total = cellCount + groupCount;
        // Empty cell: let the caller's uniform-over-all-lights fallback handle it (already covers emissives).
        if (total == 0u) { return false; }

        float u = (float(i) + HashStep(h)) / float(m);

        if (UsePowerSplit()) {
            float T = analyticPow + groupPow;
            float pA = analyticPow / T;
            // Branch on counts, never on float compares alone: pA==1.0 is not guaranteed exact, and the group path with groupCount==0 underflows the index
            if (groupCount == 0u || (cellCount > 0u && u < pA)) {
                uint j = min(uint((u / max(pA, 1e-8)) * float(cellCount)), cellCount - 1u);
                uint idx = indexList[cellOffset + j];
                if (!IsLiveLight(idx, lightData->lightCount, analyticCount)) { return false; }
                lightIdx = int(idx);
                pSelect = pA / float(cellCount);
                return true;
            }
            float target = (u - pA) / max(1.0 - pA, 1e-8) * groupPow;
            float accum = 0.0;
            uint pick = groupCount - 1u;
            for (uint k = 0u; k < groupCount; k++) {
                accum += lightData->emissiveGroups[emissiveIndexList[groupOffset + k]].power;
                if (target < accum) { pick = k; break; }
            }
            EmissiveGroup group = lightData->emissiveGroups[emissiveIndexList[groupOffset + pick]];
            if (group.lightCount == 0u) { return false; }
            uint t = min(uint(HashStep(h) * float(group.lightCount)), group.lightCount - 1u);
            uint idx = group.firstLight + t;
            if (!IsLiveLight(idx, lightData->lightCount, analyticCount)) { return false; }
            lightIdx = int(idx);
            pSelect = (group.power / T) / float(group.lightCount);
            return true;
        }

        uint j = min(uint(u * float(total)), total - 1u);
        if (j < cellCount) {
            uint idx = indexList[cellOffset + j];
            if (!IsLiveLight(idx, lightData->lightCount, analyticCount)) { return false; }
            lightIdx = int(idx);
            pSelect = 1.0 / float(total);
            return true;
        }

        // Group path draws one extra step; with no emissive groups present it is never taken
        EmissiveGroup group = lightData->emissiveGroups[emissiveIndexList[groupOffset + (j - cellCount)]];
        if (group.lightCount == 0u) { return false; }
        uint t = min(uint(HashStep(h) * float(group.lightCount)), group.lightCount - 1u);
        uint idx = group.firstLight + t;
        if (!IsLiveLight(idx, lightData->lightCount, analyticCount)) { return false; }
        lightIdx = int(idx);
        pSelect = 1.0 / (float(total) * float(group.lightCount));
        return true;
    }

    public float Marginal(int lightIdx, LightView vs, int liveCount)
    {
        // Exact: a light is a candidate iff it survived its cell list, at the same pdf the draw used
        uint total = cellCount + groupCount;
        if (total == 0u) { return 1.0 / float(liveCount); }

        bool bPowerSplit = UsePowerSplit();
        float T = analyticPow + groupPow;

        if (lightIdx < analyticCount) {
            for (uint k = 0u; k < cellCount; k++) {
                if (int(indexList[cellOffset + k]) == lightIdx) {
                    return bPowerSplit ? (analyticPow / T) / float(cellCount) : 1.0 / float(total);
                }
            }
            return 0.0;
        }

        // Group runs are disjoint, so an emissive triangle matches at most one binned group.
        for (uint k = 0u; k < groupCount; k++) {
            EmissiveGroup group = lightData->emissiveGroups[emissiveIndexList[groupOffset + k]];
            if (uint(lightIdx) >= group.firstLight && uint(lightIdx) < group.firstLight + group.lightCount) {
                return bPowerSplit ? (group.power / T) / float(group.lightCount) : 1.0 / (float(total) * float(group.lightCount));
            }
        }
        return 0.0;
    }
}
