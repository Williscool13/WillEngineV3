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
    int2 pixelCoord = int2(dtid.xy);
    float3 sceneColor = rdgTextures[pc.srcImageIndex].Load(int3(pixelCoord, 0)).rgb;

    float3 bloom = rdgTextures[pc.bloomImageIndex].Load(int3(pixelCoord, 0)).rgb;
    float3 hdrColor = sceneColor + bloom * pc.bloomIntensity;

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

    rdgRenderTextures[pc.dstImageIndex][pixelCoord] = float4(ldrColor, 1.0);
}


// ==================== Exposure ====================
groupshared uint g_sharedHistogram[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];
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
    g_sharedHistogram[groupIndex] = 0;

    GroupMemoryBarrierWithGroupSync();

    if (tid.x < pc.width && tid.y < pc.height) {
        float3 hdrColor = rdgTextures[pc.hdrImageIndex].Load(int3(tid.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor, pc.minLogLuminance, pc.oneOverLogLuminanceRange);
        InterlockedAdd(g_sharedHistogram[binIndex], 1);
    }
    GroupMemoryBarrierWithGroupSync();


    InterlockedAdd(pc.histogramBufferAddress[groupIndex], g_sharedHistogram[groupIndex]);
}

groupshared uint g_sharedHistogramAverage[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];

[numthreads(POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE, 1, 1)]
void ComputeAverageExposure(uniform ExposureCalculatePushConstant pc, uint3 tid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    uint countForThisBin = pc.histogramBufferAddress[groupIndex];
    g_sharedHistogramAverage[groupIndex] = countForThisBin * groupIndex;

    GroupMemoryBarrierWithGroupSync();

    for (uint cutoff = (POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE >> 1); cutoff > 0; cutoff >>= 1) {
        if (uint(groupIndex) < cutoff) {
            g_sharedHistogramAverage[groupIndex] += g_sharedHistogramAverage[groupIndex + cutoff];
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        float weightedLogAverage = (g_sharedHistogramAverage[0] / max(pc.totalPixels - float(countForThisBin), 1.0)) - 1.0;
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

    float2 jitter = pc.sceneData->jitter;
    jitter.y = -jitter.y;
    float2 prevJitter = pc.sceneData->prevJitter;
    prevJitter.y = -prevJitter.y;
    float2 jitterDiff = (jitter - prevJitter) * 0.5;


    for (uint y = 0; y < POST_PROCESS_MOTION_BLUR_TILE_SIZE; ++y) {

        for (uint x = 0; x < POST_PROCESS_MOTION_BLUR_TILE_SIZE; ++x) {
            uint2 pixelCoord = tileStart + uint2(x, y);

            if (any(pixelCoord >= pc.velocityBufferSize))
                continue;

            float2 velocity = rdgTextures[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).rg;
            velocity = velocity * 0.5;
            velocity.y = -velocity.y;
            float2 uv = (float2(pixelCoord) + 0.5) / pc.sceneData->mainRenderTargetSize;
            float depth = rdgTextures[pc.depthBufferIndex].Load(int3(pixelCoord, 0)).r;
            float2 cameraMotion = ComputeCameraMotion(pc.sceneData->invViewProj, pc.sceneData->prevViewProj, uv, depth);
            cameraMotion += jitterDiff;
            velocity -= cameraMotion;
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

float2 DepthCmp(float centerDepth, float sampleDepth, float depthScale) {
    return saturate(0.5 + float2(depthScale, -depthScale) * (sampleDepth - centerDepth));
}

float2 SpreadCmp(float offsetLen, float2 spreadLen) {
    return saturate(spreadLen - offsetLen + 1.0);
}

// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
[numthreads(POST_PROCESS_MOTION_BLUR_DISPATCH_X, POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1)]
void ComputeMotionBlurReconstruction(uniform MotionBlurReconstructionPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;
    if (any(pixelCoord >= pc.sceneData->mainRenderTargetSize)) return;

    float2 uv = (float2(pixelCoord) + 0.5) / pc.sceneData->mainRenderTargetSize;

    // Early out if tile has low velocity
    uint2 tileCoord = pixelCoord / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    float2 neighborMaxVelocity = rdgTextures[pc.tileNeighborMaxIndex].Load(int3(tileCoord, 0)).rg;
    float neighborMaxLen = length(neighborMaxVelocity * pc.sceneData->mainRenderTargetSize);
    float3 centerColor = rdgTextures[pc.sceneColorIndex].Load(int3(pixelCoord, 0)).rgb;
    if (neighborMaxLen < 0.5) {
        rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(centerColor, 1.0);
        return;
    }

    // Jitter correction
    float2 jitter = pc.sceneData->jitter;
    jitter.y = -jitter.y;
    float2 prevJitter = pc.sceneData->prevJitter;
    prevJitter.y = -prevJitter.y;
    float2 jitterDiff = (jitter - prevJitter) * 0.5;

    float centerDepth = rdgTextures[pc.depthBufferIndex].Load(int3(pixelCoord, 0)).r;

    // Velocity from [-1, 1] to [-0.5, 0.5]. Flip Y because of flipped viewport during rendering.
    float2 totalVelocity = rdgTextures[pc.velocityBufferIndex].Load(int3(pixelCoord, 0)).rg;
    totalVelocity = totalVelocity * 0.5;
    totalVelocity.y = -totalVelocity.y;
    // Unjitter camera motion
    float2 cameraMotion = ComputeCameraMotion(pc.sceneData->invViewProj, pc.sceneData->prevViewProj, uv, centerDepth);
    cameraMotion += jitterDiff;
    // Velocity of object only
    float2 centerVelocity = (totalVelocity - cameraMotion) * pc.velocityScale;


    // Center's blur spread in pixels
    float centerSpreadLen = length(centerVelocity * pc.sceneData->mainRenderTargetSize);
    // Total blur length we're sampling along (neighbor max defines the sampling range)
    float blurLen = neighborMaxLen;
    // Converts pixel distances to "sample units" where 1.0 = full blur length
    // This normalizes offsetLen against the blur radius for the spread comparison
    float pixelToSampleUnitsScale = 1.0 / max(blurLen * 2, 1.0);

    float3 colorAccum = centerColor;
    float weightAccum = 1.0;

    float2 blurDir = neighborMaxVelocity * pc.velocityScale;
    float noise = frac(52.9829189 * frac(0.06711056 * pixelCoord.x + 0.00583715 * pixelCoord.y));

    [ForceUnroll]
    for (int i = 1; i <= POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2; ++i) {
        float t = (float(i) - 0.5 + noise) / float(POST_PROCESS_MOTION_BLUR_SAMPLE_COUNT / 2);

        // Distance from center to this sample in pixels
        float offsetLen = t * blurLen;

        [ForceUnroll]
        for (int dir = -1; dir <= 1; dir += 2) {
            float2 sampleUV = uv + blurDir * t * float(dir);

            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) { continue; }

            int2 sampleCoord = int2(sampleUV * pc.sceneData->mainRenderTargetSize);
            float3 sampleColor = rdgTextures[pc.sceneColorIndex].Load(int3(sampleCoord, 0)).rgb;
            float sampleDepth = rdgTextures[pc.depthBufferIndex].Load(int3(sampleCoord, 0)).r;

            // Get sample's velocity for its spread length
            float2 sampleTotalVel = rdgTextures[pc.velocityBufferIndex].Load(int3(sampleCoord, 0)).rg;
            sampleTotalVel = sampleTotalVel * 0.5;
            sampleTotalVel.y = -sampleTotalVel.y;
            float2 sampleCameraMotion = ComputeCameraMotion(pc.sceneData->invViewProj, pc.sceneData->prevViewProj, sampleUV, sampleDepth);
            sampleCameraMotion += jitterDiff;
            float2 sampleVelocity = sampleTotalVel - sampleCameraMotion;

            // Sample's blur spread in pixels
            float sampleSpreadLen = length(sampleVelocity * pc.sceneData->mainRenderTargetSize);

            // Depth comparison: which pixel is in front?
            // d.x = weight if center is behind (sample is foreground)
            // d.y = weight if sample is behind (center is foreground)
            float2 d = DepthCmp(centerDepth, sampleDepth, pc.depthScale);

            // Spread comparison: does the blur reach?
            // s.x = does center's blur spread reach this sample distance?
            // s.y = does sample's blur spread reach the center?
            float2 s = SpreadCmp(offsetLen, float2(centerSpreadLen, sampleSpreadLen));

            // Combined weight:
            // d.x * s.x = center behind, center's blur reaches sample (background bleeds forward)
            // d.y * s.y = sample behind, sample's blur reaches center (foreground bleeds onto background)
            float weight = dot(d, s);

            colorAccum += sampleColor * weight;
            weightAccum += weight;
        }
    }

    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(colorAccum / weightAccum, 1.0);
}


// ==================== Bloom =======================================
float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Soft threshold curve (Jimenez 2014)
float3 ApplyThreshold(float3 color, float threshold, float softThreshold) {
    float brightness = Luminance(color);
    float soft = brightness - threshold + softThreshold;
    soft = clamp(soft, 0.0, 2.0 * softThreshold);
    soft = soft * soft / (4.0 * softThreshold + 1e-4);

    float contribution = max(soft, brightness - threshold);
    contribution /= max(brightness, 1e-4);

    return color * contribution;
}

[numthreads(POST_PROCESS_BLOOM_DISPATCH_X, POST_PROCESS_BLOOM_DISPATCH_Y, 1)]
void ComputeBloomThreshold(uniform BloomThresholdPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    uint2 outputSize;
    rdgRenderTextures[pc.outputIndex].GetDimensions(outputSize.x, outputSize.y);

    if (any(pixelCoord >= outputSize)) {
        return;
    }

    float3 color = rdgTextures[pc.inputColorIndex].Load(int3(pixelCoord, 0)).rgb;
    float3 thresholded = ApplyThreshold(color, pc.threshold, pc.softThreshold);
    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(thresholded, 1.0);
}

[numthreads(POST_PROCESS_BLOOM_DISPATCH_X, POST_PROCESS_BLOOM_DISPATCH_Y, 1)]
void ComputeBloomDownsample(uniform BloomDownsamplePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    uint2 outputSize;
    rdgRenderTextures[pc.outputIndex].GetDimensions(outputSize.x, outputSize.y);

    if (any(pixelCoord >= outputSize)) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) / float2(outputSize);

    uint2 inputSize;
    uint numMips;
    rdgTextures[pc.inputIndex].GetDimensions(0, inputSize.x, inputSize.y, numMips);

    inputSize = inputSize >> pc.srcMipLevel;
    float2 texelSize = 1.0 / float2(inputSize);

    // 13-tap filter (Jimenez 2014)
    float3 A = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-2, -2) * texelSize, pc.srcMipLevel).rgb;
    float3 B = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0, -2) * texelSize, pc.srcMipLevel).rgb;
    float3 C = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 2, -2) * texelSize, pc.srcMipLevel).rgb;

    float3 D = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1, -1) * texelSize, pc.srcMipLevel).rgb;
    float3 E = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1, -1) * texelSize, pc.srcMipLevel).rgb;

    float3 F = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-2,  0) * texelSize, pc.srcMipLevel).rgb;
    float3 G = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  0) * texelSize, pc.srcMipLevel).rgb;
    float3 H = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 2,  0) * texelSize, pc.srcMipLevel).rgb;

    float3 I = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  1) * texelSize, pc.srcMipLevel).rgb;
    float3 J = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  1) * texelSize, pc.srcMipLevel).rgb;

    float3 K = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-2,  2) * texelSize, pc.srcMipLevel).rgb;
    float3 L = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  2) * texelSize, pc.srcMipLevel).rgb;
    float3 M = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 2,  2) * texelSize, pc.srcMipLevel).rgb;

    // Weighted average (weights from Jimenez presentation)
    float3 result =
        (D + E + I + J) * 0.5 / 4.0 +
        (A + B + G + F) * 0.125 / 4.0 +
        (B + C + H + G) * 0.125 / 4.0 +
        (F + G + L + K) * 0.125 / 4.0 +
        (G + H + M + L) * 0.125 / 4.0;

    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(result, 1.0);
}

[numthreads(POST_PROCESS_BLOOM_DISPATCH_X, POST_PROCESS_BLOOM_DISPATCH_Y, 1)]
void ComputeBloomUpsample(uniform BloomUpsamplePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixelCoord = dtid.xy;

    uint2 outputSize;
    rdgRenderTextures[pc.outputIndex].GetDimensions(outputSize.x, outputSize.y);

    if (any(pixelCoord >= outputSize)) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) / float2(outputSize);

    // Lower mip size
    uint2 lowerSize;
    uint numMips;
    rdgTextures[pc.inputIndex].GetDimensions(0, lowerSize.x, lowerSize.y, numMips);
    lowerSize = lowerSize >> pc.lowerMipLevel;
    float2 texelSize = pc.radius / float2(lowerSize);

    // 9-tap tent filter (3x3 with bilinear, weights sum to 16)
    float3 result = float3(0, 0, 0);

    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1, -1) * texelSize, pc.lowerMipLevel).rgb * 1.0;
    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0, -1) * texelSize, pc.lowerMipLevel).rgb * 2.0;
    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1, -1) * texelSize, pc.lowerMipLevel).rgb * 1.0;

    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  0) * texelSize, pc.lowerMipLevel).rgb * 2.0;
    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  0) * texelSize, pc.lowerMipLevel).rgb * 4.0;
    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  0) * texelSize, pc.lowerMipLevel).rgb * 2.0;

    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  1) * texelSize, pc.lowerMipLevel).rgb * 1.0;
    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  1) * texelSize, pc.lowerMipLevel).rgb * 2.0;
    result += rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  1) * texelSize, pc.lowerMipLevel).rgb * 1.0;

    result /= 16.0;

    // Add higher mip (current level we're upsampling to)
    float3 higher = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, pc.higherMipLevel).rgb;
    result += higher;

    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(result, 1.0);
}

// ==================== Vignette & Aberration =======================
[shader("compute")]
[numthreads(POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_X, POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_Y, 1)]
void ComputeVignetteAberration(uniform VignetteChromaticAberrationPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.sceneData.mainRenderTargetSize)) return;

    uint2 pixelCoord = dtid.xy;
    float2 uv = (dtid.xy + 0.5) / pc.sceneData.mainRenderTargetSize;
    float2 centerOffset = uv - 0.5;
    float distFromCenter = length(centerOffset);

    // Chromatic aberration
    float2 direction = distFromCenter > 0.0001 ? normalize(centerOffset) : float2(0, 0);
    float aberrationOffset = distFromCenter * pc.chromaticAberrationStrength / pc.sceneData.mainRenderTargetSize.x;
    float2 uvR = uv + direction * aberrationOffset;
    float2 uvG = uv;
    float2 uvB = uv - direction * aberrationOffset;
    float r = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvR, 0).r;
    float g = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvG, 0).g;
    float b = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvB, 0).b;
    float3 color = float3(r, g, b);

    // Vignette
    float vignette = smoothstep(
        pc.vignetteRadius,
        pc.vignetteRadius - pc.vignetteSmoothness,
        distFromCenter
    );
    vignette = lerp(1.0, vignette, pc.vignetteStrength);
    color *= vignette;
    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}

float FilmGrainHash(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// ==================== Film Grain ==================================
[shader("compute")]
[numthreads(POST_PROCESS_FILM_GRAIN_DISPATCH_X, POST_PROCESS_FILM_GRAIN_DISPATCH_Y, 1)]
void ComputeFilmGrain(uniform FilmGrainPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.sceneData.mainRenderTargetSize)) return;
    uint2 pixelCoord = dtid.xy;

    float2 uv = (dtid.xy + 0.5) / pc.sceneData.mainRenderTargetSize;

    float2 grainUV = uv * pc.sceneData.mainRenderTargetSize / pc.grainSize;
    grainUV += float2(pc.frameIndex * 0.123, pc.frameIndex * 0.456);

    float noise = FilmGrainHash(grainUV);
    noise = noise * 2.0 - 1.0;

    float3 color = rdgTextures[pc.inputIndex].Load(int3(pixelCoord, 0)).rgb;
    color += noise * pc.grainStrength;
    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}

// ==================== Sharpening ==================================
[shader("compute")]
[numthreads(POST_PROCESS_SHARPENING_DISPATCH_X, POST_PROCESS_SHARPENING_DISPATCH_Y, 1)]
void ComputeSharpening(uniform SharpeningPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.sceneData.mainRenderTargetSize)) return;

    uint2 pixelCoord = dtid.xy;
    float2 uv = (dtid.xy + 0.5) / pc.sceneData.mainRenderTargetSize;
    float2 texelSize = 1.0 / pc.sceneData.mainRenderTargetSize;

    // Sample center and cross neighbors
    float3 b = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0, -1) * texelSize, 0).rgb;
    float3 d = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  0) * texelSize, 0).rgb;
    float3 e = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rgb;
    float3 f = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  0) * texelSize, 0).rgb;
    float3 h = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  1) * texelSize, 0).rgb;

    // Simple unsharp mask approach
    float3 blurred = (b + d + f + h) * 0.25;
    float3 sharpened = e + (e - blurred) * pc.sharpness;

    // Clamp to prevent overshooting
    sharpened = max(float3(0, 0, 0), sharpened);

    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(sharpened, 1.0);

    // FFX CAS (I think this is wrong somehow/somewhere)
    /*if (any(dtid.xy >= pc.sceneData.mainRenderTargetSize)) return;

    uint2 pixelCoord = dtid.xy;
    float2 uv = (dtid.xy + 0.5) / pc.sceneData.mainRenderTargetSize;
    float2 texelSize = 1.0 / pc.sceneData.mainRenderTargetSize;

    float3 a = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1, -1) * texelSize, 0).rgb;
    float3 b = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0, -1) * texelSize, 0).rgb;
    float3 c = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1, -1) * texelSize, 0).rgb;
    float3 d = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  0) * texelSize, 0).rgb;
    float3 e = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  0) * texelSize, 0).rgb;
    float3 f = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  0) * texelSize, 0).rgb;
    float3 g = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2(-1,  1) * texelSize, 0).rgb;
    float3 h = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 0,  1) * texelSize, 0).rgb;
    float3 i = rdgTextures[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + float2( 1,  1) * texelSize, 0).rgb;
    float3 minRGB = min(min(min(d, e), min(f, b)), h);
    float3 maxRGB = max(max(max(d, e), max(f, b)), h);

    // Edge Detection
    float3 amplify = clamp(minRGB / (maxRGB + 0.00001) - 1.0, 0.0, 1.0);
    amplify = sqrt(amplify);

    float3 w = amplify * pc.sharpness;
    float3 weightSum = 1.0 + 4.0 * w;

    float3 sharpened = (b + d + f + h) * w + e;
    sharpened /= weightSum;

    sharpened = clamp(sharpened, minRGB, maxRGB);

    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(sharpened, 1.0);*/
}

// ==================== Color Grading ===============================
[shader("compute")]
[numthreads(POST_PROCESS_COLOR_GRADING_DISPATCH_X, POST_PROCESS_COLOR_GRADING_DISPATCH_Y, 1)]
void ComputeColorGrading(uniform ColorGradingPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.sceneData.mainRenderTargetSize)) return;

    uint2 pixelCoord = dtid.xy;
    float3 color = rdgTextures[pc.inputIndex].Load(int3(pixelCoord, 0)).rgb;

    color *= exp2(pc.exposure);

    color = (color - 0.5) * pc.contrast + 0.5;

    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma, luma, luma), color, pc.saturation);

    if (pc.temperature > 0.0) {
        color.r += pc.temperature * 0.1;
        color.b -= pc.temperature * 0.1;
    } else {
        color.r += pc.temperature * 0.1;
        color.b -= pc.temperature * 0.1;
    }

    if (pc.tint > 0.0) {
        color.g -= pc.tint * 0.1;
    } else {
        color.g -= pc.tint * 0.1;
    }

    rdgRenderTextures[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}