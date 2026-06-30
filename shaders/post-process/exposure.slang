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
    static const float EPSILON = 0.005;
    float luminance = dot(hdrColor, float3(0.2126, 0.7152, 0.0722));
    if (luminance < EPSILON)
    {
        return 0;
    }

    float logLuminance = saturate((log2(luminance) - minLogLuminance) * oneOverLogLuminanceRange);
    return uint(logLuminance * 254.0 + 1.0);
}

[shader("compute")]
[numthreads(POST_PROCESS_LUMINANCE_DISPATCH_X, POST_PROCESS_LUMINANCE_DISPATCH_Y, 1)]
void ComputeBuildHistogramExposure(uniform HistogramBuildPushConstant pc, uint3 tid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
    g_sharedHistogram[groupIndex] = 0;

    GroupMemoryBarrierWithGroupSync();

    if (tid.x < pc.width && tid.y < pc.height) {
        float3 hdrColor = rdgTexturesFloat4[pc.hdrImageIndex].Load(int3(tid.xy, 0)).rgb;
        uint binIndex = HDRToHistogramBin(hdrColor, pc.minLogLuminance, pc.oneOverLogLuminanceRange);
        InterlockedAdd(g_sharedHistogram[binIndex], 1);
    }
    GroupMemoryBarrierWithGroupSync();


    InterlockedAdd(pc.histogramBufferAddress[groupIndex], g_sharedHistogram[groupIndex]);
}

groupshared uint g_sharedHistogramAverage[POST_PROCESS_LUMINANCE_HISTOGRAM_SIZE];

[shader("compute")]
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
