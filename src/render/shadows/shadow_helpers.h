//
// Created by William on 2026-01-06.
//

#ifndef WILL_ENGINE_SHADOW_HELPERS_H
#define WILL_ENGINE_SHADOW_HELPERS_H

#include <array>

#include <glm/glm.hpp>

#include "core/include/render_interface.h"

namespace Render
{
glm::mat4 GenerateLightSpaceMatrix(
    float cascadeExtent,
    float cascadeNear,
    float cascadeFar,
    const glm::vec3& lightDirection,
    const Core::ViewData& viewData);

std::array<glm::vec3, 8> GetPerspectiveFrustumCornersWorldSpace(float nearPlane, float farPlane, float fov, float aspect, glm::vec3 position, glm::vec3 viewDir);
} // Render

#endif //WILL_ENGINE_SHADOW_HELPERS_H
