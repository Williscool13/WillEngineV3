import push_constant_interop;
import constants_interop;
import flags_interop;
import common_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

static const float OBJECT_MOTION_FAR_VIEWZ = 3.0e4;

// Camera-only reprojection subtracted from the gbuffer motion vector leaves object motion alone.
[shader("compute")]
[numthreads(POST_PROCESS_MOTION_BLUR_DISPATCH_X, POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1)]
void ComputeObjectMotionExtract(uniform ObjectMotionExtractPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) { return; }
    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float2 objectMotion = float2(0.0, 0.0);
    float viewZ = OBJECT_MOTION_FAR_VIEWZ;
    float blurMask = 0.0;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (depth > 0.0 && (gt1.w & GBUFFER1_LIT_BIT) != 0u) {
        float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
        viewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float3 xv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewZ);
        float4 prevClip = mul(scene.prevViewProj, float4(mul(scene.invView, float4(xv, 1.0)).xyz, 1.0));
        if (prevClip.w > 0.0) {
            float2 staticDeltaUv = ((prevClip.xy / prevClip.w) * 0.5 + 0.5) - uv - scene.jitter * 0.5 + scene.prevJitter * 0.5;
            objectMotion = UnpackMotionVectors(gt1.g) * 0.5 - staticDeltaUv;
        }
        blurMask = (gt1.w & GBUFFER1_NO_MOTION_BLUR_BIT) != 0u ? 0.0 : 1.0;
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(objectMotion, viewZ, blurMask);
}

// Output per pixel: (motionUv.xy with the blur mask folded in, linear viewZ, unused).
[shader("compute")]
[numthreads(POST_PROCESS_MOTION_BLUR_DISPATCH_X, POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1)]
void ComputeMotionBlurVelocityExtract(uniform MotionBlurVelocityExtractPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.extent)) { return; }
    uint2 pixelCoord = dtid.xy;

    int2 srcCoord = min(int2((float2(pixelCoord) + 0.5) * float2(pc.renderExtent) / float2(pc.extent)), int2(pc.renderExtent) - 1);

    if (pc.bObjectOnly != 0u) {
        float4 om = rdgTexturesFloat4[pc.objectMotionIndex].Load(int3(srcCoord, 0));
        rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(om.xy * om.w, om.z, 0.0);
        return;
    }

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(srcCoord, 0));
    float depth = rdgTexturesFloat[pc.depthBufferIndex].Load(int3(srcCoord, 0)).r;
    float viewZ = depth > 0.0 ? ScreenToViewSpaceDepth(depth, pc.sceneData.depthLinearizeMult, pc.sceneData.depthLinearizeAdd) : OBJECT_MOTION_FAR_VIEWZ;
    float2 velocityUv = float2(0.0, 0.0);
    if ((gt1.w & GBUFFER1_LIT_BIT) != 0u && (gt1.w & GBUFFER1_NO_MOTION_BLUR_BIT) == 0u && depth > 0.0) {
        velocityUv = UnpackMotionVectors(gt1.g) * 0.5;
    }
    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(velocityUv, viewZ, 0.0);
}

groupshared float g_velLenSq[POST_PROCESS_MOTION_BLUR_TILE_SIZE * POST_PROCESS_MOTION_BLUR_TILE_SIZE];
groupshared float2 g_vel[POST_PROCESS_MOTION_BLUR_TILE_SIZE * POST_PROCESS_MOTION_BLUR_TILE_SIZE];

// McGuire 2012 / Jimenez 2014.
[shader("compute")]
[numthreads(POST_PROCESS_MOTION_BLUR_TILE_SIZE, POST_PROCESS_MOTION_BLUR_TILE_SIZE, 1)]
void ComputeMotionBlurTileMax(uniform MotionBlurTileVelocityPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint groupIndex : SV_GroupIndex) {
    uint2 pixelCoord = gid.xy * POST_PROCESS_MOTION_BLUR_TILE_SIZE + gtid.xy;

    float2 velocity = float2(0.0, 0.0);
    if (all(pixelCoord < pc.velocityBufferSize)) {
        velocity = rdgTexturesFloat4[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).rg * pc.velocityScale;
    }
    g_velLenSq[groupIndex] = dot(velocity, velocity);
    g_vel[groupIndex] = velocity;

    GroupMemoryBarrierWithGroupSync();

    for (uint cutoff = (POST_PROCESS_MOTION_BLUR_TILE_SIZE * POST_PROCESS_MOTION_BLUR_TILE_SIZE) >> 1; cutoff > 0; cutoff >>= 1) {
        if (groupIndex < cutoff) {
            if (g_velLenSq[groupIndex + cutoff] > g_velLenSq[groupIndex]) {
                g_velLenSq[groupIndex] = g_velLenSq[groupIndex + cutoff];
                g_vel[groupIndex] = g_vel[groupIndex + cutoff];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        float2 maxVelocity = g_vel[0];
        float pixelLen = length(maxVelocity * float2(pc.velocityBufferSize));
        if (pixelLen > pc.maxRadiusPx) {
            maxVelocity *= pc.maxRadiusPx / pixelLen;
        }
        rdgStorageFloat2[pc.tileMaxIndex][gid.xy] = maxVelocity;
    }
}

[shader("compute")]
[numthreads(POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X, POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y, 1)]
void ComputeMotionBlurNeighborMax(uniform MotionBlurNeighborMaxPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 tileCoord = dtid.xy;

    if (any(tileCoord >= pc.tileBufferSize)) {
        return;
    }

    float maxVelocityLengthSq = 0.0;
    float2 maxVelocity = float2(0.0f, 0.0f);

    int radius = int(pc.dilationRadius);
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            int2 sampleCoord = int2(tileCoord) + int2(x, y);

            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(pc.tileBufferSize) - int2(1, 1));

            float2 velocity = rdgTexturesFloat2[pc.tileMaxIndex].Load(int3(sampleCoord, 0)).rg;
            float velocityLengthSq = dot(velocity, velocity);

            if (velocityLengthSq > maxVelocityLengthSq) {
                maxVelocityLengthSq = velocityLengthSq;
                maxVelocity = velocity;
            }
        }
    }

    rdgStorageFloat2[pc.neighborMaxIndex][tileCoord] = float2(maxVelocity);

    if (pc.indirectArgsBuffer != nullptr) {
        float pixelLen = length(maxVelocity * float2(pc.velocityBufferSize));
        if (pixelLen >= 0.5) {
            uint slot;
            InterlockedAdd(pc.indirectArgsBuffer[0], 1u, slot);
            pc.tileListBuffer[slot] = tileCoord.x | (tileCoord.y << 16);
        }
    }
}

// View-depth soft classification: delta > 0 means the sample is farther (behind the center).
// x = sample-behind weight (pairs with the center's spread), y = sample-in-front weight (pairs with the sample's spread).
float2 DepthCmp(float centerViewZ, float sampleViewZ, float depthScale) {
    float delta = sampleViewZ - centerViewZ;
    return saturate(0.5 + float2(depthScale, -depthScale) * delta);
}

// offsetLen and spreadLen both in pixels
float2 SpreadCmp(float offsetLen, float2 spreadLen) {
    return saturate(spreadLen - offsetLen + 1.0);
}

// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
[shader("compute")]
[numthreads(POST_PROCESS_MOTION_BLUR_TILE_SIZE, POST_PROCESS_MOTION_BLUR_TILE_SIZE, 1)]
void ComputeMotionBlurReconstruction(uniform MotionBlurReconstructionPushConstant pc, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
    uint packedTile = pc.tileListBuffer[gid.x];
    uint2 pixelCoord = uint2(packedTile & 0xFFFFu, packedTile >> 16) * uint(POST_PROCESS_MOTION_BLUR_TILE_SIZE) + gtid.xy;
    if (any(pixelCoord >= pc.srcBufferSize)) { return; }

    float2 uv = (float2(pixelCoord) + 0.5) / pc.srcBufferSize;
    float3 centerColor = rdgTexturesFloat4[pc.sceneColorIndex].Load(int3(pixelCoord, 0)).rgb;

    float noise = frac(52.9829189 * frac(0.06711056 * pixelCoord.x + 0.00583715 * pixelCoord.y));

    // Half-tile jittered lookup
    int2 tileBufferSize = int2((pc.srcBufferSize + uint(POST_PROCESS_MOTION_BLUR_TILE_SIZE) - 1u) / uint(POST_PROCESS_MOTION_BLUR_TILE_SIZE));
    float2 jitterNoise = assetTextures[BLUE_NOISE_BINDLESS_INDEX].Load(int3(int2(pixelCoord & uint(BLUE_NOISE_SIZE_MASK)), 0)).rg;
    float2 tileJitter = (jitterNoise - 0.5) * float(POST_PROCESS_MOTION_BLUR_TILE_SIZE);
    int2 tileCoord = clamp(int2((float2(pixelCoord) + tileJitter) / float(POST_PROCESS_MOTION_BLUR_TILE_SIZE)), int2(0, 0), tileBufferSize - 1);

    // Tile velocities are already shutter-scaled and radius-clamped; per-pixel velocities below are raw and still need the scale
    float2 blurDir = rdgTexturesFloat2[pc.tileNeighborMaxIndex].Load(int3(tileCoord, 0)).rg;
    float blurLen = length(blurDir * pc.srcBufferSize);
    if (blurLen < 0.5) { return; }
    if (blurLen > pc.maxRadiusPx) {
        blurDir *= pc.maxRadiusPx / blurLen;
        blurLen = pc.maxRadiusPx;
    }

    float4 centerVelocity = rdgTexturesFloat4[pc.velocityBufferIndex].Load(int3(pixelCoord, 0));
    float centerViewZ = centerVelocity.z;
    float centerSpreadLen = min(length(centerVelocity.xy * pc.velocityScale * pc.srcBufferSize), pc.maxRadiusPx);

    float3 colorAccum = float3(0.0, 0.0, 0.0);
    float weightAccum = 0.0;
    float sampleCount = 0.0;

    int pairCount = clamp(int(ceil(blurLen * 0.5)), 2, POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2);
    for (int i = 1; i <= pairCount; ++i) {
        float t = min((float(i) - 0.5 + noise) / float(pairCount), 1.0);
        float offsetLen = t * blurLen;

        float3 pairColor[2] = { float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0) };
        float pairWeight[2] = { 0.0, 0.0 };
        bool pairValid[2] = { false, false };

        [ForceUnroll]
        for (int side = 0; side < 2; ++side) {
            float dir = side == 0 ? 1.0 : -1.0;
            float2 sampleUV = uv + blurDir * t * dir;

            pairValid[side] = all(sampleUV >= 0.0) && all(sampleUV <= 1.0);
            if (!pairValid[side]) { continue; }

            int2 sampleCoord = min(int2(sampleUV * pc.srcBufferSize), int2(pc.srcBufferSize) - 1);
            pairColor[side] = rdgTexturesFloat4[pc.sceneColorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], sampleUV, 0).rgb;
            float4 sampleVelocity = rdgTexturesFloat4[pc.velocityBufferIndex].Load(int3(sampleCoord, 0));
            float sampleViewZ = sampleVelocity.z;
            float sampleSpreadLen = min(length(sampleVelocity.xy * pc.velocityScale * pc.srcBufferSize), pc.maxRadiusPx);

            float2 d = DepthCmp(centerViewZ, sampleViewZ, pc.depthScale);
            float2 s = SpreadCmp(offsetLen, float2(centerSpreadLen, sampleSpreadLen));
            pairWeight[side] = dot(d, s);
        }

        if (pairValid[0] && pairValid[1]) {
            // Shared max keeps background reconstruction symmetric across the mover (Jimenez)
            float w = max(pairWeight[0], pairWeight[1]);
            colorAccum += (pairColor[0] + pairColor[1]) * w;
            weightAccum += 2.0 * w;
            sampleCount += 2.0;
        } else {
            [ForceUnroll]
            for (int side = 0; side < 2; ++side) {
                if (pairValid[side]) {
                    colorAccum += pairColor[side] * pairWeight[side];
                    weightAccum += pairWeight[side];
                    sampleCount += 1.0;
                }
            }
        }
    }

    if (sampleCount < 0.5) { return; }

    float4 sumColor = float4(colorAccum, weightAccum) / sampleCount;
    float3 finalColor = sumColor.rgb + (1.0 - sumColor.w) * centerColor;
    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(finalColor, 1.0);
}
