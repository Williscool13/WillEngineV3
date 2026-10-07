import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;
import pbr_functions;
import packing_functions;
import shader_constants;
import restir_common;
import lights_interop;
import ddgi_interop;
import ddgi_common;
import gi_gather_common;
import reflection_probe_interop;
import reflection_probe_common;
import indirect_lighting_common;

static const float BILATERAL_DEPTH_SIGMA = 0.01;
static const float BILATERAL_NORMAL_POWER = 32.0;
static const float BILATERAL_MATERIAL_SIGMA = 0.1;
static const float BILATERAL_SPATIAL_SIGMA = 1.0;
static const float BILATERAL_MIN_WEIGHT = 1e-6;

static void EvalSunContribution(DirectionalLightData sun, float3 N, float3 V, float3 L, float nDotV, float3 F0,
                                float metallic, float roughness, float3 specReflectance, out float3 diffuseTerm, out float3 specularTerm)
{
    diffuseTerm = float3(0.0);
    specularTerm = float3(0.0);

    float nDotL = max(dot(N, L), 0.0);
    if (nDotL <= 0.0) {
        return;
    }

    float3 H = normalize(V + L);
    float NDF = D_GGX(N, H, roughness);
    float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
    float3 F = F_SCHLICK(V, H, F0);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float orenNayar = Diffuse_OrenNayar(nDotL, nDotV, L, V, roughness);
    float3 sunIrradiance = UnpackUnorm4x8(sun.packedColor).rgb * sun.directionIntensity.w;
    float3 common = nDotL * sunIrradiance;

    diffuseTerm = kD * orenNayar * common;
    specularTerm = (NDF * G * F) / max(4.0 * nDotV * nDotL, 0.001) * common / max(specReflectance, float3(0.01));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeReSTIRRemodulate(uniform ReSTIRRemodulatePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    const uint2 pixel = dtid.xy;
    if (pixel.x >= pc.width || pixel.y >= pc.height) {
        return;
    }

    const uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if ((gt1.w & 1u) == 0u) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    const float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    const uint2 gt2 = rdgTexturesUInt2[pc.gbufferTwoIndex].Load(int3(pixel, 0));

    float3 viewNormal = UnpackNormalOct16(gt1.r);
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    roughness = max(roughness, MIN_ROUGHNESS);
    const float3 albedo = UnpackAlbedoRGBA8(gt2.r).rgb;
    const float3 emissive = UnpackEmissiveRGBE(gt2.g);

    float2 uv = (float2(pixel) + 0.5) / float2(scene.mainRenderTargetSize);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 N = normalize(viewNormal);
    float3 V = normalize(-viewPosition);
    float nDotV = max(dot(N, V), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float2 envBRDF = assetTextures[BRDF_LUT_BINDLESS_INDEX].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], float2(nDotV, roughness), 0).rg;
    float3 specReflectance = F0 * envBRDF.x + envBRDF.y;

    float3 diffuse = rdgTexturesFloat4[pc.diffuseIndex].Load(int3(pixel, 0)).rgb / scene.preExposure;
    float3 specular = rdgTexturesFloat4[pc.specularIndex].Load(int3(pixel, 0)).rgb / scene.preExposure;

    float2 shadows = pc.shadowsIndex != ~0x0u ? rdgTexturesFloat2[pc.shadowsIndex].Load(int3(pixel, 0)).xy : float2(1.0);
    float gtao = shadows.y;

    float3 sunDiffuse = float3(0.0);
    float3 sunSpecular = float3(0.0);
    if (pc.bDirectSun != 0u && shadows.x > 0.0) {
        DirectionalLightData sun = pc.lightData->directionalLight;
        float3 L = normalize(mul((float3x3)scene.view, normalize(-sun.directionIntensity.xyz)));
        float3 sunDiffuseTerm;
        float3 sunSpecularTerm;
        EvalSunContribution(sun, N, V, L, nDotV, F0, metallic, roughness, specReflectance, sunDiffuseTerm, sunSpecularTerm);
        sunDiffuse = sunDiffuseTerm * albedo * shadows.x;
        sunSpecular = sunSpecularTerm * specReflectance * shadows.x;
    }

    float3 F_ibl = F_SCHLICK_ROUGHNESS(nDotV, F0, roughness);
    const bool bReflectionEligible = roughness <= pc.reflectionRoughnessMax;
    IndirectLightingInputs indirectInputs;
    indirectInputs.pixel = pixel;
    indirectInputs.viewPosition = viewPosition;
    indirectInputs.viewN = N;
    indirectInputs.viewV = V;
    indirectInputs.roughness = roughness;
    indirectInputs.F0 = F0;
    indirectInputs.F_ibl = F_ibl;
    indirectInputs.envBRDF = envBRDF;
    indirectInputs.gtao = gtao;
    indirectInputs.preExposure = scene.preExposure;
    indirectInputs.invView = scene.invView;
    indirectInputs.cameraWorldPos = scene.cameraWorldPos.xyz;
    indirectInputs.skyboxIndex = pc.skyboxIndex;
    indirectInputs.iblIntensity = pc.iblIntensity;
    indirectInputs.giResolvedIndex = pc.giGatherMode != 0u ? pc.giResolvedIndex : ~0x0u;
    indirectInputs.skyVisIndex = pc.skyVisIndex;
    indirectInputs.reflectionIndex = bReflectionEligible ? pc.reflectionIndex : ~0x0u;
    indirectInputs.bDDGI = pc.bDDGIApply != 0;
    indirectInputs.ddgiCascades = pc.ddgiCascades;
    indirectInputs.reflectionProbeCount = pc.reflectionProbeCount;
    indirectInputs.reflectionProbes = pc.reflectionProbes;
    indirectInputs.worldGridProbeGrid = pc.worldGridProbeGrid;
    IndirectLighting indirect = ResolveIndirectLighting(indirectInputs);

    // Merged reflections are already in the denoised specular.
    const float3 indirectDiffuse = indirect.diffuse;
    const float3 reflectionSpecular = pc.bReflectionMerged != 0u && bReflectionEligible ? float3(0.0) : indirect.specular;
    float3 kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);
    float3 ibl = kD_ibl * indirectDiffuse * pc.indirectIntensity * albedo * OrenNayarDirectionalAlbedo(nDotV, roughness) * gtao + reflectionSpecular;

    if (pc.screenDiffuseOutIndex != ~0x0u) {
        float ratio = rdgTexturesFloat[pc.diffuseRatioIndex].Load(int3(pixel, 0)).r;
        rdgStorageFloat4[pc.screenDiffuseOutIndex][pixel] = float4((diffuse * ratio + indirectDiffuse) * scene.preExposure, 1.0);
    }

    float3 finalColor;
    float litAlpha = 1.0;
    if (pc.outputMode == 1u) {
        finalColor = diffuse * albedo + sunDiffuse + ibl + emissive;
    } else if (pc.outputMode == 2u) {
        finalColor = specular * specReflectance + sunSpecular + ibl + emissive;
    } else if (pc.outputMode == 3u) {
        // Raw indirect diffuse
        finalColor = indirectDiffuse;
    } else {
        finalColor = diffuse * albedo + specular * specReflectance + sunDiffuse + sunSpecular + ibl + emissive;
        litAlpha = 1.0 - saturate(Luma(specular * specReflectance + sunSpecular + reflectionSpecular) / max(Luma(finalColor), 1e-4));
    }
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(finalColor * scene.preExposure, litAlpha);
}
