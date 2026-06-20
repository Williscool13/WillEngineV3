/*
 * Sun-shadow trace for the SIGMA denoiser (NRD input convention).
 * Stochastic 1spp: one ray per pixel, jittered uniformly inside the sun's angular cone (half-angle =
 * angularRadius), so partial occlusion in the penumbra shows up as Monte-Carlo binary visibility that
 * SIGMA's spatial blur + temporal stabilization reconstruct into the true soft shadow. Each occluded
 * sample also packs a penumbra radius (0.5 * distanceToOccluder * tan(angularRadius)) to drive the blur
 * kernel size; lit samples carry FP16_MAX.
 * Output R16G16: .r = binary visibility (1 lit / 0 occluded), .g = packed penumbra.
 */
import common_interop;
import push_constant_interop;
import lights_interop;
import bindless_declarations;
import packing_functions;

import shader_functions;
import restir_common;
import sigma_utils;
import random_common;

/** Uniformly sample a ray direction inside the sun's angular cone (half-angle = angularRadius) around the mean direction. */
static float3 SampleSunConeDirection(float3 toLight, float angularRadius, float u1, float u2)
{
    float cosThetaMax = cos(angularRadius);
    float cosTheta = lerp(1.0, cosThetaMax, u1);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = 6.28318530718 * u2;

    float3 w = toLight;
    float3 up = abs(w.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, w));
    float3 b = cross(w, t);
    return normalize(sinTheta * cos(phi) * t + sinTheta * sin(phi) * b + cosTheta * w);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dtid : SV_DispatchThreadID, uniform RTSunShadowPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    // Map this (possibly half-res) output pixel to a silhouette-correct full-res source pixel, then read the full-res depth/gbuffer there. At full res this is the identity.
    int2 fullPixel = FetchFullPixel(pc.quadSelectionIndex, int2(pixel), pc.pixelScale);

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(fullPixel, 0));

    if (pc.pixelScale > 1u) {
        rdgStorageFloat[pc.outputDepthIndex][pixel] = depth;
        rdgStorageUInt4[pc.outputGbufferIndex][pixel] = gt1;
    }

    if (depth <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, SIGMA_FP16_MAX);
        return;
    }

    float2 uv = (float2(fullPixel) + 0.5) / float2(pc.fullExtent);
    float3 worldPos = ReconstructWorldPosition(scene.invViewProj, uv, depth);

    float3 viewNormal = normalize(UnpackNormalOct16(gt1.r));
    float3 worldN = normalize(mul((float3x3)scene.invView, viewNormal));

    float3 toLight = normalize(-pc.lightData->directionalLight.directionIntensity.xyz);

    // Surface faces away from the sun: fully self-shadowed (umbra), no ray needed.
    if (dot(worldN, toLight) <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(0.0, 0.0);
        return;
    }

    uint rng = SeedRng(pixel, pc.frameIndex, 0x515A4Bu);
    float u1 = HashStep(rng);
    float u2 = HashStep(rng);
    float3 rayDir = SampleSunConeDirection(toLight, pc.lightData->directionalLight.angularRadius, u1, u2);

    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];

    float3 toCamera = scene.cameraWorldPos.xyz - worldPos;
    float camDist = length(toCamera);
    float originBias = max(0.05, camDist * 0.001);

    RayDesc ray;
    ray.Origin = worldPos + (toCamera / camDist) * originBias;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, 0, 0x01, ray);
    q.Proceed();

    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        float hitDist = q.CommittedRayT();
        float tanAngularRadius = tan(pc.lightData->directionalLight.angularRadius);
        float penumbra = min(0.5 * hitDist * tanAngularRadius, 32768.0);
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(0.0, penumbra);
    } else {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, SIGMA_FP16_MAX);
    }
}
