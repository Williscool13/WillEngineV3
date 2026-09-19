import common_interop;
import push_constant_interop;
import lights_interop;
import light_index_common;
import instance_mask_interop;
import bindless_declarations;
import constants_interop;
import shader_constants;

import shader_functions;
import pbr_functions;
import packing_functions;
import random_common;
import rt_alpha_test_common;

static RTAlphaTestBuffers GTAlphaBuffers(RTGroundTruthDIPushConstant pc)
{
    return { pc.instanceBuffer, pc.primitiveBuffer, pc.materialBuffer, pc.indexBuffer, pc.vertexAttrBuffer };
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

struct GTSurface
{
    float3 worldPos;
    float3 shadowOrigin;
    float3 N;
    float3 V;
    float nDotV;
    float3 albedo;
    float3 F0;
    float roughness;
    float metallic;
}

static float3 EvalLightPoint(RTGroundTruthDIPushConstant pc, GTSurface sf, LightInfo al, float3 lightSamplePos)
{
    float3 toLight = lightSamplePos - sf.worldPos;
    float lightDist = max(length(toLight), 1e-5);
    float3 L = toLight / lightDist;

    float cosShade = max(dot(sf.N, L), 0.0);
    float cosLight = (al.type == LIGHT_TYPE_SPHERE)
        ? max(dot(normalize(lightSamplePos - al.position.xyz), -L), 0.0)
        : max(dot(-al.normal.xyz, L), 0.0);
    float atten = LightRangeCull(lightDist, al.range) * LightConeFalloff(cosLight, al.normal.w, al.position.w);
    if (cosShade <= 0.0 || cosLight <= 0.0 || atten <= 0.0) {
        return float3(0.0);
    }

    float3 toLightShadow = lightSamplePos - sf.shadowOrigin;
    float shadowDist = length(toLightShadow);
    RayDesc shadowRay;
    shadowRay.Origin = sf.shadowOrigin;
    shadowRay.Direction = toLightShadow / shadowDist;
    shadowRay.TMin = 0.0;
    shadowRay.TMax = shadowDist - max(0.002, shadowDist * 0.005);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_OCCLUDER, shadowRay);
    RTProceedAlphaTested(q, GTAlphaBuffers(pc));
    if (q.CommittedStatus() != COMMITTED_NOTHING) {
        return float3(0.0);
    }

    float3 H = normalize(sf.V + L);
    float NDF = D_GGX(sf.N, H, sf.roughness);
    float G = G_SMITH_HEIGHT_CORRELATED(sf.N, sf.V, L, sf.roughness);
    float3 F = F_SCHLICK(sf.V, H, sf.F0);
    float3 spec = (NDF * G * F) / max(4.0 * sf.nDotV * cosShade, 0.001);

    float3 kD = (1.0 - F) * (1.0 - sf.metallic);
    float3 diff = kD * sf.albedo * Diffuse_OrenNayar(cosShade, sf.nDotV, L, sf.V, sf.roughness);

    return (diff + spec) * cosShade * LightGeometryTerm(cosLight, lightDist) * al.intensity * UnpackUnorm4x8(al.packedColor).rgb * atten;
}

static float EmissiveMeshletWeight(EmissiveMeshlet meshlet, float3 p)
{
    if (meshlet.lightCount == 0u || any(p < meshlet.aabbMin) || any(p > meshlet.aabbMax)) {
        return 0.0;
    }
    float3 d = max(max(meshlet.aabbMin + meshlet.rangePad - p, p - (meshlet.aabbMax - meshlet.rangePad)), 0.0);
    return meshlet.power / max(dot(d, d), LIGHT_GEOM_MIN_DIST2);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeRTGroundTruthDI(uint3 dtid : SV_DispatchThreadID, uniform RTGroundTruthDIPushConstant pc)
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
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(background * scene.preExposure, 1.0);
        return;
    }

    float3 worldPos = ReconstructWorldPosition(scene.invViewProj, uv, depth);
    float3 cameraPos = scene.cameraWorldPos.xyz;

    // Unpack gbuffer
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    uint2 gt2 = rdgTexturesUInt2[pc.gbufferTwoIndex].Load(int3(pixel, 0));

    float3 viewNormal = UnpackNormalOct16(gt1.r);
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    float4 albedo = UnpackAlbedoRGBA8(gt2.r);
    float3 emissiveRGB = UnpackEmissiveRGBE(gt2.g);
    roughness = max(roughness, MIN_ROUGHNESS);

    // Transform view normal to world space
    float3 N = normalize(mul(float3x3(scene.invView), viewNormal));
    float3 V = normalize(cameraPos - worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.xyz, metallic);
    float nDotV = max(dot(N, V), 0.0);

    float footprint = RayOriginPixelFootprint(ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd), scene.ndcToViewMulXPixelSize, nDotV);
    float3 shadowOrigin = OffsetGBufferRayOrigin(worldPos, N, footprint, 1.0);

    int lightCount = pc.lightData->lightCount;

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;
    float4 accum = pc.accumulationBuffer[pixelIdx];
    float count = float(pc.accumulationCount);

    // One sample per frame
    uint seed = pixelIdx + pc.frameIndex * 1973904141u;
    seed = seed ^ (seed >> 16);
    seed *= 0x45d9f3bu;
    seed = seed ^ (seed >> 16);
    float rng0 = float(seed & 0x00FFFFFFu) / float(0x01000000u);

    uint seed2 = seed * 2654435761u;
    seed2 = seed2 ^ (seed2 >> 16);
    float2 xi = float2(
        float(seed2 & 0x00FFFFFFu) / float(0x01000000u),
        float((seed2 >> 8) & 0x00FFFFFFu) / float(0x01000000u)
    );

    float3 directLight = float3(0.0);

    GTSurface sf;
    sf.worldPos = worldPos;
    sf.shadowOrigin = shadowOrigin;
    sf.N = N;
    sf.V = V;
    sf.nDotV = nDotV;
    sf.albedo = albedo.xyz;
    sf.F0 = F0;
    sf.roughness = roughness;
    sf.metallic = metallic;

    int analyticCount = min(int(pc.lightData->analyticLightCount), lightCount);

    if (analyticCount > 0) {
        // Range-culled uniform NEE
        int inRange = 0;
        int lightIdx = -1;
        for (int pass = 0; pass < 2 && lightIdx < 0; pass++) {
            int target = (pass == 1 && inRange > 0) ? clamp(int(rng0 * float(inRange)), 0, inRange - 1) : 0;
            int seen = 0;
            for (int li = 0; li < analyticCount; li++) {
                LightInfo cand = pc.lightData->lights[li];
                float extent = (cand.type == LIGHT_TYPE_SPHERE) ? cand.right.w : cand.right.w + cand.up.w;
                if (distance(worldPos, cand.position.xyz) - extent >= cand.range) { continue; }
                if (pass == 0) {
                    inRange++;
                } else if (seen++ == target) {
                    lightIdx = li;
                    break;
                }
            }
            if (inRange == 0) { break; }
        }
        if (lightIdx >= 0) {
            LightInfo al = pc.lightData->lights[lightIdx];

            // Sample a point on the light in world space (branch by type)
            float3 lightSamplePos;
            float lightArea;
            if (al.type == LIGHT_TYPE_SPHERE) {
                float radius = al.right.w;
                float z = 1.0 - 2.0 * xi.x;
                float rr = sqrt(max(1.0 - z * z, 0.0));
                float phi = 2.0 * PI * xi.y;
                float3 dir = float3(rr * cos(phi), rr * sin(phi), z);
                lightSamplePos = al.position.xyz + radius * dir;
                lightArea = 4.0 * PI * radius * radius;
            } else if (al.type == LIGHT_TYPE_DISK) {
                float2 d = ConcentricSampleDisk(xi.x, xi.y) * al.right.w;
                lightSamplePos = al.position.xyz + al.right.xyz * d.x + al.up.xyz * d.y;
                lightArea = PI * al.right.w * al.right.w;
            } else {
                lightSamplePos = al.position.xyz
                    + al.right.xyz * (xi.x * 2.0 - 1.0) * al.right.w
                    + al.up.xyz    * (xi.y * 2.0 - 1.0) * al.up.w;
                lightArea = 4.0 * al.right.w * al.up.w;
            }

            directLight = EvalLightPoint(pc, sf, al, lightSamplePos) * float(inRange) * lightArea;
        }
    }

    // Emissive NEE: meshlet by power / dist^2, then a uniform triangle (not the up.w prefix, so this stays independent of ReSTIR's sampling)
    {
        uint emRng = SeedRng(pixel, pc.frameIndex, 0x3E51A7u);
        uint meshCount = uint(pc.lightData->emissiveMeshCount);
        float totalWeight = 0.0;
        float pickedWeight = 0.0;
        uint pickedMeshlet = ~0u;
        float target = 0.0;
        for (int pass = 0; pass < 2 && pickedMeshlet == ~0u; pass++) {
            float running = 0.0;
            for (uint m = 0u; m < meshCount && pickedMeshlet == ~0u; m++) {
                EmissiveMesh mesh = pc.lightData->emissiveMeshes[m];
                if (mesh.meshletCount == 0u || any(worldPos < mesh.aabbMin) || any(worldPos > mesh.aabbMax)) { continue; }
                for (uint c = 0u; c < mesh.meshletCount; c++) {
                    float w = EmissiveMeshletWeight(pc.lightData->emissiveMeshlets[mesh.firstMeshlet + c], worldPos);
                    if (w <= 0.0) { continue; }
                    running += w;
                    if (pass == 1 && (running > target || running >= totalWeight)) {
                        pickedMeshlet = mesh.firstMeshlet + c;
                        pickedWeight = w;
                        break;
                    }
                }
            }
            if (pass == 0) {
                totalWeight = running;
                if (totalWeight <= 0.0) { break; }
                target = HashStep(emRng) * totalWeight;
            }
        }

        if (pickedMeshlet != ~0u) {
            EmissiveMeshlet meshlet = pc.lightData->emissiveMeshlets[pickedMeshlet];
            uint tri = min(uint(HashStep(emRng) * float(meshlet.lightCount)), meshlet.lightCount - 1u);
            LightInfo al = pc.lightData->lights[meshlet.firstLight + tri];
            float su = sqrt(HashStep(emRng));
            float b2 = HashStep(emRng) * su;
            float3 lightSamplePos = al.position.xyz + (1.0 - su) * al.right.xyz + b2 * al.up.xyz;
            float lightArea = 0.5 * length(cross(al.right.xyz, al.up.xyz));
            float invPdf = (totalWeight / pickedWeight) * float(meshlet.lightCount) * lightArea;
            directLight += EvalLightPoint(pc, sf, al, lightSamplePos) * invPdf;
        }
    }

    // Directional (sun) light: stochastic cone-sampled shadow + BRDF, converges to a true soft-shadow reference.
    float3 sunLight = float3(0.0);
    {
        DirectionalLightData dl = pc.lightData->directionalLight;
        float sunIntensity = dl.directionIntensity.w;
        if (sunIntensity > 0.0) {
            float3 toLight = normalize(-dl.directionIntensity.xyz);

            uint sunRng = SeedRng(pixel, pc.frameIndex, 0x5C6E1Fu);
            float su1 = HashStep(sunRng);
            float su2 = HashStep(sunRng);
            float3 L = SampleSunConeDirection(toLight, dl.angularRadius, su1, su2);

            float cosShade = dot(N, L);
            if (cosShade > 0.0) {
                RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];

                RayDesc sunRay;
                sunRay.Origin = shadowOrigin;
                sunRay.Direction = L;
                sunRay.TMin = 0.0;
                sunRay.TMax = 10000.0;

                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
                q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, sunRay);
                RTProceedAlphaTested(q, GTAlphaBuffers(pc));

                if (q.CommittedStatus() == COMMITTED_NOTHING) {
                    float3 H = normalize(V + L);
                    float NDF = D_GGX(N, H, roughness);
                    float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
                    float3 F = F_SCHLICK(V, H, F0);
                    float3 spec = (NDF * G * F) / max(4.0 * nDotV * cosShade, 0.001);

                    float3 kS = F;
                    float3 kD = (1.0 - kS) * (1.0 - metallic);
                    float orenNayar = Diffuse_OrenNayar(cosShade, nDotV, L, V, roughness);
                    float3 diff = kD * albedo.xyz * orenNayar;

                    float3 sunColor = UnpackUnorm4x8(dl.packedColor).rgb;
                    sunLight = (diff + spec) * cosShade * sunIntensity * sunColor;
                }
            }
        }
    }

    float3 newSample = directLight + sunLight + emissiveRGB;
    accum = (accum * count + float4(newSample, 1.0)) / (count + 1.0);
    pc.accumulationBuffer[pixelIdx] = accum;
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(accum.xyz * scene.preExposure, 1.0);
}
