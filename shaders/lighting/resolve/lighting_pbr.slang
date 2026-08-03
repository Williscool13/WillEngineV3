import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import ddgi_interop;
import ddgi_common;

import shader_functions;
import pbr_functions;
import packing_functions;
import shader_constants;
import punctual_lighting_common;
import world_grid_interop;
import binning_common;
import visibility_lighting_common;
import gi_gather_common;
import reflection_probe_interop;
import reflection_probe_common;

import bindless_declarations;

#define GI_SKY_SPECULAR_OCCLUSION 1

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
    float3 worldPosition = mul(pc.sceneData[pc.sceneDataIndex].invView, float4(viewPosition, 1.0)).xyz;

    // Clamp roughness so D_GGX never produces 0/0 at NdotH=1 (degenerate delta lobe)
    float roughness = max(li.roughness, MIN_ROUGHNESS);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    // Directional sun is applied separately in the RT directional lighting pass.
    float gtao = li.shadows.y;
    float3 directLight = float3(0.0);

    // Area / sphere lights, selected per WorldGridBinning cell.
    if (pc.worldGridBuffer != 0)
    {
        WorldGridKey worldGridKey = WorldGridResolveCell(worldPosition, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz);
        uint2 gridEntry = pc.worldGridBuffer[worldGridKey.cellIndex];
        for (uint j = 0; j < gridEntry.y; j++)
        {
            uint lightIndex = pc.worldGridIndexList[gridEntry.x + j];
            PunctualLight punctual = DegenerateLight(pc.lightData->lights[lightIndex], pc.sceneData[pc.sceneDataIndex].view);
            directLight += ShadePunctual(punctual, N, V, viewPosition, li.albedo.xyz, li.metallic, roughness, F0);
        }
    }
    else
    {
        for (int j = 0; j < pc.lightData->lightCount; j++)
        {
            PunctualLight punctual = DegenerateLight(pc.lightData->lights[j], pc.sceneData[pc.sceneDataIndex].view);
            directLight += ShadePunctual(punctual, N, V, viewPosition, li.albedo.xyz, li.metallic, roughness, F0);
        }
    }

    // DDGI probe diffuse
    float3 ibl = 0.0;
    float3 indirectDiffuse = float3(0.0);
    {
        float3 F_ibl = F_SCHLICK_ROUGHNESS(nDotV, F0, roughness);
        float3 kD_ibl = (1.0 - F_ibl) * (1.0 - li.metallic);
        float3 worldN = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].invView, N));

        if (pc.giGatherMode != 0u) {
            indirectDiffuse = rdgTexturesFloat4[pc.giResolvedIndex].Load(int3(pixel, 0)).rgb;
        } else {
            if (pc.skyboxIndex >= 0) {
                indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], worldN) * pc.iblIntensity;
            }
            float giW = 0.0;
            if (pc.bDDGIApply != 0) {
                float3 cameraDirection = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].invView, viewPosition));
                float edgeFade;
                float4 gi = DDGISampleIrradianceCascaded(worldPosition, worldN, cameraDirection, pc.ddgiCascades, length(viewPosition), edgeFade);
                indirectDiffuse = gi.rgb * gi.w + indirectDiffuse * (1.0 - max(gi.w, edgeFade));
                giW = gi.w;
            }
            if (pc.reflectionProbeCount > 0u) {
                float probeFade;
                int probeIndex = PickReflectionProbeBinned(worldPosition, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, probeFade);
                if (probeIndex >= 0) {
                    ReflectionProbeGPU probe = pc.reflectionProbes[probeIndex];
                    float3 probeIrr = SampleReflectionProbeIrradiance(probe, worldN) * probe.intensity;
                    probeIrr *= lerp(1.0, min(1.0, Luma(indirectDiffuse) / max(Luma(probeIrr), 1e-4)), giW);
                    indirectDiffuse = lerp(indirectDiffuse, probeIrr, probeFade);
                }
            }
        }

        // Select, don't lerp: a traced ray already resolves sky visibility and occlusion, so it replaces the probe specular outright and skips GTAO.
        float3 reflectionSpecular = float3(0.0);
        if (pc.reflectionIndex != ~0x0u && roughness <= pc.reflectionRoughnessMax) {
            float2 envBRDF = assetTextures[BRDF_LUT_BINDLESS_INDEX].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], float2(nDotV, roughness), 0).rg;
            reflectionSpecular = rdgTexturesFloat4[pc.reflectionIndex].Load(int3(pixel, 0)).rgb * (F0 * envBRDF.x + envBRDF.y);
        } else {
            if (pc.skyboxIndex >= 0) {
                reflectionSpecular = SpecularReflection(assetCubemaps[pc.skyboxIndex], assetTextures[BRDF_LUT_BINDLESS_INDEX], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], V, N, pc.sceneData[pc.sceneDataIndex].invView, roughness, F_ibl) * pc.iblIntensity;
#if GI_SKY_SPECULAR_OCCLUSION
                // The diffuse GI already resolves local sky reachability, reuse for specular
                float3 skyIrradiance = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], worldN) * pc.iblIntensity;
                reflectionSpecular *= saturate(Luma(indirectDiffuse) / max(Luma(skyIrradiance), 1e-4));
#endif
            }
            if (pc.reflectionProbeCount > 0u) {
                float probeFade;
                int probeIndex = PickReflectionProbeBinned(worldPosition, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, probeFade);
                if (probeIndex >= 0) {
                    ReflectionProbeGPU probe = pc.reflectionProbes[probeIndex];
                    float3 worldR = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].invView, reflect(-V, N)));
                    float2 envBRDF = assetTextures[BRDF_LUT_BINDLESS_INDEX].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], float2(nDotV, roughness), 0).rg;
                    float3 probeRadiance = SampleReflectionProbeSpecular(probe, worldPosition, worldR, roughness);
                    float3 probeTerm = probeRadiance * (F_ibl * envBRDF.x + envBRDF.y) * probe.intensity;
                    reflectionSpecular = lerp(reflectionSpecular, probeTerm, probeFade);
                }
            }
            reflectionSpecular *= gtao;
        }
        ibl = kD_ibl * indirectDiffuse * li.albedo.xyz * gtao + reflectionSpecular;
    }

    float3 finalColor = directLight + ibl + li.emissiveRGB;
    rdgStorageFloat4[pc.primaryOutputImageIndex][pixel] = float4(finalColor, 1.0);
}
