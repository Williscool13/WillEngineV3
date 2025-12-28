import push_constant_interop;

[[vk::binding(0, 0)]]
SamplerState samplers[];
[[vk::binding(1, 0)]]
Texture2D textures[];
[[vk::binding(2, 0)]]
RWTexture2D<float4> renderTextures[];


[[vk::push_constant]]
DepthDebugPushConstant pc;

float LinearizeDepth(float depth) {
    float zNear = pc.nearPlane;
    float zFar = pc.farPlane;
    float flipped = 1.0 - depth;
    return (2.0 * zNear) / (zFar + zNear - flipped * (zFar - zNear));
}

[numthreads(16, 16, 1)]
void computeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.extent.x || threadID.y >= pc.extent.y) return;

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.extent);
    float depth = textures[pc.depthTextureIndex].SampleLevel(samplers[pc.samplerIndex], uv, 0).r;
    float linear = LinearizeDepth(depth);
    renderTextures[pc.outputImageIndex][threadID.xy] = float4(linear, linear, linear, 1.0);
}