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
import regir_hash;

#include "restir_features_macros.slang"

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 fullPixel = FetchFullPixel(pc.quadSelectionIndex, pixel, pc.pixelScale);

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    if (depth <= 0.0) {
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float worldTexelSize = RestirWorldTexelSize(viewSpaceDepth, scene.ndcToViewMulXPixelSize, pc.pixelScale);
    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    int mBrdf = (roughness <= RESTIR_BRDF_ROUGHNESS_MAX) ? RESTIR_M_BRDF : 0;

    int lightCount = pc.lightData->lightCount;

    Reservoir r_gen = ReservoirEmpty();
    ReservoirAccum accum_gen = ReservoirAccumEmpty();

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 cameraPos = scene.cameraWorldPos.xyz;
    float camDist = distance(worldPos, cameraPos);

    bool useGrid = false;
    uint gridBase = 0u;
    float3 cellCenterVS = float3(0.0);
    float cellTargetSum = 0.0;
    float cellOccupancy = 0.0;
    if (camDist <= REGIR_MAX_DIST) {
        float regirJitter = ReGIRCellSizeAtLevel(ReGIRLevelFromDistance(camDist));
        float3 jitter = (float3(HashStep(h), HashStep(h), HashStep(h)) - 0.5) * regirJitter;

        [unroll]
        for (int attempt = 0; attempt < 2 && !useGrid; attempt++)
        {
            float3 queryPos = (attempt == 0) ? (worldPos + jitter) : worldPos;
            ReGIRHashKey key = ReGIRComputeKey(queryPos, cameraPos);
            uint cellSlot = ReGIRHashFind(pc.hashEntries, key);
            if (cellSlot != REGIR_HASH_INVALID) {
                float2 cd = pc.cellData[cellSlot];
                if (cd.x > 0.0) {
                    cellTargetSum = cd.x;
                    gridBase = ReGIRCellReservoirBase(cellSlot);
                    cellCenterVS = mul(scene.view, float4(ReGIRCellCenterFromKey(key), 1.0)).xyz;
                    cellOccupancy = cd.y;
                    useGrid = true;
                }
            }
        }
    }

    if (lightCount > 0) {
        float invLightCount = 1.0 / float(lightCount);
        for (int i = 0; i < RESTIR_M_LIGHT; i++) {
            uint candLightIdx = ~0u;
            float3 candXi = float3(0.0);
            LightVS candVS = (LightVS)0;
            float w_i = 0.0;

            int lightIdx = -1;
            float pSelect = 0.0;
            if (useGrid) {
                uint slotInCell = min(uint(HashStep(h) * float(REGIR_RESERVOIRS_PER_CELL)), REGIR_RESERVOIRS_PER_CELL - 1u);
                ReGIRReservoir gr = pc.reservoirs[gridBase + slotInCell];
                if (gr.lightIdx != ~0u && gr.lightIdx < uint(lightCount) && gr.totalWeight > 0.0) {
                    lightIdx = int(gr.lightIdx);
                    pSelect = gr.targetPdf / gr.totalWeight;
                } else {
                    lightIdx = clamp(int(HashStep(h) * float(lightCount)), 0, lightCount - 1);
                    pSelect = invLightCount;
                }
            } else {
                lightIdx = clamp(int(HashStep(h) * float(lightCount)), 0, lightCount - 1);
                pSelect = invLightCount;
            }

            if (lightIdx >= 0) {
                LightVS vs = UnpackLightVS(pc.lightVS[lightIdx]);
                float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
                if (pSelect > 0.0) {
                    float uU = HashStep(h);
                    float uV = HashStep(h);
                    float3 Xi = SampleLightSurface(vs, viewPosition, uU, uV);
                    LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);
                    float p_hat = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lightColor, vs.intensity);
                    float p_light_area = pSelect * LightSamplePdfArea(vs, viewPosition, Xi);
                    float p_brdf_sa = GGXPdf(N, V, s.L, roughness);
                    float p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sa * s.cosLight / max(s.dist * s.dist, 1e-4)) : 0.0;
                    float denom = float(RESTIR_M_LIGHT) * p_light_area + float(mBrdf) * p_brdf_area;
                    w_i = (denom > 0.0) ? (p_hat / denom) : 0.0;
                    candLightIdx = uint(lightIdx);
                    candXi = Xi;
                    candVS = vs;
                }
            }

            float uSelect = HashStep(h);
            if (ReservoirAddSample(accum_gen, w_i, uSelect) && candLightIdx != ~0u) {
                r_gen.lightIdx = candLightIdx;
                r_gen.sampleOffsetPacked = EncodeSampleOffset(candXi, candVS);
            }
        }

        for (int i = 0; i < mBrdf; i++) {
            float uXi0 = HashStep(h);
            float uXi1 = HashStep(h);
            float uSelect = HashStep(h);

            float p_brdf_sample;
            float3 brdfL = ImportanceSampleGGX(float2(uXi0, uXi1), N, V, roughness, p_brdf_sample);

            float p_hat_Xi = 0.0;
            int bestLightIdx = -1;
            float3 Xi = float3(0.0);
            float p_light_area = 0.0;
            float p_brdf_area = 0.0;
            LightVS bestVS = (LightVS)0;

            if (dot(N, brdfL) > 0.0 && p_brdf_sample > 0.0) {
                int hitLightIdx = -1;
                float hitT = 0.0;
#if RESTIR_ENABLE_BRDF_RAY
                float3 worldN = normalize(mul(float3x3(scene.invView), N));
                RayDesc ray;
                ray.Origin = worldPos + worldN * 0.05;
                ray.Direction = normalize(mul(float3x3(scene.invView), brdfL));
                ray.TMin = 0.001;
                ray.TMax = 1e30;
                RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
                q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, 0xFF, ray);
                q.Proceed();
                if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                    uint hl = pc.instanceBuffer[q.CommittedInstanceID()].lightIndex;
                    float t;
                    if (hl != ~0u && hl < uint(lightCount) && RayIntersectLight(UnpackLightVS(pc.lightVS[hl]), viewPosition, brdfL, t)) {
                        hitLightIdx = int(hl);
                        hitT = t;
                    }
                }
#else
                float nearestT = 1e30;
                for (int li2 = 0; li2 < lightCount; li2++) {
                    float t;
                    if (RayIntersectLight(UnpackLightVS(pc.lightVS[li2]), viewPosition, brdfL, t) && t < nearestT) {
                        nearestT = t;
                        hitLightIdx = li2;
                    }
                }
                hitT = nearestT;
#endif
                if (hitLightIdx >= 0) {
                    LightVS vs = UnpackLightVS(pc.lightVS[hitLightIdx]);
                    Xi = viewPosition + brdfL * hitT;
                    LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);
                    float pSelect = useGrid ? (cellOccupancy * (EvalCellLightTarget(vs, cellCenterVS, UnpackUnorm4x8(vs.packedColor).rgb) / cellTargetSum) + (1.0 - cellOccupancy) * (1.0 / float(lightCount))) : (1.0 / float(lightCount));
                    p_light_area = pSelect * LightSamplePdfArea(vs, viewPosition, Xi);
                    p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sample * s.cosLight / max(s.dist * s.dist, 1e-4)) : 0.0;
                    float3 brdfLightColor = UnpackUnorm4x8(vs.packedColor).rgb;
                    p_hat_Xi = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, brdfLightColor, vs.intensity);
                    bestLightIdx = hitLightIdx;
                    bestVS = vs;
                }
            }

            float denom = float(RESTIR_M_LIGHT) * p_light_area + float(mBrdf) * p_brdf_area;
            float w_i = (denom > 0.0) ? (p_hat_Xi / denom) : 0.0;
            if (ReservoirAddSample(accum_gen, w_i, uSelect) && bestLightIdx >= 0) {
                r_gen.lightIdx = uint(bestLightIdx);
                r_gen.sampleOffsetPacked = EncodeSampleOffset(Xi, bestVS);
            }
        }

    }

    if (pc.lightData->directionalLight.directionIntensity.w > 0.0) {
        DirectionalLightData sun = pc.lightData->directionalLight;
        float su1 = HashStep(h);
        float su2 = HashStep(h);
        float p_cone;
        float3 sunDirWorld = SampleSunConeDirection(SunDirectionWorld(sun), sun.angularRadius, su1, su2, p_cone);
        float3 sunDirView = normalize(mul((float3x3)scene.view, sunDirWorld));
        float p_hat_sun = EvalSunPHat(sunDirView, V, N, li.albedo.xyz, F0, li.metallic, roughness, SunRadiance(sun));
        float w_sun = (p_cone > 0.0) ? (p_hat_sun / p_cone) : 0.0;
        if (ReservoirAddSample(accum_gen, w_sun, HashStep(h))) {
            r_gen.lightIdx = LIGHT_IDX_SUN;
            r_gen.sampleOffsetPacked = PackNormalOct16(sunDirWorld);
        }
    }

    if (accum_gen.M > 0) { r_gen.M = 1u; }

    if (r_gen.lightIdx != ~0u) {
        float p_hat_gen = EvalReservoirPHat(r_gen.lightIdx, r_gen.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, pc.lightData->directionalLight, pc.lightVS);
        ReservoirFinalise(r_gen, accum_gen, p_hat_gen, 1.0);

#if RESTIR_ENABLE_INITIAL_VISIBILITY
        if (pc.bInitialVisibility != 0u) {
            float3 worldN = normalize(mul(float3x3(scene.invView), N));
            float p_hat_vis = EvalReservoirPHatVisibility(r_gen.lightIdx, r_gen.sampleOffsetPacked, viewPosition, N, V, li.albedo.xyz, F0, li.metallic, roughness, scene.view, scene.invView, pc.lightData->directionalLight, pc.lightVS, rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldTexelSize);
            if (p_hat_vis <= 0.0) { r_gen.W = 0.0; }
        }
#endif
    }

    pc.outputBuffer[pixelIdx] = r_gen;
}
