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

    const int M_light = 8;
    const int M_brdf = 1;

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
            if (ReservoirAddSample(accum, r, misWeight, uSelect))
            {
                r.lightIdx = uint(lightIdx);
                r.sampleOffset = EncodeSampleOffset(samplePos, vs);
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
            if (ReservoirAddSample(accum, r, misWeight, uSelect))
            {
                r.lightIdx = uint(bestLightIdx);
                r.sampleOffset = EncodeSampleOffset(samplePos, vs);
            }
        }

        // Finalise W using pHat re-evaluated at the winner
        if (r.lightIdx != ~0u)
        {
            AreaLightData al = pc.lightData->areaLights[r.lightIdx];
            AreaLightVS vs = TransformAreaLight(al, view);
            float3 samplePos = DecodeSamplePos(r, vs);
            AreaLightSample s = EvalAreaLightSample(vs, samplePos, viewPosition, N, al.range);

            if (s.cosShade > 0.0 && s.cosLight > 0.0)
            {
                float3 Hal = normalize(V + s.L);
                float3 Fal = F_SCHLICK(V, Hal, F0);
                float3 kDal = (1.0 - Fal) * (1.0 - li.metallic);
                float3 alColor = UnpackUnorm4x8(al.packedColor).rgb;
                float geomTerm = s.cosLight / max(s.dist * s.dist, 0.0001);
                float3 brdfCos = (kDal * li.albedo.xyz + Fal) * s.cosShade * geomTerm * al.intensity * alColor * s.attenuation;
                float pHatWinner = dot(brdfCos, float3(0.2126, 0.7152, 0.0722));
                ReservoirFinalise(r, accum, pHatWinner);
            }
        }
    }

    pc.reservoirBuffer[pixel.y * pc.renderExtent.x + pixel.x] = r;
}
