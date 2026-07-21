import push_constant_interop;
import constants_interop;
import flags_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

// Camera-only reprojection subtracted from the gbuffer motion vector leaves object motion alone. Both terms
// carry the same jitter offset and the same current-frame NDC, so those cancel and the residual is the
// model-matrix delta only; the jitter terms must still be spelled out to match the gbuffer expression.
[shader("compute")]
[numthreads(POST_PROCESS_MOTION_BLUR_DISPATCH_X, POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1)]
void ComputeMotionBlurVelocityExtract(uniform MotionBlurVelocityExtractPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.extent)) { return; }
    uint2 pixelCoord = dtid.xy;

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixelCoord, 0));
    float depth = rdgTexturesFloat[pc.depthBufferIndex].Load(int3(pixelCoord, 0)).r;
    if ((gt1.w & GBUFFER1_LIT_BIT) == 0u || depth <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixelCoord] = float2(0.0, 0.0);
        return;
    }

    float2 velocity = UnpackMotionVectors(gt1.g);
    if (pc.bObjectOnly != 0u) {
        float2 uv = (float2(pixelCoord) + 0.5) / float2(pc.extent);
        float3 worldPos = ReconstructWorldPosition(pc.sceneData.invViewProj, uv, depth);
        float4 prevClip = mul(pc.sceneData.prevViewProj, float4(worldPos, 1.0));
        if (prevClip.w <= 1e-6) {
            rdgStorageFloat2[pc.outputIndex][pixelCoord] = float2(0.0, 0.0);
            return;
        }
        float2 cameraVelocity = (prevClip.xy / prevClip.w - (uv * 2.0 - 1.0)) - pc.sceneData.jitter + pc.sceneData.prevJitter;
        velocity -= cameraVelocity;
    }

    rdgStorageFloat2[pc.outputIndex][pixelCoord] = velocity;
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
        velocity = rdgTexturesFloat2[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).rg * pc.velocityScale;
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
        float pixelLen = length(maxVelocity * 0.5 * float2(pc.velocityBufferSize));
        if (pixelLen > float(POST_PROCESS_MOTION_BLUR_TILE_SIZE)) {
            maxVelocity *= float(POST_PROCESS_MOTION_BLUR_TILE_SIZE) / pixelLen;
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

    [ForceUnroll]
    for (int y = -1; y <= 1; ++y) {
        [ForceUnroll]
        for (int x = -1; x <= 1; ++x) {
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
[numthreads(POST_PROCESS_MOTION_BLUR_DISPATCH_X, POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1)]
void ComputeMotionBlurReconstruction(uniform MotionBlurReconstructionPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;
    if (any(pixelCoord >= pc.srcBufferSize)) { return; }

    float2 uv = (float2(pixelCoord) + 0.5) / pc.srcBufferSize;
    float depthLinearizeMult = pc.sceneData.depthLinearizeMult;
    float depthLinearizeAdd = pc.sceneData.depthLinearizeAdd;

    uint2 tileCoord = pixelCoord / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    float3 centerColor = rdgTexturesFloat4[pc.sceneColorIndex].Load(int3(pixelCoord, 0)).rgb;

    // Tile velocities are already shutter-scaled and tile-clamped; per-pixel velocities below are raw and still need the scale
    float2 blurDir = rdgTexturesFloat2[pc.tileNeighborMaxIndex].Load(int3(tileCoord, 0)).rg * 0.5;
    float blurLen = length(blurDir * pc.srcBufferSize);
    if (blurLen < 0.5) {
        rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(centerColor, 1.0);
        return;
    }
    if (blurLen > float(POST_PROCESS_MOTION_BLUR_TILE_SIZE)) {
        blurDir *= float(POST_PROCESS_MOTION_BLUR_TILE_SIZE) / blurLen;
        blurLen = float(POST_PROCESS_MOTION_BLUR_TILE_SIZE);
    }

    float2 centerVelocityUv = rdgTexturesFloat2[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).rg * 0.5;
    float centerViewZ = ScreenToViewSpaceDepth(rdgTexturesFloat[pc.depthBufferIndex].Load(int3(pixelCoord, 0)).r, depthLinearizeMult, depthLinearizeAdd);
    float centerSpreadLen = min(length(centerVelocityUv * pc.velocityScale * pc.srcBufferSize), float(POST_PROCESS_MOTION_BLUR_TILE_SIZE));

    float3 colorAccum = float3(0.0, 0.0, 0.0);
    float weightAccum = 0.0;
    float sampleCount = 0.0;

    float noise = frac(52.9829189 * frac(0.06711056 * pixelCoord.x + 0.00583715 * pixelCoord.y));

    for (int i = 1; i <= POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2; ++i) {
        float t = min((float(i) - 0.5 + noise) / float(POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2), 1.0);
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

            int2 sampleCoord = int2(sampleUV * pc.srcBufferSize);
            pairColor[side] = rdgTexturesFloat4[pc.sceneColorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], sampleUV, 0).rgb;
            float sampleViewZ = ScreenToViewSpaceDepth(rdgTexturesFloat[pc.depthBufferIndex].Load(int3(sampleCoord, 0)).r, depthLinearizeMult, depthLinearizeAdd);
            float2 sampleVelocityUv = rdgTexturesFloat2[pc.velocityBufferIndex].Load(int3(sampleCoord, 0)).rg * 0.5;
            float sampleSpreadLen = min(length(sampleVelocityUv * pc.velocityScale * pc.srcBufferSize), float(POST_PROCESS_MOTION_BLUR_TILE_SIZE));

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

    if (sampleCount < 0.5) {
        rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(centerColor, 1.0);
        return;
    }

    float4 sumColor = float4(colorAccum, weightAccum) / sampleCount;
    float3 finalColor = sumColor.rgb + (1.0 - sumColor.w) * centerColor;
    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(finalColor, 1.0);
}
