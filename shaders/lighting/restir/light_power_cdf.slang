import common_interop;
import push_constant_interop;
import lights_interop;
import light_index_common;
import restir_interop;
import shader_constants;
import packing_functions;

static const uint CDF_GROUP = 256u;

groupshared float gScan[CDF_GROUP];
groupshared float gWaveTotal[CDF_GROUP / 32u];
groupshared uint gMaxIdx;

static float ReduceSum(uint tid, float v)
{
    gScan[tid] = v;
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (tid < s) { gScan[tid] += gScan[tid + s]; }
        GroupMemoryBarrierWithGroupSync();
    }
    float r = gScan[0];
    GroupMemoryBarrierWithGroupSync();
    return r;
}

static float ReduceMin(uint tid, float v)
{
    gScan[tid] = v;
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (tid < s) { gScan[tid] = min(gScan[tid], gScan[tid + s]); }
        GroupMemoryBarrierWithGroupSync();
    }
    float r = gScan[0];
    GroupMemoryBarrierWithGroupSync();
    return r;
}

static float ReduceMax(uint tid, float v)
{
    gScan[tid] = v;
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (tid < s) { gScan[tid] = max(gScan[tid], gScan[tid + s]); }
        GroupMemoryBarrierWithGroupSync();
    }
    float r = gScan[0];
    GroupMemoryBarrierWithGroupSync();
    return r;
}

static float LightPower(LightInfo L)
{
    float area;
    if (L.type == LIGHT_TYPE_SPHERE) {
        float radius = L.right.w;
        area = 4.0 * PI * radius * radius;
    } else if (L.type == LIGHT_TYPE_TRIANGLE) {
        area = 0.5 * length(cross(L.right.xyz, L.up.xyz));
    } else if (L.type == LIGHT_TYPE_DISK) {
        area = PI * L.right.w * L.right.w;
    } else {
        area = 4.0 * L.right.w * L.up.w;
    }
    float3 color = UnpackUnorm4x8(L.packedColor).rgb;
    float power = L.intensity * area * max(color.r, max(color.g, color.b));
    // Lambertian emitter restricted to the spot cone: exitant fraction = 1 - cos^2 at the falloff midpoint (1 for 0/0)
    float cosMid = 0.5 * (L.normal.w + L.position.w);
    power *= 1.0 - cosMid * cosMid;
    return max(power, 0.0);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeLightPowerCDF(uniform LightPowerCDFPushConstant pc, uint3 localID : SV_GroupThreadID)
{
    uint tid = localID.x;
    uint waveId = tid / WaveGetLaneCount();
    uint waveCount = CDF_GROUP / WaveGetLaneCount();
    float runningTotal = 0.0;
    for (uint base = 0u; base < pc.liveCount; base += CDF_GROUP) {
        uint i = base + tid;
        float p = 0.0;
        if (i < pc.liveCount) {
            p = LightPower(pc.lightData->lights[LiveLightIndex(i, pc.analyticCount)]);
        }
        float inclusive = WavePrefixSum(p) + p;
        float waveTotal = WaveActiveSum(p);
        if (WaveIsFirstLane()) { gWaveTotal[waveId] = waveTotal; }
        GroupMemoryBarrierWithGroupSync();

        float waveBase = 0.0;
        float chunkTotal = 0.0;
        for (uint w = 0u; w < waveCount; w++) {
            float t = gWaveTotal[w];
            if (w < waveId) { waveBase += t; }
            chunkTotal += t;
        }
        if (i < pc.liveCount) {
            pc.cdf[i] = runningTotal + waveBase + inclusive;
        }
        runningTotal += chunkTotal;
        GroupMemoryBarrierWithGroupSync();
    }

#if WILL_EDITOR
    if (pc.readback != nullptr) {
        float invTotal = runningTotal > 0.0 ? 1.0 / runningTotal : 0.0;
        const float tileShare = 1.0 / float(REGIR_TILE_SIZE);
        const float cellShare = 1.0 / float(REGIR_RESERVOIRS_PER_CELL * REGIR_FILL_CANDIDATES);
        float belowTile = 0.0;
        float belowCell = 0.0;
        float rareShare = 0.0;
        float minShare = 1.0;
        float maxShare = 0.0;
        uint maxIdx = ~0u;
        for (uint i = tid; i < pc.liveCount; i += 256u) {
            float lower = i > 0u ? pc.cdf[i - 1u] : 0.0;
            float q = (pc.cdf[i] - lower) * invTotal;
            if (q < tileShare) { belowTile += 1.0; rareShare += q; }
            if (q < cellShare) { belowCell += 1.0; }
            minShare = min(minShare, q);
            if (q > maxShare) { maxShare = q; maxIdx = i; }
        }
        if (tid == 0u) { gMaxIdx = ~0u; }
        belowTile = ReduceSum(tid, belowTile);
        belowCell = ReduceSum(tid, belowCell);
        rareShare = ReduceSum(tid, rareShare);
        minShare = ReduceMin(tid, minShare);
        float maxShareAll = ReduceMax(tid, maxShare);
        if (maxIdx != ~0u && maxShare == maxShareAll) { InterlockedMin(gMaxIdx, maxIdx); }
        GroupMemoryBarrierWithGroupSync();
        if (tid == 0u) {
            pc.readback->cdfTotalPower = runningTotal;
            pc.readback->cdfLiveCount = pc.liveCount;
            pc.readback->cdfBelowTile = uint(belowTile);
            pc.readback->cdfBelowCell = uint(belowCell);
            pc.readback->cdfRareShare = rareShare;
            pc.readback->cdfMinShare = pc.liveCount > 0u ? minShare : 0.0;
            pc.readback->cdfMaxShare = maxShareAll;
            pc.readback->cdfMaxIdx = gMaxIdx != ~0u ? LiveLightIndex(gMaxIdx, pc.analyticCount) : ~0u;
        }
    }
#endif
}
