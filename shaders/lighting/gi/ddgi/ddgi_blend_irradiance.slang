import push_constant_interop;
import ddgi_interop;
import ddgi_common;
import bindless_declarations;
import shader_functions;

// =============== Shader Summary ============
// DDGI blend irradiance: one thread group per probe tile; each interior texel cosine-integrates the probe's rays into octahedral irradiance stored as pow(E, 1/gamma), hysteresis-blended in that perceptual space with RTXGI's change/brightness thresholds. Slots whose lattice cell changed since last frame (window scroll) or with no history start fresh. Border texels copy wrapped interior texels so bilinear taps stay seamless.
// Atlas alpha carries the variance guide: relative deviation of ray luma over the probe's ray set, EMA'd with the same hysteresis. It predicts how noisy a 1spp cosine gather is near this probe (spiky incident light -> high), and only steers denoiser thresholds, so it needs to be monotonic, not calibrated.

static float DDGIMaxComponent(float3 v)
{
    return max(v.x, max(v.y, v.z));
}

groupshared float4 gsRays[DDGI_MAX_RAYS_PER_PROBE];
groupshared float3 gsRayDirections[DDGI_MAX_RAYS_PER_PROBE];
groupshared float4 gsTexels[DDGI_IRRADIANCE_TILE * DDGI_IRRADIANCE_TILE];

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
            gsRayDirections[r] = DDGIRotate(DDGISphericalFibonacci(r, rayBudget), pc.rayRotation);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    int2 texel = int2(gtid.xy);
    bool bInterior = all(texel >= 1) && all(texel <= int(DDGI_IRRADIANCE_INTERIOR));
    if (bInterior && bFrozen) {
        gsTexels[threadIndex] = rdgStorageFloat4[pc.atlasOutIndex][uint2(tileOrigin + texel)];
    } else if (bInterior) {
        float3 texelDirection = DDGIOctTexelDirection(texel, DDGI_IRRADIANCE_INTERIOR);
        float3 radianceSum = float3(0.0);
        float weightSum = 0.0;
        // Probe-wide (unweighted) luma moments over the same ray set; every interior thread computes the identical value, which is cheaper than a reduction at these ray counts.
        float lumaSum = 0.0;
        float lumaSqSum = 0.0;
        float validRays = 0.0;
        for (uint r = 0; r < rayBudget; ++r) {
            // Backface hits are excluded entirely (RTXGI); counting their weight darkens probes near geometry.
            float4 rayData = gsRays[r];
            if (rayData.w < 0.0) {
                continue;
            }
            float3 radiance = rayData.rgb;
            if (radiance.x < 0.0) {
                // Light-proxy hit: x carries -(emission.r + 1)
                radiance.x = -radiance.x - 1.0;
            }
            float3 rayDirection = gsRayDirections[r];
            float weight = max(dot(texelDirection, rayDirection), 0.0);
            radianceSum += radiance * weight;
            weightSum += weight;
            float luma = dot(radiance, float3(0.2126, 0.7152, 0.0722));
            lumaSum += luma;
            lumaSqSum += luma * luma;
            validRays += 1.0;
        }
        // The EMA runs in perceptual space (pow 1/gamma): a rare bright ray moves the encoded value little, so small
        // sky apertures stop pulsing. Thresholds and the delta clamp follow RTXGI's probe blending.
        float3 result = weightSum > 1e-4 ? radianceSum / weightSum : float3(0.0);
        result = pow(result, 1.0 / pc.volume.irradianceGamma);

        float guide = 0.0;
        if (validRays > 1.0) {
            float m1 = lumaSum / validRays;
            float m2 = lumaSqSum / validRays;
            guide = sqrt(max(m2 - m1 * m1, 0.0)) / max(m1, 1e-4);
        }

        if (pc.bHistoryValid != 0) {
            if (bSameCellPrev) {
                float4 previous = rdgStorageFloat4[pc.atlasOutIndex][uint2(tileOrigin + texel)];

                if (pc.bRestartValid != 0 && pc.probeRestart[probeIndex] != 0) {
                    // Relocation teleport or revival is a real change, not noise; skip the anti-flicker clamps.
                    result = lerp(result, previous.rgb, max(pc.hysteresis - 0.75, 0.0));
                    guide = lerp(guide, previous.a, max(pc.hysteresis - 0.75, 0.0));
                } else {
                    float hysteresis = pc.hysteresis;
                    if (DDGIMaxComponent(previous.rgb - result) > pc.irradianceThreshold) {
                        // Never fully discard history here; a 1spp update is too noisy to trust alone.
                        hysteresis = max(hysteresis - 0.75, 0.1);
                    }
                    float3 delta = result - previous.rgb;
                    if (DDGIMaxComponent(delta) > pc.brightnessThreshold) {
                        delta *= 0.25;
                    }
                    float3 lerpDelta = (1.0 - hysteresis) * delta;
                    if (DDGIMaxComponent(result) < DDGIMaxComponent(previous.rgb)) {
                        const float minDarkeningStep = 1.0 / 1024.0;
                        lerpDelta = min(max(float3(minDarkeningStep), abs(lerpDelta)), abs(delta)) * sign(lerpDelta);
                    }
                    result = previous.rgb + lerpDelta;
                    // Guide EMA keeps the base hysteresis even when the irradiance thresholds fire: a lighting step spikes the single-frame deviation estimate, and letting that spike through would loosen the gather clamps in the exact frames they must catch the change.
                    guide = lerp(guide, previous.a, pc.hysteresis);
                }
            }
        }
        gsTexels[threadIndex] = float4(result, guide);
    }
    GroupMemoryBarrierWithGroupSync();

    int2 source = DDGIOctBorderSource(texel, int(DDGI_IRRADIANCE_INTERIOR));
    rdgStorageFloat4[pc.atlasOutIndex][uint2(tileOrigin + texel)] = gsTexels[uint(source.y) * DDGI_IRRADIANCE_TILE + uint(source.x)];
}
