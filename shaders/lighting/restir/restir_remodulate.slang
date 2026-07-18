import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;
import pbr_functions;
import restir_common;
import ddgi_interop;
import ddgi_common;
import gi_gather_common;

static const float BILATERAL_DEPTH_SIGMA = 0.01;
static const float BILATERAL_NORMAL_POWER = 32.0;
static const float BILATERAL_MATERIAL_SIGMA = 0.1;
static const float BILATERAL_SPATIAL_SIGMA = 1.0;
static const float BILATERAL_MIN_WEIGHT = 1e-6;

#define GI_SKY_SPECULAR_OCCLUSION 1

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
    roughness = max(roughness, 0.045);
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

    float3 diffuse = rdgTexturesFloat4[pc.diffuseIndex].Load(int3(pixel, 0)).rgb;
    float3 specular = rdgTexturesFloat4[pc.specularIndex].Load(int3(pixel, 0)).rgb;

    // Indirect: diffuse from the DDGI probe volume when it covers this point (skybox irradiance fallback), specular from the prefiltered skybox.
    float3 indirectDiffuse = float3(0.0);
    float3 ibl = float3(0.0);
    {
        float3 F_ibl = F_SCHLICK_ROUGHNESS(nDotV, F0, roughness);
        float3 kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);
        float3 worldN = normalize(mul(float3x3(scene.invView), N));

        float3 skyIrradiance = float3(0.0);
        if (pc.skyboxIndex >= 0) {
            skyIrradiance = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], worldN) * pc.iblIntensity;
        }

        if (pc.giGatherMode != 0u) {
            // Upscaled gather; already contains the skybox and probe fallbacks.
            indirectDiffuse = rdgTexturesFloat4[pc.giResolvedIndex].Load(int3(pixel, 0)).rgb;
        } else {
            indirectDiffuse = skyIrradiance;
            if (pc.bDDGIApply != 0) {
                float3 worldPosition = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
                float3 cameraDirection = normalize(mul(float3x3(scene.invView), viewPosition));
                float edgeFade;
                float4 gi = DDGISampleIrradianceCascaded(worldPosition, worldN, cameraDirection, pc.ddgiCascades, length(viewPosition), edgeFade);
                indirectDiffuse = gi.rgb * gi.w + indirectDiffuse * (1.0 - max(gi.w, edgeFade));
            }
        }

        float3 reflectionSpecular = float3(0.0);
        if (pc.skyboxIndex >= 0) {
            reflectionSpecular = SpecularReflection(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], V, N, scene.invView, roughness, F_ibl, envBRDF) * pc.iblIntensity;
#if GI_SKY_SPECULAR_OCCLUSION
            // The diffuse GI already resolves local sky reachability, reuse for specular
            reflectionSpecular *= saturate(Luma(indirectDiffuse) / max(Luma(skyIrradiance), 1e-4));
#endif
        }

        if (pc.reflectionIndex != ~0x0u && roughness <= pc.reflectionRoughnessMax) {
            float3 tracedReflection = rdgTexturesFloat4[pc.reflectionIndex].Load(int3(pixel, 0)).rgb;
            reflectionSpecular = tracedReflection * specReflectance;
        }

        float gtao = 1.0;
        if (pc.shadowsIndex != ~0x0u) {
            gtao = rdgTexturesFloat2[pc.shadowsIndex].Load(int3(pixel, 0)).y;
        }
        ibl = kD_ibl * indirectDiffuse * albedo * gtao + reflectionSpecular;
    }

    float3 finalColor;
    if (pc.outputMode == 1u) {
        finalColor = diffuse * albedo + ibl + emissive;
    } else if (pc.outputMode == 2u) {
        finalColor = specular * specReflectance + ibl + emissive;
    } else if (pc.outputMode == 3u) {
        // Raw indirect diffuse
        finalColor = indirectDiffuse;
    } else {
        finalColor = diffuse * albedo + specular * specReflectance + ibl + emissive;
    }
    if (pc.giGatherMode >= 2u) {
        finalColor = GIGatherDebugColor(pc.giDataIndex, pc.giResolvedIndex, pixel, pc.giGatherMode, indirectDiffuse);
    }
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(finalColor, 1.0);
}
