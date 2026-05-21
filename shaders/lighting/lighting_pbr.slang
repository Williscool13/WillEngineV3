import push_constant_interop;
import shader_functions;
import constants_interop;
import model_interop;
import pbr_functions;
import visibility_lighting_common;

import bindless_declarations;

[numthreads(16, 16, 1)]
void ComputeMain(uniform LightingResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    LightingDispatchParameters lightParams = pc.lightDispatchBuffer[pc.lightingIndex];
    uint2 pixel = threadID.xy + uint2(lightParams.minX, lightParams.minY);
    if (any(pixel >= uint2(lightParams.maxX, lightParams.maxY))) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData[pc.sceneDataIndex].depthLinearizeMult, pc.sceneData[pc.sceneDataIndex].depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData[pc.sceneDataIndex].ndcToViewMul, pc.sceneData[pc.sceneDataIndex].ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);

    // Clamp roughness so D_GGX never produces 0/0 at NdotH=1 (degenerate delta lobe)
    float roughness = max(li.roughness, 0.045);

    // Directional light in view space (transform from world space)
    float3 L = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].view, normalize(-pc.lightData->mainLightDirection.xyz)));
    float3 H = normalize(V + L);

    // SPECULAR: Cook-Torrance BRDF
    float NDF = D_GGX(N, H, roughness);
    float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float3 F = F_SCHLICK(V, H, F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = numerator / max(denominator, 0.001);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - li.metallic);

    // DIFFUSE: Oren-Nayar
    float nDotL = max(dot(N, L), 0.0);
    float nDotV = max(dot(N, V), 0.0);
    float orenNayar = Diffuse_OrenNayar(nDotL, nDotV, L, V, roughness);
    float3 diffuse = kD * li.albedo.xyz * orenNayar;

    float shadowFactor = li.shadows.x;
    float gtao = li.shadows.y;

    const float lightIntensity = pc.lightData->mainLightDirection.w;
    const float3 lightColor = pc.lightData->mainLightColor.rgb;
    float3 directLight = (diffuse + specular) * nDotL * shadowFactor * lightIntensity * lightColor;

    // IBL
    float3 ibl = 1.0;
    if (pc.skyboxIndex >= 0)
    {
        float NdotV = max(dot(N, V), 0.0);
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
        float3 F_ibl = F_SCHLICK_ROUGHNESS(NdotV, F0, roughness);
        float3 kS_ibl = F_ibl;
        float3 kD_ibl = (1.0 - kS_ibl) * (1.0 - li.metallic);

        float3 worldN = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].invView, N));
        float3 irradiance = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], worldN);
        float3 reflectionDiffuse = irradiance * li.albedo.xyz;
        float3 reflectionSpecular = SpecularReflection(assetCubemaps[pc.skyboxIndex], assetTextures[BRDF_LUT_BINDLESS_INDEX], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], V, N, pc.sceneData[pc.sceneDataIndex].invView, roughness, F_ibl);
        ibl = (kD_ibl * reflectionDiffuse + reflectionSpecular) * gtao;
    }

    float3 finalColor = directLight + ibl + li.emissiveRGB;
    rdgStorageFloat4[pc.outputImageIndex][pixel] = float4(finalColor, 1.0);
}
