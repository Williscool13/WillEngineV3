import push_constant_interop;
import constants_interop;

import bindless_declarations;

[[vk::push_constant]]
DebugVisualizePushConstant pc;

float LinearizeDepth(float depth) {
    float zNear = pc.nearPlane;
    float zFar = pc.farPlane;
    float flipped = 1.0 - depth;
    return (2.0 * zNear) / (zFar + zNear - flipped * (zFar - zNear));
}

[numthreads(16, 16, 1)]
void ComputeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.dstExtent.x || threadID.y >= pc.dstExtent.y) return;

    float2 srcUV = (float2(threadID.xy) + 0.5) / float2(pc.dstExtent);

    switch (pc.debugType) {
        case 1: // Depth
            float depth = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 0).r;
            float linear = LinearizeDepth(depth);
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(linear, 0.0f, 0.0f, 1.0);
            break;
        case 2: // Albedo
            float4 albedo  = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 0);
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(albedo.x, albedo.y, albedo.z, 1.0f);
            break;
        case 3: // Normal (view space, stored in packed unorm [0,1])
            float3 normal = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 0).xyz;
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(normal, 1.0);
            break;
        case 4: // PBR (R=metallic, G=roughness)
            float4 pbr = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 0);
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(pbr.r, pbr.g, 0.0, 1.0);
            break;
        case 5: // Velocity
            float4 velocity = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 0);
            velocity *= 10000.0f;
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(velocity.x, velocity.g, 0.0f, 1.0);
            break;
        case 6: // CSM
            float4 color = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 0);
            color /= 1000.0f;
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(color.x, color.x, color.x, 1.0f);
            break;
        case 7: // CSM
            float4 color2 = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 1);
            color2 /= 1000.0f;
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(color2.x, color2.x, color2.x, 1.0f);
            break;
        case 8:
            float4 color3 = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 2);
            color3 /= 1000.0f;
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(color3.x, color3.x, color3.x, 1.0f);
            break;
        case 9:
            float4 color4 = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 3);
            color4 /= 1000.0f;
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(color4.x, color4.x, color4.x, 1.0f);
            break;
        case 10:
            float4 color5 = rdgTextures[pc.textureIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], srcUV, 4);
            color5 /= 1000.0f;
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(color5.x, color5.x, color5.x, 1.0f);
            break;
        default:
            rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(1.0, 0.0, 1.0, 1.0); // Magenta for invalid
            break;
    }
}