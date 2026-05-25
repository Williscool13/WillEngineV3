import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;

import bindless_declarations;
import restir_common;

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

    float2 uv = (float2(pixel) + 0.5) / float2(pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData[pc.sceneDataIndex].depthLinearizeMult, pc.sceneData[pc.sceneDataIndex].depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData[pc.sceneDataIndex].ndcToViewMul, pc.sceneData[pc.sceneDataIndex].ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);

    // Clamp roughness so D_GGX never produces 0/0 at NdotH=1 (degenerate delta lobe)
    float roughness = max(li.roughness, 0.045);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    float gtao = li.shadows.y;

    float3 directLight = float3(0.0, 0.0, 0.0);

    // Basic ReSTIR DI
    {
        const int M_light = 32;
        const int M_brdf = 1;
        const int M_total = M_light + M_brdf;
        int lightCount = pc.lightData->areaLightCount;
        if (lightCount > 0) {
            uint h = pixel.x * 1973u ^ pixel.y * 9277u ^ pc.frameIndex * 2654435761u;
            float4x4 view = pc.sceneData[pc.sceneDataIndex].view;

            Reservoir r = ReservoirEmpty();
            ReservoirSample winner = { -1, float3(0.0, 0.0, 0.0), 0.0 };

            // Light sampling candidates
            for (int i = 0; i < M_light; i++) {
                float uLight = HashStep(h);
                float uU = HashStep(h);
                float uV = HashStep(h);
                float uSelect = HashStep(h);

                int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
                AreaLightData al = pc.lightData->areaLights[lightIdx];
                AreaLightVS vs = TransformAreaLight(al, view);
                float3 samplePos = SampleAreaLightPos(vs, uU, uV);
                AreaLightSample s = EvalAreaLightSample(vs, samplePos, viewPosition, N, al.range);

                float pHat = 0.0;
                if (s.cosShade > 0.0 && s.cosLight > 0.0 && s.attenuation > 0.0)
                {
                    float3 Hal = normalize(V + s.L);
                    float3 Fal = F_SCHLICK(V, Hal, F0);
                    float3 kDal = (1.0 - Fal) * (1.0 - li.metallic);
                    float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                    float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
                    float3 brdfCos = (kDal * li.albedo.xyz + Fal) * s.cosShade * geomTerm * al.intensity * alColor * s.attenuation;
                    pHat = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
                }

                float pLight = 1.0 / (float(lightCount) * vs.area);
                float pBrdf = GGXPdf(N, V, s.L, roughness);
                float misWeight = pHat / (float(M_light) * pLight + float(M_brdf) * pBrdf);
                if (ReservoirAddSample(r, i, misWeight, uSelect))
                {
                    winner = { lightIdx, s.samplePos, s.attenuation };
                }
            }

            // BRDF candidates
            for (int i = 0; i < M_brdf; i++)
            {
                float uXi0 = HashStep(h);
                float uXi1 = HashStep(h);
                float uSelect = HashStep(h);

                float brdfPdf;
                float3 brdfL = ImportanceSampleGGX(float2(uXi0, uXi1), N, V, roughness, brdfPdf);

                if (dot(N, brdfL) <= 0.0 || brdfPdf <= 0.0) { continue; }

                // Brute-force search: find the closest area light hit along brdfL
                float bestT = 1e30;
                int bestLightIdx = -1;

                for (int j = 0; j < lightCount; j++)
                {
                    AreaLightVS vs = TransformAreaLight(pc.lightData->areaLights[j], view);
                    float t;
                    if (RayIntersectQuad(viewPosition, brdfL, vs.center, vs.normal, vs.right, vs.up, vs.halfWidth, vs.halfHeight, t) && t < bestT)
                    {
                        bestT = t;
                        bestLightIdx = j;
                    }
                }

                if (bestLightIdx < 0) { continue; }

                AreaLightData al = pc.lightData->areaLights[bestLightIdx];
                AreaLightVS vs = TransformAreaLight(al, view);
                float3 samplePos = viewPosition + brdfL * bestT;
                AreaLightSample s = EvalAreaLightSample(vs, samplePos, viewPosition, N, al.range);

                float pHat = 0.0;
                if (s.cosLight > 0.0 && s.attenuation > 0.0)
                {
                    float3 Hal = normalize(V + brdfL);
                    float3 Fal = F_SCHLICK(V, Hal, F0);
                    float3 kDal = (1.0 - Fal) * (1.0 - li.metallic);
                    float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                    float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
                    float3 brdfCos = (kDal * li.albedo.xyz + Fal) * s.cosShade * geomTerm * al.intensity * alColor * s.attenuation;
                    pHat = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
                }

                float pLight = 1.0 / (float(lightCount) * vs.area);
                float misWeight = pHat / (float(M_light) * pLight + float(M_brdf) * brdfPdf);
                if (ReservoirAddSample(r, M_light + i, misWeight, uSelect))
                {
                    winner = { bestLightIdx, samplePos, s.attenuation };
                }
            }

            if (r.y >= 0 && winner.lightIdx >= 0)
            {
                AreaLightData al = pc.lightData->areaLights[winner.lightIdx];
                AreaLightVS vs = TransformAreaLight(al, view);
                AreaLightSample s = EvalAreaLightSample(vs, winner.samplePos, viewPosition, N, al.range);

                if (s.cosShade > 0.0 && s.cosLight > 0.0)
                {
                    float3 Hal = normalize(V + s.L);
                    float NDFal = D_GGX(N, Hal, roughness);
                    float Gal   = G_SMITH_HEIGHT_CORRELATED(N, V, s.L, roughness);
                    float3 Fal  = F_SCHLICK(V, Hal, F0);
                    float3 specularAl = (NDFal * Gal * Fal) / max(4.0 * nDotV * s.cosShade, 0.001);
                    float3 kDal = (1.0 - Fal) * (1.0 - li.metallic);
                    float orenNayarAl = Diffuse_OrenNayar(s.cosShade, nDotV, s.L, V, roughness);
                    float3 diffuseAl = kDal * li.albedo.xyz * orenNayarAl;
                    float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;

                    float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
                    float3 brdfCos = (kDal * li.albedo.xyz + Fal) * s.cosShade * geomTerm * al.intensity * alColor * winner.attenuation;
                    float pHatWinner = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
                    ReservoirFinalise(r, pHatWinner);

                    directLight = (diffuseAl + specularAl) * geomTerm * al.intensity * alColor * winner.attenuation * r.W;
                }
            }
        }
    }

    float3 finalColor = directLight + li.emissiveRGB;
    rdgStorageFloat4[pc.outputImageIndex][pixel] = float4(finalColor, 1.0);
}
