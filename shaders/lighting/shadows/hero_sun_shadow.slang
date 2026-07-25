/*
 * Deterministic sun shadow cast by hero geometry alone (INSTANCE_MASK_HERO), which the traced sun shadow excludes.
 * The occluder set is a single character, so the sun-disk visibility integral is solved by fixed quadrature instead of
 * Monte-Carlo: every pixel traces the SAME stratified sun-disk directions every frame, so the result carries no noise
 * and needs no denoiser or history. Pixels whose sun ray misses the hero's swept bounds early-out before any tracing.
 * Output R8: fraction of the sun disk surviving the hero, a multiplier for the sun the consumer already has.
 * Consumers must scale by it, never subtract it: their value is denoised while this one is pixel-sharp, and
 * differencing the two leaves a residue that is invisible in full sun but glaring once the hero has darkened the pixel.
 */
import common_interop;
import constants_interop;
import push_constant_interop;
import lights_interop;
import instance_mask_interop;
import bindless_declarations;
import packing_functions;
import shader_functions;

/** Slab test of the ray against the AABB. Returns false when the box is entirely behind the origin. */
static bool IntersectAABB(float3 origin, float3 direction, float3 boundsMin, float3 boundsMax, out float tExit)
{
    float3 invDir = 1.0 / direction;
    float3 t0 = (boundsMin - origin) * invDir;
    float3 t1 = (boundsMax - origin) * invDir;
    float3 tSmall = min(t0, t1);
    float3 tBig = max(t0, t1);
    float tEnter = max(max(tSmall.x, tSmall.y), tSmall.z);
    tExit = min(min(tBig.x, tBig.y), tBig.z);
    return tExit >= max(tEnter, 0.0);
}

/** i-th of `count` fixed sun-cone directions: golden-angle spiral, uniform in solid angle to match the traced sun shadow. */
static float3 SunDiskDirection(float3 toLight, float angularRadius, uint i, uint count)
{
    float3 up = abs(toLight.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, toLight));
    float3 b = cross(toLight, t);

    float cosThetaMax = cos(angularRadius);
    float cosTheta = 1.0 - ((float(i) + 0.5) / float(count)) * (1.0 - cosThetaMax);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = float(i) * 2.39996322972865332;
    return normalize(sinTheta * cos(phi) * t + sinTheta * sin(phi) * b + cosTheta * toLight);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeHeroSunShadow(uint3 dtid : SV_DispatchThreadID, uniform HeroSunShadowPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    rdgStorageFloat[pc.outputIndex][pixel] = 1.0;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float3 worldPos = ReconstructWorldPosition(scene.invViewProj, uv, depth);

    float3 toLight = normalize(-pc.lightData->directionalLight.directionIntensity.xyz);

    // Footprint test firs
    float angularRadius = pc.lightData->directionalLight.angularRadius;
    float3 boundsCenter = (pc.heroBoundsMin.xyz + pc.heroBoundsMax.xyz) * 0.5;
    float margin = (length(boundsCenter - worldPos) + length(pc.heroBoundsMax.xyz - boundsCenter)) * tan(angularRadius);

    float tExit;
    if (!IntersectAABB(worldPos, toLight, pc.heroBoundsMin.xyz - margin, pc.heroBoundsMax.xyz + margin, tExit)) {
        return;
    }

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    float3 viewNormal = normalize(UnpackNormalOct16(gt1.r));
    float3 worldN = normalize(mul((float3x3)scene.invView, viewNormal));

    if (dot(worldN, toLight) <= 0.0) {
        return;
    }

    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float footprint = RayOriginPixelFootprint(viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(viewNormal, normalize(-viewPosition))));

    // Origin matches rt_sun_shadow and RestirSunShadowVisible: no NoL division, which would slide the shadow sideways.
    RayDesc ray;
    ray.Origin = OffsetGBufferRayOrigin(worldPos, worldN, footprint, 1.0);
    ray.TMin = 0.0;
    ray.TMax = tExit;

    RaytracingAccelerationStructure tlas = rdgAccelerationStructures[pc.tlasIndex];
    uint blockedCount = 0;
    for (uint i = 0; i < pc.sampleCount; i++) {
        ray.Direction = SunDiskDirection(toLight, angularRadius, i, pc.sampleCount);

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(tlas, 0, INSTANCE_MASK_HERO, ray);
        q.Proceed();
        if (q.CommittedStatus() != COMMITTED_NOTHING) {
            blockedCount++;
        }
    }

    rdgStorageFloat[pc.outputIndex][pixel] = 1.0 - float(blockedCount) / float(pc.sampleCount);
}
