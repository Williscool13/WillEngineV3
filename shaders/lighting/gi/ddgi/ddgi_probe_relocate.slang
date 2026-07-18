import push_constant_interop;
import ddgi_interop;
import ddgi_common;
import bindless_declarations;

// =============== Shader Summary ============
// DDGI probe relocate: one thread per probe applies RTXGI probe relocation to this frame's ray distances (light-proxy hits ignored), writing next frame's per-slot world offset (xyz, capped at 45% of spacing) and backface fraction (w, the sampler's dead-probe test). Also classifies probe activity and flags temporal restarts for real moves and dead-to-alive flips.

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
