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

/** NEE variant: sun + one uniformly picked local light, both shadow-ray tested, + emissive + DDGI feedback. Fallback for DDGI probe rays when the world cache has no slot for the hit. */
public float3 ShadeProbeRayHitNEE(DDGIProbeTracePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics, float3 worldPosition, float3 rayDirection, uint rngSeed)
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

    // NEE (analytic lights only). Outer cascades skip it to save the shadow ray, keeping only sun + emissive + feedback)
    int analyticCount = pc.lightData->analyticLightCount;
    if (pc.bLocalNEE != 0 && analyticCount > 0) {
        uint rng = rngSeed;
        float rng0 = HashStep(rng);
        float2 xi;
        xi.x = HashStep(rng);
        xi.y = HashStep(rng);
        int lightIdx = clamp(int(rng0 * float(analyticCount)), 0, analyticCount - 1);
        LightInfo light = pc.lightData->lights[lightIdx];

        float3 lightSamplePos;
        float lightArea;
        if (light.type == LIGHT_TYPE_SPHERE) {
            float radius = light.right.w;
            float z = 1.0 - 2.0 * xi.x;
            float rr = sqrt(max(1.0 - z * z, 0.0));
            float phi = 2.0 * PI * xi.y;
            lightSamplePos = light.position.xyz + radius * float3(rr * cos(phi), rr * sin(phi), z);
            lightArea = 4.0 * PI * radius * radius;
        } else {
            lightSamplePos = light.position.xyz + light.right.xyz * (xi.x * 2.0 - 1.0) * light.right.w + light.up.xyz * (xi.y * 2.0 - 1.0) * light.up.w;
            lightArea = 4.0 * light.right.w * light.up.w;
        }

        float3 toLight = lightSamplePos - worldPosition;
        float lightDist = max(length(toLight), 1e-5);
        float3 L = toLight / lightDist;
        float cosShade = dot(N, L);
        float cosLight = light.type == LIGHT_TYPE_SPHERE
            ? max(dot(normalize(lightSamplePos - light.position.xyz), -L), 0.0)
            : max(dot(-light.normal.xyz, L), 0.0);
        float atten = LightRangeCull(lightDist, light.range);
        if (cosShade > 0.0 && cosLight > 0.0 && atten > 0.0) {
            float3 rayOrigin = worldPosition + N * 0.05;
            float3 toLightShadow = lightSamplePos - rayOrigin;
            float shadowDist = length(toLightShadow);

            RayDesc shadowRay;
            shadowRay.Origin = rayOrigin;
            shadowRay.Direction = toLightShadow / shadowDist;
            shadowRay.TMin = 0.001;
            shadowRay.TMax = shadowDist - 0.05;

            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
            shadow.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_DDGI, shadowRay);
            shadow.Proceed();
            if (shadow.CommittedStatus() == COMMITTED_NOTHING) {
                radiance += diffuseColor * Diffuse_OrenNayar(cosShade, NoV, L, V, roughness) * cosShade * LightGeometryTerm(cosLight, lightDist) * light.intensity * UnpackUnorm4x8(light.packedColor).rgb * atten * float(analyticCount) * lightArea;
            }
        }
    }

    if (pc.bFeedbackValid != 0) {
        float4 gi = DDGISampleIrradianceCascaded(worldPosition, N, rayDirection, pc.previousCascades);
        radiance += diffuseColor * gi.rgb * pc.bounceIntensity;
    }
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

// V = N (NoV = 1): no single ray direction to use, and this value gets read back from every direction later.
public float3 ShadeWorldCacheCell(WorldCacheShadePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics, WorldGridPunctualLights worldGridLights)
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

    float3 indirectDiffuse = float3(0.0);
    if (pc.skyboxIndex >= 0) {
        indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], N) * pc.iblIntensity;
    }
    if (pc.bFeedbackValid != 0) {
        float4 gi = DDGISampleIrradianceCascaded(worldPosition, N, -N, pc.previousCascades);
        indirectDiffuse = lerp(indirectDiffuse, gi.rgb * pc.bounceIntensity, gi.w);
    }
    radiance += diffuseColor * indirectDiffuse;
    return radiance;
}
