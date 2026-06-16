/*
 * Sun-shadow trace for the SIGMA denoiser (matches NRD's input convention).
 * One ray toward the light CENTER -> clean binary visibility (no Monte-Carlo noise). The soft
 * penumbra is synthesized later by SIGMA's penumbra-sized blur, so here we only PACK the penumbra
 * radius per pixel: penumbra = 0.5 * distanceToOccluder * tan(sunAngularRadius), or FP16_MAX if lit.
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

    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];

    RayDesc ray;
    ray.Origin = worldPos;
    ray.Direction = toLight;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, 0, 0xFF, ray);
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
