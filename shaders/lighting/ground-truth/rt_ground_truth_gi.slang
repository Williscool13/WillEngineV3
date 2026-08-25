import common_interop;
import model_interop;
import push_constant_interop;
import lights_interop;
import instance_mask_interop;
import bindless_declarations;
import constants_interop;
import shader_constants;

import shader_functions;
import pbr_functions;
import packing_functions;
import random_common;

// =============== Shader Summary ============
// Progressive path-traced reference for the DDGI indirect diffuse: primary surface from the gbuffer, direct = one NEE light sample + cone-sampled sun (identical estimator to rt_ground_truth_di),
// indirect = cosine-sampled diffuse-only path (Oren-Nayar at every bounce vertex, NEE at each, skybox on miss), accumulated per pixel until reset.
// Indirect is modulated by the primary's kD * albedo like the engine's composite, so DDGI-on screenshots compare apples-to-apples. Specular indirect is deliberately absent (DDGI doesn't model it).

static const uint GT_GI_MAX_BOUNCES = 8;

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

/** pdf = cos/PI; dividing it out of a diffuse bounce cancels the cosine, leaving diffuse * OrenNayar * PI as the throughput factor. */
static float3 SampleCosineHemisphere(float3 n, float u1, float u2)
{
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u1, 0.0)));
}

struct GTSurface
{
    float3 normal;
    float3 diffuseColor;
    float roughness;
    float3 emissive;
};

/** Triangle fetch for bounce vertices; indices are global (rebased at upload), same chain as the DDGI trace. */
static GTSurface FetchHitSurface(RTGroundTruthGIPushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics)
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

    MaterialProperties material = pc.materialBuffer[instance.materialIndex];
    float4 albedo = material.colorFactor * vertexColor;
    if (material.textureImageIndices.x >= 0) {
        float2 colorUV = uv * material.colorUvTransform.xy + material.colorUvTransform.zw;
        albedo *= assetTextures[material.textureImageIndices.x].SampleLevel(assetSamplers[material.textureSamplerIndices.x], colorUV, 0);
    }
    float3 emissive = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureImageIndices.w >= 0) {
        float2 emissiveUV = uv * material.emissiveUvTransform.xy + material.emissiveUvTransform.zw;
        emissive *= assetTextures[material.textureImageIndices.w].SampleLevel(assetSamplers[material.textureSamplerIndices.w], emissiveUV, 0).rgb;
    }

    GTSurface surface;
    surface.normal = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));
    float metallic;
    float mrRoughness;
    SampleMetalRoughLod(material, uv, 0, metallic, mrRoughness);
    surface.diffuseColor = albedo.rgb * (1.0 - metallic);
    surface.roughness = max(mrRoughness, MIN_ROUGHNESS);
    surface.emissive = emissive;
    return surface;
}

/** Direct diffuse radiance at a bounce vertex: cone-sampled sun + one uniformly picked light, both shadow-ray tested (estimator matches rt_ground_truth_di). Oren-Nayar diffuse (V = direction back toward the previous vertex). */
static float3 SampleDirectDiffuse(RTGroundTruthGIPushConstant pc, float3 worldPosition, float3 N, float3 V, float3 diffuseColor, float roughness, inout uint rng)
{
    float3 radiance = float3(0.0);
    float NoV = max(dot(N, V), 0.0);
    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];

    DirectionalLightData sun = pc.lightData->directionalLight;
    if (sun.directionIntensity.w > 0.0) {
        float su1 = HashStep(rng);
        float su2 = HashStep(rng);
        float3 L = SampleSunConeDirection(normalize(-sun.directionIntensity.xyz), sun.angularRadius, su1, su2);
        float NoL = dot(N, L);
        if (NoL > 0.0) {
            RayDesc shadowRay;
            shadowRay.Origin = OffsetHitRayOrigin(worldPosition, N);
            shadowRay.Direction = L;
            shadowRay.TMin = 0.0;
            shadowRay.TMax = 10000.0;

            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
            shadow.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, shadowRay);
            shadow.Proceed();
            if (shadow.CommittedStatus() == COMMITTED_NOTHING) {
                radiance += diffuseColor * Diffuse_OrenNayar(NoL, NoV, L, V, roughness) * NoL * sun.directionIntensity.w * UnpackUnorm4x8(sun.packedColor).rgb;
            }
        }
    }

    // NEE (analytic lights only)
    int analyticCount = pc.lightData->analyticLightCount;
    if (analyticCount > 0) {
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
            float3 rayOrigin = OffsetHitRayOrigin(worldPosition, N);
            float3 toLightShadow = lightSamplePos - rayOrigin;
            float shadowDist = length(toLightShadow);

            RayDesc shadowRay;
            shadowRay.Origin = rayOrigin;
            shadowRay.Direction = toLightShadow / shadowDist;
            shadowRay.TMin = 0.0;
            shadowRay.TMax = shadowDist - max(0.002, shadowDist * 0.005);

            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
            shadow.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, shadowRay);
            shadow.Proceed();
            if (shadow.CommittedStatus() == COMMITTED_NOTHING) {
                radiance += diffuseColor * Diffuse_OrenNayar(cosShade, NoV, L, V, roughness) * cosShade * LightGeometryTerm(cosLight, lightDist) * light.intensity * UnpackUnorm4x8(light.packedColor).rgb * atten * float(analyticCount) * lightArea;
            }
        }
    }
    return radiance;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeRTGroundTruthGI(uint3 dtid : SV_DispatchThreadID, uniform RTGroundTruthGIPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);

    // Primary miss: draw the environment map (matches the skybox pass reconstruction + Y flip).
    if (depth <= 1e-6) {
        float3 background = float3(0.0);
        if (pc.skyboxIndex >= 0) {
            float2 ndc = uv * 2.0 - 1.0;
            float4 farPoint = mul(scene.invViewProj, float4(ndc, 1.0, 1.0));
            float3 rayDir = normalize(farPoint.xyz / farPoint.w - scene.cameraWorldPos.xyz);
            rayDir.y = -rayDir.y;
            background = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], rayDir, 0).rgb;
        }
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(background, 1.0);
        return;
    }

    float3 worldPos = ReconstructWorldPosition(scene.invViewProj, uv, depth);
    float3 cameraPos = scene.cameraWorldPos.xyz;

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    uint2 gt2 = rdgTexturesUInt2[pc.gbufferTwoIndex].Load(int3(pixel, 0));

    float3 viewNormal = UnpackNormalOct16(gt1.r);
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    float4 albedo = UnpackAlbedoRGBA8(gt2.r);
    float3 emissiveRGB = UnpackEmissiveRGBE(gt2.g);
    roughness = max(roughness, MIN_ROUGHNESS);

    float3 N = normalize(mul(float3x3(scene.invView), viewNormal));
    float3 V = normalize(cameraPos - worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.xyz, metallic);
    float nDotV = max(dot(N, V), 0.0);

    float footprint = RayOriginPixelFootprint(ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd), scene.ndcToViewMulXPixelSize, nDotV);
    float3 primaryOrigin = OffsetGBufferRayOrigin(worldPos, N, footprint, 1.0);

    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
    uint rng = SeedRng(pixel, pc.frameIndex, 0x61C3A9u);

    // Direct at the primary surface: one light sample + cone-sampled sun, full BRDF (spec + Oren-Nayar diffuse), matching rt_ground_truth_di.
    float3 direct = float3(0.0);
    // NEE (analytic lights)
    int analyticCount = pc.lightData->analyticLightCount;
    if (analyticCount > 0) {
        float rng0 = HashStep(rng);
        float2 xi;
        xi.x = HashStep(rng);
        xi.y = HashStep(rng);
        int lightIdx = clamp(int(rng0 * float(analyticCount)), 0, analyticCount - 1);
        LightInfo al = pc.lightData->lights[lightIdx];

        float3 lightSamplePos;
        float lightArea;
        if (al.type == LIGHT_TYPE_SPHERE) {
            float radius = al.right.w;
            float z = 1.0 - 2.0 * xi.x;
            float rr = sqrt(max(1.0 - z * z, 0.0));
            float phi = 2.0 * PI * xi.y;
            lightSamplePos = al.position.xyz + radius * float3(rr * cos(phi), rr * sin(phi), z);
            lightArea = 4.0 * PI * radius * radius;
        } else {
            lightSamplePos = al.position.xyz + al.right.xyz * (xi.x * 2.0 - 1.0) * al.right.w + al.up.xyz * (xi.y * 2.0 - 1.0) * al.up.w;
            lightArea = 4.0 * al.right.w * al.up.w;
        }

        float3 toLight = lightSamplePos - worldPos;
        float lightDist = max(length(toLight), 1e-5);
        float3 L = toLight / lightDist;
        float cosShade = max(dot(N, L), 0.0);
        float cosLight = al.type == LIGHT_TYPE_SPHERE
            ? max(dot(normalize(lightSamplePos - al.position.xyz), -L), 0.0)
            : max(dot(-al.normal.xyz, L), 0.0);
        float atten = LightRangeCull(lightDist, al.range);

        if (cosShade > 0.0 && cosLight > 0.0 && atten > 0.0) {
            float3 toLightShadow = lightSamplePos - primaryOrigin;
            float shadowDist = length(toLightShadow);
            RayDesc shadowRay;
            shadowRay.Origin = primaryOrigin;
            shadowRay.Direction = toLightShadow / shadowDist;
            shadowRay.TMin = 0.0;
            shadowRay.TMax = shadowDist - max(0.002, shadowDist * 0.005);

            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
            q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, shadowRay);
            q.Proceed();
            if (q.CommittedStatus() == COMMITTED_NOTHING) {
                float3 H = normalize(V + L);
                float NDF = D_GGX(N, H, roughness);
                float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
                float3 F = F_SCHLICK(V, H, F0);
                float3 spec = (NDF * G * F) / max(4.0 * nDotV * max(dot(N, L), 0.0), 0.001);

                float3 kD = (1.0 - F) * (1.0 - metallic);
                float orenNayar = Diffuse_OrenNayar(cosShade, nDotV, L, V, roughness);
                float3 diff = kD * albedo.xyz * orenNayar;

                float geomTerm = LightGeometryTerm(cosLight, lightDist);
                float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                direct = (diff + spec) * cosShade * geomTerm * al.intensity * alColor * atten * float(analyticCount) * lightArea;
            }
        }
    }

    float3 sunLight = float3(0.0);
    {
        DirectionalLightData dl = pc.lightData->directionalLight;
        if (dl.directionIntensity.w > 0.0) {
            float su1 = HashStep(rng);
            float su2 = HashStep(rng);
            float3 L = SampleSunConeDirection(normalize(-dl.directionIntensity.xyz), dl.angularRadius, su1, su2);

            float cosShade = dot(N, L);
            if (cosShade > 0.0) {
                RayDesc sunRay;
                sunRay.Origin = primaryOrigin;
                sunRay.Direction = L;
                sunRay.TMin = 0.0;
                sunRay.TMax = 10000.0;

                RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
                q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, sunRay);
                q.Proceed();
                if (q.CommittedStatus() == COMMITTED_NOTHING) {
                    float3 H = normalize(V + L);
                    float NDF = D_GGX(N, H, roughness);
                    float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
                    float3 F = F_SCHLICK(V, H, F0);
                    float3 spec = (NDF * G * F) / max(4.0 * nDotV * cosShade, 0.001);

                    float3 kD = (1.0 - F) * (1.0 - metallic);
                    float orenNayar = Diffuse_OrenNayar(cosShade, nDotV, L, V, roughness);
                    float3 diff = kD * albedo.xyz * orenNayar;

                    sunLight = (diff + spec) * cosShade * dl.directionIntensity.w * UnpackUnorm4x8(dl.packedColor).rgb;
                }
            }
        }
    }

    // Indirect: diffuse-only path (Oren-Nayar). The diffuse transport is evaluated per vertex at sample time (cosine-sampled: f * cos / pdf = diffuse * OrenNayar * PI), so the view-dependent term matches the direct paths. Primary diffuse keeps the engine's (1 - F_ibl) * (1 - metallic) modulation.
    float3 indirect = float3(0.0);
    float3 F_ibl = F_SCHLICK_ROUGHNESS(nDotV, F0, roughness);
    float3 throughput = float3(1.0);
    float3 vertexDiffuse = (1.0 - F_ibl) * (1.0 - metallic) * albedo.xyz;
    float vertexRoughness = roughness;
    float3 vertexNormal = N;
    float3 vertexView = V;
    float3 origin = primaryOrigin;
    for (uint bounce = 0; bounce < GT_GI_MAX_BOUNCES; ++bounce) {
        float u1 = HashStep(rng);
        float u2 = HashStep(rng);
        float3 direction = SampleCosineHemisphere(vertexNormal, u1, u2);

        float NoL = max(dot(vertexNormal, direction), 0.0);
        float NoV = max(dot(vertexNormal, vertexView), 0.0);
        throughput *= vertexDiffuse * Diffuse_OrenNayar(NoL, NoV, direction, vertexView, vertexRoughness) * PI;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = direction;
        ray.TMin = 0.0;
        ray.TMax = 10000.0;

        // Light proxies excluded. NEE owns all light emission.
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, ray);
        q.Proceed();

        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            if (pc.skyboxIndex >= 0) {
                // Matches the Y flip the visible skybox pass applies when sampling the cubemap.
                float3 skyDirection = float3(direction.x, -direction.y, direction.z);
                indirect += throughput * assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb * pc.iblIntensity;
            }
            break;
        }
        if (!q.CommittedTriangleFrontFace()) {
            break;
        }

        float hitT = q.CommittedRayT();
        float3 hitPosition = origin + direction * hitT;
        Instance instance = pc.instanceBuffer[q.CommittedInstanceID()];
        GTSurface surface = FetchHitSurface(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());

        float3 vNext = -direction;
        indirect += throughput * surface.emissive;
        indirect += throughput * SampleDirectDiffuse(pc, hitPosition, surface.normal, vNext, surface.diffuseColor, surface.roughness, rng);

        vertexDiffuse = surface.diffuseColor;
        vertexRoughness = surface.roughness;
        vertexNormal = surface.normal;
        vertexView = vNext;
        origin = OffsetHitRayOrigin(hitPosition, surface.normal);
    }

    float3 newSample = direct + sunLight + indirect + emissiveRGB;

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;
    float4 accum = pc.accumulationBuffer[pixelIdx];
    float count = float(pc.accumulationCount);
    accum = (accum * count + float4(newSample, 1.0)) / (count + 1.0);
    pc.accumulationBuffer[pixelIdx] = accum;
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(accum.xyz, 1.0);
}
