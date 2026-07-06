import model_interop;
import push_constant_interop;
import ddgi_interop;
import ddgi_common;
import gpu_debug_draw;
import bindless_declarations;
import constants_interop;

// =============== Shader Summary ============
// DDGI probe volume passes. Trace: one thread per probe ray, ray query against the scene TLAS; misses fetch skybox radiance, hits are black for now (hit shading is the next checkpoint).
// Blend: one thread group per probe tile; each interior texel cosine-integrates the probe's rays into octahedral irradiance stored as pow(E, 1/gamma), hysteresis-blended in that perceptual space with RTXGI's change/brightness thresholds. Slots whose lattice cell changed since last frame (window scroll) or with no history start fresh. Border texels copy wrapped interior texels so bilinear taps stay seamless.
// Debug: one sphere per probe, L1 SH fitted from the probe's decoded atlas tile.

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeDDGIProbeTrace(uint3 dtid: SV_DispatchThreadID, uniform DDGIProbeTracePushConstant pc)
{
    uint rayIndex = dtid.x;
    uint probeIndex = dtid.y;
    if (rayIndex >= pc.raysPerProbe) {
        return;
    }

    int3 slot = DDGISlotFromProbeIndex(probeIndex, pc.volume);
    int3 cell = DDGICellFromSlot(slot, pc.volume);
    float3 direction = DDGIRotate(DDGISphericalFibonacci(rayIndex, pc.raysPerProbe), pc.rayRotation);

    RayDesc ray;
    ray.Origin = DDGIProbeWorldPosition(cell, pc.volume);
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = 10000.0;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, 0x01, ray);
    q.Proceed();

    float4 result;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        float hitT = q.CommittedRayT();
        // Backface hits mark "probe likely inside geometry" with a shortened negative distance, classic DDGI convention.
        result = float4(0.0, 0.0, 0.0, q.CommittedTriangleFrontFace() ? hitT : -0.2 * hitT);
    } else {
        float3 radiance = float3(0.0);
        if (pc.skyboxIndex >= 0) {
            // Matches the Y flip the visible skybox pass applies when sampling the cubemap.
            float3 skyDirection = float3(direction.x, -direction.y, direction.z);
            radiance = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb;
        }
        result = float4(radiance, 10000.0);
    }
    pc.rayData[probeIndex * pc.raysPerProbe + rayIndex] = result;
}

/** Border texel -> wrapped interior source texel; identity for interior texels. Corners copy the diagonally opposite interior corner. */
static int2 DDGIOctBorderSource(int2 texel)
{
    const int n = int(DDGI_IRRADIANCE_INTERIOR);
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
    int2 tileOrigin = DDGIProbeTileOrigin(slot, pc.volume);

    uint threadIndex = gtid.y * DDGI_IRRADIANCE_TILE + gtid.x;
    for (uint r = threadIndex; r < pc.raysPerProbe; r += DDGI_IRRADIANCE_TILE * DDGI_IRRADIANCE_TILE) {
        gsRays[r] = pc.rayData[probeIndex * pc.raysPerProbe + r];
    }
    GroupMemoryBarrierWithGroupSync();

    int2 texel = int2(gtid.xy);
    bool bInterior = all(texel >= 1) && all(texel <= int(DDGI_IRRADIANCE_INTERIOR));
    if (bInterior) {
        float3 texelDirection = DDGIOctTexelDirection(texel);
        float3 radianceSum = float3(0.0);
        float weightSum = 0.0;
        for (uint r = 0; r < pc.raysPerProbe; ++r) {
            float3 rayDirection = DDGIRotate(DDGISphericalFibonacci(r, pc.raysPerProbe), pc.rayRotation);
            float weight = max(dot(texelDirection, rayDirection), 0.0);
            radianceSum += gsRays[r].rgb * weight;
            weightSum += weight;
        }
        // The EMA runs in perceptual space (pow 1/gamma): a rare bright ray moves the encoded value little, so small
        // sky apertures stop pulsing. Thresholds and the delta clamp follow RTXGI's probe blending.
        float3 result = weightSum > 1e-4 ? radianceSum / weightSum : float3(0.0);
        result = pow(result, 1.0 / pc.irradianceGamma);

        if (pc.bHistoryValid != 0) {
            DDGIVolumeParams previousVolume = pc.volume;
            previousVolume.baseCell = pc.previousBaseCell;
            if (all(DDGICellFromSlot(slot, previousVolume) == DDGICellFromSlot(slot, pc.volume))) {
                float3 previous = rdgTexturesFloat4[pc.atlasHistoryIndex].Load(int3(tileOrigin + texel, 0)).rgb;

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
        gsTexels[threadIndex] = result;
    }
    GroupMemoryBarrierWithGroupSync();

    int2 source = DDGIOctBorderSource(texel);
    float3 value = gsTexels[uint(source.y) * DDGI_IRRADIANCE_TILE + uint(source.x)];
    rdgStorageFloat4[pc.atlasOutIndex][uint2(tileOrigin + texel)] = float4(value, 1.0);
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

    // Least-squares L1 fit over the tile: E(n) ~ dc + dot(dir, n), with dir = 3 * mean(E * n) since mean(n n^T) = I/3 over the sphere.
    int2 tileOrigin = DDGIProbeTileOrigin(slot, pc.volume);
    float3 dc = float3(0.0);
    float3 dirR = float3(0.0);
    float3 dirG = float3(0.0);
    float3 dirB = float3(0.0);
    for (int ty = 1; ty <= int(DDGI_IRRADIANCE_INTERIOR); ++ty) {
        for (int tx = 1; tx <= int(DDGI_IRRADIANCE_INTERIOR); ++tx) {
            float3 encoded = rdgTexturesFloat4[pc.irradianceAtlasIndex].Load(int3(tileOrigin + int2(tx, ty), 0)).rgb;
            float3 irradiance = pow(max(encoded, float3(0.0)), pc.irradianceGamma);
            float3 texelDirection = DDGIOctTexelDirection(int2(tx, ty));
            dc += irradiance;
            dirR += irradiance.r * texelDirection;
            dirG += irradiance.g * texelDirection;
            dirB += irradiance.b * texelDirection;
        }
    }
    float invCount = 1.0 / float(DDGI_IRRADIANCE_INTERIOR * DDGI_IRRADIANCE_INTERIOR);
    dc *= invCount;
    dirR *= 3.0 * invCount;
    dirG *= 3.0 * invCount;
    dirB *= 3.0 * invCount;

    GPUDebugAddSphere(pc.sphereArgs, pc.sphereBuffer, position, radius,
                      float4(dc.r, dirR), float4(dc.g, dirG), float4(dc.b, dirB));
}
