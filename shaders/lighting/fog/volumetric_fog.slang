import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;
import instance_mask_interop;
import ddgi_interop;
import ddgi_common;
import volumetric_fog_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import binning_common;
import random_common;
import shader_constants;

import bindless_declarations;

static const uint FOG_DEBUG_FOG_ONLY = 1u;
static const uint FOG_DEBUG_AMBIENT = 2u;
static const uint FOG_DEBUG_DDGI = 3u;
static const uint FOG_DEBUG_DDGI_COVERAGE = 4u;
static const uint FOG_DEBUG_SUN = 5u;
static const uint FOG_DEBUG_LOCAL_LIGHTS = 6u;

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

/** Henyey-Greenstein; cosTheta between the direction to the light and the view ray. */
float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

/** Isotropic phase: the sphere's mean radiance, from the cosine-convolved sky above and below. */
float3 FogAmbientRadiance(int skyboxIndex, float iblIntensity, float ambientScale)
{
    if (skyboxIndex < 0) {
        return float3(0.0);
    }
    SamplerState linearSampler = assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX];
    float3 up = DiffuseIrradiance(assetCubemaps[skyboxIndex], linearSampler, float3(0.0, 1.0, 0.0));
    float3 down = DiffuseIrradiance(assetCubemaps[skyboxIndex], linearSampler, float3(0.0, -1.0, 0.0));
    return 0.5 * (up + down) * iblIntensity * ambientScale;
}

/** Unshadowed sun radiance scattered along viewDir; zero when the sun is off or ignored by fog. */
float3 FogSunRadiance(DirectionalLightData sun, float3 viewDir, float g)
{
    if (sun.directionIntensity.w <= 0.0 || sun.volumetricScale <= 0.0) {
        return float3(0.0);
    }
    float3 L = normalize(-sun.directionIntensity.xyz);
    return UnpackUnorm4x8(sun.packedColor).rgb * sun.directionIntensity.w * PhaseHG(dot(L, viewDir), g) * sun.volumetricScale;
}

/** Extinction integrated along the ray from tStart to tEnd; density is flat below baseHeight and falls off exponentially above. */
float FogOpticalDepth(float originY, float dirY, float tStart, float tEnd, float density, float heightFalloff, float baseHeight)
{
    if (abs(heightFalloff * dirY) < 1e-3) {
        float midHeight = originY + dirY * 0.5 * (tStart + tEnd) - baseHeight;
        return density * exp(-heightFalloff * max(midHeight, 0.0)) * (tEnd - tStart);
    }
    float tCross = (baseHeight - originY) / dirY;
    float2 below = dirY > 0.0 ? float2(tStart, min(tEnd, tCross)) : float2(max(tStart, tCross), tEnd);
    float2 above = dirY > 0.0 ? float2(max(tStart, tCross), tEnd) : float2(tStart, min(tEnd, tCross));
    float depth = density * max(below.y - below.x, 0.0);
    if (above.y > above.x) {
        float heightA = max(originY + dirY * above.x - baseHeight, 0.0);
        float heightB = max(originY + dirY * above.y - baseHeight, 0.0);
        depth += density * (exp(-heightFalloff * heightA) - exp(-heightFalloff * heightB)) / (heightFalloff * dirY);
    }
    return depth;
}

bool FogVisible(uint tlasIndex, uint instanceMask, float3 origin, float3 direction, float tMax)
{
    if (tlasIndex == ~0u) {
        return true;
    }
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = tMax;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(rdgAccelerationStructures[tlasIndex], 0, instanceMask, ray);
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_NOTHING;
}

/** Unshadowed in-scatter estimate from the light's centre, for picking which light gets this froxel's shadow ray. */
float FogLightTarget(LightInfo light, float3 p, float3 viewDir, float g)
{
    if (light.volumetricScale <= 0.0 || light.intensity <= 0.0) {
        return 0.0;
    }
    float3 toLight = light.position.xyz - p;
    float dist = max(length(toLight), 1e-3);
    float3 L = toLight / dist;
    float irradiance;
    if (light.type == LIGHT_TYPE_SPHERE) {
        float radius = light.right.w;
        irradiance = PI * radius * radius / max(dist * dist, radius * radius);
    } else {
        bool bDisk = light.type == LIGHT_TYPE_DISK;
        float area = bDisk ? PI * light.right.w * light.right.w : 4.0 * light.right.w * light.up.w;
        float cosLight = dot(-light.normal.xyz, L);
        bool bConed = light.position.w > 0.0;
        if (bConed) {
            // Tight beams light points far off the centre's axis; gate on the beam and drop the centre's cone falloff.
            if (!LightConeReaches(light.position.xyz, light.normal.xyz, light.right.xyz, light.up.xyz, light.right.w, light.up.w, bDisk, light.position.w, p, 0.0)) {
                return 0.0;
            }
            cosLight = max(cosLight, 0.05);
        } else if (cosLight <= 0.0) {
            return 0.0;
        }
        irradiance = area * cosLight / max(dist * dist, area);
    }
    irradiance *= light.intensity * light.coneScale * LightRangeCull(dist, light.range) * LightFalloffScale(dist, light.falloffBias);
    return Luma(UnpackUnorm4x8(light.packedColor).rgb) * irradiance * PhaseHG(dot(L, viewDir), g) * light.volumetricScale;
}

/** One emitter sample of the light with a shadow ray: radiance scattered toward the camera per unit scattering coefficient. */
float3 FogShadeLight(LightInfo light, uint tlasIndex, float3 p, float3 viewDir, float g, float2 u)
{
    float u1 = u.x;
    float u2 = u.y;
    float3 samplePos;
    float lightArea;
    if (light.type == LIGHT_TYPE_SPHERE) {
        float radius = light.right.w;
        float z = 1.0 - 2.0 * u1;
        float rr = sqrt(max(1.0 - z * z, 0.0));
        float phi = 2.0 * PI * u2;
        samplePos = light.position.xyz + radius * float3(rr * cos(phi), rr * sin(phi), z);
        lightArea = 4.0 * PI * radius * radius;
    } else {
        lightArea = SampleEmitterArea(light.position.xyz, light.normal.xyz, light.right.xyz, light.up.xyz, light.right.w, light.up.w, light.type == LIGHT_TYPE_DISK, light.position.w, p, u1, u2, samplePos);
    }
    if (lightArea <= 0.0) {
        return float3(0.0);
    }

    float3 toLight = samplePos - p;
    float dist = max(length(toLight), 1e-5);
    float3 L = toLight / dist;
    float cosLight = light.type == LIGHT_TYPE_SPHERE
        ? max(dot(normalize(samplePos - light.position.xyz), -L), 0.0)
        : max(dot(-light.normal.xyz, L), 0.0);
    float atten = LightRangeCull(dist, light.range) * LightConeFalloff(cosLight, light.normal.w, light.position.w) * LightFalloffScale(dist, light.falloffBias);
    if (cosLight <= 0.0 || atten <= 0.0) {
        return float3(0.0);
    }
    if (!FogVisible(tlasIndex, INSTANCE_MASK_OCCLUDER, p, L, dist - max(0.002, dist * 0.005))) {
        return float3(0.0);
    }

    float softDist2 = 0.25 * (light.type == LIGHT_TYPE_SPHERE ? light.right.w * light.right.w : lightArea / PI);
    return UnpackUnorm4x8(light.packedColor).rgb * light.intensity * light.coneScale * atten * cosLight / (dist * dist + softDist2) * lightArea
        * PhaseHG(dot(L, viewDir), g) * light.volumetricScale;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeVolumetricFogScatter(uniform VolumetricFogScatterPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= pc.gridSize) || dtid.z >= VOLUMETRIC_FOG_SLICES) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint rng = SeedRng(dtid.xy, pc.frameIndex, dtid.z * 0x9E3779B9u);

    // Shade a jittered point inside the froxel; the history converges to the froxel's average.
    float3 jitter = float3(SpatioTemporalBlueNoise2D(dtid.xy, pc.frameIndex, dtid.z), SpatioTemporalBlueNoise2D(dtid.xy, pc.frameIndex, dtid.z + 64u).x);
    float2 sampleUV = (float2(dtid.xy) + jitter.xy) * float(VOLUMETRIC_FOG_TILE_SIZE) / float2(pc.renderExtent);
    float3 viewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, sampleUV, FogSliceDepth(float(dtid.z) + jitter.z, pc.maxDistance));
    float3 worldPos = mul(scene.invView, float4(viewPos, 1.0)).xyz;
    float3 viewDir = normalize(worldPos - scene.cameraWorldPos.xyz);

    float extinction = pc.albedoDensity.a * exp(-pc.heightFalloff * max(worldPos.y - pc.baseHeight, 0.0));
    if (!(extinction > 0.0)) {
        rdgStorage3DFloat4[pc.scatterOutIndex][dtid] = float4(0.0);
        return;
    }

    // No pixel in the tile sees past its farthest surface; the margin covers Apply's depth jitter and trilinear lookup.
    bool bCulled = false;
    float tileDepth = rdgTexturesFloat[pc.tileDepthIndex].Load(int3(dtid.xy, 0)).r;
    if (tileDepth > 0.0) {
        float farthest = ScreenToViewSpaceDepth(tileDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        bCulled = float(dtid.z) > FogDepthToSlice(farthest, pc.maxDistance) + 1.5;
    }

    float3 ambient = bCulled ? float3(0.0) : FogAmbientRadiance(pc.skyboxIndex, pc.iblIntensity, pc.ambientScale);
    float3 ddgi = float3(0.0);
    float ddgiCoverage = 0.0;
    if (!bCulled && pc.ddgiCascades != nullptr) {
        // One random direction per frame; the history averages it over the sphere, which is what an isotropic phase sees.
        float2 u = SpatioTemporalBlueNoise2D(dtid.xy, pc.frameIndex, dtid.z + 32u);
        float z = 1.0 - 2.0 * u.x;
        float r = sqrt(max(1.0 - z * z, 0.0));
        float3 direction = float3(r * cos(2.0 * PI * u.y), r * sin(2.0 * PI * u.y), z);
        float edgeFade;
        float4 gi = DDGISampleIrradianceCascaded(worldPos, direction, viewDir, pc.ddgiCascades, 0.0, edgeFade);
        ddgi = gi.rgb * gi.w * pc.ambientScale;
        ddgiCoverage = gi.w;
        ambient = ddgi + ambient * (1.0 - max(gi.w, edgeFade));
    }

    DirectionalLightData sun = pc.lightData->directionalLight;
    float3 sunRadiance = FogSunRadiance(sun, viewDir, pc.anisotropy);
    if (!(!bCulled && any(sunRadiance > 0.0) && FogVisible(pc.tlasIndex, INSTANCE_MASK_SUN_OCCLUDER, worldPos, normalize(-sun.directionIntensity.xyz), 10000.0))) {
        sunRadiance = float3(0.0);
    }

    float3 localRadiance = float3(0.0);

    if (!bCulled && pc.worldGridBuffer != nullptr) {
        WorldGridKey key = WorldGridResolveCell(worldPos, scene.cameraWorldPos.xyz);
        uint2 cell = key.bValid ? pc.worldGridBuffer[key.cellIndex] : uint2(0u);
        // Crowded cells visit an evenly spaced subset from a random start; every light is visited with probability visitCount / count.
        uint visitCount = min(cell.y, VOLUMETRIC_FOG_MAX_LIGHT_CANDIDATES);
        uint stride = max(cell.y / max(visitCount, 1u), 1u);
        uint start = min(uint(HashStep(rng) * float(cell.y)), max(cell.y, 1u) - 1u);
        float wSum = 0.0;
        float selectedWeight = 0.0;
        uint selectedLight = 0u;
        for (uint i = 0u; i < visitCount; ++i) {
            uint lightIndex = pc.worldGridIndexList[cell.x + (start + i * stride) % cell.y];
            float weight = FogLightTarget(pc.lightData->lights[lightIndex], worldPos, viewDir, pc.anisotropy);
            if (!(weight > 0.0)) {
                continue;
            }
            wSum += weight;
            if (HashStep(rng) * wSum < weight) {
                selectedWeight = weight;
                selectedLight = lightIndex;
            }
        }
        if (selectedWeight > 0.0) {
            localRadiance = FogShadeLight(pc.lightData->lights[selectedLight], pc.tlasIndex, worldPos, viewDir, pc.anisotropy, SpatioTemporalBlueNoise2D(dtid.xy, pc.frameIndex, dtid.z + 96u))
                * (wSum / selectedWeight * float(cell.y) / float(visitCount));
        }
    }

    float3 radiance = ambient + sunRadiance + localRadiance;
    switch (pc.debugMode) {
        case FOG_DEBUG_AMBIENT: radiance = ambient; break;
        case FOG_DEBUG_DDGI: radiance = ddgi; break;
        case FOG_DEBUG_SUN: radiance = sunRadiance; break;
        case FOG_DEBUG_LOCAL_LIGHTS: radiance = localRadiance; break;
        default: break;
    }
    float4 result = float4(extinction * pc.albedoDensity.rgb * radiance * scene.preExposure, extinction);
    if (pc.debugMode == FOG_DEBUG_DDGI_COVERAGE) {
        result.rgb = extinction * float3(1.0, ddgiCoverage, ddgiCoverage);
    }

    if (pc.historyIndex != ~0u) {
        float3 centreView = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, FogFroxelUV(dtid.xy, pc.renderExtent), FogSliceDepth(float(dtid.z) + 0.5, pc.maxDistance));
        float3 centreWorld = mul(scene.invView, float4(centreView, 1.0)).xyz;
        float3 prevView = mul(scene.prevView, float4(centreWorld, 1.0)).xyz;
        float prevDepth = -prevView.z;
        if (prevDepth > VOLUMETRIC_FOG_NEAR) {
            // Inverse of CheapReconstructViewSpacePosition; the projection is assumed unchanged since last frame.
            float2 prevUV = float2((prevView.x / prevDepth - scene.ndcToViewAdd.x) / scene.ndcToViewMul.x, (-prevView.y / prevDepth - scene.ndcToViewAdd.y) / scene.ndcToViewMul.y);
            float3 uvw = float3(prevUV * float2(pc.renderExtent) / (float2(pc.gridSize) * float(VOLUMETRIC_FOG_TILE_SIZE)), FogDepthToSlice(prevDepth, pc.maxDistance) / float(VOLUMETRIC_FOG_SLICES));
            if (all(uvw >= 0.0) && all(uvw <= 1.0)) {
                float4 history = rdgTextures3DFloat4[pc.historyIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvw, 0.0);
                history.rgb *= scene.preExposure / max(scene.prevPreExposure, 1e-8);
                result = lerp(history, result, bCulled ? 0.0 : VOLUMETRIC_FOG_HISTORY_ALPHA);
            }
        }
    }

    rdgStorage3DFloat4[pc.scatterOutIndex][dtid] = result;
}

/** Farthest (smallest reverse-Z) depth per froxel tile; 0 where any pixel is sky. */
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeVolumetricFogTileDepth(uniform VolumetricFogTileDepthPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= pc.gridSize)) {
        return;
    }

    uint2 base = dtid.xy * VOLUMETRIC_FOG_TILE_SIZE;
    float farthest = 1.0;
    for (uint y = 0u; y < VOLUMETRIC_FOG_TILE_SIZE; ++y) {
        for (uint x = 0u; x < VOLUMETRIC_FOG_TILE_SIZE; ++x) {
            uint2 pixel = base + uint2(x, y);
            if (any(pixel >= pc.renderExtent)) {
                continue;
            }
            farthest = min(farthest, rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r);
        }
    }
    rdgStorageFloat[pc.tileDepthOutIndex][dtid.xy] = farthest;
}

/** 3x3 tent within each slice; runs after the history blend so the history itself never compounds the blur. */
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeVolumetricFogFilter(uniform VolumetricFogFilterPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= pc.gridSize) || dtid.z >= VOLUMETRIC_FOG_SLICES) {
        return;
    }

    float4 sum = float4(0.0);
    float weightSum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 p = int2(dtid.xy) + int2(x, y);
            if (any(p < 0) || any(p >= int2(pc.gridSize))) {
                continue;
            }
            float weight = float((2 - abs(x)) * (2 - abs(y)));
            sum += rdgTextures3DFloat4[pc.scatterIndex].Load(int4(p, dtid.z, 0)) * weight;
            weightSum += weight;
        }
    }
    rdgStorage3DFloat4[pc.filteredOutIndex][dtid] = sum / weightSum;
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
    bool bSky = depth <= 0.0;
    float viewDepth = bSky ? pc.maxDistance : ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);

    float4 color = rdgStorageFloat4[pc.colorIndex][pixel];
    float4 fog = float4(0.0, 0.0, 0.0, 1.0);
    // Texel s holds the fog from the near plane to the far edge of slice s.
    float slice = FogDepthToSlice(viewDepth, pc.maxDistance);
    if (slice > 0.0) {
        float3 uvw;
        // Jitter the lookup by up to half a froxel so its blocky edges become noise that TAA averages out.
        float2 jitterXY = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 0u) - 0.5;
        float jitterZ = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 64u).x - 0.5;
        uvw.xy = (float2(pixel) + 0.5 + jitterXY * float(VOLUMETRIC_FOG_TILE_SIZE)) / (float2(pc.gridSize) * float(VOLUMETRIC_FOG_TILE_SIZE));
        uvw.z = (clamp(slice + jitterZ, 1.0, float(VOLUMETRIC_FOG_SLICES)) - 0.5) / float(VOLUMETRIC_FOG_SLICES);
        fog = rdgTextures3DFloat4[pc.integratedIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvw, 0.0);
        fog = lerp(float4(0.0, 0.0, 0.0, 1.0), fog, saturate(slice));

        // Past the grid the fog continues analytically: unshadowed sun and sky ambient through closed-form height fog.
        if ((bSky || viewDepth > pc.maxDistance) && pc.debugMode <= FOG_DEBUG_FOG_ONLY) {
            float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
            float3 viewRay = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, 1.0);
            float rayScale = length(viewRay);
            float3 viewDir = normalize(mul(scene.invView, float4(viewRay, 0.0)).xyz);
            float tStart = pc.maxDistance * rayScale;
            float tEnd = bSky ? max(VOLUMETRIC_FOG_SKY_DISTANCE, tStart) : viewDepth * rayScale;
            float tailTransmittance = exp(-FogOpticalDepth(scene.cameraWorldPos.y, viewDir.y, tStart, tEnd, pc.albedoDensity.a, pc.heightFalloff, pc.baseHeight));
            float3 radiance = FogAmbientRadiance(pc.skyboxIndex, pc.iblIntensity, pc.ambientScale) + FogSunRadiance(pc.lightData->directionalLight, viewDir, pc.anisotropy);
            fog.rgb += fog.a * pc.albedoDensity.rgb * radiance * scene.preExposure * (1.0 - tailTransmittance);
            fog.a *= tailTransmittance;
        }

        color.rgb = color.rgb * fog.a + fog.rgb;
        rdgStorageFloat4[pc.colorIndex][pixel] = color;
    }

    if (pc.foggedCopyIndex != ~0u) {
        rdgStorageFloat4[pc.foggedCopyIndex][pixel] = color;
    }
    if (pc.debugOutIndex != ~0u) {
        float3 debug = pc.debugMode == FOG_DEBUG_DDGI_COVERAGE ? fog.rgb / max(1.0 - fog.a, 1e-4) : fog.rgb;
        rdgStorageFloat4[pc.debugOutIndex][pixel] = float4(debug, 1.0);
    }
}
