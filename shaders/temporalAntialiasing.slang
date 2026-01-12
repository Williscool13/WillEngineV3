import common_interop;
import push_constant_interop;
import shader_functions;

import bindless_declarations;

[[vk::push_constant]]
TemporalAntialiasingPushConstant pc;

float3 neighborhoodClamp(float3 historyColor, float2 uv) {
    float2 du = float2(pc.sceneData.texelSize.x, 0.0);
    float2 dv = float2(0.0, pc.sceneData.texelSize.y);

    float3 topL = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv - dv - du, 0).rgb;
    float3 topM = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv - dv, 0).rgb;
    float3 topR = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv - dv + du, 0).rgb;

    float3 midL = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv - du, 0).rgb;
    float3 midM = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv, 0).rgb;
    float3 midR = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv + du, 0).rgb;

    float3 botL = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv + dv - du, 0).rgb;
    float3 botM = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv + dv, 0).rgb;
    float3 botR = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv + dv + du, 0).rgb;

    float3 minColor = min(min(min(min(min(min(min(min(topL, topM), topR), midL), midM), midR), botL), botM), botR);
    float3 maxColor = max(max(max(max(max(max(max(max(topL, topM), topR), midL), midM), midR), botL), botM), botR);

    return clamp(historyColor, minColor, maxColor);
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
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y) +
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y) +
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], tc12, 0).rgb * (w12.x * w12.y) +
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y) +
        rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);

    return color;
}

// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/ and help from AI
float2 GetVelocityDilated(float2 uv) {
    float closestDepth = 1.0; // far plane
    float2 closestVelocity = float2(0.0);

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(x, y) * pc.sceneData.texelSize;
            float2 sampleUv = uv + offset;

            float depth = rdgTextures[pc.depthIndex].SampleLevel(rdgSamplers[pc.pointSamplerIndex], sampleUv, 0).r;
            if (depth < closestDepth) {
                closestDepth = depth;
                closestVelocity = rdgTextures[pc.velocityIndex].SampleLevel(rdgSamplers[pc.pointSamplerIndex], sampleUv, 0).rg;
            }
        }
    }

    return closestVelocity;
}

[numthreads(16, 16, 1)]
void computeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData.mainRenderTargetSize.x || threadID.y >= pc.sceneData.mainRenderTargetSize.y) return;
    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.sceneData.mainRenderTargetSize);

    //float2 velocity = rdgTextures[pc.velocityIndex].SampleLevel(rdgSamplers[pc.pointSamplerIndex], uv, 0).rg;
    float2 velocity = GetVelocityDilated(uv);
    velocity = (velocity - 0.5);
    velocity.y = -velocity.y;

    float2 historyUv = uv - velocity;
    bool validHistory = all(historyUv >= float2(0.0)) && all(historyUv <= float2(1.0));

    float3 currentColor = rdgTextures[pc.colorResolvedIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], uv, 0).rgb;
    float3 resultRGB;

    if (!validHistory) {
        resultRGB = currentColor;
    } else {
        float3 currentYCoCg = RGBToYCoCg(currentColor);
        // float3 historyColor = rdgTextures[pc.colorHistoryIndex].SampleLevel(rdgSamplers[pc.linearSamplerIndex], historyUv, 0).rgb;
        float3 historyColor = SampleHistoryCatmullRom(historyUv);
        float3 historyYCoCg = RGBToYCoCg(historyColor);

        float3 neighborClampedYCoCg = RGBToYCoCg(neighborhoodClamp(YCoCgToRGB(historyYCoCg), uv));
        historyYCoCg = neighborClampedYCoCg;

        float alpha = 0.1;
        float3 resultYCoCg = lerp(historyYCoCg, currentYCoCg, alpha);
        resultRGB = YCoCgToRGB(resultYCoCg);
    }

    rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(resultRGB, 1.0);
}