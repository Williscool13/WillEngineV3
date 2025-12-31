

[[vk::binding(0, 0)]]
SamplerState samplers[];
[[vk::binding(1, 0)]]
Texture2D textures[];
[[vk::binding(2, 0)]]
RWTexture2D<float4> renderTextures[];

[[vk::push_constant]]
TemporalAntialiasingPushConstant pc;

[numthreads(16, 16, 1)]
void computeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.extent.x || threadID.y >= pc.extent.y) return;
    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.extent);


    float2 velocity = textures[pc.velocityIndex].SampleLevel(samplers[pc.pointSamplerIndex], uv, 0).rg;
    float2 historyUv = uv - velocity;
    bool validHistory = all(historyUv >= float2(0.0)) && all(historyUv <= vec2(1.0));

    float4 colorData = textures[pc.colorResolvedIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv, 0);
    float3 resultRGB;
    if (validHistory) {
        resultRGB = colorData;
    } else {
        float3 currentYCoCg = RGBToYCoCg(currentColor);
        float3 historyColor = textures[pc.colorHistoryIndex].SampleLevel(samplers[pc.linearSamplerIndex], uv, 0).rgb;
        float3 historyYCoCg = RGBToYCoCg(historyColor);

        vec3 neighborClampedYCoCg = RGBToYCoCg(neighborhoodClamping(historyColor, uv));
        vec3 varianceClampedYCoCg = varianceClipping(neighborClampedYCoCg, uv);
        historyYCoCg = varianceClampedYCoCg;

        //historyYCoCg = varianceClipping(historyYCoCg, uv);

        float alpha = 1 - push.blendValue;
        vec3 resultYCoCg = mix(currentYCoCg, historyYCoCg, alpha);
        resultRGB = YCoCgToRGB(resultYCoCg);
    }
}