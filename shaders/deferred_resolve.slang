import push_constant_interop;
import shader_functions;
import constants_interop;
import pbr_functions;

import bindless_declarations;

[numthreads(16, 16, 1)]
void ComputeMain(uniform DeferredResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize.x || threadID.y >= pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize.y) return;

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize);
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(threadID.xy, 0)).r;
    if (depth <= 0.0) {
        return;
    }
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(threadID.xy, 0));
    uint2 gt2 = rdgTexturesUInt2[pc.gbufferTwoIndex].Load(int3(threadID.xy, 0));
    float3 viewNormal = UnpackNormalOct16(gt1.r);
    float roughness, metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    float4 albedo = UnpackAlbedoRGB8(gt2.r);
    float3 emissiveRGB = UnpackEmissiveRGBE(gt2.g);
    float4 emissiveData = float4(emissiveRGB, 1.0);
    float2 shadows = float2(1.0f);
    if (pc.shadowsIndex != ~0x0u){
        shadows = rdgTexturesFloat2[pc.shadowsIndex].Load(int3(threadID.xy, 0)).xy;
    }

    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData[pc.sceneDataIndex].depthLinearizeMult, pc.sceneData[pc.sceneDataIndex].depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData[pc.sceneDataIndex].ndcToViewMul, pc.sceneData[pc.sceneDataIndex].ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(viewNormal);
    float3 V = normalize(-viewPosition);

    // Directional light in view space (transform from world space)
    float3 L = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].view, normalize(-pc.lightData->mainLightDirection.xyz)));
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

    // IBL
    float3 ibl = 1.0;
    if (pc.skyboxIndex >= 0)
    {
        float NdotV = max(dot(N, V), 0.0);
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.xyz, metallic);
        float3 F_ibl = F_SCHLICK_ROUGHNESS(NdotV, F0, roughness);
        float3 kS_ibl = F_ibl;
        float3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);

        float3 worldN = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].invView, N));
        float3 irradiance = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_BINDLESS_INDEX], worldN);
        float3 reflectionDiffuse = irradiance * albedo.xyz;
        float3 reflectionSpecular = SpecularReflection(assetCubemaps[pc.skyboxIndex], assetTextures[BRDF_LUT_BINDLESS_INDEX], assetSamplers[ASSET_SAMPLER_BINDLESS_INDEX], V, N, pc.sceneData[pc.sceneDataIndex].invView, roughness, F_ibl);
        ibl = (kD_ibl * reflectionDiffuse + reflectionSpecular) * gtao;
    }

    float3 emissive = emissiveRGB;
    float3 finalColor = directLight + ibl + emissive;
    rdgStorageFloat4[pc.outputImageIndex][threadID.xy] = float4(finalColor, 1.0);
}