import common_interop;
import push_constant_interop;
import shader_functions;
import constants_interop;

import bindless_declarations;

[[vk::push_constant]]
TemporalAntialiasingPushConstant pc;

[numthreads(16, 16, 1)]
void ComputeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData.mainRenderTargetSize.x || threadID.y >= pc.sceneData.mainRenderTargetSize.y) return;
    int2 texelCoord = int2(threadID.xy);
    float2 uv = (float2(texelCoord.xy) + 0.5) / float2(pc.sceneData.mainRenderTargetSize);

    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(texelCoord, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUv = uv + velocityNDC * 0.5;
    bool validHistory = all(historyUv >= float2(0.0)) && all(historyUv <= float2(1.0));

    float3 currentColor = rdgTexturesFloat4[pc.colorResolvedIndex].Load(int3(texelCoord, 0)).rgb;
    float3 resultRGB;

    if (!validHistory) {
        resultRGB = currentColor;
    } else {
        float3 historyColor = rdgTexturesFloat4[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], historyUv, 0).rgb;
        resultRGB = lerp(historyColor, currentColor, 0.01);
    }

    rdgStorageFloat4[pc.outputImageIndex][threadID.xy] = float4(resultRGB, 1.0);
    rdgStorageFloat4[pc.outputCopyIndex][threadID.xy] = float4(resultRGB, 1.0);
}
