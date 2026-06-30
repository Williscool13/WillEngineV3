import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;

import shader_functions;
import pbr_functions;
import packing_functions;

import bindless_declarations;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeDirectionalLight(uniform DirectionalLightPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    uint2 gt2 = rdgTexturesUInt2[pc.gbufferTwoIndex].Load(int3(pixel, 0));

    float3 viewNormal = UnpackNormalOct16(gt1.r);
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    float3 albedo = UnpackAlbedoRGB8(gt2.r).rgb;

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(viewNormal);
    float3 V = normalize(-viewPosition);
    roughness = max(roughness, 0.045);

    float3 L = normalize(mul((float3x3)scene.view, normalize(-pc.lightData->directionalLight.directionIntensity.xyz)));
    float3 H = normalize(V + L);

    float NDF = D_GGX(N, H, roughness);
    float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = F_SCHLICK(V, H, F0);
    float3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.001);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    float nDotL = max(dot(N, L), 0.0);
    float nDotV = max(dot(N, V), 0.0);
    float orenNayar = Diffuse_OrenNayar(nDotL, nDotV, L, V, roughness);
    float3 diffuse = kD * albedo * orenNayar;

    float visibility;
    if (pc.pixelScale == 1u) {
        visibility = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pixel, 0)).r;
    } else {
        // Joint bilateral upsample of the half-res shadow, guided by this full-res pixel's depth + normal.
        float2 hp = uv * float2(pc.shadowExtent) - 0.5;
        int2 base = int2(floor(hp));
        float2 f = hp - float2(base);
        int2 maxCoord = int2(pc.shadowExtent) - 1;
        float wsum = 0.0;
        float vsum = 0.0;
        float bestW = -1.0;
        float nearest = 1.0;
        [unroll]
        for (int j = 0; j < 4; j++) {
            int2 off = int2(j & 1, j >> 1);
            int2 tp = clamp(base + off, int2(0), maxCoord);
            float bw = (off.x == 0 ? 1.0 - f.x : f.x) * (off.y == 0 ? 1.0 - f.y : f.y);
            float td = rdgTexturesFloat[pc.shadowDepthIndex].Load(int3(tp, 0)).r;
            if (td <= 0.0) { continue; }
            float tVis = rdgTexturesFloat2[pc.shadowIndex].Load(int3(tp, 0)).r;
            float tViewZ = ScreenToViewSpaceDepth(td, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float3 tN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.shadowNormalIndex].Load(int3(tp, 0)).r));
            float rel = abs(tViewZ - viewSpaceDepth) / max(max(tViewZ, viewSpaceDepth), 1e-5);
            float wz = saturate(1.0 - rel / 0.03);
            float wn = pow(max(dot(N, tN), 0.0), 16.0);
            float w = bw * wz * wn + 1e-5;
            vsum += tVis * w;
            wsum += w;
            if (w > bestW) { bestW = w; nearest = tVis; }
        }
        visibility = wsum > 1e-4 ? vsum / wsum : nearest;
    }

    // SIGMA stores shadow as sqrt(visibility); unpack to linear here (binary fallback squares to itself).
    visibility *= visibility;

    float intensity = pc.lightData->directionalLight.directionIntensity.w;
    float3 lightColor = UnpackUnorm4x8(pc.lightData->directionalLight.packedColor).rgb;
    float3 sun = (diffuse + specular) * nDotL * visibility * intensity * lightColor;

    float3 existing = rdgStorageFloat4[pc.outputIndex][pixel].rgb;
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(existing + sun, 1.0);
}
