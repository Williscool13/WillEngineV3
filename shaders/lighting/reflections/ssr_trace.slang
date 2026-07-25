import common_interop;
import push_constant_interop;
import reflection_interop;

import shader_functions;
import packing_functions;
import restir_common;
import random_common;

import bindless_declarations;

static const float SSR_RAY_MAX_DISTANCE = 100.0;
static const float SSR_STRIDE_START = 2.0;
static const float SSR_STRIDE_GROWTH = 1.15;
static const uint SSR_REFINE_STEPS = 4u;

// Screen-space producer for the Default path: same GGX ray as reflection_trace, marched on depth_copy instead of the TLAS.
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeSSRTrace(uniform SSRTracePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    uint field = pc.activeCheckerboardField;
    if (field != 0u && !IsActiveCheckerboardPixel(int2(pixel), false, field)) {
        return;
    }

    uint pixelIdx = ReservoirIndex(PixelPosToReservoirPos(pixel, field), pc.renderExtent.x, field);

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
    // reflection_shade reconstructs this same origin to turn hitT back into a world position
    float3 rayOrigin = ReflectionRayOrigin(worldPos, worldN, viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(N, V)));
    float3 rayDir = normalize(mul(float3x3(scene.invView), brdfL));
    refl.rayDir = rayDir;

    // clip.w is the linear eye depth (the perspective w-row is -viewZ), so 1/w is linear across the screen segment.
    float3 rayEnd = rayOrigin + rayDir * SSR_RAY_MAX_DISTANCE;
    float4 clipStart = mul(scene.viewProj, float4(rayOrigin, 1.0));
    float4 clipEnd = mul(scene.viewProj, float4(rayEnd, 1.0));
    if (clipStart.w <= 0.0) {
        refl.hitT = REFLECTION_HIT_T_SKY;
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }
    if (clipEnd.w <= 0.0) {
        float tClip = (clipStart.w - 1e-3) / (clipStart.w - clipEnd.w);
        clipEnd = lerp(clipStart, clipEnd, tClip);
    }

    float2 extentF = float2(pc.renderExtent);
    float2 screenStart = (clipStart.xy / clipStart.w * 0.5 + 0.5) * extentF;
    float2 screenEnd = (clipEnd.xy / clipEnd.w * 0.5 + 0.5) * extentF;
    float invWStart = 1.0 / clipStart.w;
    float invWEnd = 1.0 / clipEnd.w;

    float2 segment = screenEnd - screenStart;
    float segmentLength = length(segment);
    if (segmentLength < 1.0) {
        refl.hitT = REFLECTION_HIT_T_SKY;
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }
    float2 segmentDir = segment / segmentLength;

    float2 marchNoise = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 7u);
    float stride = SSR_STRIDE_START;
    float travelled = SSR_STRIDE_START * marchNoise.x;
    float prevTravelled = 0.0;
    bool bHit = false;
    float hitTravelled = 0.0;

    for (uint step = 0u; step < pc.ssrMaxSteps; ++step) {
        travelled += stride;
        if (travelled >= segmentLength) {
            break;
        }
        float2 marchPixel = screenStart + segmentDir * travelled;
        if (any(marchPixel < float2(0.0)) || any(marchPixel >= extentF)) {
            break;
        }
        float sampleDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(int2(marchPixel), 0)).r;
        if (sampleDepth > 0.0) {
            float sceneLinearDepth = ScreenToViewSpaceDepth(sampleDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float rayLinearDepth = 1.0 / lerp(invWStart, invWEnd, travelled / segmentLength);

            if (rayLinearDepth - sceneLinearDepth > 0.0) {
                bHit = true;
                hitTravelled = travelled;
                break;
            }
        }
        prevTravelled = travelled;
        stride *= SSR_STRIDE_GROWTH;
    }

    if (!bHit) {
        refl.hitT = REFLECTION_HIT_T_SKY;
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }

    float lo = prevTravelled;
    float hi = hitTravelled;
    for (uint r = 0u; r < SSR_REFINE_STEPS; ++r) {
        float mid = 0.5 * (lo + hi);
        float2 midPixel = screenStart + segmentDir * mid;
        float sampleDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(int2(midPixel), 0)).r;
        float sceneLinearDepth = sampleDepth > 0.0 ? ScreenToViewSpaceDepth(sampleDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd) : 1e30;
        float rayLinearDepth = 1.0 / lerp(invWStart, invWEnd, mid / segmentLength);
        if (rayLinearDepth - sceneLinearDepth > 0.0) {
            hi = mid;
        } else {
            lo = mid;
        }
    }

    float2 hitPixel = screenStart + segmentDir * hi;
    int2 hitCoord = clamp(int2(hitPixel), int2(0, 0), int2(pc.renderExtent) - int2(1, 1));

    float refinedSceneDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(hitCoord, 0)).r;
    if (refinedSceneDepth <= 0.0) {
        refl.hitT = REFLECTION_HIT_T_SKY;
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }
    float refinedSceneLinear = ScreenToViewSpaceDepth(refinedSceneDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float refinedRayLinear = 1.0 / lerp(invWStart, invWEnd, hi / segmentLength);
    if (refinedRayLinear - refinedSceneLinear > pc.ssrThickness) {
        refl.hitT = REFLECTION_HIT_T_SKY;
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }

    float3 hitNormal = normalize(mul(float3x3(scene.invView), normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(hitCoord, 0)).r))));
    if (dot(hitNormal, rayDir) >= 0.0) {
        refl.hitT = REFLECTION_HIT_T_SKY;
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }

    // Stochastic screen-edge fade: acceptance falls to 0 across the outer border band.
    float2 hitUV = (float2(hitCoord) + 0.5) / extentF;
    float2 edgeDist = min(hitUV, float2(1.0) - hitUV);
    float edgeAccept = saturate(min(edgeDist.x, edgeDist.y) / max(pc.edgeFade, 1e-4));
    if (marchNoise.y > edgeAccept) {
        refl.hitT = REFLECTION_HIT_T_SKY;
        pc.reflectionDescriptors[pixelIdx] = refl;
        return;
    }

    float hitViewDepth = refinedSceneLinear;
    float3 hitViewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, hitUV, hitViewDepth);
    float3 hitWorld = mul(scene.invView, float4(hitViewPos, 1.0)).xyz;

    refl.hitT = length(hitWorld - rayOrigin);
    refl.bary = hitUV;
    refl.instanceID = REFLECTION_INSTANCE_NONE;
    refl.primIndex = 0u;
    pc.reflectionDescriptors[pixelIdx] = refl;
}
