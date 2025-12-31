import push_constant_interop;

[[vk::binding(0, 0)]]
SamplerState samplers[];
[[vk::binding(1, 0)]]
Texture2D textures[];
[[vk::binding(2, 0)]]
RWTexture2D<float4> renderTextures[];


[[vk::push_constant]]
DebugVisualizePushConstant pc;

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
    switch (pc.debugType) {
        case 1: // Depth
            float depth = textures[pc.textureIndex].SampleLevel(samplers[pc.samplerIndex], uv, 0).r;
            float linear = LinearizeDepth(depth);
            renderTextures[pc.outputImageIndex][threadID.xy] = float4(linear, linear, linear, 1.0);
            break;
        case 2: // Albedo
            float4 albedo  = textures[pc.textureIndex].SampleLevel(samplers[pc.samplerIndex], uv, 0);
            renderTextures[pc.outputImageIndex][threadID.xy] = float4(albedo.x, albedo.y, albedo.z, 1.0f);
            break;
        case 3: // Normal (view space, stored in packed unorm [0,1])
            float3 normal = textures[pc.textureIndex].SampleLevel(samplers[pc.samplerIndex], uv, 0).xyz;
            renderTextures[pc.outputImageIndex][threadID.xy] = float4(normal, 1.0);
            break;
        case 4: // PBR (R=metallic, G=roughness)
            float4 pbr = textures[pc.textureIndex].SampleLevel(samplers[pc.samplerIndex], uv, 0);
            renderTextures[pc.outputImageIndex][threadID.xy] = float4(pbr.r, pbr.g, 0.0, 1.0);
            break;
        case 5: // Velocity
            float2 velocity = textures[pc.textureIndex].SampleLevel(samplers[pc.samplerIndex], uv, 0).xy;
            float2 centered = abs((velocity - 0.5) * 100.0);
            renderTextures[pc.outputImageIndex][threadID.xy] = float4(centered, 0.0, 1.0);
            break;
        default:
            renderTextures[pc.outputImageIndex][threadID.xy] = float4(1.0, 0.0, 1.0, 1.0); // Magenta for invalid
            break;
    }

}