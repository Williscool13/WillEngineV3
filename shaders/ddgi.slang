import model_interop;
import push_constant_interop;
import lights_interop;
import ddgi_interop;
import instance_mask_interop;
import ddgi_common;
import gpu_debug_draw;
import bindless_declarations;
import constants_interop;
import shader_constants;
import shader_functions;
import packing_functions;
import pbr_functions;
import random_common;
import hit_shading_common;
import world_cache_hash;
import world_cache_interop;

// =============== Shader Summary ============
// DDGI probe volume passes. Trace: one thread per probe ray against the scene TLAS + light proxies; misses sample the skybox, front-face hits key+read the world radiance cache (last frame's shaded value; NEE shading only as a fallback when the cache has no slot for the hit). Light-proxy hits carry no radiance, only the hit distance for the visibility moments.
// Blend: one thread group per probe tile; each interior texel cosine-integrates the probe's rays into octahedral irradiance stored as pow(E, 1/gamma), hysteresis-blended in that perceptual space with RTXGI's change/brightness thresholds. Slots whose lattice cell changed since last frame (window scroll) or with no history start fresh. Border texels copy wrapped interior texels so bilinear taps stay seamless.
// Blend visibility: same layout at 16x16 tiles; each texel integrates mean/mean^2 ray distance with a sharpened (pow distanceExponent) cosine lobe, for the Chebyshev occlusion test at sampling.
// Relocate: one thread per probe applies RTXGI probe relocation to this frame's ray distances (light-proxy hits ignored), writing next frame's per-slot world offset (xyz, capped at 45% of spacing) and backface fraction (w, the sampler's dead-probe test).
// Debug: one sphere per probe at its relocated position, L1 SH fitted from the probe's decoded atlas tile; dead probes render flat red.

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
    if (pc.bActiveValid != 0 && bSameCellPrev && pc.probeActive[probeIndex] == 0) {
        rayBudget = min(DDGI_SENTINEL_RAYS, pc.raysPerProbe);
    }
    if (rayIndex >= rayBudget) {
        return;
    }

    // The per-frame rotation doubles as relocation's direction search; found escapes persist in the carried offset.
    float3 direction = DDGIRotate(DDGISphericalFibonacci(rayIndex, rayBudget), pc.rayRotation);

    // Relocation offsets are last frame's
    float3 origin = DDGIProbeWorldPosition(cell, pc.volume);
    if (pc.bOffsetsValid != 0 && bSameCellPrev) {
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
            // -1 in x tags a light-proxy hit so relocation can ignore it; blending clamps it back to black.
            result = float4(-1.0, 0.0, 0.0, hitT);
        } else if (q.CommittedTriangleFrontFace()) {
            float3 hitWorldPos = ray.Origin + direction * hitT;
            float3 radiance = float3(0.0);
            bool bCacheRead = false;

            if (pc.bWorldCacheValid != 0) {
                float3 hitNormal = DecodeHitNormal(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
                float3 approxCameraPos = (float3(pc.volume.baseCell) + float3(pc.volume.probeCount) * 0.5) * pc.volume.probeSpacing;
                WorldCacheKey wcKey = WorldCacheComputeKey(hitWorldPos, hitNormal, approxCameraPos);
                bool isNew;
                uint wcSlot = WorldCacheHashInsert(pc.worldCache->entries, pc.worldCache->keys, wcKey, isNew);
                if (wcSlot != WORLD_CACHE_HASH_INVALID) {
                    uint packedRadiance = pc.worldCache->cells[wcSlot].packedRadiance;
                    if (isNew) {
                        packedRadiance = WorldCacheWarmStartRadiance(pc.worldCache->entries, pc.worldCache->cells, wcKey);
                        pc.worldCache->cells[wcSlot].packedRadiance = packedRadiance;
                        // Seed counts as one accumulated sample so the first real shade replaces half of it.
                        pc.worldCache->cells[wcSlot].changeStreak = 1u << 16u;
                    }

                    if (packedRadiance != WORLD_CACHE_RADIANCE_UNSHADED) {
                        radiance = UnpackEmissiveRGBE(packedRadiance);
                        bCacheRead = true;
                    }
                    pc.worldCache->cells[wcSlot].lastTouched = pc.frameIndex;

                    uint prevActive;
                    InterlockedExchange(pc.worldCache->active[wcSlot], 1u, prevActive);
                    if (prevActive == 0u) {
                        WorldCacheHitDescriptor wcHit;
                        wcHit.instanceID = q.CommittedInstanceID();
                        wcHit.primitiveIndex = q.CommittedPrimitiveIndex();
                        wcHit.packedBary = PackUnorm2x16(q.CommittedTriangleBarycentrics());
                        wcHit.pad = 0u;
                        pc.worldCache->descriptors[wcSlot] = wcHit;
                    }
                }
            }

            if (!bCacheRead) {
                uint rng = SeedRng(uint2(rayIndex, probeIndex), pc.frameIndex, 0xDD61u);
                radiance = ShadeProbeRayHitNEE(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitWorldPos, direction, rng);
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
            radiance = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb;
        }
        result = float4(radiance, 10000.0);
    }
    pc.rayData[probeIndex * pc.raysPerProbe + rayIndex] = result;
}

/** Border texel -> wrapped interior source texel; identity for interior texels. Corners copy the diagonally opposite interior corner. */
static int2 DDGIOctBorderSource(int2 texel, int n)
{
    bool onX = texel.x == 0 || texel.x == n + 1;
    bool onY = texel.y == 0 || texel.y == n + 1;
    if (!onX && !onY) {
        return texel;
    }
    if (onX && onY) {
        return int2(texel.x == 0 ? n : 1, texel.y == 0 ? n : 1);
    }
    if (onY) {
        return int2(n + 1 - texel.x, texel.y == 0 ? 1 : n);
    }
    return int2(texel.x == 0 ? 1 : n, n + 1 - texel.y);
}

static float DDGIMaxComponent(float3 v)
{
    return max(v.x, max(v.y, v.z));
}

groupshared float4 gsRays[DDGI_MAX_RAYS_PER_PROBE];
groupshared float3 gsTexels[DDGI_IRRADIANCE_TILE * DDGI_IRRADIANCE_TILE];

[shader("compute")]
[numthreads(DDGI_IRRADIANCE_TILE, DDGI_IRRADIANCE_TILE, 1)]
void ComputeDDGIBlendIrradiance(uint3 groupId: SV_GroupID, uint3 gtid: SV_GroupThreadID, uniform DDGIProbeBlendPushConstant pc)
{
    uint probeIndex = groupId.x;
    int3 slot = DDGISlotFromProbeIndex(probeIndex, pc.volume);
    int2 tileOrigin = DDGIProbeTileOrigin(slot, pc.volume, DDGI_IRRADIANCE_TILE);

    DDGIVolumeParams previousVolume = pc.volume;
    previousVolume.baseCell = pc.previousBaseCell;
    bool bSameCellPrev = all(DDGICellFromSlot(slot, previousVolume) == DDGICellFromSlot(slot, pc.volume));
    bool bInactive = pc.bActiveValid != 0 && bSameCellPrev && pc.probeActive[probeIndex] == 0;
    // Frozen inactive probe
    bool bFrozen = bInactive && pc.bHistoryValid != 0;
    uint rayBudget = bInactive ? min(DDGI_SENTINEL_RAYS, pc.raysPerProbe) : pc.raysPerProbe;

    uint threadIndex = gtid.y * DDGI_IRRADIANCE_TILE + gtid.x;
    if (!bFrozen) {
        for (uint r = threadIndex; r < rayBudget; r += DDGI_IRRADIANCE_TILE * DDGI_IRRADIANCE_TILE) {
            gsRays[r] = pc.rayData[probeIndex * pc.raysPerProbe + r];
        }
    }
    GroupMemoryBarrierWithGroupSync();

    int2 texel = int2(gtid.xy);
    bool bInterior = all(texel >= 1) && all(texel <= int(DDGI_IRRADIANCE_INTERIOR));
    if (bInterior && bFrozen) {
        gsTexels[threadIndex] = rdgTexturesFloat4[pc.atlasHistoryIndex].Load(int3(tileOrigin + texel, 0)).rgb;
    } else if (bInterior) {
        float3 texelDirection = DDGIOctTexelDirection(texel, DDGI_IRRADIANCE_INTERIOR);
        float3 radianceSum = float3(0.0);
        float weightSum = 0.0;
        for (uint r = 0; r < rayBudget; ++r) {
            // Backface hits are excluded entirely (RTXGI); counting their weight darkens probes near geometry.
            float4 rayData = gsRays[r];
            if (rayData.w < 0.0) {
                continue;
            }
            float3 rayDirection = DDGIRotate(DDGISphericalFibonacci(r, rayBudget), pc.rayRotation);
            float weight = max(dot(texelDirection, rayDirection), 0.0);
            radianceSum += max(rayData.rgb, float3(0.0)) * weight;
            weightSum += weight;
        }
        // The EMA runs in perceptual space (pow 1/gamma): a rare bright ray moves the encoded value little, so small
        // sky apertures stop pulsing. Thresholds and the delta clamp follow RTXGI's probe blending.
        float3 result = weightSum > 1e-4 ? radianceSum / weightSum : float3(0.0);
        result = pow(result, 1.0 / pc.volume.irradianceGamma);

        if (pc.bHistoryValid != 0) {
            if (bSameCellPrev) {
                float3 previous = rdgTexturesFloat4[pc.atlasHistoryIndex].Load(int3(tileOrigin + texel, 0)).rgb;

                if (pc.bRestartValid != 0 && pc.probeRestart[probeIndex] != 0) {
                    // Relocation teleport or revival is a real change, not noise; skip the anti-flicker clamps.
                    result = lerp(result, previous, max(pc.hysteresis - 0.75, 0.0));
                } else {
                    float hysteresis = pc.hysteresis;
                    if (DDGIMaxComponent(result - previous) > pc.irradianceThreshold) {
                        hysteresis = max(hysteresis - 0.75, 0.0);
                    }
                    float3 delta = result - previous;
                    if (DDGIMaxComponent(delta) > pc.brightnessThreshold) {
                        delta *= 0.25;
                    }
                    float3 lerpDelta = (1.0 - hysteresis) * delta;
                    if (DDGIMaxComponent(result) < DDGIMaxComponent(previous)) {
                        const float minDarkeningStep = 1.0 / 1024.0;
                        lerpDelta = min(max(float3(minDarkeningStep), abs(lerpDelta)), abs(delta)) * sign(lerpDelta);
                    }
                    result = previous + lerpDelta;
                }
            }
        }
        gsTexels[threadIndex] = result;
    }
    GroupMemoryBarrierWithGroupSync();

    int2 source = DDGIOctBorderSource(texel, int(DDGI_IRRADIANCE_INTERIOR));
    float3 value = gsTexels[uint(source.y) * DDGI_IRRADIANCE_TILE + uint(source.x)];
    rdgStorageFloat4[pc.atlasOutIndex][uint2(tileOrigin + texel)] = float4(value, 1.0);
}

groupshared float gsRayDistances[DDGI_MAX_RAYS_PER_PROBE];
groupshared float2 gsVisTexels[DDGI_VISIBILITY_TILE * DDGI_VISIBILITY_TILE];

[shader("compute")]
[numthreads(DDGI_VISIBILITY_TILE, DDGI_VISIBILITY_TILE, 1)]
void ComputeDDGIBlendVisibility(uint3 groupId: SV_GroupID, uint3 gtid: SV_GroupThreadID, uniform DDGIProbeBlendPushConstant pc)
{
    uint probeIndex = groupId.x;
    int3 slot = DDGISlotFromProbeIndex(probeIndex, pc.volume);
    int2 tileOrigin = DDGIProbeTileOrigin(slot, pc.volume, DDGI_VISIBILITY_TILE);

    DDGIVolumeParams previousVolume = pc.volume;
    previousVolume.baseCell = pc.previousBaseCell;
    bool bSameCellPrev = all(DDGICellFromSlot(slot, previousVolume) == DDGICellFromSlot(slot, pc.volume));
    bool bInactive = pc.bActiveValid != 0 && bSameCellPrev && pc.probeActive[probeIndex] == 0;
    bool bFrozen = bInactive && pc.bHistoryValid != 0;
    uint rayBudget = bInactive ? min(DDGI_SENTINEL_RAYS, pc.raysPerProbe) : pc.raysPerProbe;

    // abs() folds the negative backface marker back into a distance; the clamp bounds what Chebyshev ever compares against.
    float maxDistance = length(pc.volume.probeSpacing) * 1.5;
    uint threadIndex = gtid.y * DDGI_VISIBILITY_TILE + gtid.x;
    if (!bFrozen) {
        for (uint r = threadIndex; r < rayBudget; r += DDGI_VISIBILITY_TILE * DDGI_VISIBILITY_TILE) {
            gsRayDistances[r] = min(abs(pc.rayData[probeIndex * pc.raysPerProbe + r].w), maxDistance);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    int2 texel = int2(gtid.xy);
    bool bInterior = all(texel >= 1) && all(texel <= int(DDGI_VISIBILITY_INTERIOR));
    if (bInterior && bFrozen) {
        gsVisTexels[threadIndex] = rdgTexturesFloat2[pc.atlasHistoryIndex].Load(int3(tileOrigin + texel, 0)).rg;
    } else if (bInterior) {
        float3 texelDirection = DDGIOctTexelDirection(texel, DDGI_VISIBILITY_INTERIOR);
        float2 momentsSum = float2(0.0);
        float weightSum = 0.0;
        for (uint r = 0; r < rayBudget; ++r) {
            float3 rayDirection = DDGIRotate(DDGISphericalFibonacci(r, rayBudget), pc.rayRotation);
            float weight = pow(max(dot(texelDirection, rayDirection), 0.0), pc.distanceExponent);
            momentsSum += float2(gsRayDistances[r], gsRayDistances[r] * gsRayDistances[r]) * weight;
            weightSum += weight;
        }
        float2 result = weightSum > 1e-4 ? momentsSum / weightSum : float2(0.0);

        if (pc.bHistoryValid != 0 && bSameCellPrev) {
            float2 previous = rdgTexturesFloat2[pc.atlasHistoryIndex].Load(int3(tileOrigin + texel, 0)).rg;
            float hysteresis = pc.hysteresis;
            if (pc.bRestartValid != 0 && pc.probeRestart[probeIndex] != 0) {
                hysteresis = max(hysteresis - 0.75, 0.0);
            }
            result = lerp(result, previous, hysteresis);
        }
        gsVisTexels[threadIndex] = result;
    }
    GroupMemoryBarrierWithGroupSync();

    int2 source = DDGIOctBorderSource(texel, int(DDGI_VISIBILITY_INTERIOR));
    rdgStorageFloat2[pc.atlasOutIndex][uint2(tileOrigin + texel)] = gsVisTexels[uint(source.y) * DDGI_VISIBILITY_TILE + uint(source.x)];
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeDDGIProbeRelocate(uint3 dtid: SV_DispatchThreadID, uniform DDGIProbeRelocatePushConstant pc)
{
    uint probeIndex = dtid.x;
    if (probeIndex >= pc.volume.probeCount.x * pc.volume.probeCount.y * pc.volume.probeCount.z) {
        return;
    }
    int3 slot = DDGISlotFromProbeIndex(probeIndex, pc.volume);
    int3 cell = DDGICellFromSlot(slot, pc.volume);
    DDGIVolumeParams previousVolume = pc.volume;
    previousVolume.baseCell = pc.previousBaseCell;
    bool bSameCellPrev = all(DDGICellFromSlot(slot, previousVolume) == cell);

    float3 offset = float3(0.0);
    float previousBackfaceFraction = -1.0;
    if (pc.bOffsetsValid != 0 && bSameCellPrev) {
        float4 previous = pc.offsetsIn[probeIndex];
        offset = previous.xyz;
        previousBackfaceFraction = previous.w;
    }
    float3 previousOffset = offset;

    bool bWasActive = !(pc.bActiveValid != 0 && bSameCellPrev && pc.activeIn[probeIndex] == 0);
    uint rayBudget = bWasActive ? pc.raysPerProbe : min(DDGI_SENTINEL_RAYS, pc.raysPerProbe);

    uint backfaceCount = 0;
    float closestBackfaceDistance = 1e27;
    uint closestBackfaceIndex = 0xFFFFFFFF;
    float closestFrontfaceDistance = 1e27;
    uint closestFrontfaceIndex = 0xFFFFFFFF;
    float farthestFrontfaceDistance = 0.0;
    uint farthestFrontfaceIndex = 0xFFFFFFFF;
    for (uint r = 0; r < rayBudget; ++r) {
        float4 rayData = pc.rayData[probeIndex * pc.raysPerProbe + r];
        if (rayData.x < 0.0) {
            continue;
        }
        float hitT = rayData.w;
        if (hitT < 0.0) {
            backfaceCount++;
            float backfaceDistance = hitT * -5.0;
            if (backfaceDistance < closestBackfaceDistance) {
                closestBackfaceDistance = backfaceDistance;
                closestBackfaceIndex = r;
            }
        } else {
            if (hitT < closestFrontfaceDistance) {
                closestFrontfaceDistance = hitT;
                closestFrontfaceIndex = r;
            }
            if (hitT > farthestFrontfaceDistance) {
                farthestFrontfaceDistance = hitT;
                farthestFrontfaceIndex = r;
            }
        }
    }

    float backfaceFraction = float(backfaceCount) / float(max(rayBudget, 1u));
    uint restart = 0;
    if (bWasActive) {
        // RTXGI probe relocation
        bool bEscape = false;
        float3 fullOffset = offset;
        if (closestBackfaceIndex != 0xFFFFFFFF && backfaceFraction > 0.25) {
            float3 backfaceDirection = DDGIRotate(DDGISphericalFibonacci(closestBackfaceIndex, rayBudget), pc.rayRotation);
            fullOffset = offset + backfaceDirection * (closestBackfaceDistance + pc.minFrontfaceDistance * 0.5);
            bEscape = true;
        } else if (closestFrontfaceDistance < pc.minFrontfaceDistance && farthestFrontfaceIndex != 0xFFFFFFFF) {
            float3 closestDirection = DDGIRotate(DDGISphericalFibonacci(closestFrontfaceIndex, rayBudget), pc.rayRotation);
            float3 farthestDirection = DDGIRotate(DDGISphericalFibonacci(farthestFrontfaceIndex, rayBudget), pc.rayRotation);
            // Only move if the far direction actually opposes the wall, so the probe never pushes through the far side.
            if (dot(closestDirection, farthestDirection) <= 0.0) {
                // Deficit step, not RTXGI's min(farthest, 1): full-size jumps oscillate at this spacing.
                fullOffset = offset + farthestDirection * min(pc.minFrontfaceDistance - closestFrontfaceDistance, farthestFrontfaceDistance);
            }
        } else if (closestFrontfaceDistance > pc.minFrontfaceDistance) {
            float offsetLength = length(offset);
            if (offsetLength > 1e-5) {
                float moveBackMargin = min(closestFrontfaceDistance - pc.minFrontfaceDistance, offsetLength);
                fullOffset = offset - (offset / offsetLength) * moveBackMargin;
            }
        }

        // Cap at 45% of spacing (trilinear domain). reject rather than clamp so unreachable escapes freeze instead of wandering.
        float3 normalizedOffset = fullOffset / pc.volume.probeSpacing;
        if (dot(normalizedOffset, normalizedOffset) < 0.45 * 0.45) {
            // Frontface-only branches chase a rotation-dependent target; sub-deadband moves are jitter, not convergence. Escapes always apply.
            float3 deltaNormalized = (fullOffset - offset) / pc.volume.probeSpacing;
            if (bEscape || dot(deltaNormalized, deltaNormalized) > DDGI_RELOCATE_DEADBAND * DDGI_RELOCATE_DEADBAND) {
                offset = fullOffset;
            }
        }

        // A real move or dead-to-alive flip flags a temporal restart for next frame's blends, the first fed by rays from the new position.
        if (previousBackfaceFraction >= 0.0) {
            float3 movedNormalized = (offset - previousOffset) / pc.volume.probeSpacing;
            bool bRevived = previousBackfaceFraction > DDGI_PROBE_DEAD_BACKFACE_FRACTION && backfaceFraction <= DDGI_PROBE_DEAD_BACKFACE_FRACTION;
            if (dot(movedNormalized, movedNormalized) > DDGI_RELOCATE_DEADBAND * DDGI_RELOCATE_DEADBAND || bRevived) {
                restart = 1;
            }
        }
    } else {
        backfaceFraction = max(previousBackfaceFraction, 0.0);
    }

    if (pc.bClassify != 0) {
        float activeDistance = length(pc.volume.probeSpacing) * 1.5;
        uint active = (backfaceCount > 0 || closestFrontfaceDistance < activeDistance) ? 1u : 0u;
        if (!bWasActive && active != 0) {
            restart = 1;
        }
        pc.activeOut[probeIndex] = active;
    }
    pc.restartOut[probeIndex] = restart;
    pc.offsetsOut[probeIndex] = float4(offset, backfaceFraction);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeDDGIProbeDebug(uint3 dtid: SV_DispatchThreadID, uniform DDGIProbeDebugPushConstant pc)
{
    if (any(dtid >= pc.volume.probeCount)) {
        return;
    }
    int3 slot = int3(dtid);
    int3 cell = DDGICellFromSlot(slot, pc.volume);
    float3 position = DDGIProbeWorldPosition(cell, pc.volume);
    float radius = 0.08 * min(pc.volume.probeSpacing.x, min(pc.volume.probeSpacing.y, pc.volume.probeSpacing.z));
    uint probeIndex = DDGIProbeIndexFromSlot(slot, pc.volume);
    if (pc.bOffsetsValid != 0) {
        float4 probeOffset = pc.probeOffsets[probeIndex];
        position += probeOffset.xyz;
        if (probeOffset.w > DDGI_PROBE_DEAD_BACKFACE_FRACTION) {
            GPUDebugAddSphereFlat(pc.sphereArgs, pc.sphereBuffer, position, radius, float3(1.0, 0.0, 0.0));
            return;
        }
    }

    if (pc.bActiveValid != 0 && pc.probeActive[probeIndex] == 0) {
        if (pc.bHideInactive == 0) {
            GPUDebugAddSphereFlat(pc.sphereArgs, pc.sphereBuffer, position, radius, float3(0.15, 0.3, 0.9));
        }
        return;
    }

    // Least-squares L1 fit over the tile: E(n) ~ dc + dot(dir, n), with dir = 3 * mean(E * n) since mean(n n^T) = I/3 over the sphere.
    int2 tileOrigin = DDGIProbeTileOrigin(slot, pc.volume, DDGI_IRRADIANCE_TILE);
    float3 dc = float3(0.0);
    float3 dirR = float3(0.0);
    float3 dirG = float3(0.0);
    float3 dirB = float3(0.0);
    for (int ty = 1; ty <= int(DDGI_IRRADIANCE_INTERIOR); ++ty) {
        for (int tx = 1; tx <= int(DDGI_IRRADIANCE_INTERIOR); ++tx) {
            float3 encoded = rdgTexturesFloat4[pc.irradianceAtlasIndex].Load(int3(tileOrigin + int2(tx, ty), 0)).rgb;
            float3 irradiance = pow(max(encoded, float3(0.0)), pc.volume.irradianceGamma);
            float3 texelDirection = DDGIOctTexelDirection(int2(tx, ty), DDGI_IRRADIANCE_INTERIOR);
            dc += irradiance;
            dirR += irradiance.r * texelDirection;
            dirG += irradiance.g * texelDirection;
            dirB += irradiance.b * texelDirection;
        }
    }
    float invCount = pc.probeDebugExposure / float(DDGI_IRRADIANCE_INTERIOR * DDGI_IRRADIANCE_INTERIOR);
    float3 tint = UnpackUnorm4x8(pc.packedTint).rgb;
    dc *= invCount * tint;
    dirR *= 3.0 * invCount * tint.r;
    dirG *= 3.0 * invCount * tint.g;
    dirB *= 3.0 * invCount * tint.b;

    GPUDebugAddSphere(pc.sphereArgs, pc.sphereBuffer, position, radius,
                      float4(dc.r, dirR), float4(dc.g, dirG), float4(dc.b, dirB));
}
