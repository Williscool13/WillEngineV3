import common_interop;
import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import restir_interop;
import reflection_interop;
import instance_mask_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;

import bindless_declarations;
import restir_common;
import random_common;
import regir_hash_common;
import restir_light_proposal_common;

#include "restir_features_macros.h"

// Generic over the candidate proposal (ReGIR grid or WorldGrid strongest-K bin); the two backends differ only in Acquire/Draw/Marginal.
void ReSTIRDIBase<P : ILightProposal>(inout P prop, ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID) {
    uint field = pc.activeCheckerboardField;
    uint stride = RestirCheckerboardStride(pc.renderExtent.x, field);
    uint2 reservoirCoord = threadID.xy;
    if (reservoirCoord.x >= stride || reservoirCoord.y >= pc.renderExtent.y) {
        return;
    }
    uint2 pixel = ReservoirPosToPixelPos(reservoirCoord, field);
    if (pixel.x >= pc.renderExtent.x) {
        // Odd-width phantom column: past extent but still in the aliased buffer; keep initialized.
        pc.outputBuffer[reservoirCoord.y * stride + reservoirCoord.x] = ReservoirEmpty();
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 fullPixel = pixel;

    uint pixelIdx = reservoirCoord.y * stride + reservoirCoord.x;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    if (depth <= 0.0) {
        pc.outputBuffer[pixelIdx] = ReservoirEmpty();
        return;
    }

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));
    LightingInputs li = ReconstructLightingInputs(fullPixel, gt1, pc.gbufferTwoIndex);

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float worldTexelSize = RayOriginPixelFootprint(viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(N, V)));
    float roughness = max(li.roughness, RESTIR_MIN_ROUGHNESS);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

    int mBrdf = (roughness <= pc.brdfRoughnessMax) ? RESTIR_M_BRDF : 0;

    int lightCount = pc.lightData->lightCount;

    Reservoir r_gen = ReservoirEmpty();
    ReservoirAccum accum_gen = ReservoirAccumEmpty();

    uint h = (pixel.x * 1973u) ^ (pixel.y * 9277u) ^ (pc.frameIndex * 2654435761u);
    h ^= h >> 17u; h *= 0xbf324c81u; h ^= h >> 13u;

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 cameraPos = scene.cameraWorldPos.xyz;
    float camDist = distance(worldPos, cameraPos);

    prop.Acquire(worldPos, cameraPos, scene.view, camDist, h);

    if (lightCount > 0) {
        float invLightCount = 1.0 / float(lightCount);
        for (int i = 0; i < RESTIR_M_LIGHT; i++) {
            uint candLightIdx = ~0u;
            float3 candXi = float3(0.0);
            LightView candVS = (LightView)0;
            float w_i = 0.0;

            int lightIdx;
            float pSelect;
            if (!prop.Draw(uint(i), uint(RESTIR_M_LIGHT), lightCount, h, lightIdx, pSelect)) {
                float u = (float(i) + HashStep(h)) / float(RESTIR_M_LIGHT);
                lightIdx = clamp(int(u * float(lightCount)), 0, lightCount - 1);
                pSelect = invLightCount;
            }

            if (lightIdx >= 0) {
                LightView vs = UnpackLightVS(pc.lightVS[lightIdx]);
                float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
                if (pSelect > 0.0) {
                    float2 xiLight;
                    if (i == 0) {
                        xiLight = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 3u);
                    } else {
                        xiLight = float2(HashStep(h), HashStep(h));
                    }
                    float3 Xi = SampleLightSurface(vs, viewPosition, xiLight.x, xiLight.y);
                    LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);
                    float p_hat = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lightColor, vs.intensity, pc.lightSpecularFromReflectionsMax);
                    float p_light_area = pSelect * LightSamplePdfArea(vs, viewPosition, Xi);
                    float p_brdf_sa = GGXPdf(N, V, s.L, roughness);
                    // Same d2 floor as LightGeometryTerm so the MIS denominator stays consistent with p_hat at contact distances.
                    float p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sa * s.cosLight / max(s.dist * s.dist, LIGHT_GEOM_MIN_DIST2)) : 0.0;
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
            // The STBN sequence yields one well-placed pair per pixel per frame; extra rays stay white.
            float2 xiBrdf;
            if (i == 0) {
                xiBrdf = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 2u);
            } else {
                xiBrdf = float2(HashStep(h), HashStep(h));
            }
            float uSelect = HashStep(h);

            float p_brdf_sample;
            float3 brdfL = ImportanceSampleGGX(xiBrdf, N, V, roughness, p_brdf_sample);

            float p_hat_Xi = 0.0;
            int bestLightIdx = -1;
            float3 Xi = float3(0.0);
            float p_light_area = 0.0;
            float p_brdf_area = 0.0;
            LightView bestVS = (LightView)0;

            if (dot(N, brdfL) > 0.0) {
                int hitLightIdx = -1;
                float hitT = 0.0;
                bool bTriHit = false;
                float3 triXi = float3(0.0);
                LightView hitVS = (LightView)0;
#if RESTIR_ENABLE_BRDF_RAY
                float3 worldN = normalize(mul(float3x3(scene.invView), N));
                RayDesc ray;
                ray.Origin = ReflectionRayOrigin(worldPos, worldN, viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(N, V)));
                ray.Direction = normalize(mul(float3x3(scene.invView), brdfL));
                ray.TMin = 0.0;
                ray.TMax = 1e30;
                RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
                q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_ALL, ray);
                q.Proceed();

                bool bReflEligible = roughness <= pc.reflectionRoughnessMax;
                if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                    Instance hitInst = pc.instanceBuffer[q.CommittedInstanceID()];
                    bool bOwnedByReSTIR = false;
                    if (hitInst.emissiveTriLightBase != ~0u) {
                        uint idx = hitInst.emissiveTriLightBase + q.CommittedPrimitiveIndex();
                        if (idx < uint(lightCount)) {
                            hitLightIdx = int(idx);
                            hitVS = UnpackLightVS(pc.lightVS[idx]);
                            float2 bary = q.CommittedTriangleBarycentrics();
                            // Exact view-space hit from barycentrics avoids the origin-offset t error of a re-intersection
                            triXi = hitVS.center + bary.x * hitVS.right + bary.y * hitVS.up;
                            bTriHit = true;
                        }
                        bOwnedByReSTIR = true;
                    } else {
                        uint hl = hitInst.lightIndex;
                        if (hl != ~0u && hl < uint(lightCount)) {
                            LightView lvs = UnpackLightVS(pc.lightVS[hl]);
                            float t;
                            if (RayIntersectLight(lvs, viewPosition, brdfL, t)) {
                                hitLightIdx = int(hl);
                                hitT = t;
                                hitVS = lvs;
                                bOwnedByReSTIR = true;
                            }
                        }
                    }

                    // Reflections piggyback this already-traced ray
                    if (bReflEligible) {
                        ReflectionHitDescriptor refl;
                        refl.rayDir = ray.Direction;
                        // Defer specular from local lights to the traced reflection pass across its whole eligible roughness range
                        if (bOwnedByReSTIR && roughness > pc.lightSpecularFromReflectionsMax) {
                            refl.hitT = REFLECTION_HIT_T_OWNED;
                            refl.bary = float2(0.0);
                            refl.instanceID = ~0u;
                            refl.primIndex = ~0u;
                        } else {
                            refl.hitT = q.CommittedRayT();
                            refl.bary = q.CommittedTriangleBarycentrics();
                            refl.instanceID = q.CommittedInstanceID();
                            refl.primIndex = q.CommittedPrimitiveIndex();
                        }
                        pc.reflectionDescriptors[pixelIdx] = refl;
                    }
                } else if (bReflEligible) {
                    ReflectionHitDescriptor refl;
                    refl.rayDir = ray.Direction;
                    refl.hitT = REFLECTION_HIT_T_SKY;
                    refl.bary = float2(0.0);
                    refl.instanceID = ~0u;
                    refl.primIndex = ~0u;
                    pc.reflectionDescriptors[pixelIdx] = refl;
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
#if RESTIR_ENABLE_BRDF_RAY
                    LightView vs = hitVS;
#else
                    LightView vs = UnpackLightVS(pc.lightVS[hitLightIdx]);
#endif
                    Xi = bTriHit ? triXi : viewPosition + brdfL * hitT;
                    LightSample s = EvalLightSample(vs, Xi, viewPosition, N, vs.range);
                    // Backend-specific marginal light-selection pdf for the BRDF-hit light (ReGIR: cell-average model; Bin: exact top-K membership).
                    float pSelect = prop.Marginal(hitLightIdx, vs, lightCount);
                    p_light_area = pSelect * LightSamplePdfArea(vs, viewPosition, Xi);
                    p_brdf_area = (s.cosLight > 0.0) ? (p_brdf_sample * s.cosLight / max(s.dist * s.dist, LIGHT_GEOM_MIN_DIST2)) : 0.0;
                    float3 brdfLightColor = UnpackUnorm4x8(vs.packedColor).rgb;
                    p_hat_Xi = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, brdfLightColor, vs.intensity, pc.lightSpecularFromReflectionsMax);
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

    if (accum_gen.M > 0) { r_gen.M = 1u; }

    if (r_gen.lightIdx != ~0u) {
        LightView winnerVS = UnpackLightVS(pc.lightVS[r_gen.lightIdx]);
        float3 yStar = DecodeSamplePos(r_gen.sampleOffsetPacked, winnerVS);
        LightSample s = EvalLightSample(winnerVS, yStar, viewPosition, N, winnerVS.range);
        float p_hat_gen = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, UnpackUnorm4x8(winnerVS.packedColor).rgb, winnerVS.intensity, pc.lightSpecularFromReflectionsMax);
        ReservoirFinalize(r_gen, accum_gen, p_hat_gen, 1.0);
#if RESTIR_ENABLE_INITIAL_VISIBILITY
        if (pc.bInitialVisibility != 0u && r_gen.W > 0.0) {
            float3 worldN = normalize(mul(float3x3(scene.invView), N));
            float3 worldyStar = mul(scene.invView, float4(yStar, 1.0)).xyz;
            if (!RestirShadowVisible(rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, worldyStar, worldTexelSize)) { r_gen.W = 0.0; }
        }
#endif
    }

    pc.outputBuffer[pixelIdx] = r_gen;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReSTIRDIBaseReGIR(uniform ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    ReGIRProposal prop;
    prop.hashEntries = pc.hashEntries;
    prop.reservoirs = pc.reservoirs;
    prop.cellData = pc.cellData;
    ReSTIRDIBase(prop, pc, threadID);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReSTIRDIBaseBin(uniform ReSTIRDICombinedTemporalPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    WorldGridProposal prop;
    prop.grid = pc.worldGridBuffer;
    prop.indexList = pc.worldGridIndexList;
    prop.emissiveGrid = pc.worldGridEmissiveGrid;
    prop.emissiveIndexList = pc.worldGridEmissiveIndexList;
    prop.lightData = pc.lightData;
    prop.analyticCount = pc.lightData->analyticLightCount;
    ReSTIRDIBase(prop, pc, threadID);
}
