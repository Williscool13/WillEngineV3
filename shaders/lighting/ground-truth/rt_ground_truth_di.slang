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
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(background, 1.0);
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

    int analyticCount = int(pc.lightData->analyticLightCount);
    int liveCount = int(LiveLightCount(lightCount, analyticCount));

    if (liveCount > 0) {
        // Range-culled uniform NEE
        int inRange = 0;
        int lightIdx = -1;
        for (int pass = 0; pass < 2 && lightIdx < 0; pass++) {
            int target = (pass == 1 && inRange > 0) ? clamp(int(rng0 * float(inRange)), 0, inRange - 1) : 0;
            int seen = 0;
            for (int i = 0; i < liveCount; i++) {
                int li = int(LiveLightIndex(uint(i), analyticCount));
                LightInfo cand = pc.lightData->lights[li];
                float extent;
                if (cand.type == LIGHT_TYPE_SPHERE) {
                    extent = cand.right.w;
                } else if (cand.type == LIGHT_TYPE_TRIANGLE) {
                    extent = max(length(cand.right.xyz), length(cand.up.xyz));
                } else {
                    extent = cand.right.w + cand.up.w;
                }
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
            } else if (al.type == LIGHT_TYPE_TRIANGLE) {
                float su = sqrt(xi.x);
                float b1 = 1.0 - su;
                float b2 = xi.y * su;
                lightSamplePos = al.position.xyz + b1 * al.right.xyz + b2 * al.up.xyz;
                lightArea = 0.5 * length(cross(al.right.xyz, al.up.xyz));
            } else {
                lightSamplePos = al.position.xyz
                    + al.right.xyz * (xi.x * 2.0 - 1.0) * al.right.w
                    + al.up.xyz    * (xi.y * 2.0 - 1.0) * al.up.w;
                lightArea = 4.0 * al.right.w * al.up.w;
            }

            float3 toLight = lightSamplePos - worldPos;
            float lightDist = max(length(toLight), 1e-5);
            float3 L = toLight / lightDist;

            float cosShade = max(dot(N, L), 0.0);
            float cosLight = (al.type == LIGHT_TYPE_SPHERE)
                ? max(dot(normalize(lightSamplePos - al.position.xyz), -L), 0.0)
                : max(dot(-al.normal.xyz, L), 0.0);
            float atten = LightRangeCull(lightDist, al.range);

            if (cosShade > 0.0 && cosLight > 0.0 && atten > 0.0) {
                RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];

                // Distance/TMax measured from the offset origin so the origin's advance toward the light
                // (cosShade * offset) does not eat the far-end margin and false-hit the light's own mesh.
                float3 toLightShadow = lightSamplePos - shadowOrigin;
                float shadowDist = length(toLightShadow);
                RayDesc shadowRay;
                shadowRay.Origin = shadowOrigin;
                shadowRay.Direction = toLightShadow / shadowDist;
                shadowRay.TMin = 0.0;
                shadowRay.TMax = shadowDist - max(0.002, shadowDist * 0.005);

                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
                q.TraceRayInline(tlas, 0, INSTANCE_MASK_OCCLUDER, shadowRay);
                RTProceedAlphaTested(q, GTAlphaBuffers(pc));

                bool visible = (q.CommittedStatus() == COMMITTED_NOTHING);
                if (visible) {
                    float3 H = normalize(V + L);
                    float NDF = D_GGX(N, H, roughness);
                    float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
                    float3 F = F_SCHLICK(V, H, F0);
                    float3 spec = (NDF * G * F) / max(4.0 * nDotV * max(dot(N, L), 0.0), 0.001);

                    float3 kS = F;
                    float3 kD = (1.0 - kS) * (1.0 - metallic);
                    float orenNayar = Diffuse_OrenNayar(cosShade, nDotV, L, V, roughness);
                    float3 diff = kD * albedo.xyz * orenNayar;

                    float geomTerm = LightGeometryTerm(cosLight, lightDist);
                    float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                    directLight = (diff + spec) * cosShade * geomTerm * al.intensity * alColor * atten * float(inRange) * lightArea;
                }
            }
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
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(accum.xyz, 1.0);
}
