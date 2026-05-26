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

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDIGeneratePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        pc.reservoirBuffer[pixel.y * pc.renderExtent.x + pixel.x] = ReservoirEmpty();
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, 0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        pc.reservoirBuffer[pixel.y * pc.renderExtent.x + pixel.x] = ReservoirEmpty();
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

    // M_i: number of candidates per strategy
    const int M_light = 32;
    const int M_brdf = 1;
    const int M = M_light + M_brdf;

    int lightCount = pc.lightData->areaLightCount;

    Reservoir r = ReservoirEmpty();

    if (lightCount > 0) {
        uint h = pixel.x * 1973u ^ pixel.y * 9277u ^ pc.frameIndex * 2654435761u;
        float4x4 view = scene.view;

        ReservoirAccum accum = ReservoirAccumEmpty();

        // Light sampling candidates
        for (int i = 0; i < M_light; i++) {
            float uLight = HashStep(h);
            float uU = HashStep(h);
            float uV = HashStep(h);
            float uSelect = HashStep(h);

            int lightIdx = clamp(int(uLight * float(lightCount)), 0, lightCount - 1);
            AreaLightData al = pc.lightData->areaLights[lightIdx];
            AreaLightVS vs = TransformAreaLight(al, view);
            float3 Xi = SampleAreaLightPos(vs, uU, uV);
            AreaLightSample s = EvalAreaLightSample(vs, Xi, viewPosition, N, al.range);

            // p_hat(Xi): target PDF (integrand without visibility)
            float p_hat_Xi = 0.0;
            if (s.cosShade > 0.0 && s.cosLight > 0.0 && s.attenuation > 0.0)
            {
                float3 H = normalize(V + s.L);
                float3 F = F_SCHLICK(V, H, F0);
                float3 kD = (1.0 - F) * (1.0 - li.metallic);
                float3 lightColor = UnpackUnorm4x8(al.packedColor).rgb;
                float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
                float3 brdfCos = (kD * li.albedo.xyz + F) * s.cosShade * geomTerm * al.intensity * lightColor * s.attenuation;
                p_hat_Xi = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
            }

            // Talbot MIS: w_i = p_hat(Xi) / (M_light * p_light(Xi) + M_brdf * p_brdf(Xi))
            float p_light = 1.0 / (float(lightCount) * vs.area);
            float p_brdf = GGXPdf(N, V, s.L, roughness);
            float denom = float(M_light) * p_light + float(M_brdf) * p_brdf;
            float w_i = (denom > 0.0) ? (p_hat_Xi / denom) : 0.0;
            if (ReservoirAddSample(accum, r, w_i, uSelect))
            {
                r.lightIdx = uint(lightIdx);
                r.sampleOffset = EncodeSampleOffset(Xi, vs);
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
            float p_light = 0.0;
            AreaLightVS bestVS = (AreaLightVS)0;

            if (dot(N, brdfL) > 0.0 && p_brdf_sample > 0.0)
            {
                float bestT = 1e30;
                for (int j = 0; j < lightCount; j++)
                {
                    AreaLightVS vs = TransformAreaLight(pc.lightData->areaLights[j], view);
                    float t;
                    if (RayIntersectQuad(viewPosition, brdfL, vs.center, vs.normal, vs.right, vs.up, vs.halfWidth, vs.halfHeight, t) && t < bestT)
                    {
                        bestT = t;
                        bestLightIdx = j;
                        bestVS = vs;
                    }
                }

                if (bestLightIdx >= 0)
                {
                    AreaLightData al = pc.lightData->areaLights[bestLightIdx];
                    Xi = viewPosition + brdfL * bestT;
                    AreaLightSample s = EvalAreaLightSample(bestVS, Xi, viewPosition, N, al.range);
                    p_light = 1.0 / (float(lightCount) * bestVS.area);

                    // p_hat(Xi): target PDF (integrand without visibility)
                    if (s.cosLight > 0.0 && s.attenuation > 0.0)
                    {
                        float3 H = normalize(V + brdfL);
                        float3 F = F_SCHLICK(V, H, F0);
                        float3 kD = (1.0 - F) * (1.0 - li.metallic);
                        float3 lightColor = UnpackUnorm4x8(al.packedColor).rgb;
                        float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
                        float3 brdfCos = (kD * li.albedo.xyz + F) * s.cosShade * geomTerm * al.intensity * lightColor * s.attenuation;
                        p_hat_Xi = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
                    }
                }
            }

            // Talbot MIS: w_i = p_hat(Xi) / (M_light * p_light(Xi) + M_brdf * p_brdf(Xi))
            float denom = float(M_light) * p_light + float(M_brdf) * p_brdf_sample;
            float w_i = (denom > 0.0) ? (p_hat_Xi / denom) : 0.0;
            if (ReservoirAddSample(accum, r, w_i, uSelect) && bestLightIdx >= 0)
            {
                r.lightIdx = uint(bestLightIdx);
                r.sampleOffset = EncodeSampleOffset(Xi, bestVS);
            }
        }

        // Finalise W = wSum / p_hat(y*)
        if (r.lightIdx != ~0u)
        {
            AreaLightData al = pc.lightData->areaLights[r.lightIdx];
            AreaLightVS vs = TransformAreaLight(al, view);
            float3 yStar = DecodeSamplePos(r, vs);
            AreaLightSample s = EvalAreaLightSample(vs, yStar, viewPosition, N, al.range);

            if (s.cosShade > 0.0 && s.cosLight > 0.0)
            {
                float3 H = normalize(V + s.L);
                float3 F = F_SCHLICK(V, H, F0);
                float3 kD = (1.0 - F) * (1.0 - li.metallic);
                float3 lightColor = UnpackUnorm4x8(al.packedColor).rgb;
                float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
                float3 brdfCos = (kD * li.albedo.xyz + F) * s.cosShade * geomTerm * al.intensity * lightColor * s.attenuation;
                float p_hat_yStar = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
                ReservoirFinalise(r, accum, p_hat_yStar);
            }
        }
    }

    pc.reservoirBuffer[pixel.y * pc.renderExtent.x + pixel.x] = r;
}
