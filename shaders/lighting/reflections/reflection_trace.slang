import common_interop;
import push_constant_interop;
import reflection_interop;
import instance_mask_interop;

import shader_functions;
import packing_functions;
import restir_common;
import random_common;

import bindless_declarations;

// Dedicated producer for the non-ReSTIR path, where no BRDF ray exists to piggyback.
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReflectionTrace(uniform ReflectionTracePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    uint pixelIdx = pixel.y * pc.renderExtent.x + pixel.x;

    // Every pixel writes exactly once, so no separate clear pass is needed. OWNED is the "contribute nothing" default.
    ReflectionHitDescriptor refl;
    refl.rayDir = float3(0.0);
    refl.hitT = REFLECTION_HIT_T_OWNED;
    refl.bary = float2(0.0);
    refl.instanceID = ~0u;
    refl.primIndex = ~0u;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    float roughness;
    float metallic;
    UnpackPBR(gt1.b, roughness, metallic);
    roughness = max(roughness, 0.045);
    if (roughness > pc.roughnessMax) {
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 N = normalize(UnpackNormalOct16(gt1.r));
    float3 V = normalize(-viewPosition);

    float pdf;
    float2 xi = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 2u);
    float3 brdfL = ImportanceSampleGGX(xi, N, V, roughness, pdf);
    if (dot(N, brdfL) <= 0.0) {
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 worldN = normalize(mul(float3x3(scene.invView), N));

    RayDesc ray;
    // reflection_shade reconstructs this same origin to turn hitT back into a world position
    ray.Origin = worldPos + worldN * 0.05;
    ray.Direction = normalize(mul(float3x3(scene.invView), brdfL));
    ray.TMin = 0.001;
    ray.TMax = 1e30;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_ALL, ray);
    q.Proceed();

    refl.rayDir = ray.Direction;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        refl.hitT = q.CommittedRayT();
        refl.bary = q.CommittedTriangleBarycentrics();
        refl.instanceID = q.CommittedInstanceID();
        refl.primIndex = q.CommittedPrimitiveIndex();
    } else {
        refl.hitT = REFLECTION_HIT_T_SKY;
    }

    pc.reflectionDescriptors[pixelIdx] = refl;
}
