import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import shadow_interop;
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
import indirect_lighting_common;

import bindless_declarations;
import bucket_dispatch_common;

static float LocalShadowVisibility(VisibilityLightingPushConstant pc, uint lightIndex, float3 worldPos, float3 worldN)
{
    if (pc.localShadows == nullptr || lightIndex >= pc.localShadows->lightCount) {
        return 1.0;
    }
    uint packed = pc.localShadows->lightShadow[lightIndex];
    if (packed == LOCAL_SHADOW_NONE) {
        return 1.0;
    }

    uint viewIndex = packed & 0xFFFFu;
    uint faceMask = packed >> 16;
    if (faceMask != 0u) {
        float3 d = worldPos - pc.lightData->lights[lightIndex].position.xyz;
        float3 a = abs(d);
        uint face = a.x >= a.y && a.x >= a.z ? (d.x >= 0.0 ? 0u : 1u) : a.y >= a.z ? (d.y >= 0.0 ? 2u : 3u) : (d.z >= 0.0 ? 4u : 5u);
        if ((faceMask & (1u << face)) == 0u) {
            return 1.0;
        }
        viewIndex += countbits(faceMask & ((1u << face) - 1u));
    }
    ShadowViewGPU view = pc.localShadows->views[viewIndex];
    float texel = view.texelSize * distance(worldPos, view.eye.xyz);
    float4 clip = mul(view.viewProj, float4(worldPos + worldN * (texel * pc.localShadows->normalOffsetTexels), 1.0));
    if (clip.w <= 0.0) {
        return 1.0;
    }
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }

    float2 uv = (ndc.xy * 0.5 + 0.5) * view.atlasScaleOffset.xy + view.atlasScaleOffset.zw;
    float2 atlasTexel = 1.0 / float2(pc.localShadows->atlasExtent);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            visibility += rdgTexturesFloat[pc.localShadowAtlasIndex].SampleCmpLevelZero(rdgCompareSamplers[RDG_LINEAR_DEPTH_SAMPLER_INDEX], uv + float2(x, y) * atlasTexel, ndc.z);
        }
    }
    return visibility / 9.0;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeLightingPBR(uniform VisibilityLightingPushConstant pc, uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID) {
    uint2 pixel = BucketPixel(pc.tileListBuffer, pc.tileCapacity, pc.lightingIndex, gid.x, gtid.xy);
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.materialBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData[pc.sceneDataIndex].depthLinearizeMult, pc.sceneData[pc.sceneDataIndex].depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData[pc.sceneDataIndex].ndcToViewMul, pc.sceneData[pc.sceneDataIndex].ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float3 worldPosition = mul(pc.sceneData[pc.sceneDataIndex].invView, float4(viewPosition, 1.0)).xyz;
    float3 worldN = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].invView, N));

    // Clamp roughness so D_GGX never produces 0/0 at NdotH=1 (degenerate delta lobe)
    float roughness = max(li.roughness, MIN_ROUGHNESS);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    float gtao = li.shadows.y;
    float3 directLight = float3(0.0);

    float3 L = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].view, normalize(-pc.lightData->directionalLight.directionIntensity.xyz)));
    float nDotL = max(dot(N, L), 0.0);
    if (nDotL > 0.0 && li.shadows.x > 0.0) {
        float3 H = normalize(V + L);
        float3 F = F_SCHLICK(V, H, F0);
        float3 specular = (D_GGX(N, H, roughness) * G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness) * F) / max(4.0 * nDotV * nDotL, 0.001);
        float3 diffuse = (1.0 - F) * (1.0 - li.metallic) * li.albedo.xyz * Diffuse_OrenNayar(nDotL, nDotV, L, V, roughness);
        float3 sunColor = UnpackUnorm4x8(pc.lightData->directionalLight.packedColor).rgb * pc.lightData->directionalLight.directionIntensity.w;
        directLight += (diffuse + specular) * nDotL * li.shadows.x * sunColor;
    }

    // Area / sphere lights, selected per WorldGridBinning cell.
    if (pc.worldGridBuffer != 0)
    {
        WorldGridKey worldGridKey = WorldGridResolveCell(worldPosition, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz);
        uint2 gridEntry = pc.worldGridBuffer[worldGridKey.cellIndex];
        for (uint j = 0; j < gridEntry.y; j++)
        {
            uint lightIndex = pc.worldGridIndexList[gridEntry.x + j];
            PunctualLight punctual = DegenerateLight(pc.lightData->lights[lightIndex], pc.sceneData[pc.sceneDataIndex].view);
            directLight += ShadePunctual(punctual, N, V, viewPosition, li.albedo.xyz, li.metallic, roughness, F0) * LocalShadowVisibility(pc, lightIndex, worldPosition, worldN);
        }
    }
    else
    {
        for (int j = 0; j < pc.lightData->lightCount; j++)
        {
            PunctualLight punctual = DegenerateLight(pc.lightData->lights[j], pc.sceneData[pc.sceneDataIndex].view);
            directLight += ShadePunctual(punctual, N, V, viewPosition, li.albedo.xyz, li.metallic, roughness, F0) * LocalShadowVisibility(pc, uint(j), worldPosition, worldN);
        }
    }

    float2 envBRDF = assetTextures[BRDF_LUT_BINDLESS_INDEX].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], float2(nDotV, roughness), 0).rg;
    float3 F_ibl = F_SCHLICK_ROUGHNESS(nDotV, F0, roughness);
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
    indirectInputs.preExposure = pc.sceneData[pc.sceneDataIndex].preExposure;
    indirectInputs.invView = pc.sceneData[pc.sceneDataIndex].invView;
    indirectInputs.cameraWorldPos = pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz;
    indirectInputs.skyboxIndex = pc.skyboxIndex;
    indirectInputs.iblIntensity = pc.iblIntensity;
    indirectInputs.giResolvedIndex = pc.giGatherMode != 0u ? pc.giResolvedIndex : ~0x0u;
    indirectInputs.skyVisIndex = pc.skyVisIndex;
    indirectInputs.reflectionIndex = roughness <= pc.reflectionRoughnessMax ? pc.reflectionIndex : ~0x0u;
    indirectInputs.bDDGI = pc.bDDGIApply != 0;
    indirectInputs.ddgiCascades = pc.ddgiCascades;
    indirectInputs.reflectionProbeCount = pc.reflectionProbeCount;
    indirectInputs.reflectionProbes = pc.reflectionProbes;
    indirectInputs.worldGridProbeGrid = pc.worldGridProbeGrid;
    IndirectLighting indirect = ResolveIndirectLighting(indirectInputs);

    float3 kD_ibl = (1.0 - F_ibl) * (1.0 - li.metallic);
    float3 ibl = kD_ibl * indirect.diffuse * pc.indirectIntensity * li.albedo.xyz * OrenNayarDirectionalAlbedo(nDotV, roughness) * gtao + indirect.specular;

    float3 finalColor = directLight + ibl + li.emissiveRGB;
    rdgStorageFloat4[pc.primaryOutputImageIndex][pixel] = float4(finalColor * pc.sceneData[pc.sceneDataIndex].preExposure, 1.0);
}
