/*
 * ReSTIR remodulate / composite.
 * Recombines the demodulated diffuse and specular intermediates back into final radiance:
 *   final = diffuse * albedo + specular * specReflectance + emissive
 * specReflectance is the same preintegrated env-BRDF term the resolve divided out.
 *
 * When pixelScale > 1 the diff/spec inputs are at half-res; bilateral upsampling weights each of the 4 bilinear taps by depth+normal similarity so cross-surface blending is suppressed at edges.
 */
import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

static const float BILATERAL_DEPTH_SIGMA = 0.01;
static const float BILATERAL_NORMAL_POWER = 32.0;
// todo add weighting by material ID to try to catch vastly different PBR properties
// todo add weighting by material ID to try to catch vastly different PBR properties
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

    // --- Bilateral upsampling ---
    // When pixelScale == 1 this degenerates to a simple Load (bw[0]==1, same-surface tap).
    uint2 halfExtent = uint2(pc.width, pc.height) / pc.pixelScale;

    float2 hTexel = (float2(pixel) + 0.5) / float(pc.pixelScale) - 0.5;
    int2 hBase = int2(floor(hTexel));
    float2 hFrac = hTexel - float2(hBase);

    float bw[4];
    bw[0] = (1.0 - hFrac.x) * (1.0 - hFrac.y);
    bw[1] = hFrac.x * (1.0 - hFrac.y);
    bw[2] = (1.0 - hFrac.x) * hFrac.y;
    bw[3] = hFrac.x          * hFrac.y;

    int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

    float totalW = 0.0;
    float3 diffSum = float3(0.0);
    float3 specSum = float3(0.0);

    for (int i = 0; i < 4; i++) {
        int2 hPos = clamp(hBase + offsets[i], int2(0, 0), int2(halfExtent) - 1);
        int2 fullPos = hPos * int(pc.pixelScale);

        uint4 tapGt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPos, 0));
        float3 tapNormal = normalize(UnpackNormalOct16(tapGt1.r));
        float tapRaw = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPos, 0)).r;
        float tapViewZ = ScreenToViewSpaceDepth(tapRaw, scene.depthLinearizeMult, scene.depthLinearizeAdd);

        float depthDiff = abs(tapViewZ - viewSpaceDepth) / (viewSpaceDepth + 1e-6);
        float wDepth = exp(-depthDiff / BILATERAL_DEPTH_SIGMA);
        float wNormal = pow(max(0.0, dot(tapNormal, N)), BILATERAL_NORMAL_POWER);
        float w = bw[i] * wDepth * wNormal;

        diffSum += rdgTexturesFloat4[pc.diffuseIndex].Load(int3(hPos, 0)).rgb * w;
        specSum += rdgTexturesFloat4[pc.specularIndex].Load(int3(hPos, 0)).rgb * w;
        totalW += w;
    }

    float3 diffuse, specular;
    if (totalW > BILATERAL_MIN_WEIGHT) {
        diffuse = diffSum / totalW;
        specular = specSum / totalW;
    } else {
        // All taps rejected; fall back to nearest half-res sample.
        int2 nearest = clamp(int2(round(hTexel)), int2(0, 0), int2(halfExtent) - 1);
        diffuse = rdgTexturesFloat4[pc.diffuseIndex].Load(int3(nearest, 0)).rgb;
        specular = rdgTexturesFloat4[pc.specularIndex].Load(int3(nearest, 0)).rgb;
    }

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
