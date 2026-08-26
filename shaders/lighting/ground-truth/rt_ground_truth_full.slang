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
// Full path-traced reference: direct illumination (analytic light + cone-sampled sun, full BRDF) plus a complete indirect path that carries BOTH diffuse and
// specular lobes at every vertex (single-sample MIS between the two), with NEE at each bounce and the skybox on miss. Unlike rt_ground_truth_gi (diffuse-only
// indirect), this includes specular GI: mirror/glossy inter-reflections and specular reflections of the environment. Accumulated per pixel until reset.
// Analytic light emission is MIS-complete: NEE (area sampling) is balance-heuristic paired with BRDF rays that intersect the analytic light shapes in-shader
// (sphere/quad along each bounce segment, sun cone on miss), so smooth-surface light reflections and reflective caustics converge instead of relying on
// NEE through a near-delta GGX lobe. Lights stay transparent to rays, matching the shadow-ray mask. Reuses RTGroundTruthGIPushConstant verbatim.

static const uint GT_FULL_MAX_BOUNCES = 8;
static const float GT_FIREFLY_CLAMP_BASE = 16.0;

static float GTLuminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

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

/** pdf = cos/PI. */
static float3 SampleCosineHemisphere(float3 n, float u1, float u2)
{
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u1, 0.0)));
}

/** GGX (NDF) half-vector sample about n; direction pdf = D(H) * (N.H) / (4 * V.H). */
static float3 SampleGGXHalfVector(float3 n, float roughness, float u1, float u2)
{
    float a = roughness * roughness;
    float phi = 6.28318530718 * u1;
    float cosTheta = sqrt((1.0 - u2) / (1.0 + (a * a - 1.0) * u2));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return normalize(t * h.x + b * h.y + n * h.z);
}

static float BrdfSelectPSpec(float3 albedo, float metallic, float3 F0)
{
    float diffLum = GTLuminance(albedo * (1.0 - metallic));
    float specLum = GTLuminance(F0);
    return clamp(specLum / max(specLum + diffLum, 1e-4), 0.1, 0.9);
}

/** Solid-angle pdf of SampleSurfaceBrdf for direction L; must stay in lockstep with its sampling. */
static float BrdfPdf(float3 N, float3 V, float3 L, float roughness, float pSpec)
{
    float nDotL = max(dot(N, L), 0.0);
    float3 H = normalize(V + L);
    float nDotH = max(dot(N, H), 0.0);
    float vDotH = max(dot(V, H), 1e-4);
    float pdfDiff = nDotL / PI;
    float pdfSpec = D_GGX(N, H, roughness) * nDotH / max(4.0 * vDotH, 1e-4);
    return pSpec * pdfSpec + (1.0 - pSpec) * pdfDiff;
}

struct GTFullSurface
{
    float3 normal;
    float3 albedo;
    float metallic;
    float roughness;
    float3 emissive;
    float3 F0;
};

/** Triangle fetch for bounce vertices (same chain as the DDGI trace); pulls full material for specular bounces. */
static GTFullSurface FetchHitSurface(RTGroundTruthGIPushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics)
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
        albedo *= assetTextures[NonUniformResourceIndex(material.textureImageIndices.x)].SampleLevel(assetSamplers[NonUniformResourceIndex(material.textureSamplerIndices.x)], colorUV, 0);
    }
    float3 emissive = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureImageIndices.w >= 0) {
        float2 emissiveUV = uv * material.emissiveUvTransform.xy + material.emissiveUvTransform.zw;
        emissive *= assetTextures[NonUniformResourceIndex(material.textureImageIndices.w)].SampleLevel(assetSamplers[NonUniformResourceIndex(material.textureSamplerIndices.w)], emissiveUV, 0).rgb;
    }

    float metallic;
    float mrRoughness;
    SampleMetalRoughLod(material, uv, 0, metallic, mrRoughness);
    float roughness = max(mrRoughness, MIN_ROUGHNESS);

    GTFullSurface surface;
    surface.normal = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));
    surface.albedo = albedo.rgb;
    surface.metallic = metallic;
    surface.roughness = roughness;
    surface.emissive = emissive;
    surface.F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metallic);
    return surface;
}

/** Direct radiance at a vertex: cone-sampled sun + one uniformly picked analytic light, both shadow-ray tested, full (diffuse + specular) BRDF. rayOrigin is P already lifted off its surface; the caller owns that offset because a gbuffer vertex and a bounce vertex need different scales. bMisPartner = a BRDF ray will leave this vertex (CollectAnalyticLightHits is the paired estimator); false at the final path vertex so NEE keeps full weight. */
static float3 EvalDirectLighting(RTGroundTruthGIPushConstant pc, float3 P, float3 rayOrigin, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, bool bMisPartner, inout uint rng)
{
    float3 radiance = float3(0.0);
    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
    float nDotV = max(dot(N, V), 0.0);
    float pSpec = BrdfSelectPSpec(albedo, metallic, F0);

    // NEE (analytic lights only)
    int analyticCount = pc.lightData->analyticLightCount;
    if (analyticCount > 0) {
        float rsel = HashStep(rng);
        float2 xi = float2(HashStep(rng), HashStep(rng));
        int lightIdx = clamp(int(rsel * float(analyticCount)), 0, analyticCount - 1);
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

        float3 toLight = lightSamplePos - P;
        float lightDist = max(length(toLight), 1e-5);
        float3 L = toLight / lightDist;
        float cosShade = max(dot(N, L), 0.0);
        float cosLight = al.type == LIGHT_TYPE_SPHERE
            ? max(dot(normalize(lightSamplePos - al.position.xyz), -L), 0.0)
            : max(dot(-al.normal.xyz, L), 0.0);
        float atten = LightRangeCull(lightDist, al.range);
        if (cosShade > 0.0 && cosLight > 0.0 && atten > 0.0) {
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
                float3 H = normalize(V + L);
                float NDF = D_GGX(N, H, roughness);
                float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
                float3 F = F_SCHLICK(V, H, F0);
                float3 spec = (NDF * G * F) / max(4.0 * nDotV * cosShade, 0.001);
                float3 kD = (1.0 - F) * (1.0 - metallic);
                float orenNayar = Diffuse_OrenNayar(cosShade, nDotV, L, V, roughness);
                float3 diff = kD * albedo * orenNayar;
                float geomTerm = LightGeometryTerm(cosLight, lightDist);
                float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                float w = 1.0;
                if (bMisPartner) {
                    float pdfLightArea = 1.0 / (float(analyticCount) * lightArea);
                    float pdfBrdfArea = BrdfPdf(N, V, L, roughness, pSpec) * cosLight / max(lightDist * lightDist, 1e-8);
                    w = pdfLightArea / max(pdfLightArea + pdfBrdfArea, 1e-12);
                }
                radiance += (diff + spec) * cosShade * geomTerm * al.intensity * alColor * atten * float(analyticCount) * lightArea * w;
            }
        }
    }

    DirectionalLightData dl = pc.lightData->directionalLight;
    if (dl.directionIntensity.w > 0.0) {
        float su1 = HashStep(rng);
        float su2 = HashStep(rng);
        float3 L = SampleSunConeDirection(normalize(-dl.directionIntensity.xyz), dl.angularRadius, su1, su2);
        float cosShade = dot(N, L);
        if (cosShade > 0.0) {
            RayDesc sunRay;
            sunRay.Origin = rayOrigin;
            sunRay.Direction = L;
            sunRay.TMin = 0.0;
            sunRay.TMax = 10000.0;

            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadow;
            shadow.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, sunRay);
            shadow.Proceed();
            if (shadow.CommittedStatus() == COMMITTED_NOTHING) {
                float3 H = normalize(V + L);
                float NDF = D_GGX(N, H, roughness);
                float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
                float3 F = F_SCHLICK(V, H, F0);
                float3 spec = (NDF * G * F) / max(4.0 * nDotV * cosShade, 0.001);
                float3 kD = (1.0 - F) * (1.0 - metallic);
                float orenNayar = Diffuse_OrenNayar(cosShade, nDotV, L, V, roughness);
                float3 diff = kD * albedo * orenNayar;
                float w = 1.0;
                if (bMisPartner) {
                    float coneSolidAngle = 2.0 * PI * (1.0 - cos(dl.angularRadius));
                    if (coneSolidAngle > 1e-7) {
                        float pdfCone = 1.0 / coneSolidAngle;
                        w = pdfCone / (pdfCone + BrdfPdf(N, V, L, roughness, pSpec));
                    }
                }
                radiance += (diff + spec) * cosShade * dl.directionIntensity.w * UnpackUnorm4x8(dl.packedColor).rgb * w;
            }
        }
    }

    return radiance;
}

/** MIS partner to EvalDirectLighting's NEE: emission a BRDF ray with pdf brdfPdf collects from analytic lights crossed along [0, maxT). Lights are transparent to rays (matching the shadow-ray mask), so every light on the segment contributes; the implied surface radiance intensity * color * LightRangeCull matches the NEE estimator's units exactly. */
static float3 CollectAnalyticLightHits(RTGroundTruthGIPushConstant pc, float3 rayOrigin, float3 rayDir, float maxT, float brdfPdf)
{
    float3 radiance = float3(0.0);
    int analyticCount = pc.lightData->analyticLightCount;
    for (int i = 0; i < analyticCount; ++i) {
        LightInfo al = pc.lightData->lights[i];
        if (al.intensity <= 0.0) { continue; }

        float t;
        float cosLight;
        float lightArea;
        if (al.type == LIGHT_TYPE_SPHERE) {
            float radius = al.right.w;
            float3 oc = rayOrigin - al.position.xyz;
            float b = dot(oc, rayDir);
            float c = dot(oc, oc) - radius * radius;
            float disc = b * b - c;
            if (disc <= 0.0) { continue; }
            t = -b - sqrt(disc);
            if (t <= 0.0 || t >= maxT) { continue; }
            cosLight = dot(normalize(rayOrigin + rayDir * t - al.position.xyz), -rayDir);
            lightArea = 4.0 * PI * radius * radius;
        } else if (al.type == LIGHT_TYPE_AREA) {
            float denom = dot(al.normal.xyz, rayDir);
            if (abs(denom) < 1e-6) { continue; }
            t = dot(al.position.xyz - rayOrigin, al.normal.xyz) / denom;
            if (t <= 0.0 || t >= maxT) { continue; }
            float3 rel = rayOrigin + rayDir * t - al.position.xyz;
            if (abs(dot(rel, al.right.xyz)) > al.right.w || abs(dot(rel, al.up.xyz)) > al.up.w) { continue; }
            cosLight = dot(-al.normal.xyz, rayDir);
            lightArea = 4.0 * al.right.w * al.up.w;
        } else {
            continue;
        }

        if (cosLight <= 0.0) { continue; }
        float atten = LightRangeCull(t, al.range);
        if (atten <= 0.0) { continue; }
        float pdfLightArea = 1.0 / (float(analyticCount) * lightArea);
        float pdfBrdfArea = brdfPdf * cosLight / max(t * t, 1e-8);
        float w = pdfBrdfArea / max(pdfBrdfArea + pdfLightArea, 1e-12);
        radiance += UnpackUnorm4x8(al.packedColor).rgb * al.intensity * atten * w;
    }
    return radiance;
}

struct BrdfSample
{
    float3 direction;
    float3 weight; // f * cos / pdf
    float pdf;
    bool valid;
};

/** One-sample combined diffuse+specular BRDF importance sample; weight uses the full BRDF over the combined pdf (unbiased, keeps both lobes). */
static BrdfSample SampleSurfaceBrdf(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, inout uint rng)
{
    BrdfSample result;
    result.direction = float3(0.0, 0.0, 1.0);
    result.weight = float3(0.0);
    result.pdf = 0.0;
    result.valid = false;

    float nDotV = max(dot(N, V), 1e-4);

    float pSpec = BrdfSelectPSpec(albedo, metallic, F0);

    float3 L;
    if (HashStep(rng) < pSpec) {
        float u1 = HashStep(rng);
        float u2 = HashStep(rng);
        float3 H = SampleGGXHalfVector(N, roughness, u1, u2);
        L = reflect(-V, H);
    } else {
        float u1 = HashStep(rng);
        float u2 = HashStep(rng);
        L = SampleCosineHemisphere(N, u1, u2);
    }

    float nDotL = dot(N, L);
    if (nDotL <= 0.0) {
        return result;
    }

    float3 H = normalize(V + L);
    float nDotH = max(dot(N, H), 0.0);
    float vDotH = max(dot(V, H), 1e-4);

    float NDF = D_GGX(N, H, roughness);
    float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
    float3 F = F_SCHLICK(V, H, F0);
    float3 spec = (NDF * G * F) / max(4.0 * nDotV * nDotL, 1e-4);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float orenNayar = Diffuse_OrenNayar(nDotL, nDotV, L, V, roughness);
    float3 diff = kD * albedo * orenNayar;
    float3 f = diff + spec;

    float pdf = BrdfPdf(N, V, L, roughness, pSpec);
    if (pdf <= 1e-6) {
        return result;
    }

    result.direction = L;
    result.weight = f * nDotL / pdf;
    result.pdf = pdf;
    result.valid = true;
    return result;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeRTGroundTruthFull(uint3 dtid : SV_DispatchThreadID, uniform RTGroundTruthGIPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);

    float lensRadius = f16tof32(pc.dofPackedApertureFocus & 0xFFFFu);
    if (lensRadius > 0.0) {
        float focusDistance = max(f16tof32(pc.dofPackedApertureFocus >> 16), 0.05);
        float3 cameraPos = scene.cameraWorldPos.xyz;
        float3 camRight = mul(scene.invView, float4(1.0, 0.0, 0.0, 0.0)).xyz;
        float3 camUp = mul(scene.invView, float4(0.0, 1.0, 0.0, 0.0)).xyz;
        RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
        uint rng = SeedRng(pixel, pc.frameIndex, 0x51C0FEu);

        uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;
        float4 accum = pc.accumulationBuffer[pixelIdx];
        float count = float(pc.accumulationCount);

        uint spp = max(pc.samplesPerFrame, 1u);
        float3 sampleSum = float3(0.0);
        for (uint s = 0; s < spp; ++s) {
            float2 jitteredUv = (float2(pixel) + float2(HashStep(rng), HashStep(rng))) / float2(pc.renderExtent);
            float2 ndc = jitteredUv * 2.0 - 1.0;
            float4 farPoint = mul(scene.invViewProj, float4(ndc, 1.0, 1.0));
            float3 pinholeDir = normalize(farPoint.xyz / farPoint.w - cameraPos);
            float dirViewZ = abs(mul(scene.view, float4(pinholeDir, 0.0)).z);
            float3 focalPoint = cameraPos + pinholeDir * (focusDistance / max(dirViewZ, 1e-4));

            float lensR = sqrt(HashStep(rng)) * lensRadius;
            float lensPhi = 6.28318530718 * HashStep(rng);
            float3 rayOrigin = cameraPos + camRight * (lensR * cos(lensPhi)) + camUp * (lensR * sin(lensPhi));
            float3 rayDir = normalize(focalPoint - rayOrigin);

            float3 radiance = float3(0.0);
            float3 throughput = float3(1.0);
            float segmentPdf = 1e8;

            for (uint segment = 0; segment <= GT_FULL_MAX_BOUNCES; ++segment) {
                RayDesc ray;
                ray.Origin = rayOrigin;
                ray.Direction = rayDir;
                ray.TMin = 0.0;
                ray.TMax = 10000.0;

                RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
                q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, ray);
                q.Proceed();

                const bool bTriangleHit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
                float segmentT = bTriangleHit ? q.CommittedRayT() : ray.TMax;
                radiance += throughput * CollectAnalyticLightHits(pc, rayOrigin, rayDir, segmentT, segmentPdf);

                if (!bTriangleHit) {
                    float3 skyDirection = float3(rayDir.x, -rayDir.y, rayDir.z);
                    if (segment == 0) {
                        // Matches the pinhole path's primary-miss background exactly
                        if (pc.skyboxIndex >= 0) {
                            radiance += assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb;
                        }
                    } else {
                        if (pc.skyboxIndex >= 0) {
                            radiance += throughput * assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb * pc.iblIntensity;
                        }
                        DirectionalLightData dl = pc.lightData->directionalLight;
                        if (dl.directionIntensity.w > 0.0) {
                            float cosThetaMax = cos(dl.angularRadius);
                            float coneSolidAngle = 2.0 * PI * (1.0 - cosThetaMax);
                            if (coneSolidAngle > 1e-7 && dot(rayDir, normalize(-dl.directionIntensity.xyz)) >= cosThetaMax) {
                                float pdfCone = 1.0 / coneSolidAngle;
                                float w = segmentPdf / (segmentPdf + pdfCone);
                                radiance += throughput * (dl.directionIntensity.w / coneSolidAngle) * UnpackUnorm4x8(dl.packedColor).rgb * w;
                            }
                        }
                    }
                    break;
                }

                float3 hitPosition = rayOrigin + rayDir * segmentT;
                Instance instance = pc.instanceBuffer[q.CommittedInstanceID()];
                GTFullSurface surface = FetchHitSurface(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
                float3 shadingN = dot(surface.normal, -rayDir) < 0.0 ? -surface.normal : surface.normal;
                float3 Vnext = -rayDir;
                float3 hitOrigin = OffsetHitRayOrigin(hitPosition, shadingN);

                radiance += throughput * surface.emissive;
                radiance += throughput * EvalDirectLighting(pc, hitPosition, hitOrigin, shadingN, Vnext, surface.albedo, surface.metallic, surface.roughness, surface.F0, segment < GT_FULL_MAX_BOUNCES, rng);

                BrdfSample bs = SampleSurfaceBrdf(shadingN, Vnext, surface.albedo, surface.metallic, surface.roughness, surface.F0, rng);
                if (!bs.valid) {
                    break;
                }
                throughput *= bs.weight;
                rayOrigin = hitOrigin;
                rayDir = bs.direction;
                segmentPdf = bs.pdf;
            }

            float lum = GTLuminance(radiance);
            float cap = GT_FIREFLY_CLAMP_BASE * sqrt(count + float(s) + 1.0);
            if (lum > cap) {
                radiance *= cap / lum;
            }
            sampleSum += radiance;
        }

        accum = (accum * count + float4(sampleSum, float(spp))) / (count + float(spp));
        pc.accumulationBuffer[pixelIdx] = accum;
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(accum.xyz, 1.0);
        return;
    }

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

    float footprint = RayOriginPixelFootprint(ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd), scene.ndcToViewMulXPixelSize, max(dot(N, V), 0.0));
    float3 primaryOrigin = OffsetGBufferRayOrigin(worldPos, N, footprint, 1.0);

    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
    uint rng = SeedRng(pixel, pc.frameIndex, 0x7A1B3Du);

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;
    float4 accum = pc.accumulationBuffer[pixelIdx];
    float count = float(pc.accumulationCount);

    uint spp = max(pc.samplesPerFrame, 1u);
    float3 sampleSum = float3(0.0);
    for (uint s = 0; s < spp; ++s) {

    // Direct at the primary surface (full BRDF NEE).
    float3 direct = EvalDirectLighting(pc, worldPos, primaryOrigin, N, V, albedo.xyz, metallic, roughness, F0, true, rng);

    // Indirect: full BRDF path carrying both diffuse and specular lobes. NEE at each bounce, skybox on miss.
    float3 indirect = float3(0.0);
    float3 throughput = float3(1.0);
    float3 rayOrigin = primaryOrigin;
    float3 Nv = N;
    float3 Vv = V;
    float3 curAlbedo = albedo.xyz;
    float curMetallic = metallic;
    float curRoughness = roughness;
    float3 curF0 = F0;

    for (uint bounce = 0; bounce < GT_FULL_MAX_BOUNCES; ++bounce) {
        BrdfSample bs = SampleSurfaceBrdf(Nv, Vv, curAlbedo, curMetallic, curRoughness, curF0, rng);
        if (!bs.valid) {
            break;
        }
        throughput *= bs.weight;

        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.Direction = bs.direction;
        ray.TMin = 0.0;
        ray.TMax = 10000.0;

        // Light proxies excluded from the TLAS trace; analytic light emission along the segment is collected by CollectAnalyticLightHits (MIS partner to NEE).
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, ray);
        q.Proceed();

        const bool bTriangleHit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
        float segmentT = bTriangleHit ? q.CommittedRayT() : ray.TMax;
        indirect += throughput * CollectAnalyticLightHits(pc, rayOrigin, bs.direction, segmentT, bs.pdf);

        if (!bTriangleHit) {
            if (pc.skyboxIndex >= 0) {
                float3 skyDirection = float3(bs.direction.x, -bs.direction.y, bs.direction.z);
                indirect += throughput * assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb * pc.iblIntensity;
            }
            DirectionalLightData dl = pc.lightData->directionalLight;
            if (dl.directionIntensity.w > 0.0) {
                float cosThetaMax = cos(dl.angularRadius);
                float coneSolidAngle = 2.0 * PI * (1.0 - cosThetaMax);
                if (coneSolidAngle > 1e-7 && dot(bs.direction, normalize(-dl.directionIntensity.xyz)) >= cosThetaMax) {
                    float pdfCone = 1.0 / coneSolidAngle;
                    float w = bs.pdf / (bs.pdf + pdfCone);
                    indirect += throughput * (dl.directionIntensity.w / coneSolidAngle) * UnpackUnorm4x8(dl.packedColor).rgb * w;
                }
            }
            break;
        }

        float hitT = segmentT;
        float3 hitPosition = ray.Origin + bs.direction * hitT;
        Instance instance = pc.instanceBuffer[q.CommittedInstanceID()];
        GTFullSurface surface = FetchHitSurface(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());

        // Two-sided shading: face the geometric normal toward the incoming ray so specular chains hitting a back face keep going instead of terminating.
        float3 shadingN = dot(surface.normal, -bs.direction) < 0.0 ? -surface.normal : surface.normal;
        float3 Vnext = -bs.direction;

        float3 hitOrigin = OffsetHitRayOrigin(hitPosition, shadingN);

        indirect += throughput * surface.emissive;
        indirect += throughput * EvalDirectLighting(pc, hitPosition, hitOrigin, shadingN, Vnext, surface.albedo, surface.metallic, surface.roughness, surface.F0, bounce + 1 < GT_FULL_MAX_BOUNCES, rng);

        rayOrigin = hitOrigin;
        Nv = shadingN;
        Vv = Vnext;
        curAlbedo = surface.albedo;
        curMetallic = surface.metallic;
        curRoughness = surface.roughness;
        curF0 = surface.F0;
    }

    float3 newSample = direct + indirect + emissiveRGB;

    // Progressive firefly clamp: the cap grows with sqrt(count) so the bias vanishes as accumulation converges (consistent estimator).
    float lum = GTLuminance(newSample);
    float cap = GT_FIREFLY_CLAMP_BASE * sqrt(count + float(s) + 1.0);
    if (lum > cap) {
        newSample *= cap / lum;
    }
    sampleSum += newSample;

    } // spp

    accum = (accum * count + float4(sampleSum, float(spp))) / (count + float(spp));
    pc.accumulationBuffer[pixelIdx] = accum;
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(accum.xyz, 1.0);
}
