import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import ddgi_interop;
import ddgi_common;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;

import bindless_declarations;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeLightingPBR(uniform VisibilityLightingPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
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

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData[pc.sceneDataIndex].depthLinearizeMult, pc.sceneData[pc.sceneDataIndex].depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData[pc.sceneDataIndex].ndcToViewMul, pc.sceneData[pc.sceneDataIndex].ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);

    // Clamp roughness so D_GGX never produces 0/0 at NdotH=1 (degenerate delta lobe)
    float roughness = max(li.roughness, 0.045);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    // Directional sun is applied separately in the RT directional lighting pass.
    float gtao = li.shadows.y;
    float3 directLight = float3(0.0);

    // Point lights
    for (int i = 0; i < pc.lightData->pointLightCount; i++)
    {
        PointLightData pl = pc.lightData->pointLights[i];

        float3 plViewPos = mul(pc.sceneData[pc.sceneDataIndex].view, float4(pl.positionRange.xyz, 1.0)).xyz;
        float3 plVec = plViewPos - viewPosition;
        float dist = length(plVec);
        float range = pl.positionRange.w;
        if (dist >= range) { continue; }

        float3 Lpl = plVec / dist;
        float3 Hpl = normalize(V + Lpl);

        float NDFpl = D_GGX(N, Hpl, roughness);
        float Gpl = G_SMITH_HEIGHT_CORRELATED(N, V, Lpl, roughness);
        float3 Fpl = F_SCHLICK(V, Hpl, F0);
        float3 specularPl = (NDFpl * Gpl * Fpl) / max(4.0 * max(dot(N, V), 0.0) * max(dot(N, Lpl), 0.0), 0.001);

        float3 kSpl = Fpl;
        float3 kDpl = (1.0 - kSpl) * (1.0 - li.metallic);
        float nDotLpl = max(dot(N, Lpl), 0.0);
        float orenNayarPl = Diffuse_OrenNayar(nDotLpl, nDotV, Lpl, V, roughness);
        float3 diffusePl = kDpl * li.albedo.xyz * orenNayarPl;

        float attenuation = LightDistanceFalloff(dist, range);
        float3 plColor = UnpackUnorm4x8(pl.packedColor).rgb;
        directLight += (diffusePl + specularPl) * nDotLpl * attenuation * pl.intensity * plColor;
    }

    // DDGI probe diffuse
    float3 ibl = 0.0;
    {
        float3 F_ibl = F_SCHLICK_ROUGHNESS(nDotV, F0, roughness);
        float3 kD_ibl = (1.0 - F_ibl) * (1.0 - li.metallic);
        float3 worldN = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].invView, N));

        float3 indirectDiffuse = float3(0.0);
        if (pc.skyboxIndex >= 0) {
            indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], worldN) * pc.iblIntensity;
        }
        if (pc.bDDGIApply != 0) {
            float3 worldPosition = mul(pc.sceneData[pc.sceneDataIndex].invView, float4(viewPosition, 1.0)).xyz;
            float3 cameraDirection = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].invView, viewPosition));
            float4 gi = DDGISampleIrradiance(worldPosition, worldN, cameraDirection, pc.ddgiVolume, pc.ddgiIrradianceIndex, pc.ddgiVisibilityIndex);
            // gi.w fades to the skybox fallback over the volume's outermost cell.
            indirectDiffuse = lerp(indirectDiffuse, gi.rgb, gi.w);
        }

        float3 reflectionSpecular = float3(0.0);
        if (pc.skyboxIndex >= 0) {
            reflectionSpecular = SpecularReflection(assetCubemaps[pc.skyboxIndex], assetTextures[BRDF_LUT_BINDLESS_INDEX], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], V, N, pc.sceneData[pc.sceneDataIndex].invView, roughness, F_ibl) * pc.iblIntensity;
        }
        ibl = (kD_ibl * indirectDiffuse * li.albedo.xyz + reflectionSpecular) * gtao;
    }

    float3 finalColor = directLight + ibl + li.emissiveRGB;
    rdgStorageFloat4[pc.primaryOutputImageIndex][pixel] = float4(finalColor, 1.0);
}
