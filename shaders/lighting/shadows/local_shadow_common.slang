module local_shadow_common;

import common_interop;
import constants_interop;
import lights_interop;
import shadow_interop;
import bindless_declarations;
import shader_constants;

static const uint LOCAL_SHADOW_BLOCKER_TAPS = 12;
static const uint LOCAL_SHADOW_FILTER_TAPS = 16;
static const float LOCAL_SHADOW_MIN_RADIUS_TEXELS = 1.0;
static const float LOCAL_SHADOW_MAX_RADIUS_TEXELS = 16.0;
static const float LOCAL_SHADOW_MAX_SLOPE_RATIO = 4.0;
static const float LOCAL_SHADOW_GOLDEN_ANGLE = 2.39996323;

static float2 LocalVogelOffset(uint i, uint count, float phi)
{
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * LOCAL_SHADOW_GOLDEN_ANGLE + phi;
    return r * float2(cos(theta), sin(theta));
}

static float LightSourceRadius(LightInfo light)
{
    if (light.type == LIGHT_TYPE_AREA) {
        return 0.5 * (light.right.w + light.up.w);
    }
    return light.right.w;
}

static float3 ProjectToAtlas(ShadowViewGPU view, float3 worldPos)
{
    float4 clip = mul(view.viewProj, float4(worldPos, 1.0));
    float3 ndc = clip.xyz / clip.w;
    return float3((ndc.xy * 0.5 + 0.5) * view.atlasScaleOffset.xy + view.atlasScaleOffset.zw, ndc.z);
}

// d(depth) / d(atlas uv) across the receiver's plane, so wide kernels compare against the plane instead of the centre depth.
static float2 ReceiverDepthGradient(ShadowViewGPU view, float3 worldPos, float3 worldN, float texelWorld, float3 center)
{
    float3 t1 = normalize(cross(worldN, abs(worldN.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0)));
    float3 t2 = cross(worldN, t1);
    float3 a = ProjectToAtlas(view, worldPos + t1 * texelWorld) - center;
    float3 b = ProjectToAtlas(view, worldPos + t2 * texelWorld) - center;
    float det = a.x * b.y - b.x * a.y;
    float2 num = float2(b.y * a.z - a.y * b.z, a.x * b.z - b.x * a.z);
    float numLength = length(num);
    if (numLength == 0.0) {
        return float2(0.0);
    }
    float typical = (abs(a.z) + abs(b.z)) / max(length(a.xy) + length(b.xy), 1e-12);
    float maxGradient = LOCAL_SHADOW_MAX_SLOPE_RATIO * typical;
    if (numLength >= maxGradient * abs(det)) {
        return num / numLength * (det < 0.0 ? -maxGradient : maxGradient);
    }
    return num / det;
}

/** Visibility of a local light from a shaded point, 1 when the light has no shadow map this frame. */
public float LocalShadowVisibility(LocalShadowData* data, LightInfo light, uint lightIndex, uint atlasIndex, float3 worldPos, float3 worldN, float2 noise)
{
    if (data == nullptr || lightIndex >= data->lightCount) {
        return 1.0;
    }
    uint packed = data->lightShadow[lightIndex];
    if (packed == LOCAL_SHADOW_NONE) {
        return 1.0;
    }

    uint viewIndex = packed & 0xFFFFu;
    uint faceMask = (packed >> LOCAL_SHADOW_FACE_SHIFT) & 0x3Fu;
    if (faceMask != 0u) {
        float3 d = worldPos - light.position.xyz;
        float3 a = abs(d);
        uint face = a.x >= a.y && a.x >= a.z ? (d.x >= 0.0 ? 0u : 1u) : a.y >= a.z ? (d.y >= 0.0 ? 2u : 3u) : (d.z >= 0.0 ? 4u : 5u);
        if ((faceMask & (1u << face)) == 0u) {
            return 1.0;
        }
        viewIndex += countbits(faceMask & ((1u << face) - 1u));
    }
    ShadowViewGPU view = data->views[viewIndex];
    float strength = float(packed >> LOCAL_SHADOW_STRENGTH_SHIFT) / float(LOCAL_SHADOW_STRENGTH_MAX);

    float texelWorld = view.texelSize * distance(worldPos, view.eye.xyz);
    float3 receiverPos = worldPos + worldN * (texelWorld * data->normalOffsetTexels);
    float4 clip = mul(view.viewProj, float4(receiverPos, 1.0));
    if (clip.w <= 0.0) {
        return 1.0;
    }
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }

    float3 center = float3((ndc.xy * 0.5 + 0.5) * view.atlasScaleOffset.xy + view.atlasScaleOffset.zw, ndc.z);
    float2 atlasTexel = 1.0 / float2(data->atlasExtent);
    float2 tileMin = view.atlasScaleOffset.zw + 0.5 * atlasTexel;
    float2 tileMax = view.atlasScaleOffset.zw + view.atlasScaleOffset.xy - 0.5 * atlasTexel;
    float2 gradient = ReceiverDepthGradient(view, receiverPos, worldN, texelWorld, center);
    float phi = noise.x * 2.0 * PI;

    float radius = LOCAL_SHADOW_MIN_RADIUS_TEXELS;
    if (data->bPCSS != 0u) {
        float lightSize = LightSourceRadius(light);
        float receiverDistance = clip.w;
        float searchRadius = clamp(lightSize / texelWorld, LOCAL_SHADOW_MIN_RADIUS_TEXELS, LOCAL_SHADOW_MAX_RADIUS_TEXELS);
        float blockerSum = 0.0;
        uint blockerCount = 0u;
        for (uint i = 0u; i < LOCAL_SHADOW_BLOCKER_TAPS; ++i) {
            float2 o = LocalVogelOffset(i, LOCAL_SHADOW_BLOCKER_TAPS, phi) * searchRadius * atlasTexel;
            float2 uv = clamp(center.xy + o, tileMin, tileMax);
            float blocker = rdgTexturesFloat[atlasIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv, 0).r;
            if (blocker > center.z + dot(gradient, uv - center.xy)) {
                blockerSum += blocker;
                blockerCount += 1u;
            }
        }
        if (blockerCount == 0u) {
            return 1.0;
        }
        // Reverse-Z depth back to distance along the view axis.
        float farPlane = view.nearPlane + view.depthRange;
        float blockerDepth = blockerSum / float(blockerCount);
        float blockerDistance = view.nearPlane * farPlane / (blockerDepth * view.depthRange + view.nearPlane);
        float penumbraWorld = lightSize * max(receiverDistance - blockerDistance, 0.0) / max(blockerDistance, 1e-4);
        radius = clamp(penumbraWorld / texelWorld, LOCAL_SHADOW_MIN_RADIUS_TEXELS, LOCAL_SHADOW_MAX_RADIUS_TEXELS);
    }

    float visibility = 0.0;
    for (uint i = 0u; i < LOCAL_SHADOW_FILTER_TAPS; ++i) {
        float2 o = LocalVogelOffset(i, LOCAL_SHADOW_FILTER_TAPS, phi) * radius * atlasTexel;
        float2 uv = clamp(center.xy + o, tileMin, tileMax);
        visibility += rdgTexturesFloat[atlasIndex].SampleCmpLevelZero(rdgCompareSamplers[RDG_LINEAR_DEPTH_SAMPLER_INDEX], uv, center.z + dot(gradient, uv - center.xy));
    }
    visibility /= float(LOCAL_SHADOW_FILTER_TAPS);
    return lerp(1.0, visibility, strength);
}
