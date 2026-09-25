import push_constant_interop;
import ddgi_interop;
import ddgi_common;
import bindless_declarations;
import shader_functions;

// =============== Shader Summary ============
// DDGI blend irradiance: one thread group per probe tile; each interior texel cosine-integrates the probe's rays into octahedral irradiance stored as pow(E, 1/gamma), hysteresis-blended in linear space with RTXGI's change/brightness thresholds taken relative to the previous value. Slots whose lattice cell changed since last frame (window scroll) or with no history start fresh. Border texels copy wrapped interior texels so bilinear taps stay seamless.

static float DDGIMaxComponent(float3 v)
{
    return max(v.x, max(v.y, v.z));
}

// Ray luma cap as a multiple of the probe's mean ray luma: one ray hitting a small bright emitter otherwise lights the whole probe for an update.
static const float DDGI_RELATIVE_RAY_CLAMP = 6.0;

static float3 DDGIDecodeRayRadiance(float4 rayData)
{
    float3 radiance = rayData.rgb;
    if (radiance.x < 0.0) {
        radiance.x = -radiance.x - 1.0;
    }
    return radiance;
}

groupshared float4 gsRays[DDGI_MAX_RAYS_PER_PROBE];
groupshared float3 gsRayDirections[DDGI_MAX_RAYS_PER_PROBE];
groupshared float4 gsTexels[DDGI_IRRADIANCE_TILE * DDGI_IRRADIANCE_TILE];
groupshared float gsRayLumaCap;

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
    if (bInactive && pc.bHistoryValid != 0) {
        rdgStorageFloat4[pc.atlasOutIndex][uint2(tileOrigin + int2(gtid.xy))] = rdgStorageFloat4[pc.atlasInIndex][uint2(tileOrigin + int2(gtid.xy))];
        return;
    }
    uint rayBudget = bInactive ? min(DDGI_SENTINEL_RAYS, pc.raysPerProbe) : pc.raysPerProbe;

    uint threadIndex = gtid.y * DDGI_IRRADIANCE_TILE + gtid.x;
    for (uint r = threadIndex; r < rayBudget; r += DDGI_IRRADIANCE_TILE * DDGI_IRRADIANCE_TILE) {
        gsRays[r] = pc.rayData[probeIndex * pc.raysPerProbe + r];
        gsRayDirections[r] = DDGIRotate(DDGISphericalFibonacci(r, rayBudget), pc.rayRotation);
    }
    GroupMemoryBarrierWithGroupSync();

    if (threadIndex == 0u) {
        float cap = 1e30;
        for (uint pass = 0u; pass < 2u; ++pass) {
            float lumaSum = 0.0;
            float count = 0.0;
            for (uint r = 0; r < rayBudget; ++r) {
                if (gsRays[r].w < 0.0 || any(isnan(gsRays[r].rgb))) {
                    continue;
                }
                lumaSum += min(Luma(DDGIDecodeRayRadiance(gsRays[r])), cap);
                count += 1.0;
            }
            float mean = count > 0.0 ? lumaSum / count : 0.0;
            cap = mean > 1e-8 ? DDGI_RELATIVE_RAY_CLAMP * mean : 1e30;
        }
        gsRayLumaCap = cap;
    }
    GroupMemoryBarrierWithGroupSync();

    int2 texel = int2(gtid.xy);
    bool bInterior = all(texel >= 1) && all(texel <= int(DDGI_IRRADIANCE_INTERIOR));
    if (bInterior) {
        float3 texelDirection = DDGIOctTexelDirection(texel, DDGI_IRRADIANCE_INTERIOR);
        float3 radianceSum = float3(0.0);
        float weightSum = 0.0;
        for (uint r = 0; r < rayBudget; ++r) {
            // Backface hits are excluded entirely (RTXGI); counting their weight darkens probes near geometry.
            float4 rayData = gsRays[r];
            if (rayData.w < 0.0 || any(isnan(rayData.rgb))) {
                continue;
            }
            float3 radiance = DDGIDecodeRayRadiance(rayData);
            float rayLuma = Luma(radiance);
            if (rayLuma > gsRayLumaCap) {
                radiance *= gsRayLumaCap / rayLuma;
            }
            float3 rayDirection = gsRayDirections[r];
            float weight = max(dot(texelDirection, rayDirection), 0.0);
            radianceSum += radiance * weight;
            weightSum += weight;
        }
        // pow(1/gamma) is storage only; blending in that space converges to (E[x^(1/gamma)])^gamma < E[x] and loses rare bright rays.
        float3 result = weightSum > 1e-4 ? radianceSum / weightSum : float3(0.0);

        if (pc.bHistoryValid != 0) {
            if (bSameCellPrev) {
                float4 previous = rdgStorageFloat4[pc.atlasInIndex][uint2(tileOrigin + texel)];
                float3 previousLinear = pow(max(previous.rgb, float3(0.0)), pc.volume.irradianceGamma);

                if (pc.bRestartValid != 0 && pc.probeRestart[probeIndex] != 0) {
                    // Relocation teleport or revival is a real change, not noise; skip the anti-flicker clamps.
                    result = lerp(result, previousLinear, max(pc.hysteresis - 0.75, 0.0));
                } else {
                    // Thresholds are fractions of the previous value, so they behave the same at any scene brightness.
                    float previousMax = DDGIMaxComponent(previousLinear);
                    float hysteresis = pc.hysteresis;
                    if (DDGIMaxComponent(previousLinear - result) > pc.irradianceThreshold * previousMax) {
                        // Never fully discard history here; a 1spp update is too noisy to trust alone.
                        hysteresis = max(hysteresis - 0.75, 0.1);
                    }
                    float3 delta = result - previousLinear;
                    if (DDGIMaxComponent(delta) > pc.brightnessThreshold * previousMax) {
                        delta *= 0.25;
                    }
                    float3 lerpDelta = (1.0 - hysteresis) * delta;
                    if (DDGIMaxComponent(result) < previousMax) {
                        float minDarkeningStep = previousMax / 1024.0;
                        lerpDelta = min(max(float3(minDarkeningStep), abs(lerpDelta)), abs(delta)) * sign(lerpDelta);
                    }
                    result = previousLinear + lerpDelta;
                }
            }
        }
        if (any(isnan(result))) {
            result = float3(0.0);
        }
        gsTexels[threadIndex] = float4(pow(max(result, float3(0.0)), 1.0 / pc.volume.irradianceGamma), 0.0);
    }
    GroupMemoryBarrierWithGroupSync();

    int2 source = DDGIOctBorderSource(texel, int(DDGI_IRRADIANCE_INTERIOR));
    rdgStorageFloat4[pc.atlasOutIndex][uint2(tileOrigin + texel)] = gsTexels[uint(source.y) * DDGI_IRRADIANCE_TILE + uint(source.x)];
}
