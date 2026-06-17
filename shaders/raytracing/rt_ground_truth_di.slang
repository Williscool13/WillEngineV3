import common_interop;
import push_constant_interop;
import lights_interop;
import bindless_declarations;
import shader_constants;

import shader_functions;
import pbr_functions;
import packing_functions;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dtid : SV_DispatchThreadID, uniform RTGroundTruthDIPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float3 worldPos = ReconstructWorldPosition(scene.invViewProj, uv, depth);
    float3 cameraPos = scene.cameraWorldPos.xyz;

    // Unpack gbuffer
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    uint2 gt2 = rdgTexturesUInt2[pc.gbufferTwoIndex].Load(int3(pixel, 0));

    float3 viewNormal = UnpackNormalOct16(gt1.r);
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    float4 albedo = UnpackAlbedoRGB8(gt2.r);
    float3 emissiveRGB = UnpackEmissiveRGBE(gt2.g);
    roughness = max(roughness, 0.045);

    // Transform view normal to world space
    float3 N = normalize(mul(float3x3(scene.invView), viewNormal));
    float3 V = normalize(cameraPos - worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.xyz, metallic);
    float nDotV = max(dot(N, V), 0.0);

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

    if (lightCount > 0) {
        int lightIdx = clamp(int(rng0 * float(lightCount)), 0, lightCount - 1);
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
        float atten = LightDistanceFalloff(lightDist, al.range);

        if (cosShade > 0.0 && cosLight > 0.0 && atten > 0.0) {
            RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];

            // Distance/TMax measured from the offset origin so the origin's advance toward the light
            // (cosShade * offset) does not eat the far-end margin and false-hit the light's own mesh.
            float3 rayOrigin = worldPos + N * 0.05;
            float3 toLightShadow = lightSamplePos - rayOrigin;
            float shadowDist = length(toLightShadow);
            RayDesc shadowRay;
            shadowRay.Origin = rayOrigin;
            shadowRay.Direction = toLightShadow / shadowDist;
            shadowRay.TMin = 0.001;
            shadowRay.TMax = shadowDist - 0.05;

            RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
            q.TraceRayInline(tlas, 0, 0xFF, shadowRay);
            q.Proceed();

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
                directLight = (diff + spec) * cosShade * geomTerm * al.intensity * alColor * atten * float(lightCount) * lightArea;
            }
        }
    }

    float3 newSample = directLight + emissiveRGB;
    accum = (accum * count + float4(newSample, 1.0)) / (count + 1.0);
    pc.accumulationBuffer[pixelIdx] = accum;
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(accum.xyz, 1.0);
}
