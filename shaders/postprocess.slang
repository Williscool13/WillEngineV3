import push_constant_interop;
import constants_interop;

import shader_functions;

import bindless_declarations;


// ==================== Tonemapping ====================
static const float3x3 ACESInputMat = {
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

static const float3x3 ACESOutputMat = {
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

// Godot (?) https://github.com/TheRealMJP/BakingLab
float3 ACESFitted(float3 color) {
    color = mul(ACESInputMat, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// Uncharted 2 https://www.gdcvault.com/play/1012351/Uncharted-2-HDR
float3 Uncharted2Tonemap(float3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// Classic reinhard
float3 ReinhardTonemap(float3 x) {
    return x / (1.0 + x);
}

// Lottes 2016 https://gpuopen.com/learn/optimized-reversible-tonemapper-for-resolve/
float3 LottesTonemapSimple(float3 c) {
    return c / (max(max(c.r, c.g), c.b) + 1.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeSDRTonemap(uniform TonemapSDRPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    int2 texelCoord = int2(dtid.xy);
    float3 hdrColor = rdgTextures[pc.srcImageIndex].Load(int3(texelCoord, 0)).rgb;

    float avgLuminance = pc.luminanceBufferAddress[0];
    float exposure = pc.targetLuminance / max(avgLuminance, 0.001);
    float3 exposedColor = hdrColor * exposure;

    float3 ldrColor;
    switch (pc.tonemapOperator) {
        case 0:
            ldrColor = ACESFitted(exposedColor);
            break;
        case 1:
            ldrColor = Uncharted2Tonemap(exposedColor);
            break;
        case 2:
            ldrColor = ReinhardTonemap(exposedColor);
            break;
        case 3:
            ldrColor = LottesTonemapSimple(exposedColor);
            break;
        default:
            ldrColor = exposedColor;
            break;
    }

    rdgRenderTextures[pc.dstImageIndex][texelCoord] = float4(ldrColor, 1.0);
}


// ==================== Exposure ====================
groupshared uint sharedHistogram[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];
// https://bruop.github.io/exposure/
// https://www.alextardif.com/HistogramLuminance.html
uint HDRToHistogramBin(float3 hdrColor, float minLogLuminance, float oneOverLogLuminanceRange)
{
    static const float EPSILON = 0.005;
    float luminance = dot(hdrColor, float3(0.2126, 0.7152, 0.0722));
    if (luminance < EPSILON)
    {
        return 0;
    }

    float logLuminance = saturate((log2(luminance) - minLogLuminance) * oneOverLogLuminanceRange);
    return uint(logLuminance * 254.0 + 1.0);
}

[numthreads(POST_PROCESS_LUMINANCE_DISPATCH_X, POST_PROCESS_LUMINANCE_DISPATCH_Y, 1)]
void ComputeBuildHistogramExposure(uniform HistogramBuildPushConstant pc, uint3 tid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    sharedHistogram[groupIndex] = 0;

    GroupMemoryBarrierWithGroupSync();

    if (tid.x < pc.width && tid.y < pc.height) {
        float3 hdrColor = rdgTextures[pc.hdrImageIndex].Load(int3(tid.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor, pc.minLogLuminance, pc.oneOverLogLuminanceRange);
        InterlockedAdd(sharedHistogram[binIndex], 1);
    }
    GroupMemoryBarrierWithGroupSync();


    InterlockedAdd(pc.histogramBufferAddress[groupIndex], sharedHistogram[groupIndex]);
}

groupshared uint sharedHistogramAverage[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];

[numthreads(POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE, 1, 1)]
void ComputeAverageExposure(uniform ExposureCalculatePushConstant pc, uint3 tid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    uint countForThisBin = pc.histogramBufferAddress[groupIndex];
    sharedHistogramAverage[groupIndex] = countForThisBin * groupIndex;

    GroupMemoryBarrierWithGroupSync();

    for (uint cutoff = (POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE >> 1); cutoff > 0; cutoff >>= 1) {
        if (uint(groupIndex) < cutoff) {
            sharedHistogramAverage[groupIndex] += sharedHistogramAverage[groupIndex + cutoff];
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        float weightedLogAverage = (sharedHistogramAverage[0] / max(pc.totalPixels - float(countForThisBin), 1.0)) - 1.0;
        float weightedAvgLum = exp2(((weightedLogAverage / 254.0) * pc.logLuminanceRange) + pc.minLogLuminance);
        float lumLastFrame = pc.luminanceBufferAddress[0];
        float adaptedLum = lumLastFrame + (weightedAvgLum - lumLastFrame) * pc.adaptationSpeed;
        pc.luminanceBufferAddress[0] = adaptedLum;
    }
}

// ==================== Motion Blur (Per Object) ====================
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

            float2 velocity = rdgTextures[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).rg;
            float velocityLengthSq = dot(velocity, velocity);

            if (velocityLengthSq > maxVelocityLengthSq) {
                maxVelocityLengthSq = velocityLengthSq;
                maxVelocity = velocity;
            }
        }
    }

    rdgRenderTextures[pc.tileMaxIndex][tileCoord] = float4(maxVelocity, 0, 0);
}

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

            float2 velocity = rdgTextures[pc.tileMaxIndex].Load(int3(sampleCoord, 0)).rg;
            float velocityLengthSq = dot(velocity, velocity);

            if (velocityLengthSq > maxVelocityLengthSq) {
                maxVelocityLengthSq = velocityLengthSq;
                maxVelocity = velocity;
            }
        }
    }

    rdgRenderTextures[pc.neighborMaxIndex][tileCoord] = float4(maxVelocity, 0.0f, 0.0f);
}

float2 ComputeCameraMotion(float4x4 invViewProj, float4x4 prevViewProj, float2 uv, float depth) {
    float3 worldPos = ReconstructWorldPosition(invViewProj, uv, depth);

    float4 prevClipPos = mul(prevViewProj, float4(worldPos, 1.0));
    float2 prevNDC = prevClipPos.xy / prevClipPos.w;
    prevNDC.y = -prevNDC.y;
    float2 prevUV = prevNDC * 0.5 + 0.5;

    return uv - prevUV;
}

[numthreads(POST_PROCESS_MOTION_BLUR_DISPATCH_X, POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1)]
void ComputeMotionBlurReconstruction(uniform MotionBlurReconstructionPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    if (any(pixelCoord >= pc.sceneData->mainRenderTargetSize)) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) / pc.sceneData->mainRenderTargetSize;

    // Tile rejection (reject low velocity)
    uint2 tileCoord = pixelCoord / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    float2 neighborMaxVelocity = rdgTextures[pc.tileNeighborMaxIndex].Load(int3(tileCoord, 0)).rg;
    float neighborMaxMagnitude = length(neighborMaxVelocity * pc.sceneData->mainRenderTargetSize);

    float3 centerColor = rdgTextures[pc.sceneColorIndex].Load(int3(pixelCoord, 0)).rgb;
    if (neighborMaxMagnitude < 0.5) {
        rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(centerColor, 1.0);
        return;
    }

    float centerDepth = rdgTextures[pc.depthBufferIndex].Load(int3(pixelCoord, 0)).r;
    float2 totalVelocity = rdgTextures[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).rg;
    totalVelocity = totalVelocity * 0.5;
    totalVelocity.y = -totalVelocity.y;

    float2 cameraMotion = ComputeCameraMotion(pc.sceneData->invViewProj, pc.sceneData->prevViewProj, uv, centerDepth);
    float2 jitter = pc.sceneData->jitter;
    jitter.y = -jitter.y;
    float2 prevJitter = pc.sceneData->prevJitter;
    prevJitter.y = -prevJitter.y;
    cameraMotion += (jitter - prevJitter) * 0.5;
    float2 centerVelocity = (totalVelocity - cameraMotion) * pc.velocityScale;

    float3 colorAccum = centerColor;
    float weightAccum = 1.0;

    float noise = frac(52.9829189 * frac(0.06711056 * pixelCoord.x + 0.00583715 * pixelCoord.y));
    float2 blurVelocity = centerVelocity;

    [ForceUnroll]
    for (int i = 1; i <= POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2; ++i) {
        float t = (float(i) - 0.5 + noise) / float(POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2);

        [ForceUnroll]
        for (int dir = -1; dir <= 1; dir += 2) {
            float2 sampleUV = uv + blurVelocity * t * float(dir);

            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
                continue;
            }

            int2 sampleCoord = int2(sampleUV * pc.sceneData->mainRenderTargetSize);
            float3 sampleColor = rdgTextures[pc.sceneColorIndex].Load(int3(sampleCoord, 0)).rgb;
            float sampleDepth = rdgTextures[pc.depthBufferIndex].Load(int3(sampleCoord, 0)).r;

            // Depth-aware weighting
            float depthDiff = centerDepth - sampleDepth;
            float depthWeight = saturate(1.0 - (depthDiff - pc.depthThreshold) / pc.depthFalloff);

            colorAccum += sampleColor * depthWeight;
            weightAccum += depthWeight;
        }
    }

    float3 finalColor = colorAccum / weightAccum;

    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(finalColor, 1.0);
}