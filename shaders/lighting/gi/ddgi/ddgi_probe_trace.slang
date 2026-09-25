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

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeDDGIProbeTrace(uint3 dtid: SV_DispatchThreadID, uniform DDGIProbeTracePushConstant pc)
{
    uint rayIndex = dtid.x;
    uint probeIndex = dtid.y;
    int3 slot = DDGISlotFromProbeIndex(probeIndex, pc.volume);
    int3 cell = DDGICellFromSlot(slot, pc.volume);
    DDGIVolumeParams previousVolume = pc.volume;
    previousVolume.baseCell = int3(int(pc.previousBaseCellXY & 0xFFFFu) - 32768, int(pc.previousBaseCellXY >> 16u) - 32768, int(pc.previousBaseCellZFlags & 0xFFFFu) - 32768);
    bool bSameCellPrev = all(DDGICellFromSlot(slot, previousVolume) == cell);

    uint raysPerProbe = pc.raysAndShadeInterval & 0xFFFFu;
    // Inactive probes trace only the sentinel budget
    uint rayBudget = raysPerProbe;
    if (pc.probeActive != nullptr && bSameCellPrev && pc.probeActive[probeIndex] == 0) {
        rayBudget = min(DDGI_SENTINEL_RAYS, raysPerProbe);
    }
    if (rayIndex >= rayBudget) {
        return;
    }

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
        bool bMirror = IsTwoSidedBackHit(pc.materialBuffer, instance, q.CommittedTriangleFrontFace());
        if (instance.lightIndex != 0xFFFFFFFF) {
            float3 radiance = float3(0.0);
            // Negative x tags a light-proxy hit; x carries -(emission.r + 1).
            result = float4(-(radiance.r + 1.0), radiance.g, radiance.b, hitT);
        } else if (q.CommittedTriangleFrontFace() || bMirror) {
            float3 hitWorldPos = ray.Origin + direction * hitT;
            float3 radiance = float3(0.0);
            bool bCacheRead = false;
            SceneData scene = pc.sceneData[0];

            if (pc.radianceCache != nullptr) {
                float3 hitNormal = DecodeHitNormal(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), bMirror);
                RadianceCacheKey wcKey = RadianceCacheComputeKey(hitWorldPos, hitNormal, scene.cameraWorldPos.xyz);
                bool isNew;
                uint wcSlot = RadianceCacheHashInsert(pc.radianceCache->entries, pc.radianceCache->keys, pc.radianceCache->stats, wcKey, isNew);
                // Checksum alias, or a same-dispatch insert whose key is not written yet.
                if (wcSlot != RADIANCE_CACHE_HASH_INVALID && !isNew && any(pc.radianceCache->keys[wcSlot] != RadianceCachePackKey(wcKey))) {
                    wcSlot = RADIANCE_CACHE_HASH_INVALID;
                }
                if (wcSlot != RADIANCE_CACHE_HASH_INVALID) {
                    uint2 packedRadiance = pc.radianceCache->cells[wcSlot].packedRadiance;
                    if (isNew) {
                        uint seedCount;
                        packedRadiance = RadianceCacheWarmStartRadiance(pc.radianceCache->entries, pc.radianceCache->keys, pc.radianceCache->cells, wcKey, seedCount);
                        pc.radianceCache->cells[wcSlot].packedRadiance = packedRadiance;
                        pc.radianceCache->cells[wcSlot].changeStreak = seedCount << 16u;
                    }

                    if (RadianceCacheRadianceShaded(packedRadiance)) {
                        float3 hitEmissive;
                        float3 hitDiffuseColor = DecodeHitDiffuseEmissive(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitEmissive);
                        radiance = RadianceCacheUnpackRadiance(packedRadiance) * hitDiffuseColor + hitEmissive;
                        bCacheRead = true;
                    }
                    pc.radianceCache->cells[wcSlot].lastTouched = pc.frameIndex;

                    uint shadeInterval = (pc.raysAndShadeInterval >> 16u) + (wcSlot & 3u);
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
                                wcHit.bMirror = bMirror ? 1u : 0u;
                                pc.radianceCache->descriptors[wcSlot] = wcHit;
                            }
                        }
                    }
                }
            }

            if (!bCacheRead) {
                uint neeSeed = SeedRng(uint2(probeIndex, rayIndex), pc.frameIndex, 0x1E5CACEu);
                radiance = ShadeProbeRayHitNEE(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), bMirror, hitWorldPos, direction, neeSeed, 0.75 * hitT);
            }

            // Firefly clamp
            float maxChannel = max(radiance.r, max(radiance.g, radiance.b));
            if (pc.maxRayRadiance > 0.0 && maxChannel > pc.maxRayRadiance) {
                radiance *= pc.maxRayRadiance / maxChannel;
            }
            result = float4(radiance, hitT);
        } else {
            // Negative shortened distance marks a backface hit.
            result = float4(0.0, 0.0, 0.0, -0.2 * hitT);
        }
    } else {
        float3 radiance = float3(0.0);
        if (pc.skyboxIndex >= 0 && (pc.previousBaseCellZFlags & (1u << 17u)) == 0u) {
            // Matches the Y flip the skybox pass applies.
            float3 skyDirection = float3(direction.x, -direction.y, direction.z);
            // Mip 1, not the cosine-convolved level, or the probe integration convolves the sky twice.
            radiance = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 1).rgb * pc.iblIntensity;
        }
        result = float4(radiance, 10000.0);
    }
    pc.rayData[probeIndex * raysPerProbe + rayIndex] = result;
}
