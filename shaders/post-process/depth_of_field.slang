import push_constant_interop;
import constants_interop;
import flags_interop;
import common_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

static const float DOF_SKY_VIEWZ = 3.0e4;

// Signed circle of confusion in full-res pixels
float DofSignedCoc(float viewZ, float focusDistance, float sharpHalfRange, float nearTransitionInv, float farTransitionInv, float nearRadiusPx, float farRadiusPx) {
    float delta = viewZ - focusDistance;
    float dist = abs(delta) - sharpHalfRange;
    if (dist <= 0.0) { return 0.0; }
    if (delta < 0.0) { return -saturate(dist * nearTransitionInv) * nearRadiusPx; }
    return saturate(dist * farTransitionInv) * farRadiusPx;
}

[shader("compute")]
[numthreads(POST_PROCESS_DOF_DISPATCH_X, POST_PROCESS_DOF_DISPATCH_Y, 1)]
void ComputeDofCoc(uniform DofCocPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.outputExtent)) { return; }

    float3 colors[4];
    float cocs[4];
    float coc = 0.0;
    float maxMagnitude = -1.0;
    float minFar = 1e9;

    for (int i = 0; i < 4; ++i) {
        int2 fullCoord = min(int2(pixel) * 2 + int2(i & 1, i >> 1), int2(pc.inputExtent) - 1);
        colors[i] = rdgTexturesFloat4[pc.sceneColorIndex].Load(int3(fullCoord, 0)).rgb;

        int2 depthCoord = min(int2((float2(fullCoord) + 0.5) * float2(pc.renderExtent) / float2(pc.inputExtent)), int2(pc.renderExtent) - 1);
        float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(depthCoord, 0)).r;
        float viewZ = depth > 0.0 ? ScreenToViewSpaceDepth(depth, pc.sceneData.depthLinearizeMult, pc.sceneData.depthLinearizeAdd) : DOF_SKY_VIEWZ;

        cocs[i] = DofSignedCoc(viewZ, pc.focusDistance, pc.sharpHalfRange, pc.nearTransitionInv, pc.farTransitionInv, pc.nearRadiusPx, pc.farRadiusPx);
        if (abs(cocs[i]) > maxMagnitude) {
            maxMagnitude = abs(cocs[i]);
            coc = cocs[i];
        }
    }

    for (int j = 0; j < 16; ++j) {
        int2 ringCoord = clamp(int2(pixel) * 2 + int2((j & 3) - 1, (j >> 2) - 1), int2(0, 0), int2(pc.inputExtent) - 1);
        int2 ringDepthCoord = min(int2((float2(ringCoord) + 0.5) * float2(pc.renderExtent) / float2(pc.inputExtent)), int2(pc.renderExtent) - 1);
        float ringDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(ringDepthCoord, 0)).r;
        float ringViewZ = ringDepth > 0.0 ? ScreenToViewSpaceDepth(ringDepth, pc.sceneData.depthLinearizeMult, pc.sceneData.depthLinearizeAdd) : DOF_SKY_VIEWZ;
        float ringCoc = DofSignedCoc(ringViewZ, pc.focusDistance, pc.sharpHalfRange, pc.nearTransitionInv, pc.farTransitionInv, pc.nearRadiusPx, pc.farRadiusPx);
        minFar = min(minFar, max(ringCoc, 0.0));
    }

    float3 color = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    float cocSign = coc >= 0.0 ? 1.0 : -1.0;
    for (int i = 0; i < 4; ++i) {
        float w = (cocs[i] * cocSign > 0.0 ? 1.0 : 0.0) + 1e-2;
        color += colors[i] * w;
        weightSum += w;
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(color / weightSum, coc);
    rdgStorageFloat[pc.farMinIndex][pixel] = minFar;
}

groupshared float g_maxNear[POST_PROCESS_DOF_TILE_SIZE * POST_PROCESS_DOF_TILE_SIZE];
groupshared float g_maxFar[POST_PROCESS_DOF_TILE_SIZE * POST_PROCESS_DOF_TILE_SIZE];


[shader("compute")]
[numthreads(POST_PROCESS_DOF_TILE_SIZE, POST_PROCESS_DOF_TILE_SIZE, 1)]
void ComputeDofTileMax(uniform DofTileMaxPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex) {
    uint2 pixel = gid.xy * POST_PROCESS_DOF_TILE_SIZE + gtid.xy;

    float coc = 0.0;
    if (all(pixel < pc.sourceExtent)) {
        coc = rdgTexturesFloat4[pc.cocIndex].Load(int3(pixel, 0)).a;
    }
    g_maxNear[groupIndex] = max(-coc, 0.0);
    g_maxFar[groupIndex] = max(coc, 0.0);

    GroupMemoryBarrierWithGroupSync();

    for (uint cutoff = (POST_PROCESS_DOF_TILE_SIZE * POST_PROCESS_DOF_TILE_SIZE) >> 1; cutoff > 0; cutoff >>= 1) {
        if (groupIndex < cutoff) {
            g_maxNear[groupIndex] = max(g_maxNear[groupIndex], g_maxNear[groupIndex + cutoff]);
            g_maxFar[groupIndex] = max(g_maxFar[groupIndex], g_maxFar[groupIndex + cutoff]);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        rdgStorageFloat2[pc.tileMaxIndex][gid.xy] = float2(g_maxNear[0], g_maxFar[0]);
    }
}

[shader("compute")]
[numthreads(POST_PROCESS_DOF_NEIGHBOR_DISPATCH_X, POST_PROCESS_DOF_NEIGHBOR_DISPATCH_Y, 1)]
void ComputeDofNeighborMax(uniform DofNeighborMaxPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 tileCoord = dtid.xy;
    if (any(tileCoord >= pc.tileExtent)) { return; }

    float2 maxCoc = float2(0.0, 0.0);
    int radius = int(pc.dilationRadius);
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            int2 sampleCoord = int2(tileCoord) + int2(x, y);
            if (any(sampleCoord < int2(0, 0)) || any(sampleCoord >= int2(pc.tileExtent))) { continue; }
            maxCoc = max(maxCoc, rdgTexturesFloat2[pc.tileMaxIndex].Load(int3(sampleCoord, 0)).rg);
        }
    }

    rdgStorageFloat2[pc.neighborMaxIndex][tileCoord] = maxCoc;

    if (max(maxCoc.x, maxCoc.y) >= 1.0) {
        uint slot;
        InterlockedAdd(pc.indirectArgsBuffer[0], 1u, slot);
        pc.tileListBuffer[slot] = tileCoord.x | (tileCoord.y << 16);
    }
}

static const float DOF_GOLDEN_ANGLE = 2.39996323;

[shader("compute")]
[numthreads(POST_PROCESS_DOF_TILE_SIZE, POST_PROCESS_DOF_TILE_SIZE, 1)]
void ComputeDofGather(uniform DofGatherPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID) {
    uint packedTile = pc.tileListBuffer[gid.x];
    uint2 tileCoord = uint2(packedTile & 0xFFFFu, packedTile >> 16);
    uint2 pixel = tileCoord * uint(POST_PROCESS_DOF_TILE_SIZE) + gtid.xy;
    if (any(pixel >= pc.sourceExtent)) { return; }

    float2 tileMax = rdgTexturesFloat2[pc.tileNeighborMaxIndex].Load(int3(tileCoord, 0)).rg * 0.5;
    float searchRadius = max(tileMax.x, tileMax.y);
    if (searchRadius < 0.5) { return; }

    float4 centerSample = rdgTexturesFloat4[pc.cocIndex].Load(int3(pixel, 0));
    float centerCoc = centerSample.a * 0.5;

    float2 noise = assetTextures[BLUE_NOISE_BINDLESS_INDEX].Load(int3(int2(pixel & uint(BLUE_NOISE_SIZE_MASK)), 0)).rg;
    noise = frac(noise + float(pc.frameIndex & 63u) * float2(0.7548776662, 0.5698402909));
    float angleOffset = noise.x * 6.2831853;

    int sampleCount = clamp(int(ceil(searchRadius * 5.0)), 8, POST_PROCESS_DOF_SAMPLE_COUNT);

    float3 nearColor = float3(0.0, 0.0, 0.0);
    float nearWeight = 0.0;
    float nearCoverage = 0.0;
    float3 farColor = centerSample.rgb * 1e-3;
    float farWeight = 1e-3;

    for (int i = 0; i < sampleCount; ++i) {
        float radius = searchRadius * sqrt((float(i) + noise.y) / float(sampleCount));
        float angle = float(i) * DOF_GOLDEN_ANGLE + angleOffset;
        int2 sampleCoord = clamp(int2(round(float2(pixel) + float2(cos(angle), sin(angle)) * radius)), int2(0, 0), int2(pc.sourceExtent) - 1);

        float4 s = rdgTexturesFloat4[pc.cocIndex].Load(int3(sampleCoord, 0));
        float sampleCoc = s.a * 0.5;
        float spread = abs(sampleCoc);
        float coverage = saturate(spread - radius + 1.0);
        if (coverage <= 0.0) { continue; }

        float weight = coverage / max(spread * spread, 1.0);
        weight /= 1.0 + dot(s.rgb, float3(0.2126, 0.7152, 0.0722));

        if (sampleCoc < 0.0) {
            nearColor += s.rgb * weight;
            nearWeight += weight;
            nearCoverage += coverage;
        } else {
            // Sharper than the center means in front of it, which must not bleed backwards
            float behind = saturate(sampleCoc - centerCoc + 1.0);
            farColor += s.rgb * weight * behind;
            farWeight += weight * behind;
        }
    }

    float nearFrac = tileMax.x > 0.0 ? (tileMax.x * tileMax.x) / (searchRadius * searchRadius) : 1.0;
    float nearAlpha = max(saturate(nearCoverage / max(float(sampleCount) * nearFrac, 1.0)), saturate(-centerCoc));

    rdgStorageFloat4[pc.nearOutputIndex][pixel] = float4(nearColor / max(nearWeight, 1e-6), nearAlpha);
    rdgStorageFloat4[pc.farOutputIndex][pixel] = float4(farColor / farWeight, 1.0);
}

[shader("compute")]
[numthreads(POST_PROCESS_DOF_DISPATCH_X, POST_PROCESS_DOF_DISPATCH_Y, 1)]
void ComputeDofLayerBlur(uniform DofLayerBlurPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.extent)) { return; }

    float4 nearSum = float4(0.0, 0.0, 0.0, 0.0);
    float4 farSum = float4(0.0, 0.0, 0.0, 0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 c = clamp(int2(pixel) + int2(x, y), int2(0, 0), int2(pc.extent) - 1);
            float4 n = rdgTexturesFloat4[pc.nearIndex].Load(int3(c, 0));
            float4 f = rdgTexturesFloat4[pc.farIndex].Load(int3(c, 0));
            nearSum += float4(n.rgb * n.a, n.a);
            farSum += float4(f.rgb * f.a, f.a);
        }
    }

    float4 nearOut = float4(nearSum.rgb / max(nearSum.a, 1e-6), nearSum.a / 9.0);
    float4 farBlurred = float4(farSum.rgb / max(farSum.a, 1e-6), farSum.a / 9.0);

    float centerCoc = rdgTexturesFloat4[pc.cocIndex].Load(int3(pixel, 0)).a;
    float4 farOut = lerp(rdgTexturesFloat4[pc.farIndex].Load(int3(pixel, 0)), farBlurred, saturate((max(centerCoc, 0.0) - 1.0) * 0.5));

    rdgStorageFloat4[pc.nearOutputIndex][pixel] = nearOut;
    rdgStorageFloat4[pc.farOutputIndex][pixel] = farOut;
}

[shader("compute")]
[numthreads(POST_PROCESS_DOF_DISPATCH_X, POST_PROCESS_DOF_DISPATCH_Y, 1)]
void ComputeDofComposite(uniform DofCompositePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.extent)) { return; }

    float3 color = rdgTexturesFloat4[pc.sceneColorIndex].Load(int3(pixel, 0)).rgb;
    float2 uv = (float2(pixel) + 0.5) / float2(pc.extent);

    float coc = rdgTexturesFloat[pc.farMinCocIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;

    float4 farLayer = rdgTexturesFloat4[pc.farIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
    color = lerp(color, farLayer.rgb, saturate((coc - 1.0) / 3.0));

    float4 nearLayer = rdgTexturesFloat4[pc.nearIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
    color = lerp(color, nearLayer.rgb, nearLayer.a);

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(color, 1.0);
}
