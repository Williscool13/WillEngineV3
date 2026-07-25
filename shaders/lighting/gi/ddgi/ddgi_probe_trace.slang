import model_interop;
import push_constant_interop;
import lights_interop;
import ddgi_interop;
import instance_mask_interop;
import ddgi_common;
import bindless_declarations;
import constants_interop;
import shader_constants;
import shader_functions;
import packing_functions;
import random_common;
import hit_shading_common;
import radiance_cache_hash_common;
import radiance_cache_interop;
import binning_common;
import common_interop;

// =============== Shader Summary ============
// DDGI probe trace: one thread per probe ray against the scene TLAS + light proxies; misses sample the skybox, front-face hits key+read the radiance cache (last frame's shaded value; NEE shading only as a fallback when the cache has no slot for the hit). Light-proxy hits return the light's emission (front face only), sign-tagged so relocation ignores them.

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeDDGIProbeTrace(uint3 dtid: SV_DispatchThreadID, uniform DDGIProbeTracePushConstant pc)
{
    uint rayIndex = dtid.x;
    uint probeIndex = dtid.y;
    int3 slot = DDGISlotFromProbeIndex(probeIndex, pc.volume);
    int3 cell = DDGICellFromSlot(slot, pc.volume);
    DDGIVolumeParams previousVolume = pc.volume;
    previousVolume.baseCell = pc.previousBaseCell;
    bool bSameCellPrev = all(DDGICellFromSlot(slot, previousVolume) == cell);

    // Inactive probes trace only the sentinel budget
    uint rayBudget = pc.raysPerProbe;
    if (pc.probeActive != nullptr && bSameCellPrev && pc.probeActive[probeIndex] == 0) {
        rayBudget = min(DDGI_SENTINEL_RAYS, pc.raysPerProbe);
    }
    if (rayIndex >= rayBudget) {
        return;
    }

    // The per-frame rotation doubles as relocation's direction search; found escapes persist in the carried offset.
    float3 direction = DDGIRotate(DDGISphericalFibonacci(rayIndex, rayBudget), pc.rayRotation);

    // Relocation offsets are last frame's
    float3 origin = DDGIProbeWorldPosition(cell, pc.volume);
    if (pc.probeOffsets != nullptr && bSameCellPrev) {
        origin += pc.probeOffsets[probeIndex].xyz;
    }

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = 10000.0;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_DDGI_PROBE_TRACE, ray);
    q.Proceed();

    float4 result;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        float hitT = q.CommittedRayT();
        Instance instance = pc.instanceBuffer[q.CommittedInstanceID()];
        if (instance.lightIndex != 0xFFFFFFFF) {
            float3 radiance = float3(0.0);
            if (q.CommittedTriangleFrontFace()) {
                LightInfo light = pc.lightData->lights[instance.lightIndex];
                radiance = UnpackUnorm4x8(light.packedColor).rgb * light.intensity;
                float maxChannel = max(radiance.r, max(radiance.g, radiance.b));
                if (pc.maxRayRadiance > 0.0 && maxChannel > pc.maxRayRadiance) {
                    radiance *= pc.maxRayRadiance / maxChannel;
                }
            }
            // Negative x tags a light-proxy hit so relocation/classification skip it; x carries -(emission.r + 1) for the irradiance blend to decode.
            result = float4(-(radiance.r + 1.0), radiance.g, radiance.b, hitT);
        } else if (q.CommittedTriangleFrontFace()) {
            float3 hitWorldPos = ray.Origin + direction * hitT;
            float3 radiance = float3(0.0);
            bool bCacheRead = false;
            SceneData scene = pc.sceneData[0];

            if (pc.radianceCache != nullptr) {
                float3 hitNormal = DecodeHitNormal(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
                RadianceCacheKey wcKey = RadianceCacheComputeKey(hitWorldPos, hitNormal, scene.cameraWorldPos.xyz);
                bool isNew;
                uint wcSlot = RadianceCacheHashInsert(pc.radianceCache->entries, pc.radianceCache->keys, pc.radianceCache->stats, wcKey, isNew);
                if (wcSlot != RADIANCE_CACHE_HASH_INVALID) {
                    uint2 packedRadiance = pc.radianceCache->cells[wcSlot].packedRadiance;
                    if (isNew) {
                        uint seedCount;
                        uint seedEmissive;
                        packedRadiance = RadianceCacheWarmStartRadiance(pc.radianceCache->entries, pc.radianceCache->cells, wcKey, seedCount, seedEmissive);
                        pc.radianceCache->cells[wcSlot].packedRadiance = packedRadiance;
                        pc.radianceCache->cells[wcSlot].packedEmissive = seedEmissive;
                        // Inherit the warm-start source's accumulation count so a re-keyed shell keeps its smoothing instead of a 50/50 first shade.
                        pc.radianceCache->cells[wcSlot].changeStreak = seedCount << 16u;
                    }

                    if (RadianceCacheRadianceShaded(packedRadiance)) {
                        radiance = RadianceCacheUnpackRadiance(packedRadiance) + UnpackEmissiveRGBE(pc.radianceCache->cells[wcSlot].packedEmissive);
                        bCacheRead = true;
                    }
                    pc.radianceCache->cells[wcSlot].lastTouched = pc.frameIndex;

                    uint shadeInterval = pc.radianceCacheShadeInterval + (wcSlot & 3u);
                    if (isNew || pc.frameIndex - pc.radianceCache->cells[wcSlot].lastShaded >= shadeInterval) {
                        uint prevActive;
                        InterlockedExchange(pc.radianceCache->active[wcSlot], 1u, prevActive);
                        if (prevActive == 0u) {
                            uint listIndex;
                            InterlockedAdd(pc.radianceCache->activeCount[0], 1u, listIndex);
                            if (listIndex < RADIANCE_CACHE_SHADE_BUDGET) {
                                pc.radianceCache->activeList[listIndex] = wcSlot;
                                pc.radianceCache->cells[wcSlot].lastShaded = pc.frameIndex;
                                RadianceCacheHitDescriptor wcHit;
                                wcHit.instanceID = q.CommittedInstanceID();
                                wcHit.primitiveIndex = q.CommittedPrimitiveIndex();
                                wcHit.packedBary = PackUnorm2x16(q.CommittedTriangleBarycentrics());
                                wcHit.pad = 0u;
                                pc.radianceCache->descriptors[wcSlot] = wcHit;
                            }
                        }
                    }
                }
            }

            if (!bCacheRead) {
                radiance = ShadeProbeRayHitNEE(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitWorldPos, direction, 0.75 * hitT);
            }

            // Firefly clamp
            float maxChannel = max(radiance.r, max(radiance.g, radiance.b));
            if (pc.maxRayRadiance > 0.0 && maxChannel > pc.maxRayRadiance) {
                radiance *= pc.maxRayRadiance / maxChannel;
            }
            result = float4(radiance, hitT);
        } else {
            // Backface hits mark "probe likely inside geometry" with a shortened negative distance, classic DDGI convention.
            result = float4(0.0, 0.0, 0.0, -0.2 * hitT);
        }
    } else {
        float3 radiance = float3(0.0);
        if (pc.skyboxIndex >= 0 && pc.bBounceOnly == 0) {
            // Matches the Y flip the visible skybox pass applies when sampling the cubemap.
            float3 skyDirection = float3(direction.x, -direction.y, direction.z);
            // Mip 4 is the roughness-1.0 (blurriest) prefiltered level. Reduces flickering from high frequency variance.
            radiance = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 4).rgb * pc.iblIntensity;
        }
        result = float4(radiance, 10000.0);
    }
    pc.rayData[probeIndex * pc.raysPerProbe + rayIndex] = result;
}
