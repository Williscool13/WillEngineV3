import common_interop;
import push_constant_interop;
import lights_interop;
import bindless_declarations;
import packing_functions;

import shader_functions;
import random_common;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dtid : SV_DispatchThreadID, uniform RTSunShadowPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, 0.0);
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float3 worldPos = ReconstructWorldPosition(scene.invViewProj, uv, depth);

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    float3 viewNormal = normalize(UnpackNormalOct16(gt1.r));
    float3 worldN = normalize(mul((float3x3)scene.invView, viewNormal));

    float3 toLight = normalize(-pc.lightData->directionalLight.directionIntensity.xyz);

    // Jitter the shadow ray within the sun-disk cone for soft penumbrae
    float angularRadius = pc.lightData->directionalLight.angularRadius;
    float3 sampleDir = toLight;
    if (angularRadius > 0.0)
    {
        uint h = SeedRng(pixel, pc.frameIndex, 0x6f3c12b7u);
        float u1 = HashStep(h);
        float u2 = HashStep(h);

        // Orthonormal basis with toLight as the cone axis.
        float3 up = abs(toLight.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        float3 tx = normalize(cross(up, toLight));
        float3 ty = cross(toLight, tx);

        // Uniform sample inside the cone of half-angle 'angularRadius'.
        float cosThetaMax = cos(angularRadius);
        float cosTheta = 1.0 - u1 * (1.0 - cosThetaMax);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        float phi = 6.28318530718 * u2;
        sampleDir = normalize(tx * (cos(phi) * sinTheta) + ty * (sin(phi) * sinTheta) + toLight * cosTheta);
    }

    // Sample direction below the surface horizon: occluded by the surface itself (soft terminator).
    if (dot(worldN, sampleDir) <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(0.0, 0.0);
        return;
    }

    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];

    RayDesc ray;
    ray.Origin = worldPos;
    ray.Direction = sampleDir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, 0, 0xFF, ray);
    q.Proceed();

    float visibility = 1.0;
    float hitDist = 0.0;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        visibility = 0.0;
        hitDist = q.CommittedRayT();
    }
    rdgStorageFloat2[pc.outputIndex][pixel] = float2(visibility, hitDist);
}
