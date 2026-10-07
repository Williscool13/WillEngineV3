import common_interop;
import constants_interop;
import push_constant_interop;
import shadow_interop;
import bindless_declarations;
import shader_constants;
import shader_functions;
import random_common;

static const uint CSM_BLOCKER_TAPS = 12;
static const uint CSM_FILTER_TAPS = 16;
static const float CSM_MIN_FILTER_TEXELS = 1.0;
static const float CSM_MAX_FILTER_TEXELS = 24.0;
static const float CSM_MAX_RECEIVER_SLOPE = 4.0;
static const float GOLDEN_ANGLE = 2.39996323;

static float2 VogelDiskOffset(uint i, uint count, float phi)
{
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * GOLDEN_ANGLE + phi;
    return r * float2(cos(theta), sin(theta));
}

// d(clip.z) / d(clip.xy) across the receiver's plane, so wide kernels compare against the plane instead of the centre depth.
static float2 ReceiverDepthGradient(CSMCascadeGPU cascade, float3 n)
{
    float3 t1 = normalize(cross(n, abs(n.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0)));
    float3 t2 = cross(n, t1);
    float3 a = mul(cascade.viewProj, float4(t1, 0.0)).xyz;
    float3 b = mul(cascade.viewProj, float4(t2, 0.0)).xyz;
    float det = a.x * b.y - b.x * a.y;
    float2 num = float2(b.y * a.z - a.y * b.z, a.x * b.z - b.x * a.z);
    float numLength = length(num);
    float maxGradient = CSM_MAX_RECEIVER_SLOPE * cascade.halfExtent / cascade.depthRange;
    if (numLength == 0.0) {
        return float2(0.0);
    }
    if (numLength >= maxGradient * abs(det)) {
        return num / numLength * (det < 0.0 ? -maxGradient : maxGradient);
    }
    return num / det;
}

static float FilterCSMCascade(CSMResolvePushConstant pc, CSMCascadeGPU cascade, float3 worldN, float3 clip, float phi)
{
    float texelClip = 2.0 / float(pc.csmData->resolution);
    float2 gradient = ReceiverDepthGradient(cascade, worldN);
    float2 uvScale = 0.5 * cascade.atlasScaleOffset.xy;
    float2 uv = (clip.xy * 0.5 + 0.5) * cascade.atlasScaleOffset.xy + cascade.atlasScaleOffset.zw;
    float tanAngularRadius = pc.csmData->tanAngularRadius;
    float minRadius = CSM_MIN_FILTER_TEXELS * texelClip;
    float maxRadius = CSM_MAX_FILTER_TEXELS * texelClip;

    float radius = minRadius;
    if (pc.csmData->bPCSS != 0u && tanAngularRadius > 0.0) {
        float worldToClip = tanAngularRadius / cascade.halfExtent;
        float searchRadius = clamp((1.0 - clip.z) * cascade.depthRange * worldToClip, minRadius, maxRadius);
        float blockerSum = 0.0;
        uint blockerCount = 0u;
        for (uint i = 0u; i < CSM_BLOCKER_TAPS; ++i) {
            float2 o = VogelDiskOffset(i, CSM_BLOCKER_TAPS, phi) * searchRadius;
            int2 texel = int2((uv + o * uvScale) * float2(pc.atlasExtent));
            float blocker = rdgTexturesFloat[pc.atlasIndex].Load(int3(texel, 0)).r;
            if (blocker > clip.z + dot(gradient, o)) {
                blockerSum += blocker;
                blockerCount += 1u;
            }
        }
        if (blockerCount == 0u) {
            return 1.0;
        }
        float blockerDistance = (blockerSum / float(blockerCount) - clip.z) * cascade.depthRange;
        radius = clamp(blockerDistance * worldToClip, minRadius, maxRadius);
    }

    float visibility = 0.0;
    for (uint i = 0u; i < CSM_FILTER_TAPS; ++i) {
        float2 o = VogelDiskOffset(i, CSM_FILTER_TAPS, phi) * radius;
        visibility += rdgTexturesFloat[pc.atlasIndex].SampleCmpLevelZero(rdgCompareSamplers[RDG_LINEAR_DEPTH_SAMPLER_INDEX], uv + o * uvScale, clip.z + dot(gradient, o));
    }
    return visibility / float(CSM_FILTER_TAPS);
}

// R = sqrt(visibility) to match the ray traced sun shadow, G = (cascade + 1) / CSM_MAX_CASCADES, 0 outside every cascade.
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeCSMResolve(uniform CSMResolvePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (depth <= 0.0 || (gt1.w & 1u) == 0u) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, 0.0);
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewZ);
    float3 worldPos = mul(scene.invView, float4(viewPos, 1.0)).xyz;
    float3 worldN = normalize(mul((float3x3)scene.invView, UnpackNormalOct16(gt1.r)));

    float2 noise = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 11u);
    float inner = 1.0 - (CSM_MAX_FILTER_TEXELS + 1.0) * 2.0 / float(pc.csmData->resolution);
    float band = max(pc.csmData->blendBand, 1e-4) * inner;
    uint cascadeCount = pc.csmData->cascadeCount;
    for (uint c = 0; c < cascadeCount; ++c) {
        CSMCascadeGPU cascade = pc.csmData->cascades[c];
        float3 offsetPos = worldPos + worldN * (cascade.texelWorldSize * pc.csmData->normalOffsetTexels);
        float3 clip = mul(cascade.viewProj, float4(offsetPos, 1.0)).xyz;
        float edge = max(abs(clip.x), abs(clip.y));
        if (edge > inner || clip.z < 0.0 || clip.z > 1.0) {
            continue;
        }

        // Cascades nest, so the next one always holds a point in this one's edge band.
        float blend = saturate((edge - (inner - band)) / band);
        bool bLast = c + 1u == cascadeCount;
        if (!bLast && noise.y < blend) {
            continue;
        }

        float visibility = dot(worldN, pc.csmData->toSun.xyz) > 0.0 ? FilterCSMCascade(pc, cascade, worldN, clip, noise.x * 2.0 * PI) : 0.0;
        if (bLast) {
            visibility = lerp(visibility, 1.0, blend);
        }
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(sqrt(visibility), float(c + 1) / float(CSM_MAX_CASCADES));
        return;
    }

    rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, 0.0);
}
