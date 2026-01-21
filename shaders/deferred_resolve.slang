import push_constant_interop;
import shader_functions;
import constants_interop;
import pbr_functions;

import bindless_declarations;

[numthreads(16, 16, 1)]
void ComputeMain(uniform DeferredResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData->mainRenderTargetSize.x || threadID.y >= pc.sceneData->mainRenderTargetSize.y) return;

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.sceneData->mainRenderTargetSize);
    float4 albedo = rdgTextures[pc.albedoIndex].Load(int3(threadID.xy, 0));
    float depth = rdgTextures[pc.depthIndex].Load(int3(threadID.xy, 0)).r;
    if (depth <= 0.0) {
        return;
    }
    float3 viewNormal = rdgTextures[pc.normalIndex].Load(int3(threadID.xy, 0)).xyz;
    float4 pbrData = rdgTextures[pc.pbrIndex].Load(int3(threadID.xy, 0));
    float4 emissiveData = rdgTextures[pc.emissiveIndex].Load(int3(threadID.xy, 0));
    float2 shadows = rdgTextures[pc.shadowsIndex].Load(int3(threadID.xy, 0)).xy;

    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceDepth);

    float roughness = pbrData.g;
    float metallic = pbrData.r;

    float3 N = normalize(viewNormal * 2.0 - 1.0);
    float3 V = normalize(-viewPosition);

    // Directional light in view space (transform from world space)
    float3 L = normalize(mul((float3x3)pc.sceneData->view, normalize(-pc.lightData->mainLightDirection.xyz)));
    float3 H = normalize(V + L);

    // SPECULAR: Cook-Torrance BRDF
    float NDF = D_GGX(N, H, roughness);
    float G = G_SCHLICKGGX_SMITH(N, V, L, roughness);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.xyz, metallic);
    float3 F = F_SCHLICK(V, H, F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = numerator / max(denominator, 0.001);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    // DIFFUSE: Lambert
    float nDotL = max(dot(N, L), 0.0);
    float3 diffuse = kD * albedo.xyz / 3.14159265359;

    float shadowFactor = shadows.x;
    float gtao = shadows.y;

    const float lightIntensity = pc.lightData->mainLightDirection.w;
    const float3 lightColor = pc.lightData->mainLightColor.rgb;
    float3 directLight = (diffuse + specular) * nDotL * shadowFactor * lightIntensity * lightColor;

    // Ambient
    float3 ambient = albedo.xyz * 0.03 * gtao;
    float3 emissive = emissiveData.rgb * emissiveData.a;
    float3 finalColor = directLight + ambient + emissive;
    rdgRenderTextures[pc.outputImageIndex][threadID.xy] = float4(finalColor, 1.0);
}