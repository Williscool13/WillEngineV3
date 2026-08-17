import push_constant_interop;
import constants_interop;
import flags_interop;
import common_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

static const float DOF_SKY_VIEWZ = 3.0e4;

// Signed circle of confusion in full-res pixels
float DofSignedCoc(float viewZ, DofCocPushConstant pc) {
    float delta = viewZ - pc.focusDistance;
    float dist = abs(delta) - pc.sharpHalfRange;
    if (dist <= 0.0) { return 0.0; }
    if (delta < 0.0) { return -saturate(dist * pc.nearTransitionInv) * pc.nearRadiusPx; }
    return saturate(dist * pc.farTransitionInv) * pc.farRadiusPx;
}

[shader("compute")]
[numthreads(POST_PROCESS_DOF_DISPATCH_X, POST_PROCESS_DOF_DISPATCH_Y, 1)]
void ComputeDofCoc(uniform DofCocPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.outputExtent)) { return; }

    float3 color = float3(0.0, 0.0, 0.0);
    float coc = 0.0;
    float maxMagnitude = -1.0;

    for (int i = 0; i < 4; ++i) {
        int2 fullCoord = min(int2(pixel) * 2 + int2(i & 1, i >> 1), int2(pc.inputExtent) - 1);
        color += rdgTexturesFloat4[pc.sceneColorIndex].Load(int3(fullCoord, 0)).rgb;

        int2 depthCoord = min(int2((float2(fullCoord) + 0.5) * float2(pc.renderExtent) / float2(pc.inputExtent)), int2(pc.renderExtent) - 1);
        float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(depthCoord, 0)).r;
        float viewZ = depth > 0.0 ? ScreenToViewSpaceDepth(depth, pc.sceneData.depthLinearizeMult, pc.sceneData.depthLinearizeAdd) : DOF_SKY_VIEWZ;

        float sampleCoc = DofSignedCoc(viewZ, pc);
        if (abs(sampleCoc) > maxMagnitude) {
            maxMagnitude = abs(sampleCoc);
            coc = sampleCoc;
        }
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(color * 0.25, coc);
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
    float angleOffset = noise.x * 6.2831853;

    int sampleCount = clamp(int(ceil(searchRadius * 4.0)), 8, POST_PROCESS_DOF_SAMPLE_COUNT);

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

    float nearAlpha = max(saturate(nearCoverage / float(sampleCount)), saturate(-centerCoc));

    rdgStorageFloat4[pc.nearOutputIndex][pixel] = float4(nearColor / max(nearWeight, 1e-6), nearAlpha);
    rdgStorageFloat4[pc.farOutputIndex][pixel] = float4(farColor / farWeight, 1.0);
}
