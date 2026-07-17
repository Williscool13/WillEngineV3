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
// Reuses RTGroundTruthGIPushConstant verbatim (identical inputs).

static const uint GT_FULL_MAX_BOUNCES = 8;

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
        albedo *= assetTextures[material.textureImageIndices.x].SampleLevel(assetSamplers[material.textureSamplerIndices.x], colorUV, 0);
    }
    float3 emissive = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureImageIndices.w >= 0) {
        float2 emissiveUV = uv * material.emissiveUvTransform.xy + material.emissiveUvTransform.zw;
        emissive *= assetTextures[material.textureImageIndices.w].SampleLevel(assetSamplers[material.textureSamplerIndices.w], emissiveUV, 0).rgb;
    }

    float metallic = material.metalRoughFactors.x;
    float roughness = max(material.metalRoughFactors.y, 0.045);

    GTFullSurface surface;
    surface.normal = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));
    surface.albedo = albedo.rgb;
    surface.metallic = metallic;
    surface.roughness = roughness;
    surface.emissive = emissive;
    surface.F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metallic);
    return surface;
}

/** Direct radiance at a vertex: cone-sampled sun + one uniformly picked analytic light, both shadow-ray tested, full (diffuse + specular) BRDF. */
static float3 EvalDirectLighting(RTGroundTruthGIPushConstant pc, float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, inout uint rng)
{
    float3 radiance = float3(0.0);
    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
    float nDotV = max(dot(N, V), 0.0);

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
            float3 rayOrigin = P + N * 0.05;
            float3 toLightShadow = lightSamplePos - rayOrigin;
            float shadowDist = length(toLightShadow);
            RayDesc shadowRay;
            shadowRay.Origin = rayOrigin;
            shadowRay.Direction = toLightShadow / shadowDist;
            shadowRay.TMin = 0.001;
            shadowRay.TMax = shadowDist - 0.05;

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
                radiance += (diff + spec) * cosShade * geomTerm * al.intensity * alColor * atten * float(analyticCount) * lightArea;
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
            sunRay.Origin = P + N * 0.05;
            sunRay.Direction = L;
            sunRay.TMin = 0.001;
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
                radiance += (diff + spec) * cosShade * dl.directionIntensity.w * UnpackUnorm4x8(dl.packedColor).rgb;
            }
        }
    }

    return radiance;
}

struct BrdfSample
{
    float3 direction;
    float3 weight; // f * cos / pdf
    bool valid;
};

/** One-sample combined diffuse+specular BRDF importance sample; weight uses the full BRDF over the combined pdf (unbiased, keeps both lobes). */
static BrdfSample SampleSurfaceBrdf(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, inout uint rng)
{
    BrdfSample result;
    result.direction = float3(0.0, 0.0, 1.0);
    result.weight = float3(0.0);
    result.valid = false;

    float nDotV = max(dot(N, V), 1e-4);

    float diffLum = GTLuminance(albedo * (1.0 - metallic));
    float specLum = GTLuminance(F0);
    float pSpec = clamp(specLum / max(specLum + diffLum, 1e-4), 0.1, 0.9);

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

    float pdfDiff = nDotL / PI;
    float pdfSpec = NDF * nDotH / max(4.0 * vDotH, 1e-4);
    float pdf = pSpec * pdfSpec + (1.0 - pSpec) * pdfDiff;
    if (pdf <= 1e-6) {
        return result;
    }

    result.direction = L;
    result.weight = f * nDotL / pdf;
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

    // Primary miss: draw the environment map (matches the skybox pass reconstruction + Y flip).
    if (depth <= 0.0) {
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
    float4 albedo = UnpackAlbedoRGB8(gt2.r);
    float3 emissiveRGB = UnpackEmissiveRGBE(gt2.g);
    roughness = max(roughness, 0.045);

    float3 N = normalize(mul(float3x3(scene.invView), viewNormal));
    float3 V = normalize(cameraPos - worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.xyz, metallic);

    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
    uint rng = SeedRng(pixel, pc.frameIndex, 0x7A1B3Du);

    // Direct at the primary surface (full BRDF NEE).
    float3 direct = EvalDirectLighting(pc, worldPos, N, V, albedo.xyz, metallic, roughness, F0, rng);

    // Indirect: full BRDF path carrying both diffuse and specular lobes. NEE at each bounce, skybox on miss.
    float3 indirect = float3(0.0);
    float3 throughput = float3(1.0);
    float3 P = worldPos;
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
        ray.Origin = P + Nv * 0.05;
        ray.Direction = bs.direction;
        ray.TMin = 0.001;
        ray.TMax = 10000.0;

        // Light proxies excluded. NEE owns all analytic light emission.
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, ray);
        q.Proceed();

        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            if (pc.skyboxIndex >= 0) {
                float3 skyDirection = float3(bs.direction.x, -bs.direction.y, bs.direction.z);
                indirect += throughput * assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb * pc.iblIntensity;
            }
            break;
        }

        float hitT = q.CommittedRayT();
        float3 hitPosition = ray.Origin + bs.direction * hitT;
        Instance instance = pc.instanceBuffer[q.CommittedInstanceID()];
        GTFullSurface surface = FetchHitSurface(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());

        // Two-sided shading: face the geometric normal toward the incoming ray so specular chains hitting a back face keep going instead of terminating.
        float3 shadingN = dot(surface.normal, -bs.direction) < 0.0 ? -surface.normal : surface.normal;
        float3 Vnext = -bs.direction;

        indirect += throughput * surface.emissive;
        indirect += throughput * EvalDirectLighting(pc, hitPosition, shadingN, Vnext, surface.albedo, surface.metallic, surface.roughness, surface.F0, rng);

        P = hitPosition;
        Nv = shadingN;
        Vv = Vnext;
        curAlbedo = surface.albedo;
        curMetallic = surface.metallic;
        curRoughness = surface.roughness;
        curF0 = surface.F0;
    }

    float3 newSample = direct + indirect + emissiveRGB;

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;
    float4 accum = pc.accumulationBuffer[pixelIdx];
    float count = float(pc.accumulationCount);
    accum = (accum * count + float4(newSample, 1.0)) / (count + 1.0);
    pc.accumulationBuffer[pixelIdx] = accum;
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(accum.xyz, 1.0);
}
