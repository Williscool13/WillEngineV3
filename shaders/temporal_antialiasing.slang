import common_interop;
import push_constant_interop;
import shader_functions;
import constants_interop;

import bindless_declarations;

[[vk::push_constant]]
TemporalAntialiasingPushConstant pc;

float3 VarianceClipping(float3 historyColor, float2 uv) {
    float2 du = float2(pc.sceneData.texelSize.x, 0.0);
    float2 dv = float2(0.0, pc.sceneData.texelSize.y);

    float3 topL = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv - dv - du, 0).rgb);
    float3 topM = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv - dv, 0).rgb);
    float3 topR = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv - dv + du, 0).rgb);
    float3 midL = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv - du, 0).rgb);
    float3 midM = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rgb);
    float3 midR = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + du, 0).rgb);
    float3 botL = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + dv - du, 0).rgb);
    float3 botM = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + dv, 0).rgb);
    float3 botR = RGBToYCoCg(rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + dv + du, 0).rgb);

    const float totalSamples = 9.0;
    float3 mean = (topL + topM + topR + midL + midM + midR + botL + botM + botR) / totalSamples;
    float3 squaredSum = (topL * topL + topM * topM + topR * topR +
                         midL * midL + midM * midM + midR * midR +
                         botL * botL + botM * botM + botR * botR) / totalSamples;

    float3 variance = squaredSum - mean * mean;
    float3 stddev = sqrt(max(variance, float3(0.0)));

    float3 gamma = float3(1.0, 1.25, 1.25);
    float3 minc = mean - gamma * stddev;
    float3 maxc = mean + gamma * stddev;

    float3 minColor = min(min(min(min(min(min(min(min(topL, topM), topR), midL), midM), midR), botL), botM), botR);
    float3 maxColor = max(max(max(max(max(max(max(max(topL, topM), topR), midL), midM), midR), botL), botM), botR);

    minc = max(minc, minColor);
    maxc = min(maxc, maxColor);

    return clamp(historyColor, minc, maxc);
}

// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/ and help from AI
float3 SampleHistoryCatmullRom(float2 uv) {
    float2 texelSize = pc.sceneData.texelSize;

    float2 samplePos = uv / texelSize;
    float2 texelCenter = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - texelCenter;

    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    float2 w3 = 0.5 * f3 - 0.5 * f2;

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;

    float2 tc0 = (texelCenter - 1.0) * texelSize;
    float2 tc12 = (texelCenter + offset12) * texelSize;
    float2 tc3 = (texelCenter + 2.0) * texelSize;

    float3 color =
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y) +
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y) +
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], tc12, 0).rgb                  * (w12.x * w12.y) +
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y) +
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);

    return RGBToYCoCg(color);
}

// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/ and help from AI
float2 GetVelocityDilated(float2 uv) {
    float maxMagnitude = 0.0;
    float2 dilatedVelocity = float2(0.0);

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(x, y) * pc.sceneData.texelSize;
            float2 sampleUv = uv + offset;

            float2 velocity = rdgTextures[pc.velocityIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], sampleUv, 0).rg;
            float magnitude = dot(velocity, velocity);

            if (magnitude > maxMagnitude) {
                maxMagnitude = magnitude;
                dilatedVelocity = velocity;
            }
        }
    }

    return dilatedVelocity;
}

float ComputeVelocityDisocclusion(float2 uv, float2 currentVelocityUV) {
    float2 historyUv = uv - currentVelocityUV;

    if (any(historyUv < float2(0.0)) || any(historyUv > float2(1.0))) {
        return 1.0;
    }

    float2 previousVelocityNDC = rdgTextures[pc.velocityHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], historyUv, 0).rg;
    float2 previousVelocityUV = previousVelocityNDC * 0.5;

    float velocityDelta = length(previousVelocityUV - currentVelocityUV);
    float disocclusion = saturate((velocityDelta - 0.001) * 10.0);

    return disocclusion;
}

[numthreads(16, 16, 1)]
void ComputeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData.mainRenderTargetSize.x || threadID.y >= pc.sceneData.mainRenderTargetSize.y) return;
    int2 texelCoord = int2(threadID.xy);
    float2 uv = (float2(texelCoord.xy) + 0.5) / float2(pc.sceneData.mainRenderTargetSize);


    float2 velocityNDC = GetVelocityDilated(uv);
    float2 velocity = velocityNDC * 0.5;

    float velocityDisocclusion = ComputeVelocityDisocclusion(uv, velocity);

    float2 historyUv = uv - velocity;
    bool validHistory = all(historyUv >= float2(0.0)) && all(historyUv <= float2(1.0));

    float3 currentColor = rdgTextures[pc.colorResolvedIndex].Load(int3(texelCoord, 0)).rgb;
    float3 resultRGB;

    if (!validHistory) {
        resultRGB = currentColor;
    } else {
        float3 currentYCoCg = RGBToYCoCg(currentColor);
        float3 historyYCoCg = SampleHistoryCatmullRom(historyUv);

        historyYCoCg = VarianceClipping(historyYCoCg, uv);

        float alpha = lerp(0.1, 0.9, velocityDisocclusion);

        float lumaCurrent = currentYCoCg.r;
        float lumaHistory = historyYCoCg.r;
        float lumaDiff = abs(lumaCurrent - lumaHistory) / max(lumaCurrent, 0.2);
        alpha = lerp(alpha, min(alpha * 2.0, 0.5), lumaDiff);

        float3 resultYCoCg = lerp(historyYCoCg, currentYCoCg, alpha);
        resultRGB = YCoCgToRGB(resultYCoCg);
    }

    rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(resultRGB, 1.0);
}