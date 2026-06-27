import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;
import pbr_functions;
import restir_common;

static const float BILATERAL_DEPTH_SIGMA = 0.01;
static const float BILATERAL_NORMAL_POWER = 32.0;
static const float BILATERAL_MATERIAL_SIGMA = 0.1;
static const float BILATERAL_SPATIAL_SIGMA = 1.0;
static const float BILATERAL_MIN_WEIGHT = 1e-6;

[shader("compute")]
[numthreads(8, 8, 1)]
void ReSTIRRemodulateMain(uniform ReSTIRRemodulatePushConstant pc, uint3 dtid : SV_DispatchThreadID)
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
    const float3 albedo = UnpackAlbedoRGB8(gt2.r).rgb;
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

    float3 diffuse, specular;
    if (pc.pixelScale == 1u) {
        diffuse = rdgTexturesFloat4[pc.diffuseIndex].Load(int3(pixel, 0)).rgb;
        specular = rdgTexturesFloat4[pc.specularIndex].Load(int3(pixel, 0)).rgb;
    } else {
        uint2 halfExtent = uint2(pc.width, pc.height) / pc.pixelScale;

        float2 hTexel = (float2(pixel) + 0.5) / float(pc.pixelScale) - 0.5;
        int2 hCenter = int2(round(hTexel));

        float invTwoSigmaSq = 1.0 / (2.0 * BILATERAL_SPATIAL_SIGMA * BILATERAL_SPATIAL_SIGMA);

        float totalW = 0.0;
        float3 diffSum = float3(0.0);
        float3 specSum = float3(0.0);

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int2 hPos = clamp(hCenter + int2(dx, dy), int2(0, 0), int2(halfExtent) - 1);
                int2 fullPos = FetchFullPixel(pc.quadSelectionIndex, hPos, pc.pixelScale);

                float2 diff2 = float2(hPos) - hTexel;
                float wSpatial = exp(-dot(diff2, diff2) * invTwoSigmaSq);

                uint4 tapGt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPos, 0));
                float3 tapNormal = normalize(UnpackNormalOct16(tapGt1.r));
                float tapRaw = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPos, 0)).r;
                float tapViewZ = ScreenToViewSpaceDepth(tapRaw, scene.depthLinearizeMult, scene.depthLinearizeAdd);

                float depthDiff = abs(tapViewZ - viewSpaceDepth) / (viewSpaceDepth + 1e-6);
                float wDepth = exp(-depthDiff / BILATERAL_DEPTH_SIGMA);
                float wNormal = pow(max(0.0, dot(tapNormal, N)), BILATERAL_NORMAL_POWER);

                float tapRoughness;
                float tapMetallic;
                UnpackPBR(tapGt1.b, tapRoughness, tapMetallic);
                tapRoughness = max(tapRoughness, 0.045);
                float wMaterial = exp(-(abs(tapRoughness - roughness) + abs(tapMetallic - metallic)) / BILATERAL_MATERIAL_SIGMA);

                float w = wSpatial * wDepth * wNormal * wMaterial;

                diffSum += rdgTexturesFloat4[pc.diffuseIndex].Load(int3(hPos, 0)).rgb * w;
                specSum += rdgTexturesFloat4[pc.specularIndex].Load(int3(hPos, 0)).rgb * w;
                totalW += w;
            }
        }

        if (totalW > BILATERAL_MIN_WEIGHT) {
            diffuse = diffSum / totalW;
            specular = specSum / totalW;
        } else {
            int2 nearest = clamp(hCenter, int2(0, 0), int2(halfExtent) - 1);
            diffuse = rdgTexturesFloat4[pc.diffuseIndex].Load(int3(nearest, 0)).rgb;
            specular = rdgTexturesFloat4[pc.specularIndex].Load(int3(nearest, 0)).rgb;
        }
    }

    float3 ibl = float3(0.0);
    if (pc.skyboxIndex >= 0) {
        float3 F_ibl = F_SCHLICK_ROUGHNESS(nDotV, F0, roughness);
        float3 kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);
        float3 worldN = normalize(mul(float3x3(scene.invView), N));
        float3 irradiance = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], worldN);
        float3 reflectionDiffuse = irradiance * albedo;
        float3 reflectionSpecular = SpecularReflection(assetCubemaps[pc.skyboxIndex], assetTextures[BRDF_LUT_BINDLESS_INDEX], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], V, N, scene.invView, roughness, F_ibl);
        ibl = (kD_ibl * reflectionDiffuse + reflectionSpecular) * pc.iblIntensity;
    }

    float3 finalColor;
    if (pc.outputMode == 1u) {
        finalColor = diffuse * albedo + ibl + emissive;
    } else if (pc.outputMode == 2u) {
        finalColor = specular * specReflectance + ibl + emissive;
    } else {
        finalColor = diffuse * albedo + specular * specReflectance + ibl + emissive;
    }
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(finalColor, 1.0);
}
