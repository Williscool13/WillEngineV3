import push_constant_interop;
import ddgi_interop;
import ddgi_common;
import gpu_debug_draw_common;
import bindless_declarations;
import packing_functions;

// =============== Shader Summary ============
// DDGI probe debug: one sphere per probe at its relocated position, L1 SH fitted from the probe's decoded atlas tile; dead probes render flat red, inactive probes flat blue (or hidden).
// debugMode 1 fits the visibility atlas instead: red = mean distance / miss clamp per direction, green = std/mean; a red-hot lobe is a miss-inflated mean that bypasses the Chebyshev gate.

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

    if (pc.debugMode == 1u) {
        int2 visTileOrigin = DDGIProbeTileOrigin(slot, pc.volume, DDGI_VISIBILITY_TILE);
        float maxDistance = length(pc.volume.probeSpacing) * 1.5;
        float dcMean = 0.0;
        float dcStd = 0.0;
        float3 dirMean = float3(0.0);
        float3 dirStd = float3(0.0);
        for (int ty = 1; ty <= int(DDGI_VISIBILITY_INTERIOR); ++ty) {
            for (int tx = 1; tx <= int(DDGI_VISIBILITY_INTERIOR); ++tx) {
                float2 moments = rdgTexturesFloat2[pc.visibilityAtlasIndex].Load(int3(visTileOrigin + int2(tx, ty), 0)).rg;
                float mean = moments.x / maxDistance;
                float stdRatio = sqrt(abs(moments.y - moments.x * moments.x)) / max(moments.x, 1e-4);
                float3 texelDirection = DDGIOctTexelDirection(int2(tx, ty), DDGI_VISIBILITY_INTERIOR);
                dcMean += mean;
                dirMean += mean * texelDirection;
                dcStd += stdRatio;
                dirStd += stdRatio * texelDirection;
            }
        }
        float invVisCount = pc.probeDebugExposure / float(DDGI_VISIBILITY_INTERIOR * DDGI_VISIBILITY_INTERIOR);
        GPUDebugAddSphere(pc.sphereArgs, pc.sphereBuffer, position, radius,
                          float4(dcMean * invVisCount, dirMean * 3.0 * invVisCount),
                          float4(dcStd * invVisCount, dirStd * 3.0 * invVisCount),
                          float4(0.0));
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
