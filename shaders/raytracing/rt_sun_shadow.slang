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
import constants_interop;
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

    // Map this (possibly half-res) output pixel to a silhouette-correct full-res source pixel (inlined quad selection). At full res this is the identity.
    int2 fullPixel;
    if (pc.pixelScale == 1u) {
        fullPixel = int2(pixel);
    } else {
        uint2 quadBase = pixel * pc.pixelScale;
        float2 quadUV = (float2(quadBase) + 1.0) / float2(scene.mainRenderTargetSize);
        float4 quadZ = rdgTexturesFloat[pc.depthIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUV);
        float4 quadZL = scene.depthLinearizeMult / (scene.depthLinearizeAdd - quadZ);
        float quadZMin = min(min(quadZL.x, quadZL.y), min(quadZL.z, quadZL.w));
        float quadZMax = max(max(quadZL.x, quadZL.y), max(quadZL.z, quadZL.w));
        bool quadUniform = (quadZMax - quadZMin) <= QUAD_UNIFORM_DEPTH_THRESHOLD * max(quadZMax, 1e-4);
        if (quadUniform) {
            uint4 quadN = rdgTexturesUInt4[pc.gbufferOneIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], quadUV);
            float3 quadN0 = UnpackNormalOct16(quadN.x);
            float quadMinDot = min(min(dot(quadN0, UnpackNormalOct16(quadN.y)), dot(quadN0, UnpackNormalOct16(quadN.z))), dot(quadN0, UnpackNormalOct16(quadN.w)));
            quadUniform = quadMinDot >= QUAD_UNIFORM_NORMAL_DOT_THRESHOLD;
        }
        uint quadSel;
        if (quadUniform) {
            quadSel = (pixel.x + pixel.y * 2u + pc.frameIndex) & 3u;
        } else {
            quadSel = 3u;
            float quadBest = quadZL.w;
            if (quadZL.z < quadBest) { quadBest = quadZL.z; quadSel = 2u; }
            if (quadZL.x < quadBest) { quadBest = quadZL.x; quadSel = 1u; }
            if (quadZL.y < quadBest) { quadBest = quadZL.y; quadSel = 0u; }
        }
        fullPixel = int2(quadBase + uint2(quadSel & 1u, (quadSel >> 1u) & 1u));
    }

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

    // Bias along the surface normal (not toward the camera, which slides the directional shadow sideways across the surface),
    // scaled by camera distance so a coarsely-reconstructed far position clears itself without anchoring the shadow off-place.
    float camDist = length(scene.cameraWorldPos.xyz - worldPos);
    float originBias = max(0.05, camDist * 0.001);

    RayDesc ray;
    ray.Origin = worldPos + worldN * originBias;
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
