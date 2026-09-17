import common_interop;
import push_constant_interop;
import instance_mask_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;
import random_common;
import gi_gather_common;

static const float GI_GATHER_RAY_MIN_OFFSET = 0.02;

static float3 SampleCosineHemisphere(float3 n, float u1, float u2)
{
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return normalize(r * cos(phi) * t + r * sin(phi) * b + sqrt(max(1.0 - u1, 0.0)) * n);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeGIGatherTrace(uniform GIGatherPushConstant pc, uint3 dtid: SV_DispatchThreadID)
{
    uint2 gatherPixel = dtid.xy;
    if (any(gatherPixel >= pc.gatherExtent)) {
        return;
    }

    uint2 pixel = GIGatherRepresentativePixel(gatherPixel, pc.renderExtent, pc.gatherScale);

    uint rayCount = 0u;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (pc.bSkipRay == 0u && depth > 0.0 && (gt1.w & 1u) != 0u) {
        SceneData scene = pc.sceneData[pc.sceneDataIndex];
        float3 viewNormal = UnpackNormalOct16(gt1.r);
        float3 worldN = normalize(mul((float3x3)scene.invView, viewNormal));

        float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
        float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
        float3 worldPosition = mul(scene.invView, float4(viewPosition, 1.0)).xyz;

        float varGuide = 0.0;
        if (pc.varGuideHistoryIndex != ~0x0u) {
            varGuide = rdgTexturesFloat[pc.varGuideHistoryIndex].Load(int3(gatherPixel, 0)).r;
        }

        float relDevMax = WaveActiveMax(varGuide) * GI_GATHER_VARIANCE_GUIDE_SCALE;
        float rayLerp = saturate((relDevMax - GI_GATHER_VARIANCE_RAY_DEADZONE) / (GI_GATHER_VARIANCE_GUIDE_SCALE - GI_GATHER_VARIANCE_RAY_DEADZONE));
        rayCount = clamp(uint(round(lerp(1.0, float(pc.rayCount), rayLerp))), 1u, max(pc.rayCount, 1u));

        float footprint = RayOriginPixelFootprint(viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(normalize(viewNormal), normalize(-viewPosition))));
        float3 rayOrigin = worldPosition + worldN * max(GI_GATHER_RAY_MIN_OFFSET, footprint * 2.0);
        uint hitBase = (gatherPixel.y * pc.gatherExtent.x + gatherPixel.x) * max(pc.rayCount, 1u);

        for (uint sampleIndex = 0u; sampleIndex < rayCount; ++sampleIndex) {
            float2 xi = SpatioTemporalBlueNoise2D(gatherPixel, pc.frameIndex, sampleIndex);
            float3 direction = SampleCosineHemisphere(worldN, xi.x, xi.y);

            RayDesc ray;
            ray.Origin = rayOrigin;
            ray.Direction = direction;
            ray.TMin = 0.0;
            ray.TMax = 10000.0;

            RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
            q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_DDGI, ray);
            q.Proceed();

            GIGatherHit hit;
            hit.direction = direction;
            if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                hit.hitT = q.CommittedRayT();
                hit.bary = q.CommittedTriangleBarycentrics();
                hit.instanceID = q.CommittedInstanceID();
                hit.primIndex = q.CommittedPrimitiveIndex() | (q.CommittedTriangleFrontFace() ? GI_GATHER_HIT_FRONT_FACE_BIT : 0u);
            } else {
                hit.hitT = GI_GATHER_HIT_SKY;
                hit.bary = float2(0.0, 0.0);
                hit.instanceID = 0u;
                hit.primIndex = 0u;
            }
            pc.hitBuffer[hitBase + sampleIndex] = hit;
        }
    }

    rdgStorageFloat[pc.rayMetaIndex][gatherPixel] = float(rayCount);
}
