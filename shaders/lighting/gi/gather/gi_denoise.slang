import common_interop;
import push_constant_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;
import gi_gather_common;

// Separable bilateral gaussian over the gather SH, iterated a-trous (H then V via pc.direction, stride via pc.stepSize); FP16 groupshared preload, weights from plane-distance/normal/hitT deltas.

static const int GI_DENOISE_RADIUS = 2;
static const int GI_DENOISE_MAX_STRIDE = 4;
static const int GI_DENOISE_TILE_RADIUS = GI_DENOISE_RADIUS * GI_DENOISE_MAX_STRIDE;
static const int GI_DENOISE_GROUP = 16;
static const int GI_DENOISE_TILE = GI_DENOISE_GROUP + 2 * GI_DENOISE_TILE_RADIUS;
static const float GI_DENOISE_PLANE_DIST_SIGMA = 0.01;
static const float GI_DENOISE_NORMAL_POWER = 32.0;
static const float GI_DENOISE_HITT_SENSITIVITY = 3.0;
static const float GI_DENOISE_FIREFLY_SCALE = 4.0;
static const float GI_DENOISE_HITT_FAR_METERS = 8.0;

groupshared uint2 sShR[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared uint2 sShG[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared uint2 sShB[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared uint sNormal[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared float sViewZ[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared float sHitT[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared float sAO[GI_DENOISE_TILE * GI_DENOISE_GROUP];

[shader("compute")]
[numthreads(GI_DENOISE_GROUP, GI_DENOISE_GROUP, 1)]
void ComputeGIDenoise(uniform GIDenoisePushConstant pc, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    const bool horizontal = pc.direction == 0u;
    const int2 groupOrigin = int2(gid.xy) * GI_DENOISE_GROUP;

    uint threadIndex = gtid.y * GI_DENOISE_GROUP + gtid.x;
    for (uint t = threadIndex; t < uint(GI_DENOISE_TILE * GI_DENOISE_GROUP); t += GI_DENOISE_GROUP * GI_DENOISE_GROUP) {
        int along = int(t % GI_DENOISE_TILE) - GI_DENOISE_TILE_RADIUS;
        int across = int(t / GI_DENOISE_TILE);
        int2 pos = groupOrigin + (horizontal ? int2(along, across) : int2(across, along));
        bool inside = all(pos >= int2(0)) && all(pos < int2(pc.gatherExtent));
        int2 p = clamp(pos, int2(0), int2(pc.gatherExtent) - 1);

        uint2 fullPixel = GIGatherRepresentativePixel(uint2(p), pc.renderExtent);
        uint2 g = rdgTexturesUInt2[pc.guideIndex].Load(int3(p, 0)).xy;
        float2 data = rdgTexturesFloat2[pc.dataIndex].Load(int3(p, 0)).xy;
        // Backface samples carry no radiance; excluding them here removes them as taps and reduces the center to a passthrough.
        bool valid = inside && asfloat(g.y) != 0.0 && uint(data.y + 0.5) != GI_GATHER_TIER_BACKFACE;

        sShR[t] = PackHalf2x16Pair(rdgTexturesFloat4[pc.srcShRIndex].Load(int3(p, 0)));
        sShG[t] = PackHalf2x16Pair(rdgTexturesFloat4[pc.srcShGIndex].Load(int3(p, 0)));
        sShB[t] = PackHalf2x16Pair(rdgTexturesFloat4[pc.srcShBIndex].Load(int3(p, 0)));
        sNormal[t] = g.x;
        sViewZ[t] = valid ? asfloat(g.y) : 0.0;
        // Sky misses opt out of the hitT guide (sentinel -1): a miss is a legit 0-radiance sample of the same integral as a neighboring hit, but hitT=10000 vs metres walls them off; with no IBL that blocks exactly the smoothing the outdoor speckle needs.
        // Far surface hits take the same opt-out. Past a few metres the exact distance carries no geometric correlation, but the edge-stop still reads the gap as a hard boundary and walls a far-bright outlier off from the near-dark neighbours that should average it down.
        bool bFarField = uint(data.y + 0.5) == GI_GATHER_TIER_SKY || data.x >= GI_DENOISE_HITT_FAR_METERS;
        sHitT[t] = bFarField ? -1.0 : data.x;
        sAO[t] = pc.aoIndex != ~0x0u ? rdgTexturesFloat2[pc.aoIndex].Load(int3(fullPixel, 0)).y : 1.0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 gatherPixel = uint2(groupOrigin) + gtid.xy;
    if (any(gatherPixel >= pc.gatherExtent)) {
        return;
    }

    uint along = horizontal ? gtid.x : gtid.y;
    uint across = horizontal ? gtid.y : gtid.x;
    uint centerIdx = across * GI_DENOISE_TILE + along + GI_DENOISE_TILE_RADIUS;

    float4 sumR = UnpackHalf2x16Pair(sShR[centerIdx]);
    float4 sumG = UnpackHalf2x16Pair(sShG[centerIdx]);
    float4 sumB = UnpackHalf2x16Pair(sShB[centerIdx]);
    float centerViewZ = sViewZ[centerIdx];
    if (centerViewZ != 0.0) {
        float3 centerNormal = UnpackNormalOct16(sNormal[centerIdx]);
        float centerHitT = sHitT[centerIdx];
        if (pc.stepSize == 1u) {
            // Stabilize the edge-stop reference against 1spp hitT variance. First iteration only, later iterations read already-filtered SH.
            float hitL = sViewZ[centerIdx - 1] != 0.0 && sHitT[centerIdx - 1] >= 0.0 ? sHitT[centerIdx - 1] : centerHitT;
            float hitR = sViewZ[centerIdx + 1] != 0.0 && sHitT[centerIdx + 1] >= 0.0 ? sHitT[centerIdx + 1] : centerHitT;
            centerHitT = min(centerHitT, min(hitL, hitR));

            // Anti-firefly: a 1spp ray that lands on a tiny ultra-bright patch (e.g. an area light's floor-contact strip, radiance clamped only at 20) is spatially isolated.
            // Max-channel, not Rec.709 luma: a blue outlier weighs 0.0722 there and would need ~10x the magnitude of a green one to trip the same cap.
            float3 centerL0 = float3(sumR.x, sumG.x, sumB.x);
            float centerMag = max(centerL0.r, max(centerL0.g, centerL0.b));
            float maxNeighborMag = -1.0;
            [unroll]
            for (int d = -GI_DENOISE_RADIUS; d <= GI_DENOISE_RADIUS; d++) {
                if (d == 0) { continue; }
                uint idx = uint(int(centerIdx) + d);
                if (sViewZ[idx] == 0.0) { continue; }
                float3 tapL0 = float3(UnpackHalf2x16Pair(sShR[idx]).x, UnpackHalf2x16Pair(sShG[idx]).x, UnpackHalf2x16Pair(sShB[idx]).x);
                maxNeighborMag = max(maxNeighborMag, max(tapL0.r, max(tapL0.g, tapL0.b)));
            }
            float magEps = 1e-3;
            float magCap = maxNeighborMag * GI_DENOISE_FIREFLY_SCALE + magEps;
            if (maxNeighborMag >= 0.0 && centerMag > magCap) {
                float fireflyScale = magCap / centerMag;
                sumR *= fireflyScale;
                sumG *= fireflyScale;
                sumB *= fireflyScale;
            }
        }
        uint2 centerFull = GIGatherRepresentativePixel(gatherPixel, pc.renderExtent);
        float2 centerUv = (float2(centerFull) + 0.5) / float2(pc.renderExtent);
        float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, centerUv, centerViewZ);
        float invPlaneSigma = 1.0 / (GI_DENOISE_PLANE_DIST_SIGMA * abs(centerViewZ));
        int step = clamp(int(pc.stepSize), 1, GI_DENOISE_MAX_STRIDE);

        const int2 dir = horizontal ? int2(1, 0) : int2(0, 1);
        float sumW = 1.0;
        [unroll]
        for (int d = -GI_DENOISE_RADIUS; d <= GI_DENOISE_RADIUS; d++) {
            if (d == 0) { continue; }
            int tapOffset = d * step;
            uint idx = uint(int(centerIdx) + tapOffset);
            float tapViewZ = sViewZ[idx];
            if (tapViewZ == 0.0) { continue; }

            uint2 tapFull = GIGatherRepresentativePixel(uint2(int2(gatherPixel) + dir * tapOffset), pc.renderExtent);
            float2 tapUv = (float2(tapFull) + 0.5) / float2(pc.renderExtent);
            float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, tapViewZ);
            float3 tapNormal = UnpackNormalOct16(sNormal[idx]);
            float tapHitT = sHitT[idx];

            float r = float(d) / float(GI_DENOISE_RADIUS);
            float w = exp(-0.66 * r * r);
            w *= exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
            w *= pow(saturate(dot(centerNormal, tapNormal)), GI_DENOISE_NORMAL_POWER);
            if (centerHitT >= 0.0 && tapHitT >= 0.0) {
                w *= exp(-GI_DENOISE_HITT_SENSITIVITY * abs(tapHitT - centerHitT) / (max(centerHitT, tapHitT) + 0.25));
            }
            // GTAO steering: receiver-occlusion disparity marks corner/contact texels the plane/normal/hitT guides can't separate.
            w *= exp2(-20.0 * abs(sAO[idx] - sAO[centerIdx]));

            sumR += UnpackHalf2x16Pair(sShR[idx]) * w;
            sumG += UnpackHalf2x16Pair(sShG[idx]) * w;
            sumB += UnpackHalf2x16Pair(sShB[idx]) * w;
            sumW += w;
        }
        float invW = 1.0 / sumW;
        sumR *= invW;
        sumG *= invW;
        sumB *= invW;
    }

    rdgStorageFloat4[pc.dstShRIndex][gatherPixel] = sumR;
    rdgStorageFloat4[pc.dstShGIndex][gatherPixel] = sumG;
    rdgStorageFloat4[pc.dstShBIndex][gatherPixel] = sumB;
}

// Chroma-only widening: YCoCg commutes with SH and the kernel, so CoCg gets extra wide iterations while Y keeps the base chain's resolution. Direct loads; an LDS tile at these strides would be oversized.
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeGIDenoiseChroma(uniform GIDenoisePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 gatherPixel = dtid.xy;
    if (any(gatherPixel >= pc.gatherExtent)) {
        return;
    }

    float4 shR = rdgTexturesFloat4[pc.srcShRIndex].Load(int3(gatherPixel, 0));
    float4 shG = rdgTexturesFloat4[pc.srcShGIndex].Load(int3(gatherPixel, 0));
    float4 shB = rdgTexturesFloat4[pc.srcShBIndex].Load(int3(gatherPixel, 0));

    uint2 centerGuide = rdgTexturesUInt2[pc.guideIndex].Load(int3(gatherPixel, 0)).xy;
    float2 centerData = rdgTexturesFloat2[pc.dataIndex].Load(int3(gatherPixel, 0)).xy;
    float centerViewZ = asfloat(centerGuide.y);
    if (centerViewZ != 0.0 && uint(centerData.y + 0.5) != GI_GATHER_TIER_BACKFACE) {
        SceneData scene = pc.sceneData[pc.sceneDataIndex];
        float4 shY = 0.25 * shR + 0.5 * shG + 0.25 * shB;
        // Filter chromaticity (CoCg/Y L0), not absolute CoCg: chroma magnitude scales with intensity, so absolute filtering across a steep luminance gradient dumps the bright side's chroma onto dim pixels (desaturated bright blotches near lights).
        float centerY = max(shY.x, 1e-6);
        float invCenterY = 1.0 / centerY;
        float4 sumCo = 0.5 * (shR - shB) * invCenterY;
        float4 sumCg = 0.25 * (2.0 * shG - shR - shB) * invCenterY;
        float sumW = 1.0;

        float3 centerNormal = UnpackNormalOct16(centerGuide.x);
        bool bCenterFar = uint(centerData.y + 0.5) == GI_GATHER_TIER_SKY || centerData.x >= GI_DENOISE_HITT_FAR_METERS;
        float centerHitT = bCenterFar ? -1.0 : centerData.x;
        uint2 centerFull = GIGatherRepresentativePixel(gatherPixel, pc.renderExtent);
        float centerAO = pc.aoIndex != ~0x0u ? rdgTexturesFloat2[pc.aoIndex].Load(int3(centerFull, 0)).y : 1.0;
        float2 centerUv = (float2(centerFull) + 0.5) / float2(pc.renderExtent);
        float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, centerUv, centerViewZ);
        float invPlaneSigma = 1.0 / (GI_DENOISE_PLANE_DIST_SIGMA * abs(centerViewZ));

        const int2 dir = pc.direction == 0u ? int2(1, 0) : int2(0, 1);
        int step = int(pc.stepSize);
        [unroll]
        for (int d = -GI_DENOISE_RADIUS; d <= GI_DENOISE_RADIUS; d++) {
            if (d == 0) { continue; }
            int2 tap = clamp(int2(gatherPixel) + dir * (d * step), int2(0), int2(pc.gatherExtent) - 1);
            uint2 tapGuide = rdgTexturesUInt2[pc.guideIndex].Load(int3(tap, 0)).xy;
            float2 tapData = rdgTexturesFloat2[pc.dataIndex].Load(int3(tap, 0)).xy;
            float tapViewZ = asfloat(tapGuide.y);
            if (tapViewZ == 0.0 || uint(tapData.y + 0.5) == GI_GATHER_TIER_BACKFACE) { continue; }

            uint2 tapFull = GIGatherRepresentativePixel(uint2(tap), pc.renderExtent);
            float2 tapUv = (float2(tapFull) + 0.5) / float2(pc.renderExtent);
            float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, tapViewZ);
            bool bTapFar = uint(tapData.y + 0.5) == GI_GATHER_TIER_SKY || tapData.x >= GI_DENOISE_HITT_FAR_METERS;
            float tapHitT = bTapFar ? -1.0 : tapData.x;

            float r = float(d) / float(GI_DENOISE_RADIUS);
            float w = exp(-0.66 * r * r);
            w *= exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
            w *= pow(saturate(dot(centerNormal, UnpackNormalOct16(tapGuide.x))), GI_DENOISE_NORMAL_POWER);
            if (centerHitT >= 0.0 && tapHitT >= 0.0) {
                w *= exp(-GI_DENOISE_HITT_SENSITIVITY * abs(tapHitT - centerHitT) / (max(centerHitT, tapHitT) + 0.25));
            }
            if (pc.aoIndex != ~0x0u) {
                w *= exp2(-20.0 * abs(rdgTexturesFloat2[pc.aoIndex].Load(int3(tapFull, 0)).y - centerAO));
            }
            if (w <= 1e-4) { continue; }

            float4 tapShR = rdgTexturesFloat4[pc.srcShRIndex].Load(int3(tap, 0));
            float4 tapShG = rdgTexturesFloat4[pc.srcShGIndex].Load(int3(tap, 0));
            float4 tapShB = rdgTexturesFloat4[pc.srcShBIndex].Load(int3(tap, 0));
            float tapY = max(0.25 * tapShR.x + 0.5 * tapShG.x + 0.25 * tapShB.x, 1e-6);
            w *= pow(min(centerY, tapY) / max(centerY, tapY), pc.chromaLumaPower);
            if (w <= 1e-4) { continue; }

            float tapNorm = w / tapY;
            sumCo += 0.5 * (tapShR - tapShB) * tapNorm;
            sumCg += 0.25 * (2.0 * tapShG - tapShR - tapShB) * tapNorm;
            sumW += w;
        }

        float scale = shY.x / sumW;
        float4 co = sumCo * scale;
        float4 cg = sumCg * scale;
        shR = shY + co - cg;
        shG = shY + cg;
        shB = shY - co - cg;
    }

    rdgStorageFloat4[pc.dstShRIndex][gatherPixel] = shR;
    rdgStorageFloat4[pc.dstShGIndex][gatherPixel] = shG;
    rdgStorageFloat4[pc.dstShBIndex][gatherPixel] = shB;
}
