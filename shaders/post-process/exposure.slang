import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

groupshared uint g_sharedHistogram[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];

// https://bruop.github.io/exposure/
// https://www.alextardif.com/HistogramLuminance.html
uint HDRToHistogramBin(float3 hdrColor, float minLogLuminance, float oneOverLogLuminanceRange)
{
    float epsilon = exp2(minLogLuminance);
    float luminance = dot(hdrColor, float3(0.2126, 0.7152, 0.0722));
    if (luminance < epsilon)
    {
        return 0;
    }

    float logLuminance = saturate((log2(luminance) - minLogLuminance) * oneOverLogLuminanceRange);
    return uint(logLuminance * 254.0 + 1.0);
}

[shader("compute")]
[numthreads(POST_PROCESS_LUMINANCE_DISPATCH_X, POST_PROCESS_LUMINANCE_DISPATCH_Y, 1)]
void ComputeExposureHistogram(uniform HistogramBuildPushConstant pc, uint3 tid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    g_sharedHistogram[groupIndex] = 0;

    GroupMemoryBarrierWithGroupSync();

    uint2 pixelCoord = tid.xy * 2u + pc.sampleOffset;
    if (pixelCoord.x < pc.width && pixelCoord.y < pc.height) {
        float3 hdrColor = rdgTexturesFloat4[pc.hdrImageIndex].Load(int3(pixelCoord, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor, pc.minLogLuminance, pc.oneOverLogLuminanceRange);
        InterlockedAdd(g_sharedHistogram[binIndex], 1);
    }
    GroupMemoryBarrierWithGroupSync();


    InterlockedAdd(pc.histogramBufferAddress[groupIndex], g_sharedHistogram[groupIndex]);
}

groupshared uint g_prefixSum[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];
groupshared float g_weightedSum[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];
groupshared float g_weightSum[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];

// Trimmed percentile-band average of the histogram: pixels outside [lowPercentile, highPercentile] of the non-black distribution (fireflies, sky, the noise floor) do not steer exposure.
[shader("compute")]
[numthreads(POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE, 1, 1)]
void ComputeExposureAverage(uniform ExposureCalculatePushConstant pc, uint3 tid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    uint countForThisBin = pc.histogramBufferAddress[groupIndex];
    uint validCount = groupIndex != 0 ? countForThisBin : 0;
    g_prefixSum[groupIndex] = validCount;

    GroupMemoryBarrierWithGroupSync();

    // Hillis-Steele inclusive scan
    for (uint offset = 1; offset < POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE; offset <<= 1) {
        uint v = groupIndex >= offset ? g_prefixSum[groupIndex - offset] : 0;
        GroupMemoryBarrierWithGroupSync();
        g_prefixSum[groupIndex] += v;
        GroupMemoryBarrierWithGroupSync();
    }

    float totalValid = float(g_prefixSum[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE - 1]);
    float low = pc.lowPercentile * totalValid;
    float high = pc.highPercentile * totalValid;

    float cdfHi = float(g_prefixSum[groupIndex]);
    float cdfLo = cdfHi - float(validCount);
    float contribution = clamp(min(cdfHi, high) - max(cdfLo, low), 0.0, float(validCount));
    g_weightedSum[groupIndex] = contribution * float(groupIndex);
    g_weightSum[groupIndex] = contribution;

    GroupMemoryBarrierWithGroupSync();

    for (uint cutoff = (POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE >> 1); cutoff > 0; cutoff >>= 1) {
        if (uint(groupIndex) < cutoff) {
            g_weightedSum[groupIndex] += g_weightedSum[groupIndex + cutoff];
            g_weightSum[groupIndex] += g_weightSum[groupIndex + cutoff];
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        float weightSum = g_weightSum[0];
        float weightedAvgLum;
        if (weightSum > 0.5) {
            float meanBin = g_weightedSum[0] / weightSum;
            weightedAvgLum = exp2(((meanBin - 1.0) / 254.0) * pc.logLuminanceRange + pc.minLogLuminance);
        } else {
            weightedAvgLum = pc.minAdaptedLuminance;
        }
        weightedAvgLum = clamp(weightedAvgLum, pc.minAdaptedLuminance, pc.maxAdaptedLuminance);

        float lumLastFrame = pc.luminanceBufferAddress[0];
        if (!(lumLastFrame == lumLastFrame) || lumLastFrame <= 0.0) {
            lumLastFrame = weightedAvgLum;
        }
        float alpha = weightedAvgLum < lumLastFrame ? pc.alphaBrighten : pc.alphaDarken;
        float adaptedLum = lumLastFrame + (weightedAvgLum - lumLastFrame) * alpha;
        pc.luminanceBufferAddress[0] = clamp(adaptedLum, pc.minAdaptedLuminance, pc.maxAdaptedLuminance);
    }
}
