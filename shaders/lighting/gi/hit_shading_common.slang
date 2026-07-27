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
import punctual_lighting_common;
import binning_common;
import random_common;
import shader_constants;
import constants_interop;
import radiance_cache_interop;
import radiance_cache_hash_common;
import reflection_probe_interop;
import reflection_probe_common;

// Coarse fixed mip: hit-point shading is low frequency (DDGI probes, reflection rays)
public static const float HIT_SHADING_TEXTURE_LOD = 3.0;

/** Geometry-only normal decode, no material/lighting work. Used to key the radiance cache before deciding whether a full shade is even needed. */
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

/**
 * One-sample RIS over the analytic lights binned into this cell, diffuse only.
 * Selection walks the bin with weighted reservoir sampling on the unshadowed punctual luma (arithmetic only, no rays), then the winner alone is
 * area-sampled and shadow-tested, so cost is one ray regardless of bin size. Returns sum_i f_i in expectation, since E[F_j * W/w_j] = sum_j (w_j/W) * F_j * (W/w_j).
 * @param rngSeed salted per shade event; the caller's sun samples use a different stream.
 */
static float3 ShadeLocalLightsRIS(WorldGridPunctualLights worldGridLights, LightData* lightData, uint tlasIndex, float3 worldPosition, float3 N, float3 V, float NoV, float3 albedo, float3 diffuseColor, float metallic, float roughness, uint rngSeed)
{
    WorldGridKey key = WorldGridResolveCell(worldPosition, worldGridLights.camPos);
    uint2 gridEntry = worldGridLights.grid[key.cellIndex];
    if (gridEntry.y == 0u) {
        return float3(0.0);
    }

    float4x4 view = worldGridLights.view;
    float3 viewPos = mul(view, float4(worldPosition, 1.0)).xyz;
    float3 viewN = normalize(mul((float3x3)view, N));
    float3 viewV = normalize(mul((float3x3)view, V));
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    uint rng = rngSeed ^ 0xA511E9B3u;
    float wSum = 0.0;
    float selectedWeight = 0.0;
    uint selectedLight = 0u;
    for (uint j = 0; j < gridEntry.y; j++) {
        uint lightIndex = worldGridLights.indexList[gridEntry.x + j];
        PunctualLight punctual = DegenerateLight(lightData->lights[lightIndex], view);
        float weight = Luma(ShadePunctualDiffuse(punctual, viewN, viewV, viewPos, albedo, metallic, roughness, F0));
        if (weight <= 0.0) {
            continue;
        }
        wSum += weight;
        if (HashStep(rng) * wSum < weight) {
            selectedWeight = weight;
            selectedLight = lightIndex;
        }
    }
    if (selectedWeight <= 0.0) {
        return float3(0.0);
    }

    LightInfo light = lightData->lights[selectedLight];
    float u1 = HashStep(rng);
    float u2 = HashStep(rng);
    float3 lightSamplePos;
    float lightArea;
    if (light.type == LIGHT_TYPE_SPHERE) {
        float radius = light.right.w;
        float z = 1.0 - 2.0 * u1;
        float rr = sqrt(max(1.0 - z * z, 0.0));
        float phi = 2.0 * PI * u2;
        lightSamplePos = light.position.xyz + radius * float3(rr * cos(phi), rr * sin(phi), z);
        lightArea = 4.0 * PI * radius * radius;
    } else {
        lightSamplePos = light.position.xyz + light.right.xyz * (u1 * 2.0 - 1.0) * light.right.w + light.up.xyz * (u2 * 2.0 - 1.0) * light.up.w;
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
    if (cosShade <= 0.0 || cosLight <= 0.0 || atten <= 0.0) {
        return float3(0.0);
    }

    float3 rayOrigin = OffsetHitRayOrigin(worldPosition, N);
    float3 toLightShadow = lightSamplePos - rayOrigin;
    float shadowDist = length(toLightShadow);

    RayDesc shadowRay;
    shadowRay.Origin = rayOrigin;
    shadowRay.Direction = toLightShadow / shadowDist;
    shadowRay.TMin = 0.0;
    shadowRay.TMax = shadowDist - max(0.002, shadowDist * 0.005);

    // INSTANCE_MASK_DDGI excludes light proxies; tracing against the proxy would let a light occlude itself.
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
    shadow.TraceRayInline(rdgAccelerationStructures[tlasIndex], 0, INSTANCE_MASK_DDGI, shadowRay);
    shadow.Proceed();
    if (shadow.CommittedStatus() != COMMITTED_NOTHING) {
        return float3(0.0);
    }

    float3 contribution = diffuseColor * Diffuse_OrenNayar(cosShade, NoV, L, V, roughness) * cosShade * LightGeometryTerm(cosLight, lightDist)
        * light.intensity * UnpackUnorm4x8(light.packedColor).rgb * atten * lightArea;
    return contribution * (wSum / selectedWeight);
}

/** Fallback for DDGI probe rays when the radiance cache has no slot for the hit: sun (shadow-ray tested) + emissive + one-sample RIS over the cell's analytic lights + IBL/DDGI feedback. Kept matched to ShadeRadianceCacheCell so cache-miss fallbacks agree with the cell values they stand in for. */
public float3 ShadeProbeRayHitNEE(DDGIProbeTracePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics, float3 worldPosition, float3 rayDirection, uint rngSeed, float maxBias)
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

    if (instance.lightIndex != 0xFFFFFFFF) {
        emissive = float3(0.0);
    }

    float3 radiance = emissive;
    DirectionalLightData sun = pc.lightData->directionalLight;
    if (sun.directionIntensity.w > 0.0) {
        float3 L = normalize(-sun.directionIntensity.xyz);
        float NoL = dot(N, L);
        if (NoL > 0.0) {
            RayDesc shadowRay;
            shadowRay.Origin = OffsetHitRayOrigin(worldPosition, N);
            shadowRay.Direction = L;
            shadowRay.TMin = 0.0;
            shadowRay.TMax = 10000.0;

            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
            shadow.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_DDGI, shadowRay);
            shadow.Proceed();
            if (shadow.CommittedStatus() == COMMITTED_NOTHING) {
                radiance += diffuseColor * Diffuse_OrenNayar(NoL, NoV, L, V, roughness) * NoL * sun.directionIntensity.w * UnpackUnorm4x8(sun.packedColor).rgb;
            }
        }
    }

    if (pc.worldGridBuffer != nullptr) {
        WorldGridPunctualLights worldGridLights;
        worldGridLights.view = pc.sceneData[0].view;
        worldGridLights.camPos = pc.sceneData[0].cameraWorldPos.xyz;
        worldGridLights.grid = pc.worldGridBuffer;
        worldGridLights.indexList = pc.worldGridIndexList;
        radiance += ShadeLocalLightsRIS(worldGridLights, pc.lightData, pc.tlasIndex, worldPosition, N, V, NoV, albedo.rgb, diffuseColor, material.metalRoughFactors.x, roughness, rngSeed);
    }

    float3 indirectDiffuse = float3(0.0);
    if (pc.skyboxIndex >= 0) {
        indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], N) * pc.iblIntensity;
    }
    float giW = 0.0;
    if ((pc.previousBaseCellZFlags & (1u << 16u)) != 0u) {
        float edgeFade;
        float4 gi = DDGISampleIrradianceCascaded(worldPosition, N, rayDirection, pc.previousCascades, maxBias, edgeFade);
        indirectDiffuse = gi.rgb * pc.bounceIntensity * gi.w + indirectDiffuse * (1.0 - max(gi.w, edgeFade));
        giW = gi.w;
    }
    if (pc.reflectionProbeCount > 0u) {
        float probeFade;
        int probeIndex = PickReflectionProbeBinned(worldPosition, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, pc.sceneData[0].cameraWorldPos.xyz, probeFade);
        if (probeIndex >= 0) {
            ReflectionProbeGPU probe = pc.reflectionProbes[probeIndex];
            float3 probeIrr = SampleReflectionProbeIrradiance(probe, N) * probe.intensity;
            probeIrr *= lerp(1.0, min(1.0, Luma(indirectDiffuse) / max(Luma(probeIrr), 1e-4)), giW);
            indirectDiffuse = lerp(indirectDiffuse, probeIrr, probeFade);
        }
    }
    radiance += diffuseColor * indirectDiffuse;
    return radiance;
}

/** Used by the reflection shade pass. */
public float3 ShadeReflectionRayHit(DDGIProbeTracePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics, float3 worldPosition, float3 rayDirection, uint rngSeed, WorldGridPunctualLights worldGridLights, float maxBias)
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
            shadowRay.Origin = OffsetHitRayOrigin(worldPosition, N);
            shadowRay.Direction = L;
            shadowRay.TMin = 0.0;
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

    if ((pc.previousBaseCellZFlags & (1u << 16u)) != 0u) {
        float4 gi = DDGISampleIrradianceCascaded(worldPosition, N, rayDirection, pc.previousCascades, maxBias);
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
public float3 ShadeRadianceCacheCell(RadianceCacheShadePushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics, uint rngSeed, out float3 outEmissive)
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
    // A proxy's analytic light is sampled directly by NEE
    if (instance.lightIndex != 0xFFFFFFFF) {
        emissive = float3(0.0);
    }

    outEmissive = emissive;
    float3 radiance = float3(0.0);
    DirectionalLightData sun = pc.lightData->directionalLight;
    if (sun.directionIntensity.w > 0.0) {
        float3 L = normalize(-sun.directionIntensity.xyz);
        float NoL = dot(N, L);
        if (NoL > 0.0) {
            // Zero angular radius makes every cone sample identical; one ray suffices.
            uint sampleCount = sun.angularRadius > 0.0 ? RADIANCE_CACHE_SUN_SAMPLES : 1u;
            float sunVisibility = 0.0;
            for (uint s = 0u; s < sampleCount; s++) {
                uint rng = rngSeed ^ (s * 0x9E3779B9u);
                float u1 = HashStep(rng);
                float u2 = HashStep(rng);

                RayDesc shadowRay;
                shadowRay.Origin = OffsetHitRayOrigin(worldPosition, N);
                shadowRay.Direction = SampleSunConeDirection(L, sun.angularRadius, u1, u2);
                shadowRay.TMin = 0.0;
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

    if (pc.worldGridBuffer != nullptr) {
        WorldGridPunctualLights worldGridLights;
        worldGridLights.view = pc.sceneData[pc.sceneDataIndex].view;
        worldGridLights.camPos = pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz;
        worldGridLights.grid = pc.worldGridBuffer;
        worldGridLights.indexList = pc.worldGridIndexList;
        radiance += ShadeLocalLightsRIS(worldGridLights, pc.lightData, pc.tlasIndex, worldPosition, N, V, NoV, albedo.rgb, diffuseColor, material.metalRoughFactors.x, roughness, rngSeed);
    }

    float3 indirectDiffuse = float3(0.0);
    if (pc.skyboxIndex >= 0) {
        indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], N) * pc.iblIntensity;
    }
    float giW = 0.0;
    if (pc.bFeedbackValid != 0) {
        float cellSize = RadianceCacheCellSizeAtLevel(RadianceCacheLevelFromDistance(distance(worldPosition, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz)));
        float edgeFade;
        float4 gi = DDGISampleIrradianceCascaded(worldPosition, N, -N, pc.previousCascades, cellSize, edgeFade);
        indirectDiffuse = gi.rgb * pc.bounceIntensity * gi.w + indirectDiffuse * (1.0 - max(gi.w, edgeFade));
        giW = gi.w;
    }
    if (pc.reflectionProbeCount > 0u) {
        float probeFade;
        int probeIndex = PickReflectionProbeBinned(worldPosition, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, pc.sceneData[pc.sceneDataIndex].cameraWorldPos.xyz, probeFade);
        if (probeIndex >= 0) {
            ReflectionProbeGPU probe = pc.reflectionProbes[probeIndex];
            float3 probeIrr = SampleReflectionProbeIrradiance(probe, N) * probe.intensity;
            probeIrr *= lerp(1.0, min(1.0, Luma(indirectDiffuse) / max(Luma(probeIrr), 1e-4)), giW);
            indirectDiffuse = lerp(indirectDiffuse, probeIrr, probeFade);
        }
    }
    radiance += diffuseColor * indirectDiffuse;
    return radiance;
}
