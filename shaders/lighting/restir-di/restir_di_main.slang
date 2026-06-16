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

import bindless_declarations;
import restir_common;
import random_common;

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDIGeneratePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 fullPixel = FetchFullPixel(pc.quadSelectionIndex, pixel, pc.pixelScale);

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    if (depth <= 0.0) {
        pc.reservoirBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        pc.reservoirBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    const int M_light = 16;
    const int M_brdf = 1;
    const int M = M_light + M_brdf;

    int lightCount = pc.lightData->lightCount;

    Reservoir r = ReservoirEmpty();

    if (lightCount > 0) {
        uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
        h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;
        ReservoirAccum accum = ReservoirAccumEmpty();

        // Light sampling candidates
        for (int i = 0; i < M_light; i++) {
            float uLight = HashStep(h);
            float uU = HashStep(h);
            float uV = HashStep(h);
            float uSelect = HashStep(h);

            int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
            LightVS vs = UnpackLightVS(pc.lightVS[lightIdx]);
            float3 Xi = SampleLightPos(vs, uU, uV);
            LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);

            float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
            float p_hat_Xi = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lightColor, vs.intensity);


            // Talbot MIS in area measure. p_hat has cosLight/dist² baked in, so all PDFs must be area-measure.
            // p_brdf_area = p_brdf_sa * cosLight / dist²
            float p_light_area = 1.0 / (float(lightCount) * vs.area);
            float p_brdf_sa = GGXPdf(N, V, s.L, roughness);
            float p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sa * s.cosLight / max(s.dist * s.dist, 1e-4)) : 0.0;
            float denom = float(M_light) * p_light_area + float(M_brdf) * p_brdf_area;
            float w_i = (denom > 0.0) ? (p_hat_Xi / denom) : 0.0;
            if (ReservoirAddSample(accum, r, w_i, uSelect))
            {
                r.lightIdx = uint(lightIdx);
                r.sampleOffsetPacked = EncodeSampleOffset(Xi, vs);
            }
        }

        // BRDF candidates
        for (int i = 0; i < M_brdf; i++)
        {
            float uXi0 = HashStep(h);
            float uXi1 = HashStep(h);
            float uSelect = HashStep(h);

            float p_brdf_sample;
            float3 brdfL = ImportanceSampleGGX(float2(uXi0, uXi1), N, V, roughness, p_brdf_sample);

            float p_hat_Xi = 0.0;
            int bestLightIdx = -1;
            float3 Xi = float3(0.0, 0.0, 0.0);
            float p_light_area = 0.0;
            float p_brdf_area = 0.0;
            LightVS bestVS = (LightVS)0;

            if (dot(N, brdfL) > 0.0 && p_brdf_sample > 0.0)
            {
                float bestT = 1e30;
                for (int j = 0; j < lightCount; j++)
                {
                    LightVS vs = UnpackLightVS(pc.lightVS[j]);
                    float t;
                    if (RayIntersectLight(vs, viewPosition, brdfL, t) && t < bestT)
                    {
                        bestT = t;
                        bestLightIdx = j;
                        bestVS = vs;
                    }
                }

                if (bestLightIdx >= 0)
                {
                    Xi = viewPosition + brdfL * bestT;
                    LightSample s = EvalLightSample(bestVS, Xi, viewPosition, N, bestVS.range);
                    // Area-measure PDFs. p_brdf_area = p_brdf_sa * cosLight / dist²
                    p_light_area = 1.0 / (float(lightCount) * bestVS.area);
                    p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sample * s.cosLight / max(s.dist * s.dist, 1e-4)) : 0.0;

                    float3 brdfLightColor = UnpackUnorm4x8(bestVS.packedColor).rgb;
                    p_hat_Xi = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, brdfLightColor, bestVS.intensity);
                }
            }

            // Talbot MIS in area measure (consistent with p_hat which has cosLight/dist² baked in).
            float denom = float(M_light) * p_light_area + float(M_brdf) * p_brdf_area;
            float w_i = (denom > 0.0) ? (p_hat_Xi / denom) : 0.0;
            if (ReservoirAddSample(accum, r, w_i, uSelect) && bestLightIdx >= 0)
            {
                r.lightIdx = uint(bestLightIdx);
                r.sampleOffsetPacked = EncodeSampleOffset(Xi, bestVS);
            }
        }

        r.M = uint(M);
        // Finalise W = wSum / p_hat(y*) with visibility
        if (r.lightIdx != ~0u)
        {
            LightVS vs = UnpackLightVS(pc.lightVS[r.lightIdx]);
            float3 yStar = DecodeSamplePos(r, vs);
            LightSample s = EvalLightSample(vs, yStar, viewPosition, N, vs.range);

            float3 winnerLightColor = UnpackUnorm4x8(vs.packedColor).rgb;
            float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
            float3 worldN = normalize(mul(float3x3(scene.invView), N));
            float3 worldyStar = mul(scene.invView, float4(yStar, 1.0)).xyz;
            RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
            float p_hat_yStar = EvalPHatVisibility(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, winnerLightColor, vs.intensity,
                                                   tlas, worldPos, worldN, worldyStar);
            // M_total = 1: Talbot MIS already absorbs M into each w_i, so no outer M divisor.
            ReservoirFinalise(r, accum, p_hat_yStar, 1.0);
        }
    }

    pc.reservoirBuffer[pixelIdx] = r;
}
