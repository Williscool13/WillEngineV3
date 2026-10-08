//
// Created by William on 2026-10-08.
//

#include "local_shadow_views.h"

#include <bit>
#include <glm/gtc/matrix_transform.hpp>

#include "render/types/render_types.h"

namespace Render
{
static constexpr float LOCAL_SHADOW_HYSTERESIS = 1.5f;

static constexpr glm::vec3 CUBE_AXES[6] = {{1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
static constexpr glm::vec3 CUBE_UPS[6] = {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};

static bool IsShadowEligible(const LightInfo& light)
{
    const bool bType = light.type == LIGHT_TYPE_SPHERE || light.type == LIGHT_TYPE_AREA || light.type == LIGHT_TYPE_DISK;
    return bType && (light.flags & LIGHT_FLAG_CAST_SHADOWS) != 0u && light.intensity > 0.0f && light.range > 0.0f;
}

bool IsLocalShadowSpot(const LightInfo& light)
{
    return light.type != LIGHT_TYPE_SPHERE && light.position.w >= LOCAL_SHADOW_SPOT_COS_MIN;
}

static float NearPlane(const LightInfo& light)
{
    // Past the sphere's own surface, which would otherwise occlude everything.
    return light.type == LIGHT_TYPE_SPHERE ? glm::max(light.right.w * 1.05f, LOCAL_SHADOW_NEAR) : LOCAL_SHADOW_NEAR;
}

/** Apex and far corners of a view pyramid cut off at the light's range. */
static void PyramidPoints(const glm::vec3& apex, const glm::vec3& axis, const glm::vec3& up, float tanHalf, float range, glm::vec3 (&points)[5])
{
    const glm::vec3 right = glm::normalize(glm::cross(axis, up));
    const glm::vec3 trueUp = glm::cross(right, axis);
    points[0] = apex;
    for (uint32_t c = 0; c < 4; ++c) {
        const float x = (c & 1u) ? tanHalf : -tanHalf;
        const float y = (c & 2u) ? tanHalf : -tanHalf;
        points[c + 1] = apex + range * (axis + right * x + trueUp * y);
    }
}

static bool IsPyramidVisible(const glm::vec3 (&points)[5], const Frustum& camera)
{
    for (const glm::vec4& plane : camera.planes) {
        bool bAllOut = true;
        for (const glm::vec3& point : points) {
            if (!(glm::dot(glm::vec3(plane), point) + plane.w < 0.0f)) {
                bAllOut = false;
                break;
            }
        }
        if (bAllOut) {
            return false;
        }
    }
    return true;
}

static glm::vec3 SpotUp(const glm::vec3& forward)
{
    return glm::abs(forward.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
}

static float SpotTanHalf(const LightInfo& light)
{
    return glm::tan(glm::acos(glm::clamp(light.position.w, 0.0f, 1.0f)) + LOCAL_SHADOW_CONE_MARGIN);
}

static bool IsSpotVisible(const LightInfo& light, const Frustum& camera)
{
    const glm::vec3 forward = glm::normalize(glm::vec3(light.normal));
    glm::vec3 points[5];
    PyramidPoints(glm::vec3(light.position), forward, SpotUp(forward), SpotTanHalf(light), light.range, points);
    return IsPyramidVisible(points, camera);
}

static uint32_t CubeFaceMask(const LightInfo& light, const Frustum& camera)
{
    const glm::vec3 position(light.position);
    const glm::vec3 normal(light.normal);
    const bool bOneSided = light.type != LIGHT_TYPE_SPHERE;
    uint32_t mask = 0;
    for (uint32_t f = 0; f < 6; ++f) {
        glm::vec3 points[5];
        PyramidPoints(position, CUBE_AXES[f], CUBE_UPS[f], 1.0f, light.range, points);

        bool bBehindEmitter = bOneSided;
        for (uint32_t c = 1; c < 5 && bBehindEmitter; ++c) {
            bBehindEmitter = glm::dot(points[c] - position, normal) <= 0.0f;
        }
        if (!bBehindEmitter && IsPyramidVisible(points, camera)) {
            mask |= 1u << f;
        }
    }
    return mask;
}

static ShadowViewGPU MakePerspectiveView(const glm::vec3& eye, const glm::vec3& forward, const glm::vec3& up, float tanHalf, float nearPlane, float farPlane, uint32_t tile, glm::uvec2 tiles,
                                         uint32_t resolution)
{
    const glm::mat4 view = glm::lookAt(eye, eye + forward, up);

    // Reverse-Z (near 1, far 0), y negated to match the CSM views' winding.
    glm::mat4 proj(0.0f);
    proj[0][0] = 1.0f / tanHalf;
    proj[1][1] = -1.0f / tanHalf;
    proj[2][2] = nearPlane / (farPlane - nearPlane);
    proj[2][3] = -1.0f;
    proj[3][2] = nearPlane * farPlane / (farPlane - nearPlane);

    const glm::vec2 scale = 1.0f / glm::vec2(tiles);
    ShadowViewGPU out{};
    out.viewProj = proj * view;
    out.frustum = CreateFrustum(out.viewProj);
    out.atlasScaleOffset = glm::vec4(scale, static_cast<float>(tile % tiles.x) * scale.x, static_cast<float>(tile / tiles.x) * scale.y);
    out.eye = glm::vec4(eye, 1.0f);
    out.texelSize = 2.0f * tanHalf / static_cast<float>(resolution);
    out.depthRange = farPlane - nearPlane;
    out.nearPlane = nearPlane;
    return out;
}

uint32_t LocalShadowFaceMask(const LightInfo& light, const Frustum& camera)
{
    if (!IsShadowEligible(light)) {
        return 0;
    }
    if (IsLocalShadowSpot(light)) {
        return IsSpotVisible(light, camera) ? 1u : 0u;
    }
    return CubeFaceMask(light, camera);
}

uint32_t LocalShadowViewCount(const LightInfo& light, const Frustum& camera)
{
    return static_cast<uint32_t>(std::popcount(LocalShadowFaceMask(light, camera)));
}

glm::uvec2 LocalShadowAtlasTiles(uint32_t viewBudget)
{
    const uint32_t views = glm::clamp(viewBudget, 1u, LOCAL_SHADOW_MAX_VIEWS);
    const auto columns = static_cast<uint32_t>(glm::ceil(glm::sqrt(static_cast<float>(views))));
    return {columns, (views + columns - 1) / columns};
}

static float LightPower(const LightInfo& light)
{
    const float r = static_cast<float>(light.packedColor & 0xFFu) / 255.0f;
    const float g = static_cast<float>((light.packedColor >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>((light.packedColor >> 16) & 0xFFu) / 255.0f;
    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    float area = glm::pi<float>() * light.right.w * light.right.w;
    if (light.type == LIGHT_TYPE_AREA) {
        area = 4.0f * light.right.w * light.up.w;
    }
    return light.intensity * light.coneScale * area * luma;
}

uint32_t SelectLocalShadowLights(const LightInfo* lights, uint32_t lightCount, const glm::mat4& cameraViewProj, const glm::vec3& cameraPos, const uint32_t* previous, uint32_t previousCount,
                                 uint32_t viewBudget, uint32_t* outLights, const uint32_t* skipMask)
{
    const Frustum frustum = CreateFrustum(cameraViewProj);
    uint32_t ranked[LOCAL_SHADOW_MAX_VIEWS];
    float scores[LOCAL_SHADOW_MAX_VIEWS];
    uint32_t rankedCount = 0;

    for (uint32_t i = 0; i < lightCount; ++i) {
        const LightInfo& light = lights[i];
        if (!IsShadowEligible(light) || (skipMask != nullptr && (skipMask[i >> 5] & (1u << (i & 31u))) != 0u)) {
            continue;
        }
        const glm::vec3 position(light.position);
        bool bVisible = true;
        for (const glm::vec4& plane : frustum.planes) {
            if (glm::dot(glm::vec3(plane), position) + plane.w < -light.range) {
                bVisible = false;
                break;
            }
        }
        if (!bVisible) {
            continue;
        }

        const float distance = glm::max(glm::length(position - cameraPos) - light.range, 0.0f);
        float score = LightPower(light) * light.range * light.range / ((1.0f + distance) * (1.0f + distance));
        for (uint32_t p = 0; p < previousCount; ++p) {
            if (previous[p] == i) {
                score *= LOCAL_SHADOW_HYSTERESIS;
                break;
            }
        }

        uint32_t slot = rankedCount;
        while (slot > 0 && scores[slot - 1] < score) {
            --slot;
        }
        if (slot >= LOCAL_SHADOW_MAX_VIEWS) {
            continue;
        }
        const uint32_t last = glm::min(rankedCount, LOCAL_SHADOW_MAX_VIEWS - 1);
        for (uint32_t k = last; k > slot; --k) {
            ranked[k] = ranked[k - 1];
            scores[k] = scores[k - 1];
        }
        ranked[slot] = i;
        scores[slot] = score;
        rankedCount = glm::min(rankedCount + 1, LOCAL_SHADOW_MAX_VIEWS);
    }

    const uint32_t budget = glm::min(viewBudget, LOCAL_SHADOW_MAX_VIEWS);
    uint32_t usedViews = 0;
    uint32_t pickedCount = 0;
    for (uint32_t k = 0; k < rankedCount; ++k) {
        const uint32_t views = LocalShadowViewCount(lights[ranked[k]], frustum);
        if (views == 0 || usedViews + views > budget) {
            continue;
        }
        outLights[pickedCount++] = ranked[k];
        usedViews += views;
    }
    return pickedCount;
}

LocalShadowBakeParams GetLocalShadowBakeParams(const LightInfo& light)
{
    LocalShadowBakeParams params{};
    params.eye = glm::vec3(light.position);
    params.nearPlane = NearPlane(light);
    params.farPlane = glm::max(light.range, params.nearPlane * 2.0f);
    if (IsLocalShadowSpot(light)) {
        params.forward = glm::normalize(glm::vec3(light.normal));
        params.tanHalf = SpotTanHalf(light);
        params.faceCount = 1;
    }
    else {
        params.tanHalf = LOCAL_SHADOW_CUBE_TAN;
        params.faceCount = 6;
    }
    return params;
}

ShadowViewGPU BuildLocalShadowView(const LightInfo& light, uint32_t face, uint32_t tile, glm::uvec2 tiles, uint32_t resolution)
{
    const glm::vec3 eye(light.position);
    const float nearPlane = NearPlane(light);
    const float farPlane = glm::max(light.range, nearPlane * 2.0f);
    if (IsLocalShadowSpot(light)) {
        const glm::vec3 forward = glm::normalize(glm::vec3(light.normal));
        return MakePerspectiveView(eye, forward, SpotUp(forward), SpotTanHalf(light), nearPlane, farPlane, tile, tiles, resolution);
    }
    return MakePerspectiveView(eye, CUBE_AXES[face], CUBE_UPS[face], LOCAL_SHADOW_CUBE_TAN, nearPlane, farPlane, tile, tiles, resolution);
}
} // Render
