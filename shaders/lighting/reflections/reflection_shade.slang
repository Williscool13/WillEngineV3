import common_interop;
import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import reflection_interop;
import reflection_probe_interop;
import ddgi_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import shader_constants;
import visibility_lighting_common;
import restir_common;
import ddgi_common;
import hit_shading_common;
import reflection_probe_common;
import random_common;
import binning_common;
import rt_alpha_test_common;
import instance_mask_interop;

import bindless_declarations;

/** Trace passes hit cutout as solid (no geometry buffers there); alpha-test the mirror hit here and finish the ray past any transparent texel. */
static void ResolveCutoutHit(ReflectionShadePushConstant pc, float3 rayOrigin, inout ReflectionHitDescriptor hit)
{
    if (RTAlphaTestPasses(pc.instanceBuffer, pc.primitiveBuffer, pc.materialBuffer, pc.indexBuffer, pc.vertexAttrBuffer, hit.instanceID, hit.primIndex, hit.bary)) {
        return;
    }

    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = hit.rayDir;
    ray.TMin = hit.hitT + max(1e-4, hit.hitT * 1e-4);
    ray.TMax = 1e30;

    RayQuery<> q;
    q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_ALL, ray);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            RTAlphaTestPasses(pc.instanceBuffer, pc.primitiveBuffer, pc.materialBuffer, pc.indexBuffer, pc.vertexAttrBuffer, q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics())) {
            q.CommitNonOpaqueTriangleHit();
        }
    }

    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        hit.hitT = q.CommittedRayT();
        hit.bary = q.CommittedTriangleBarycentrics();
        hit.instanceID = q.CommittedInstanceID();
        hit.primIndex = q.CommittedPrimitiveIndex();
    } else {
        hit.hitT = REFLECTION_HIT_T_SKY;
        hit.bary = float2(0.0);
        hit.instanceID = REFLECTION_INSTANCE_NONE;
        hit.primIndex = ~0u;
    }
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReflectionShade(uniform ReflectionShadePushConstant pc, uint3 threadID: SV_DispatchThreadID)
{
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    uint field = pc.activeCheckerboardField;
    if (field != 0u && !IsActiveCheckerboardPixel(int2(pixel), false, field)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    // Roughness gate before touching gbuffer two; most pixels early-out here.
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    roughness = max(roughness, MIN_ROUGHNESS);
    if (roughness > pc.roughnessMax) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, gt1, pc.gbufferTwoIndex);

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv + scene.jitter * 0.5, viewSpaceDepth);
    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float3 worldN = normalize(mul(float3x3(scene.invView), N));
    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    // Must match the base pass's BRDF ray origin exactly: the descriptor's hitT was measured from this same offset.
    float3 rayOrigin = ReflectionRayOrigin(worldPos, worldN, viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(N, V)));

    uint2 rcoord = PixelPosToReservoirPos(pixel, field);
    uint pixelIdx = ReservoirIndex(rcoord, pc.renderExtent.x, field);
    ReflectionHitDescriptor hit = pc.reflectionDescriptors[pixelIdx];
    if (pc.bAlphaTest != 0u && roughness <= pc.mirrorRoughnessMax && hit.hitT >= 0.0 && hit.instanceID != REFLECTION_INSTANCE_NONE && pc.tlasIndex != ~0u) {
        ResolveCutoutHit(pc, rayOrigin, hit);
    }

    float3 radiance = float3(0.0);
    float hitTOut = 0.0;
    if (hit.hitT >= 0.0) {
        if (hit.instanceID == REFLECTION_INSTANCE_NONE) {
            float3 hitWorldPos = rayOrigin + hit.hitT * hit.rayDir;
            bool bShaded = false;
            if (pc.litHistoryIndex != ~0u) {
                float4 prevClip = mul(scene.prevViewProj, float4(hitWorldPos, 1.0));
                if (prevClip.w > 0.0) {
                    float2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
                    if (all(prevUV >= float2(0.0)) && all(prevUV <= float2(1.0))) {
                        int2 prevPixel = clamp(int2(prevUV * float2(pc.renderExtent)), int2(0, 0), int2(pc.renderExtent) - int2(1, 1));
                        float prevRawDepth = rdgTexturesFloat[pc.depthHistoryIndex].Load(int3(prevPixel, 0)).r;
                        if (prevRawDepth > 0.0) {
                            float prevViewDepth = ScreenToViewSpaceDepth(prevRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                            if (abs(prevViewDepth - prevClip.w) <= 0.03 * prevClip.w + 0.05) {
                                int2 hitCoord = clamp(int2(hit.bary * float2(pc.renderExtent)), int2(0, 0), int2(pc.renderExtent) - int2(1, 1));
                                float3 hitN = normalize(mul(float3x3(scene.invView), normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(hitCoord, 0)).r))));
                                float3 prevWorldN = normalize(mul(transpose(float3x3(scene.prevView)), UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(prevPixel, 0)).r)));
                                if (dot(hitN, prevWorldN) > 0.5) {
                                    radiance = rdgTexturesFloat4[pc.litHistoryIndex].Load(int3(prevPixel, 0)).rgb;
                                    bShaded = true;
                                }
                            }
                        }
                    }
                }
            }
            if (bShaded) {
                hitTOut = hit.hitT;
            } else {
                if (pc.skyboxIndex >= 0) {
                    float3 skyDirection = float3(hit.rayDir.x, -hit.rayDir.y, hit.rayDir.z);
                    radiance = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb * pc.iblIntensity;
                }
                if (pc.reflectionProbeCount > 0u) {
                    ReflectionProbeBlend probeBlend = PickReflectionProbeBinned(worldPos, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, scene.cameraWorldPos.xyz);
                    if (probeBlend.indexA >= 0) {
                        float3 probeRadiance = SampleReflectionProbeSpecularBlended(probeBlend, pc.reflectionProbes, worldPos, hit.rayDir, roughness);
                        radiance = lerp(radiance, probeRadiance, probeBlend.coverage);
                    }
                }
                hitTOut = 10000.0;
            }
        } else {
            Instance inst = pc.instanceBuffer[hit.instanceID];
            uint rng = SeedRng(pixel, pc.frameIndex, 0xA17Fu);

            WorldGridPunctualLights worldGridLights = (WorldGridPunctualLights)0;
            if (pc.worldGridBuffer != nullptr) {
                worldGridLights.view = scene.view;
                worldGridLights.camPos = scene.cameraWorldPos.xyz;
                worldGridLights.grid = pc.worldGridBuffer;
                worldGridLights.indexList = pc.worldGridIndexList;
            }

            float3 hitWorldPos = rayOrigin + hit.hitT * hit.rayDir;

            // Prefer last frame's lit color at the hit (fully shadowed lighting); analytic fallback shades local lights with no visibility.
            bool bShaded = false;
            if (pc.litHistoryIndex != ~0u) {
                float4 prevClip = mul(scene.prevViewProj, float4(hitWorldPos, 1.0));
                if (prevClip.w > 0.0) {
                    float2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
                    if (all(prevUV >= float2(0.0)) && all(prevUV <= float2(1.0))) {
                        int2 prevPixel = clamp(int2(prevUV * float2(pc.renderExtent)), int2(0, 0), int2(pc.renderExtent) - int2(1, 1));
                        float prevRawDepth = rdgTexturesFloat[pc.depthHistoryIndex].Load(int3(prevPixel, 0)).r;
                        if (prevRawDepth > 0.0) {
                            float prevViewDepth = ScreenToViewSpaceDepth(prevRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                            if (abs(prevViewDepth - prevClip.w) <= 0.03 * prevClip.w + 0.05) {
                                float3 hitN = DecodeHitNormal(pc, inst, hit.primIndex, hit.bary);
                                float3 prevWorldN = normalize(mul(transpose(float3x3(scene.prevView)), UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(prevPixel, 0)).r)));
                                if (dot(hitN, prevWorldN) > 0.5) {
                                    radiance = rdgTexturesFloat4[pc.litHistoryIndex].Load(int3(prevPixel, 0)).rgb;
                                    bShaded = true;
                                }
                            }
                        }
                    }
                }
            }
            if (!bShaded) {
                radiance = ShadeReflectionRayHit(pc, inst, hit.primIndex, hit.bary, hitWorldPos, hit.rayDir, rng, worldGridLights, 0.75 * hit.hitT, scene.cameraWorldPos.xyz);
            }
            hitTOut = hit.hitT;
        }
    } else if (hit.hitT == REFLECTION_HIT_T_SKY) {
        if (pc.skyboxIndex >= 0) {
            // Matches the Y flip the visible skybox pass applies when sampling the cubemap.
            float3 skyDirection = float3(hit.rayDir.x, -hit.rayDir.y, hit.rayDir.z);
            radiance = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb * pc.iblIntensity;
        }
        if (pc.reflectionProbeCount > 0u) {
            ReflectionProbeBlend probeBlend = PickReflectionProbeBinned(worldPos, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, scene.cameraWorldPos.xyz);
            if (probeBlend.indexA >= 0) {
                float3 probeRadiance = SampleReflectionProbeSpecularBlended(probeBlend, pc.reflectionProbes, worldPos, hit.rayDir, roughness);
                radiance = lerp(radiance, probeRadiance, probeBlend.coverage);
            }
        }
        hitTOut = 10000.0;
    }
    // hit.hitT == REFLECTION_HIT_T_OWNED (or the NaN clear-sentinel): radiance and hitTOut stay 0, matching the "no hit" convention RELAX's temporal accumulation expects.

    radiance *= pc.intensity;
    if (pc.maxRayIntensity > 0.0) {
        float lum = Luma(radiance);
        if (lum > pc.maxRayIntensity) {
            radiance *= pc.maxRayIntensity / lum;
        }
    }

    // Incoming radiance is already the demodulated form: the outgoing specular is specReflectance * radiance, and every consumer applies that factor at remodulation.
    // Packed (rcoord) when a checkerboard-aware consumer (the merged lighting resolve or the reflection RELAX prepass) will resolve it; otherwise dense-duplicated to both fields so a direct .Load(pixel) fallback (denoiser off, full-rate resolve, or ReBLUR/plain modes) sees no holes. Matches lighting_pbr_restir.slang's writeCoord logic exactly.
    float4 outColor = float4(radiance, hitTOut);
    uint2 writeCoord = (field != 0u && pc.bCheckerboardPacked != 0u) ? rcoord : pixel;
    rdgStorageFloat4[pc.outputIndex][writeCoord] = outColor;
    if (field != 0u && pc.bCheckerboardPacked == 0u) {
        int2 o = OppositeFieldPixel(pixel, field);
        if (o.x >= 0 && o.x < int(pc.renderExtent.x)) {
            rdgStorageFloat4[pc.outputIndex][uint2(o)] = outColor;
        }
    }
}
