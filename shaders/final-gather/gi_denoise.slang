import common_interop;
import push_constant_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;
import gi_gather_common;

// Separable bilateral gaussian over the gather SH (H then V via pc.direction); FP16 groupshared preload, weights from plane-distance/normal/hitT deltas.

static const int GI_DENOISE_RADIUS = 4;
static const int GI_DENOISE_GROUP = 16;
static const int GI_DENOISE_TILE = GI_DENOISE_GROUP + 2 * GI_DENOISE_RADIUS;
static const float GI_DENOISE_PLANE_DIST_SIGMA = 0.01;
static const float GI_DENOISE_NORMAL_POWER = 32.0;
static const float GI_DENOISE_HITT_SENSITIVITY = 3.0;

groupshared uint2 sShR[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared uint2 sShG[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared uint2 sShB[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared uint sNormal[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared float sViewZ[GI_DENOISE_TILE * GI_DENOISE_GROUP];
groupshared float sHitT[GI_DENOISE_TILE * GI_DENOISE_GROUP];

[shader("compute")]
[numthreads(GI_DENOISE_GROUP, GI_DENOISE_GROUP, 1)]
void ComputeGIDenoise(uniform GIDenoisePushConstant pc, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    const bool horizontal = pc.direction == 0u;
    const int2 groupOrigin = int2(gid.xy) * GI_DENOISE_GROUP;

    uint threadIndex = gtid.y * GI_DENOISE_GROUP + gtid.x;
    for (uint t = threadIndex; t < uint(GI_DENOISE_TILE * GI_DENOISE_GROUP); t += GI_DENOISE_GROUP * GI_DENOISE_GROUP) {
        int along = int(t % GI_DENOISE_TILE) - GI_DENOISE_RADIUS;
        int across = int(t / GI_DENOISE_TILE);
        int2 pos = groupOrigin + (horizontal ? int2(along, across) : int2(across, along));
        bool inside = all(pos >= int2(0)) && all(pos < int2(pc.gatherExtent));
        int2 p = clamp(pos, int2(0), int2(pc.gatherExtent) - 1);

        uint2 fullPixel = GIGatherRepresentativePixel(uint2(p), pc.frameIndex, pc.renderExtent);
        float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
        uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));
        bool valid = inside && depth > 0.0 && (gt1.w & 1u) != 0u;

        sShR[t] = PackHalf2x16Pair(rdgTexturesFloat4[pc.srcShRIndex].Load(int3(p, 0)));
        sShG[t] = PackHalf2x16Pair(rdgTexturesFloat4[pc.srcShGIndex].Load(int3(p, 0)));
        sShB[t] = PackHalf2x16Pair(rdgTexturesFloat4[pc.srcShBIndex].Load(int3(p, 0)));
        sNormal[t] = gt1.r;
        sViewZ[t] = valid ? ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd) : 0.0;
        sHitT[t] = rdgTexturesFloat2[pc.dataIndex].Load(int3(p, 0)).x;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 gatherPixel = uint2(groupOrigin) + gtid.xy;
    if (any(gatherPixel >= pc.gatherExtent)) {
        return;
    }

    uint along = horizontal ? gtid.x : gtid.y;
    uint across = horizontal ? gtid.y : gtid.x;
    uint centerIdx = across * GI_DENOISE_TILE + along + GI_DENOISE_RADIUS;

    float4 sumR = UnpackHalf2x16Pair(sShR[centerIdx]);
    float4 sumG = UnpackHalf2x16Pair(sShG[centerIdx]);
    float4 sumB = UnpackHalf2x16Pair(sShB[centerIdx]);
    float centerViewZ = sViewZ[centerIdx];
    if (centerViewZ != 0.0) {
        float3 centerNormal = UnpackNormalOct16(sNormal[centerIdx]);
        float centerHitT = sHitT[centerIdx];
        uint2 centerFull = GIGatherRepresentativePixel(gatherPixel, pc.frameIndex, pc.renderExtent);
        float2 centerUv = (float2(centerFull) + 0.5) / float2(pc.renderExtent);
        float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, centerUv, centerViewZ);
        float invPlaneSigma = 1.0 / (GI_DENOISE_PLANE_DIST_SIGMA * abs(centerViewZ));

        const int2 dir = horizontal ? int2(1, 0) : int2(0, 1);
        float sumW = 1.0;
        [unroll]
        for (int d = -GI_DENOISE_RADIUS; d <= GI_DENOISE_RADIUS; d++) {
            if (d == 0) { continue; }
            uint idx = uint(int(centerIdx) + d);
            float tapViewZ = sViewZ[idx];
            if (tapViewZ == 0.0) { continue; }

            uint2 tapFull = GIGatherRepresentativePixel(uint2(int2(gatherPixel) + dir * d), pc.frameIndex, pc.renderExtent);
            float2 tapUv = (float2(tapFull) + 0.5) / float2(pc.renderExtent);
            float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, tapViewZ);
            float3 tapNormal = UnpackNormalOct16(sNormal[idx]);
            float tapHitT = sHitT[idx];

            float r = float(d) / float(GI_DENOISE_RADIUS);
            float w = exp(-0.66 * r * r);
            w *= exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
            w *= pow(saturate(dot(centerNormal, tapNormal)), GI_DENOISE_NORMAL_POWER);
            w *= exp(-GI_DENOISE_HITT_SENSITIVITY * abs(tapHitT - centerHitT) / (max(centerHitT, tapHitT) + 1e-3));

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
