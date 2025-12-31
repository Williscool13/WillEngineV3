import common_interop;
import push_constant_interop;
import shader_functions;

[[vk::binding(0, 0)]]
SamplerState samplers[];
[[vk::binding(1, 0)]]
Texture2D textures[];
[[vk::binding(2, 0)]]
RWTexture2D<float4> renderTextures[];

[[vk::push_constant]]
TemporalAntialiasingPushConstant pc;

float3 neighborhoodClamp(float3 historyColor, float2 uv) {
    float2 du = float2(pc.sceneData.texelSize.x, 0.0);
    float2 dv = float2(0.0, pc.sceneData.texelSize.y);

    float3 topL = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv - dv - du, 0).rgb;
    float3 topM = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv - dv, 0).rgb;
    float3 topR = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv - dv + du, 0).rgb;

    float3 midL = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv - du, 0).rgb;
    float3 midM = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv, 0).rgb;
    float3 midR = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv + du, 0).rgb;

    float3 botL = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv + dv - du, 0).rgb;
    float3 botM = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv + dv, 0).rgb;
    float3 botR = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv + dv + du, 0).rgb;

    float3 minColor = min(min(min(min(min(min(min(min(topL, topM), topR), midL), midM), midR), botL), botM), botR);
    float3 maxColor = max(max(max(max(max(max(max(max(topL, topM), topR), midL), midM), midR), botL), botM), botR);

    return clamp(historyColor, minColor, maxColor);
}

[numthreads(16, 16, 1)]
void computeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData.mainRenderTargetSize.x || threadID.y >= pc.sceneData.mainRenderTargetSize.y) return;
    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.sceneData.mainRenderTargetSize);

    float2 velocity = textures[pc.velocityIndex].SampleLevel(samplers[pc.pointSamplerIndex], uv, 0).rg;
    velocity = velocity - 0.5;

    float2 historyUv = uv - velocity;
    bool validHistory = all(historyUv >= float2(0.0)) && all(historyUv <= float2(1.0));

    float3 currentColor = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv, 0).rgb;
    float3 resultRGB;

    if (!validHistory) {
        resultRGB = currentColor;
    } else {
        float3 currentYCoCg = RGBToYCoCg(currentColor);
        float3 historyColor = textures[pc.colorHistoryIndex].SampleLevel(samplers[pc.linearSamplerIndex], historyUv, 0).rgb;
        float3 historyYCoCg = RGBToYCoCg(historyColor);

        float3 neighborClampedYCoCg = RGBToYCoCg(neighborhoodClamp(YCoCgToRGB(historyYCoCg), uv));
        historyYCoCg = neighborClampedYCoCg;

        float alpha = 0.1; // Blend factor (0.1 = 10% current, 90% history)
        float3 resultYCoCg = lerp(historyYCoCg, currentYCoCg, alpha);
        resultRGB = YCoCgToRGB(resultYCoCg);
    }

    renderTextures[pc.outputImageIndex][threadID.xy] = float4(resultRGB, 1.0);
}