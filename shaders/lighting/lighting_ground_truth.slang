import common_interop;
import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import restir_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;
import restir_common;

import bindless_declarations;

[numthreads(16, 16, 1)]
void ComputeMain(uniform LightingResolvePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        rdgStorageFloat4[pc.outputImageIndex][pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, ~0u, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        rdgStorageFloat4[pc.outputImageIndex][pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);

    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    int areaLightCount = pc.lightData->areaLightCount;

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;
    float4 accum = pc.accumulationBuffer[pixelIdx];
    float count = float(pc.accumulationCount);

    constexpr int samplesPerFrame = 4;
    for (int s = 0; s < samplesPerFrame; ++s) {
        uint seed = pixel.y * pc.renderExtent.x + pixel.x + (pc.frameIndex * uint(samplesPerFrame) + uint(s)) * 1973904141u;
        seed = seed ^ (seed >> 16);
        seed *= 0x45d9f3bu;
        seed = seed ^ (seed >> 16);
        float rng = float(seed & 0x00FFFFFFu) / float(0x01000000u);

        float3 directLight = float3(0.0);

        if (areaLightCount > 0) {
            int lightIdx = int(rng * float(areaLightCount));
            lightIdx = clamp(lightIdx, 0, areaLightCount - 1);

            AreaLightData al = pc.lightData->areaLights[lightIdx];
            AreaLightVS vs = TransformAreaLight(al, scene.view);

            uint seed2 = seed * 2654435761u;
            seed2 = seed2 ^ (seed2 >> 16);
            float2 xi = float2(
                float(seed2 & 0x00FFFFFFu) / float(0x01000000u),
                float((seed2 >> 8) & 0x00FFFFFFu) / float(0x01000000u)
            );
            float3 yStar = vs.center + vs.right * (xi.x * 2.0 - 1.0) * vs.halfWidth + vs.up * (xi.y * 2.0 - 1.0) * vs.halfHeight;

            AreaLightSample ls = EvalAreaLightSample(vs, yStar, viewPosition, N, al.range);

            if (ls.cosShade > 0.0 && ls.cosLight > 0.0 && ls.attenuation > 0.0) {
                float3 L = normalize(yStar - viewPosition);
                float3 H = normalize(V + L);

                float NDF = D_GGX(N, H, roughness);
                float G = G_SMITH_HEIGHT_CORRELATED(N, V, L, roughness);
                float3 F = F_SCHLICK(V, H, F0);
                float3 specular = (NDF * G * F) / max(4.0 * nDotV * max(dot(N, L), 0.0), 0.001);

                float3 kS = F;
                float3 kD = (1.0 - kS) * (1.0 - li.metallic);
                float orenNayar = Diffuse_OrenNayar(ls.cosShade, nDotV, L, V, roughness);
                float3 diffuse = kD * li.albedo.xyz * orenNayar;

                float geomTerm = ls.cosLight / max(ls.dist * ls.dist, 0.0001);
                float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                float3 f = (diffuse + specular) * ls.cosShade * geomTerm * al.intensity * alColor * ls.attenuation;

                directLight = f * float(areaLightCount) * vs.area;
            }
        }

        float3 newSample = directLight + li.emissiveRGB;
        accum = (accum * count + float4(newSample, 1.0)) / (count + 1.0);
        count += 1.0;
    }

    pc.accumulationBuffer[pixelIdx] = accum;
    rdgStorageFloat4[pc.outputImageIndex][pixel] = float4(accum.xyz, 1.0);
}
