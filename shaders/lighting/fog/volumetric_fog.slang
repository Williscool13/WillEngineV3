import common_interop;
import push_constant_interop;
import constants_interop;
import volumetric_fog_interop;

import shader_functions;
import pbr_functions;

import bindless_declarations;

float FogSliceDepth(float slice, float maxDistance)
{
    return VOLUMETRIC_FOG_NEAR * pow(maxDistance / VOLUMETRIC_FOG_NEAR, slice / float(VOLUMETRIC_FOG_SLICES));
}

float FogDepthToSlice(float viewDepth, float maxDistance)
{
    return log(max(viewDepth, 1e-4) / VOLUMETRIC_FOG_NEAR) / log(maxDistance / VOLUMETRIC_FOG_NEAR) * float(VOLUMETRIC_FOG_SLICES);
}

float2 FogFroxelUV(uint2 froxel, uint2 renderExtent)
{
    return (float2(froxel) + 0.5) * float(VOLUMETRIC_FOG_TILE_SIZE) / float2(renderExtent);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeVolumetricFogScatter(uniform VolumetricFogScatterPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= pc.gridSize) || dtid.z >= VOLUMETRIC_FOG_SLICES) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float viewDepth = FogSliceDepth(float(dtid.z) + 0.5, pc.maxDistance);
    float3 viewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, FogFroxelUV(dtid.xy, pc.renderExtent), viewDepth);
    float3 worldPos = mul(scene.invView, float4(viewPos, 1.0)).xyz;

    float extinction = pc.albedoDensity.a * exp(-pc.heightFalloff * max(worldPos.y - pc.baseHeight, 0.0));

    // Isotropic phase: the sphere's mean radiance, from the cosine-convolved sky above and below.
    float3 ambient = float3(0.0);
    if (pc.skyboxIndex >= 0) {
        SamplerState linearSampler = assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX];
        float3 up = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], linearSampler, float3(0.0, 1.0, 0.0));
        float3 down = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], linearSampler, float3(0.0, -1.0, 0.0));
        ambient = 0.5 * (up + down) * pc.iblIntensity * pc.ambientScale;
    }

    float3 scatter = extinction * pc.albedoDensity.rgb * ambient * scene.preExposure;
    rdgStorage3DFloat4[pc.scatterOutIndex][dtid] = float4(scatter, extinction);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeVolumetricFogIntegrate(uniform VolumetricFogIntegratePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= pc.gridSize)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    // Slices are planar view depth; the ray through this column travels further by this factor.
    float2 rayXY = scene.ndcToViewMul * FogFroxelUV(dtid.xy, pc.renderExtent) + scene.ndcToViewAdd;
    float rayScale = length(float3(rayXY, 1.0));

    float3 inScatter = float3(0.0);
    float transmittance = 1.0;
    float sliceStart = FogSliceDepth(0.0, pc.maxDistance);
    for (uint slice = 0; slice < VOLUMETRIC_FOG_SLICES; ++slice) {
        float sliceEnd = FogSliceDepth(float(slice + 1), pc.maxDistance);
        float pathLength = (sliceEnd - sliceStart) * rayScale;
        sliceStart = sliceEnd;

        float4 froxel = rdgTextures3DFloat4[pc.scatterIndex].Load(int4(int2(dtid.xy), int(slice), 0));
        float extinction = max(froxel.a, 1e-7);
        float sliceTransmittance = exp(-extinction * pathLength);
        // Integrated over the slice against its own extinction, so thick slices do not overshoot.
        inScatter += transmittance * froxel.rgb * (1.0 - sliceTransmittance) / extinction;
        transmittance *= sliceTransmittance;
        rdgStorage3DFloat4[pc.integratedOutIndex][uint3(dtid.xy, slice)] = float4(inScatter, transmittance);
    }
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeVolumetricFogApply(uniform VolumetricFogApplyPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    float viewDepth = depth <= 0.0 ? pc.maxDistance : ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);

    float4 color = rdgStorageFloat4[pc.colorIndex][pixel];
    // Texel s holds the fog from the near plane to the far edge of slice s.
    float slice = FogDepthToSlice(viewDepth, pc.maxDistance);
    if (slice > 0.0) {
        float3 uvw;
        uvw.xy = (float2(pixel) + 0.5) / (float2(pc.gridSize) * float(VOLUMETRIC_FOG_TILE_SIZE));
        uvw.z = (clamp(slice, 1.0, float(VOLUMETRIC_FOG_SLICES)) - 0.5) / float(VOLUMETRIC_FOG_SLICES);
        float4 fog = rdgTextures3DFloat4[pc.integratedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvw, 0.0);
        fog = lerp(float4(0.0, 0.0, 0.0, 1.0), fog, saturate(slice));
        color.rgb = color.rgb * fog.a + fog.rgb;
        rdgStorageFloat4[pc.colorIndex][pixel] = color;
    }

    if (pc.foggedCopyIndex != ~0u) {
        rdgStorageFloat4[pc.foggedCopyIndex][pixel] = color;
    }
}
