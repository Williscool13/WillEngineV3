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
 * ReGIRProposal (world-space hash grid of per-cell entry tables) and WorldGridProposal (cascaded strongest-K light bin).
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

/** ReGIR: the cell's entry table (see ReGIREntry). Draw binary-searches cumMass for a stratified u, then picks a triangle uniformly inside a meshlet;
 *  Marginal binary-searches the key, so the selection pdf is exact both ways and Talbot MIS stays unbiased. */
public struct ReGIRProposal : ILightProposal
{
    public uint* hashEntries;
    public ReGIREntry* entries;
    public uint2* cellData;
    public LightData* lightData;
    public int analyticCount;
    public int realLightCount;

    bool useGrid;
    uint entryBase;
    uint entryCount;
    float totalMass;

    [mutating] public void Acquire(float3 worldPos, float3 cameraPos, float4x4 view, float camDist, inout uint h)
    {
        useGrid = false;
        entryBase = 0u;
        entryCount = 0u;
        totalMass = 0.0;
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
                    uint2 cd = cellData[cellSlot];
                    float total = asfloat(cd.y);
                    if (cd.x > 0u && total > 0.0) {
                        entryBase = ReGIRCellEntryBase(cellSlot);
                        entryCount = cd.x;
                        totalMass = total;
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
        float u = (float(i) + HashStep(h)) / float(m);
        float target = u * totalMass;
        uint lo = 0u;
        uint hi = entryCount - 1u;
        while (lo < hi) {
            uint mid = (lo + hi) >> 1u;
            if (entries[entryBase + mid].cumMass > target) { hi = mid; } else { lo = mid + 1u; }
        }
        float lower = lo > 0u ? entries[entryBase + lo - 1u].cumMass : 0.0;
        ReGIREntry entry = entries[entryBase + lo];
        float mass = entry.cumMass - lower;
        if (!(mass > 0.0)) { return false; }

        float p = mass / totalMass;
        uint idx;
        if ((entry.key & REGIR_KEY_MESHLET) != 0u) {
            EmissiveMeshlet meshlet = lightData->emissiveMeshlets[entry.key & ~REGIR_KEY_MESHLET];
            if (meshlet.lightCount == 0u) { return false; }
            uint t = min(uint(HashStep(h) * float(meshlet.lightCount)), meshlet.lightCount - 1u);
            idx = meshlet.firstLight + t;
            p /= float(meshlet.lightCount);
        }
        else {
            idx = entry.key;
        }
        if (!IsLiveLight(idx, realLightCount, analyticCount)) { return false; }
        lightIdx = int(idx);
        pSelect = p;
        return true;
    }

    public float Marginal(int lightIdx, LightView vs, int liveCount)
    {
        if (!useGrid) { return 1.0 / float(liveCount); }
        uint target;
        uint runCount = 1u;
        if (lightIdx < analyticCount) {
            target = uint(lightIdx);
        }
        else {
            uint meshlet = lightData->meshletOf[uint(lightIdx) - uint(MAX_ANALYTIC_LIGHTS)];
            if (meshlet == ~0u) { return 0.0; }
            runCount = lightData->emissiveMeshlets[meshlet].lightCount;
            if (runCount == 0u) { return 0.0; }
            target = REGIR_KEY_MESHLET | meshlet;
        }

        // A light the cell does not hold cannot come from the grid, so its light-strategy pdf is 0
        uint lo = 0u;
        uint hi = entryCount;
        while (lo < hi) {
            uint mid = (lo + hi) >> 1u;
            if (entries[entryBase + mid].key < target) { lo = mid + 1u; } else { hi = mid; }
        }
        if (lo >= entryCount || entries[entryBase + lo].key != target) { return 0.0; }
        float lower = lo > 0u ? entries[entryBase + lo - 1u].cumMass : 0.0;
        float mass = entries[entryBase + lo].cumMass - lower;
        if (!(mass > 0.0)) { return 0.0; }
        return (mass / totalMass) / float(runCount);
    }
}

/** WorldGridBin: cascaded strongest-K light list. The cell holds analytic lights and emissive meshlets in two
 *  parallel top-K lists. A draw splits between the two lists proportional to their kept-set power sums (so low-power emissive meshlets
 *  cannot starve the analytic candidate budget), then picks uniformly among analytics, or a meshlet by its power share and a triangle
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
    uint meshletOffset;
    uint meshletCount;
    float analyticPow;
    float meshletPow;

    [mutating] public void Acquire(float3 worldPos, float3 cameraPos, float4x4 view, float camDist, inout uint h)
    {
        cellOffset = 0u;
        cellCount = 0u;
        meshletOffset = 0u;
        meshletCount = 0u;
        analyticPow = 0.0;
        meshletPow = 0.0;
        WorldGridKey key = WorldGridResolveCell(worldPos, cameraPos);
        uint2 cell = grid[key.cellIndex];
        cellOffset = cell.x;
        cellCount = cell.y;
        if (emissiveGrid != nullptr) {
            uint2 g = emissiveGrid[key.cellIndex];
            meshletOffset = g.x;
            meshletCount = g.y;
        }
        if (cellPower != nullptr) {
            float2 p = cellPower[key.cellIndex];
            analyticPow = p.x;
            meshletPow = p.y;
        }
    }

    bool UsePowerSplit()
    {
        if (cellCount > 0u && analyticPow <= 0.0) { return false; }
        if (meshletCount > 0u && meshletPow <= 0.0) { return false; }
        return analyticPow + meshletPow > 0.0;
    }

    public bool Draw(uint i, uint m, int liveCount, inout uint h, out int lightIdx, out float pSelect)
    {
        lightIdx = -1;
        pSelect = 0.0;
        uint total = cellCount + meshletCount;
        // Empty cell: let the caller's uniform-over-all-lights fallback handle it (already covers emissives).
        if (total == 0u) { return false; }

        float u = (float(i) + HashStep(h)) / float(m);

        if (UsePowerSplit()) {
            float T = analyticPow + meshletPow;
            float pA = analyticPow / T;
            // Branch on counts, never on float compares alone: pA==1.0 is not guaranteed exact, and the meshlet path with meshletCount==0 underflows the index
            if (meshletCount == 0u || (cellCount > 0u && u < pA)) {
                uint j = min(uint((u / max(pA, 1e-8)) * float(cellCount)), cellCount - 1u);
                uint idx = indexList[cellOffset + j];
                if (!IsLiveLight(idx, lightData->lightCount, analyticCount)) { return false; }
                lightIdx = int(idx);
                pSelect = pA / float(cellCount);
                return true;
            }
            float target = (u - pA) / max(1.0 - pA, 1e-8) * meshletPow;
            float accum = 0.0;
            uint pick = meshletCount - 1u;
            for (uint k = 0u; k < meshletCount; k++) {
                accum += lightData->emissiveMeshlets[emissiveIndexList[meshletOffset + k]].power;
                if (target < accum) { pick = k; break; }
            }
            EmissiveMeshlet meshlet = lightData->emissiveMeshlets[emissiveIndexList[meshletOffset + pick]];
            if (meshlet.lightCount == 0u) { return false; }
            uint t = min(uint(HashStep(h) * float(meshlet.lightCount)), meshlet.lightCount - 1u);
            uint idx = meshlet.firstLight + t;
            if (!IsLiveLight(idx, lightData->lightCount, analyticCount)) { return false; }
            lightIdx = int(idx);
            pSelect = (meshlet.power / T) / float(meshlet.lightCount);
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

        // Meshlet path draws one extra step; with no emissive meshlets present it is never taken
        EmissiveMeshlet meshlet = lightData->emissiveMeshlets[emissiveIndexList[meshletOffset + (j - cellCount)]];
        if (meshlet.lightCount == 0u) { return false; }
        uint t = min(uint(HashStep(h) * float(meshlet.lightCount)), meshlet.lightCount - 1u);
        uint idx = meshlet.firstLight + t;
        if (!IsLiveLight(idx, lightData->lightCount, analyticCount)) { return false; }
        lightIdx = int(idx);
        pSelect = 1.0 / (float(total) * float(meshlet.lightCount));
        return true;
    }

    public float Marginal(int lightIdx, LightView vs, int liveCount)
    {
        // Exact: a light is a candidate iff it survived its cell list, at the same pdf the draw used
        uint total = cellCount + meshletCount;
        if (total == 0u) { return 1.0 / float(liveCount); }

        bool bPowerSplit = UsePowerSplit();
        float T = analyticPow + meshletPow;

        if (lightIdx < analyticCount) {
            for (uint k = 0u; k < cellCount; k++) {
                if (int(indexList[cellOffset + k]) == lightIdx) {
                    return bPowerSplit ? (analyticPow / T) / float(cellCount) : 1.0 / float(total);
                }
            }
            return 0.0;
        }

        // Meshlet runs are disjoint, so an emissive triangle matches at most one binned meshlet.
        for (uint k = 0u; k < meshletCount; k++) {
            EmissiveMeshlet meshlet = lightData->emissiveMeshlets[emissiveIndexList[meshletOffset + k]];
            if (uint(lightIdx) >= meshlet.firstLight && uint(lightIdx) < meshlet.firstLight + meshlet.lightCount) {
                return bPowerSplit ? (meshlet.power / T) / float(meshlet.lightCount) : 1.0 / (float(total) * float(meshlet.lightCount));
            }
        }
        return 0.0;
    }
}
