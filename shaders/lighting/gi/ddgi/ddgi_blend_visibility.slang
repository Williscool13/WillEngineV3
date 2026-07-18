import push_constant_interop;
import ddgi_interop;
import ddgi_common;
import bindless_declarations;
import shader_functions;

// =============== Shader Summary ============
// DDGI blend visibility: one thread group per probe tile at 16x16; each texel integrates mean/mean^2 ray distance with a sharpened (pow distanceExponent) cosine lobe, for the Chebyshev occlusion test at sampling. Border texels copy wrapped interior texels so bilinear taps stay seamless.

groupshared float gsRayDistances[DDGI_MAX_RAYS_PER_PROBE];
groupshared float3 gsVisRayDirections[DDGI_MAX_RAYS_PER_PROBE];
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
            gsVisRayDirections[r] = DDGIRotate(DDGISphericalFibonacci(r, rayBudget), pc.rayRotation);
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
            float3 rayDirection = gsVisRayDirections[r];
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
