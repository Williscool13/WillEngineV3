import push_constant_interop;
import shader_functions;
import pbr_functions;

[[vk::binding(0, 0)]]
SamplerState samplers[];
[[vk::binding(1, 0)]]
Texture2D textures[];
[[vk::binding(2, 0)]]
RWTexture2D<float4> renderTextures[];

[[vk::push_constant]]
DeferredResolvePushConstant pc;

[numthreads(16, 16, 1)]
void computeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.extent.x || threadID.y >= pc.extent.y) return;

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.extent);
    float4 albedo = textures[pc.albedoIndex].SampleLevel(samplers[pc.pointSamplerIndex], uv, 0);

    float depth = textures[pc.depthIndex].SampleLevel(samplers[pc.pointSamplerIndex], uv, 0).r;
    float3 viewNormal = textures[pc.normalIndex].SampleLevel(samplers[pc.pointSamplerIndex], uv, 0).xyz;
    float4 pbrData = textures[pc.pbrIndex].SampleLevel(samplers[pc.pointSamplerIndex], uv, 0);

    float3 viewPosition = reconstructViewPosition(pc->sceneData.invProj, uv, depth);

    float roughness = pbrData.g;
    float metallic = pbrData.r;

    float3 N = normalize(viewNormal);
    float3 V = normalize(-viewPosition);

    // Directional light in view space (transform from world space)
    float3 L = normalize(mul((float3x3)pc.sceneData->view, normalize(-pc.directionalLightDirection.xyz)));
    float3 H = normalize(V + L);

    // SPECULAR: Cook-Torrance BRDF
    float NDF = D_GGX(N, H, roughness);
    //float G = G_Smith(N, V, L, roughness);
    float G = G_SCHLICKGGX_SMITH(N, V, L, roughness);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.xyz, metallic);
    float3 F = F_Schlick(V, H, F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = numerator / max(denominator, 0.001);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    // DIFFUSE: Lambert
    float nDotL = max(dot(N, L), 0.0);
    float3 diffuse = kD * albedo.xyz / 3.14159265359;

    // Simple lighting (no shadows/IBL yet)
    float3 directLight = (diffuse + specular) * nDotL * pc.directionalLightDirection.w * pc.directionalLightColor.xyz;

    // Ambient
    float3 ambient = albedo.xyz * 0.03;
    float3 finalColor = directLight + ambient;
    renderTextures[pc.outputImageIndex][threadID.xy] = float4(finalColor, 1.0);
}