/*
 * ReSTIR remodulate / composite.
 * Recombines the demodulated diffuse and specular intermediates back into final radiance:
 *   final = diffuse * albedo + specular * specReflectance + emissive
 * specReflectance is the same preintegrated env-BRDF term the resolve divided out.
 */
import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

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

    // Reconstruct the view vector so nDotV matches the resolve's env-BRDF lookup.
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

    float3 finalColor;
    if (pc.outputMode == 1u) {
        finalColor = diffuse * albedo + emissive;
    } else if (pc.outputMode == 2u) {
        finalColor = specular * specReflectance + emissive;
    } else {
        finalColor = diffuse * albedo + specular * specReflectance + emissive;
    }
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(finalColor, 1.0);
}
