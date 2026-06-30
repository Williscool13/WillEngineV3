import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

[shader("compute")]
[numthreads(POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X, POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y, 1)]
void ComputeMotionBlurTileMax(uniform MotionBlurTileVelocityPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 tileCoord = dtid.xy;

    if (any(tileCoord >= pc.tileBufferSize)) {
        return;
    }

    float maxVelocityLengthSq = 0.0;
    float2 maxVelocity = float2(0.0f, 0.0f);

    uint2 tileStart = tileCoord * POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    [ForceUnroll]
    for (uint y = 0; y < POST_PROCESS_MOTION_BLUR_TILE_SIZE; ++y) {
        [ForceUnroll]
        for (uint x = 0; x < POST_PROCESS_MOTION_BLUR_TILE_SIZE; ++x) {
            uint2 pixelCoord = tileStart + uint2(x, y);

            if (any(pixelCoord >= pc.velocityBufferSize))
                continue;

            float2 objectVelocity = rdgTexturesFloat4[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).ba;
            float velocityLengthSq = dot(objectVelocity, objectVelocity);

            if (velocityLengthSq > maxVelocityLengthSq) {
                maxVelocityLengthSq = velocityLengthSq;
                maxVelocity = objectVelocity;
            }
        }
    }

    rdgStorageFloat2[pc.tileMaxIndex][tileCoord] = float2(maxVelocity);
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

float2 DepthCmp(float centerDepth, float sampleDepth, float depthScale) {
    float s = (1 - sampleDepth);
    float c = (1 - centerDepth);
    return saturate(0.5 + float2(depthScale, -depthScale) * (s - c));
}

float2 SpreadCmp(float offsetLen, float2 spreadLen) {
    return saturate(spreadLen - offsetLen + 1.0);
}

// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
[shader("compute")]
[numthreads(POST_PROCESS_MOTION_BLUR_DISPATCH_X, POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1)]
void ComputeMotionBlurReconstruction(uniform MotionBlurReconstructionPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;
    if (any(pixelCoord >= pc.srcBufferSize)) return;

    float2 uv = (float2(pixelCoord) + 0.5) / pc.srcBufferSize;

    uint2 tileCoord = pixelCoord / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    float2 neighborMaxVelocity = rdgTexturesFloat2[pc.tileNeighborMaxIndex].Load(int3(tileCoord, 0)).rg * 0.5;
    float neighborMaxLen = length(neighborMaxVelocity * pc.srcBufferSize);
    float3 centerColor = rdgTexturesFloat4[pc.sceneColorIndex].Load(int3(pixelCoord, 0)).rgb;

    float blurLen = neighborMaxLen * pc.velocityScale;
    if (blurLen < 0.5) {
        rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(centerColor, 1.0);
        return;
    }

    float2 objectVelocity = rdgTexturesFloat4[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).ba * 0.5;
    float centerDepth = rdgTexturesFloat[pc.depthBufferIndex].Load(int3(pixelCoord, 0)).r;
    float centerSpreadLen = length(objectVelocity * pc.velocityScale * pc.srcBufferSize);

    float3 colorAccum = float3(0.0, 0.0, 0.0);
    float weightAccum = 0.0;
    float sampleCount = 0.0;

    float2 blurDir = neighborMaxVelocity * pc.velocityScale;
    float noise = frac(52.9829189 * frac(0.06711056 * pixelCoord.x + 0.00583715 * pixelCoord.y));

    for (int i = 1; i <= POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2; ++i) {
        float t = (float(i) - 0.5 + noise) / float(POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2);

        for (int dir = -1; dir <= 1; dir += 2) {
            float2 sampleUV = uv + blurDir * t * float(dir);

            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

            int2 sampleCoord = int2(sampleUV * pc.srcBufferSize);
            float3 sampleColor = rdgTexturesFloat4[pc.sceneColorIndex].Load(int3(sampleCoord, 0)).rgb;
            float sampleDepth = rdgTexturesFloat[pc.depthBufferIndex].Load(int3(sampleCoord, 0)).r;
            float2 sampleObjectVelocity = rdgTexturesFloat4[pc.velocityBufferIndex].Load(int3(sampleCoord, 0)).ba * 0.5;
            float sampleSpreadLen = length(sampleObjectVelocity * pc.velocityScale * pc.srcBufferSize);

            float2 d = DepthCmp(centerDepth, sampleDepth, pc.depthScale);
            float2 s = SpreadCmp(abs(t), float2(centerSpreadLen, sampleSpreadLen));

            float weight = dot(d, s);
            colorAccum += sampleColor * weight;
            weightAccum += weight;
            sampleCount += 1.0;
        }
    }

    float4 sumColor = float4(colorAccum, weightAccum) / sampleCount;
    float3 finalColor = sumColor.rgb + (1.0 - sumColor.w) * centerColor;
    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(finalColor, 1.0);
}
