module hit_shading_common;

import model_interop;
import push_constant_interop;
import lights_interop;
import ddgi_common;
import instance_mask_interop;
import bindless_declarations;
import shader_functions;
import packing_functions;
import pbr_functions;
import punctual_lighting;
import binning_common;
import random_common;
import shader_constants;
import constants_interop;
import world_cache_interop;

// Coarse fixed mip: hit-point shading is low frequency (DDGI probes, reflection rays)
public static const float HIT_SHADING_TEXTURE_LOD = 3.0;

/** Geometry-only normal decode, no material/lighting work. Used to key the world cache before deciding whether a full shade is even needed. */
public float3 DecodeHitNormal(DDGIProbeTracePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics)
{
    Primitive primitive = pc.primitiveBuffer[instance.primitiveIndex];
    uint firstIndex = primitive.indexOffset + triangleIndex * 3;
    VertexAttribute a0 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 0]];
    VertexAttribute a1 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 1]];
    VertexAttribute a2 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 2]];
    float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    float3 normalOS = normalize(bary.x * DecodeNormal(a0) + bary.y * DecodeNormal(a1) + bary.z * DecodeNormal(a2));
    Model model = pc.modelBuffer[instance.modelIndex];
    return normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));
}

/** Fallback for DDGI probe rays when the world cache has no slot for the hit: sun (shadow-ray tested) + emissive + the in-cell clustered local lights (deterministic, unshadowed, matching the world-cache shade the value stands in for) + IBL/DDGI feedback. */
public float3 ShadeProbeRayHitNEE(DDGIProbeTracePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics, float3 worldPosition, float3 rayDirection, uint rngSeed, WorldGridPunctualLights worldGridLights)
{
    Primitive primitive = pc.primitiveBuffer[instance.primitiveIndex];

    uint firstIndex = primitive.indexOffset + triangleIndex * 3;
    VertexAttribute a0 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 0]];
    VertexAttribute a1 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 1]];
    VertexAttribute a2 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 2]];

    float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    float3 normalOS = normalize(bary.x * DecodeNormal(a0) + bary.y * DecodeNormal(a1) + bary.z * DecodeNormal(a2));
    float2 uv = bary.x * DecodeUV(a0) + bary.y * DecodeUV(a1) + bary.z * DecodeUV(a2);
    float4 vertexColor = bary.x * DecodeColor(a0) + bary.y * DecodeColor(a1) + bary.z * DecodeColor(a2);

    Model model = pc.modelBuffer[instance.modelIndex];
    float3 N = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));

    MaterialProperties material = pc.materialBuffer[instance.materialIndex];
    float4 albedo = material.colorFactor * vertexColor;
    if (material.textureImageIndices.x >= 0) {
        float2 colorUV = uv * material.colorUvTransform.xy + material.colorUvTransform.zw;
        albedo *= assetTextures[material.textureImageIndices.x].SampleLevel(assetSamplers[material.textureSamplerIndices.x], colorUV, HIT_SHADING_TEXTURE_LOD);
    }
    float3 emissive = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureImageIndices.w >= 0) {
        float2 emissiveUV = uv * material.emissiveUvTransform.xy + material.emissiveUvTransform.zw;
        emissive *= assetTextures[material.textureImageIndices.w].SampleLevel(assetSamplers[material.textureSamplerIndices.w], emissiveUV, HIT_SHADING_TEXTURE_LOD).rgb;
    }
    float3 diffuseColor = albedo.rgb * (1.0 - material.metalRoughFactors.x);
    float roughness = max(material.metalRoughFactors.y, 0.045);
    float3 V = -rayDirection;
    float NoV = max(dot(N, V), 0.0);

    float3 radiance = emissive;
    DirectionalLightData sun = pc.lightData->directionalLight;
    if (sun.directionIntensity.w > 0.0) {
        float3 L = normalize(-sun.directionIntensity.xyz);
        float NoL = dot(N, L);
        if (NoL > 0.0) {
            RayDesc shadowRay;
            shadowRay.Origin = worldPosition + N * 0.05;
            shadowRay.Direction = L;
            shadowRay.TMin = 0.001;
            shadowRay.TMax = 10000.0;

            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
            shadow.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_DDGI, shadowRay);
            shadow.Proceed();
            if (shadow.CommittedStatus() == COMMITTED_NOTHING) {
                radiance += diffuseColor * Diffuse_OrenNayar(NoL, NoV, L, V, roughness) * NoL * sun.directionIntensity.w * UnpackUnorm4x8(sun.packedColor).rgb;
            }
        }
    }

    // Local lights via the world-space clustered grid; matches ShadeWorldCacheCell (diffuse-only) so cache-miss fallbacks agree with the cache value they stand in for.
    if (pc.bLocalNEE != 0 && worldGridLights.grid != nullptr) {
        WorldGridKey key = WorldGridResolveCell(worldPosition, worldGridLights.camPos);
        uint2 gridEntry = worldGridLights.grid[key.cellIndex];
        float3 viewPos = mul(worldGridLights.view, float4(worldPosition, 1.0)).xyz;
        float3 viewN = normalize(mul((float3x3)worldGridLights.view, N));
        float3 viewV = normalize(mul((float3x3)worldGridLights.view, V));
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, material.metalRoughFactors.x);
        for (uint j = 0; j < gridEntry.y; j++) {
            uint lightIndex = worldGridLights.indexList[gridEntry.x + j];
            PunctualLight punctual = DegenerateLight(pc.lightData->lights[lightIndex], worldGridLights.view);
            radiance += ShadePunctualDiffuse(punctual, viewN, viewV, viewPos, albedo.rgb, material.metalRoughFactors.x, roughness, F0);
        }
    }

    float3 indirectDiffuse = float3(0.0);
    if (pc.skyboxIndex >= 0) {
        indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], N) * pc.iblIntensity;
    }
    if (pc.bFeedbackValid != 0) {
        float4 gi = DDGISampleIrradianceCascaded(worldPosition, N, rayDirection, pc.previousCascades);
        indirectDiffuse = gi.rgb * pc.bounceIntensity * gi.w + indirectDiffuse * (1.0 - max(gi.w, DDGICascadeSetEdgeFade(worldPosition, pc.previousCascades)));
    }
    radiance += diffuseColor * indirectDiffuse;
    return radiance;
}

/** Used by the reflection shade pass. */
public float3 ShadeProbeRayHitAnalytical(DDGIProbeTracePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics, float3 worldPosition, float3 rayDirection, uint rngSeed, WorldGridPunctualLights worldGridLights)
{
    Primitive primitive = pc.primitiveBuffer[instance.primitiveIndex];

    uint firstIndex = primitive.indexOffset + triangleIndex * 3;
    VertexAttribute a0 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 0]];
    VertexAttribute a1 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 1]];
    VertexAttribute a2 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 2]];

    float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    float3 normalOS = normalize(bary.x * DecodeNormal(a0) + bary.y * DecodeNormal(a1) + bary.z * DecodeNormal(a2));
    float2 uv = bary.x * DecodeUV(a0) + bary.y * DecodeUV(a1) + bary.z * DecodeUV(a2);
    float4 vertexColor = bary.x * DecodeColor(a0) + bary.y * DecodeColor(a1) + bary.z * DecodeColor(a2);

    Model model = pc.modelBuffer[instance.modelIndex];
    float3 N = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));

    MaterialProperties material = pc.materialBuffer[instance.materialIndex];
    float4 albedo = material.colorFactor * vertexColor;
    if (material.textureImageIndices.x >= 0) {
        float2 colorUV = uv * material.colorUvTransform.xy + material.colorUvTransform.zw;
        albedo *= assetTextures[material.textureImageIndices.x].SampleLevel(assetSamplers[material.textureSamplerIndices.x], colorUV, HIT_SHADING_TEXTURE_LOD);
    }
    float3 emissive = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureImageIndices.w >= 0) {
        float2 emissiveUV = uv * material.emissiveUvTransform.xy + material.emissiveUvTransform.zw;
        emissive *= assetTextures[material.textureImageIndices.w].SampleLevel(assetSamplers[material.textureSamplerIndices.w], emissiveUV, HIT_SHADING_TEXTURE_LOD).rgb;
    }
    float3 diffuseColor = albedo.rgb * (1.0 - material.metalRoughFactors.x);
    float roughness = max(material.metalRoughFactors.y, 0.045);
    float3 V = -rayDirection;
    float NoV = max(dot(N, V), 0.0);

    float3 radiance = emissive;
    DirectionalLightData sun = pc.lightData->directionalLight;
    if (sun.directionIntensity.w > 0.0) {
        float3 L = normalize(-sun.directionIntensity.xyz);
        float NoL = dot(N, L);
        if (NoL > 0.0) {
            RayDesc shadowRay;
            shadowRay.Origin = worldPosition + N * 0.05;
            shadowRay.Direction = L;
            shadowRay.TMin = 0.001;
            shadowRay.TMax = 10000.0;

            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
            shadow.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_DDGI, shadowRay);
            shadow.Proceed();
            if (shadow.CommittedStatus() == COMMITTED_NOTHING) {
                radiance += diffuseColor * Diffuse_OrenNayar(NoL, NoV, L, V, roughness) * NoL * sun.directionIntensity.w * UnpackUnorm4x8(sun.packedColor).rgb;
            }
        }
    }

    if (worldGridLights.grid != nullptr) {
        WorldGridKey key = WorldGridResolveCell(worldPosition, worldGridLights.camPos);
        uint2 gridEntry = worldGridLights.grid[key.cellIndex];
        float3 viewPos = mul(worldGridLights.view, float4(worldPosition, 1.0)).xyz;
        float3 viewN = normalize(mul((float3x3)worldGridLights.view, N));
        float3 viewV = normalize(mul((float3x3)worldGridLights.view, V));
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, material.metalRoughFactors.x);
        for (uint j = 0; j < gridEntry.y; j++) {
            uint lightIndex = worldGridLights.indexList[gridEntry.x + j];
            PunctualLight punctual = DegenerateLight(pc.lightData->lights[lightIndex], worldGridLights.view);
            radiance += ShadePunctual(punctual, viewN, viewV, viewPos, albedo.rgb, material.metalRoughFactors.x, roughness, F0);
        }
    }

    if (pc.bFeedbackValid != 0) {
        float4 gi = DDGISampleIrradianceCascaded(worldPosition, N, rayDirection, pc.previousCascades);
        radiance += diffuseColor * gi.rgb;
    }
    return radiance;
}

/** Uniformly sample a ray direction inside the sun's angular cone (half-angle = angularRadius) around the mean direction. */
static float3 SampleSunConeDirection(float3 toLight, float angularRadius, float u1, float u2)
{
    float cosThetaMax = cos(angularRadius);
    float cosTheta = lerp(1.0, cosThetaMax, u1);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = 6.28318530718 * u2;

    float3 w = toLight;
    float3 up = abs(w.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, w));
    float3 b = cross(w, t);
    return normalize(sinTheta * cos(phi) * t + sinTheta * sin(phi) * b + cosTheta * w);
}

// V = N (NoV = 1): no single ray direction to use, and this value gets read back from every direction later.
// Returns the non-emissive radiosity; own emission goes out separately so the cell EMA/streak never sees bary-jitter on/off emissive texels.
public float3 ShadeWorldCacheCell(WorldCacheShadePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics, uint rngSeed, WorldGridPunctualLights worldGridLights, out float3 outEmissive)
{
    Primitive primitive = pc.primitiveBuffer[instance.primitiveIndex];

    uint firstIndex = primitive.indexOffset + triangleIndex * 3;
    VertexAttribute a0 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 0]];
    VertexAttribute a1 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 1]];
    VertexAttribute a2 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 2]];

    float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    float3 normalOS = normalize(bary.x * DecodeNormal(a0) + bary.y * DecodeNormal(a1) + bary.z * DecodeNormal(a2));
    float2 uv = bary.x * DecodeUV(a0) + bary.y * DecodeUV(a1) + bary.z * DecodeUV(a2);
    float4 vertexColor = bary.x * DecodeColor(a0) + bary.y * DecodeColor(a1) + bary.z * DecodeColor(a2);

    Model model = pc.modelBuffer[instance.modelIndex];
    float3 N = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));

    VertexPosition p0 = pc.vertexPosBuffer[pc.indexBuffer[firstIndex + 0]];
    VertexPosition p1 = pc.vertexPosBuffer[pc.indexBuffer[firstIndex + 1]];
    VertexPosition p2 = pc.vertexPosBuffer[pc.indexBuffer[firstIndex + 2]];
    float3 worldPosition = InterpolatePosition(bary, p0, p1, p2, model.modelMatrix, primitive.boundingBoxMin, primitive.boundingBoxMax - primitive.boundingBoxMin);

    MaterialProperties material = pc.materialBuffer[instance.materialIndex];
    float4 albedo = material.colorFactor * vertexColor;
    if (material.textureImageIndices.x >= 0) {
        float2 colorUV = uv * material.colorUvTransform.xy + material.colorUvTransform.zw;
        albedo *= assetTextures[material.textureImageIndices.x].SampleLevel(assetSamplers[material.textureSamplerIndices.x], colorUV, HIT_SHADING_TEXTURE_LOD);
    }
    float3 emissive = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureImageIndices.w >= 0) {
        float2 emissiveUV = uv * material.emissiveUvTransform.xy + material.emissiveUvTransform.zw;
        emissive *= assetTextures[material.textureImageIndices.w].SampleLevel(assetSamplers[material.textureSamplerIndices.w], emissiveUV, HIT_SHADING_TEXTURE_LOD).rgb;
    }
    float3 diffuseColor = albedo.rgb * (1.0 - material.metalRoughFactors.x);
    float roughness = max(material.metalRoughFactors.y, 0.045);
    float3 V = N;
    float NoV = 1.0;

    outEmissive = emissive;
    float3 radiance = float3(0.0);
    DirectionalLightData sun = pc.lightData->directionalLight;
    if (sun.directionIntensity.w > 0.0) {
        float3 L = normalize(-sun.directionIntensity.xyz);
        float NoL = dot(N, L);
        if (NoL > 0.0) {
            // Zero angular radius makes every cone sample identical; one ray suffices.
            uint sampleCount = sun.angularRadius > 0.0 ? WORLD_CACHE_SUN_SAMPLES : 1u;
            float sunVisibility = 0.0;
            for (uint s = 0u; s < sampleCount; s++) {
                uint rng = rngSeed ^ (s * 0x9E3779B9u);
                float u1 = HashStep(rng);
                float u2 = HashStep(rng);

                RayDesc shadowRay;
                shadowRay.Origin = worldPosition + N * 0.05;
                shadowRay.Direction = SampleSunConeDirection(L, sun.angularRadius, u1, u2);
                shadowRay.TMin = 0.001;
                shadowRay.TMax = 10000.0;

                RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
                shadow.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_DDGI, shadowRay);
                shadow.Proceed();
                if (shadow.CommittedStatus() == COMMITTED_NOTHING) {
                    sunVisibility += 1.0;
                }
            }
            sunVisibility /= float(sampleCount);
            if (sunVisibility > 0.0) {
                radiance += sunVisibility * diffuseColor * Diffuse_OrenNayar(NoL, NoV, L, V, roughness) * NoL * sun.directionIntensity.w * UnpackUnorm4x8(sun.packedColor).rgb;
            }
        }
    }

    if (worldGridLights.grid != nullptr) {
        WorldGridKey key = WorldGridResolveCell(worldPosition, worldGridLights.camPos);
        uint2 gridEntry = worldGridLights.grid[key.cellIndex];
        float3 viewPos = mul(worldGridLights.view, float4(worldPosition, 1.0)).xyz;
        float3 viewN = normalize(mul((float3x3)worldGridLights.view, N));
        float3 viewV = normalize(mul((float3x3)worldGridLights.view, V));
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, material.metalRoughFactors.x);
        for (uint j = 0; j < gridEntry.y; j++) {
            uint lightIndex = worldGridLights.indexList[gridEntry.x + j];
            PunctualLight punctual = DegenerateLight(pc.lightData->lights[lightIndex], worldGridLights.view);
            radiance += ShadePunctualDiffuse(punctual, viewN, viewV, viewPos, albedo.rgb, material.metalRoughFactors.x, roughness, F0);
        }
    }

    float3 indirectDiffuse = float3(0.0);
    if (pc.skyboxIndex >= 0) {
        indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], N) * pc.iblIntensity;
    }
    if (pc.bFeedbackValid != 0) {
        float4 gi = DDGISampleIrradianceCascaded(worldPosition, N, -N, pc.previousCascades);
        indirectDiffuse = gi.rgb * pc.bounceIntensity * gi.w + indirectDiffuse * (1.0 - max(gi.w, DDGICascadeSetEdgeFade(worldPosition, pc.previousCascades)));
    }
    radiance += diffuseColor * indirectDiffuse;
    return radiance;
}
