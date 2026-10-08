//
// Created by William on 2026-10-08.
//

#include "local_shadow_views.h"

#include <glm/gtc/matrix_transform.hpp>

#include "render/types/render_types.h"

namespace Render
{
static constexpr float LOCAL_SHADOW_SPOT_COS_MIN = 0.5f;
static constexpr float LOCAL_SHADOW_CONE_MARGIN = 0.035f;
static constexpr float LOCAL_SHADOW_NEAR = 0.05f;
static constexpr float LOCAL_SHADOW_HYSTERESIS = 1.5f;

uint32_t LocalShadowViewCount(const LightInfo& light)
{
    if ((light.flags & LIGHT_FLAG_CAST_SHADOWS) == 0u || light.intensity <= 0.0f || light.range <= 0.0f) {
        return 0;
    }
    const bool bArea = light.type == LIGHT_TYPE_AREA || light.type == LIGHT_TYPE_DISK;
    return bArea && light.position.w >= LOCAL_SHADOW_SPOT_COS_MIN ? 1u : 0u;
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
                                 uint32_t viewBudget, uint32_t* outLights)
{
    const Frustum frustum = CreateFrustum(cameraViewProj);
    uint32_t ranked[LOCAL_SHADOW_MAX_VIEWS];
    float scores[LOCAL_SHADOW_MAX_VIEWS];
    uint32_t rankedCount = 0;

    for (uint32_t i = 0; i < lightCount; ++i) {
        const LightInfo& light = lights[i];
        if (LocalShadowViewCount(light) == 0u) {
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
        const uint32_t views = LocalShadowViewCount(lights[ranked[k]]);
        if (usedViews + views > budget) {
            continue;
        }
        outLights[pickedCount++] = ranked[k];
        usedViews += views;
    }
    return pickedCount;
}

uint32_t BuildLocalShadowViews(const LightInfo& light, uint32_t firstTile, glm::uvec2 tiles, uint32_t resolution, ShadowViewGPU* outViews)
{
    if (LocalShadowViewCount(light) != 1u) {
        return 0;
    }

    const glm::vec3 eye(light.position);
    const glm::vec3 forward = glm::normalize(glm::vec3(light.normal));
    const glm::vec3 reference = glm::abs(forward.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::mat4 view = glm::lookAt(eye, eye + forward, reference);

    const float tanHalf = glm::tan(glm::acos(glm::clamp(light.position.w, 0.0f, 1.0f)) + LOCAL_SHADOW_CONE_MARGIN);
    const float nearPlane = LOCAL_SHADOW_NEAR;
    const float farPlane = glm::max(light.range, nearPlane * 2.0f);

    // Reverse-Z (near 1, far 0), y negated to match the CSM views' winding.
    glm::mat4 proj(0.0f);
    proj[0][0] = 1.0f / tanHalf;
    proj[1][1] = -1.0f / tanHalf;
    proj[2][2] = nearPlane / (farPlane - nearPlane);
    proj[2][3] = -1.0f;
    proj[3][2] = nearPlane * farPlane / (farPlane - nearPlane);

    const glm::vec2 scale = 1.0f / glm::vec2(tiles);

    ShadowViewGPU& out = outViews[0];
    out = {};
    out.viewProj = proj * view;
    out.frustum = CreateFrustum(out.viewProj);
    out.atlasScaleOffset = glm::vec4(scale, static_cast<float>(firstTile % tiles.x) * scale.x, static_cast<float>(firstTile / tiles.x) * scale.y);
    out.eye = glm::vec4(eye, 1.0f);
    out.texelSize = 2.0f * tanHalf / static_cast<float>(resolution);
    return 1;
}
} // Render
