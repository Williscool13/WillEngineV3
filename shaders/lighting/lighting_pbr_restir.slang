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
void ComputeMain(uniform LightingResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    LightingDispatchParameters lightParams = pc.lightDispatchBuffer[pc.lightingIndex];
    uint2 pixel = threadID.xy + uint2(lightParams.minX, lightParams.minY);
    if (any(pixel >= uint2(lightParams.maxX, lightParams.maxY))) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(scene.mainRenderTargetSize);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);

    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    float3 directLight = float3(0.0);

    bool bAnyLight = pc.reservoirBuffer != 0;
    if (bAnyLight)
    {
        uint pixelIdx = pixel.y * uint(scene.mainRenderTargetSize.x) + pixel.x;
        Reservoir r = pc.reservoirBuffer[pixelIdx];

        if (r.lightIdx != ~0u && r.W > 0.0 && r.lightIdx < uint(pc.lightData->areaLightCount))
        {
            AreaLightData al = pc.lightData->areaLights[r.lightIdx];
            AreaLightVS vs = TransformAreaLight(al, scene.view);
            float3 yStar = DecodeSamplePos(r, vs);
            AreaLightSample s = EvalAreaLightSample(vs, yStar, viewPosition, N, al.range);

            if (s.cosShade > 0.0 && s.cosLight > 0.0 && s.attenuation > 0.0)
            {
                float3 Lal = normalize(yStar - viewPosition);
                float3 Hal = normalize(V + Lal);

                float NDFal = D_GGX(N, Hal, roughness);
                float Gal = G_SMITH_HEIGHT_CORRELATED(N, V, Lal, roughness);
                float3 Fal = F_SCHLICK(V, Hal, F0);
                float3 specularAl = (NDFal * Gal * Fal) / max(4.0 * nDotV * max(dot(N, Lal), 0.0), 0.001);

                float3 kSal = Fal;
                float3 kDal = (1.0 - kSal) * (1.0 - li.metallic);
                float orenNayarAl = Diffuse_OrenNayar(s.cosShade, nDotV, Lal, V, roughness);
                float3 diffuseAl = kDal * li.albedo.xyz * orenNayarAl;

                float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
                float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                float3 f = (diffuseAl + specularAl) * s.cosShade * geomTerm * al.intensity * alColor * s.attenuation;

                directLight = f * r.W;
            }
        }
    }

    float3 finalColor = directLight + li.emissiveRGB;
    rdgStorageFloat4[pc.outputImageIndex][pixel] = float4(finalColor, 1.0);
}
