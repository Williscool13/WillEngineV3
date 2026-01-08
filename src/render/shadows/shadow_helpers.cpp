//
// Created by William on 2026-01-06.
//

#include "shadow_helpers.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "core/include/render_interface.h"
#include "core/math/constants.h"

namespace Render
{
ViewProjMatrix GenerateLightSpaceMatrix(
    float cascadeExtent,
    float cascadeNear,
    float cascadeFar,
    const glm::vec3& lightDirection,
    const Core::ViewData& viewData)
{
    std::array<glm::vec3, 8> corners = GetPerspectiveFrustumCornersWorldSpace(
        cascadeNear, cascadeFar, viewData.fovRadians,
        viewData.aspectRatio, viewData.cameraPos, viewData.cameraForward
    );

    // Alternative more expensive way to calculate corners
    // auto viewMatrix = camera->getViewMatrix();
    // auto projMatrix = glm::perspective(camera->getFov(), camera->getAspectRatio(), cascadeNear, cascadeFar);
    // auto viewProj = projMatrix * viewMatrix;
    // glm::vec3 corners2[numberOfCorners];
    // render_utils::getPerspectiveFrustumCornersWorldSpace(viewProj, corners2);

    // https://alextardif.com/shadowmapping.html
    glm::vec3 frustumCenter = glm::vec3(0.0f);
    for (const glm::vec3& corner : corners) {
        frustumCenter += corner;
    }
    frustumCenter *= (1.0f / 8.0f);

    float maxDistanceSquared = 0.0f;
    for (const glm::vec3& corner : corners) {
        const glm::vec3 diff = corner - frustumCenter;
        float distanceSquared = glm::dot(diff, diff);
        maxDistanceSquared = std::max(maxDistanceSquared, distanceSquared);
    }
    float radius = std::sqrt(maxDistanceSquared);

    float texelsPerUnit = cascadeExtent / glm::max(radius * 2.0f, 1.0f);

    const glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(texelsPerUnit));
    glm::mat4 view = glm::lookAt(-lightDirection, glm::vec3(0.0f), WORLD_UP);
    view = scaleMatrix * view;
    glm::mat4 invView = glm::inverse(view);

    glm::vec4 tempFrustumCenter = glm::vec4(frustumCenter, 1.0f);
    tempFrustumCenter = view * tempFrustumCenter;
    tempFrustumCenter.x = glm::floor(tempFrustumCenter.x);
    tempFrustumCenter.y = glm::floor(tempFrustumCenter.y);
    frustumCenter = glm::vec3(invView * tempFrustumCenter);

    // Position light camera
    glm::vec3 eye = frustumCenter - (lightDirection * radius * 2.0f);
    glm::mat4 lightView = glm::lookAt(eye, frustumCenter, WORLD_UP);

    constexpr float zMult = 10.0f;
    // glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, -radius * zMult, radius * zMult);
    // if (reversedDepth) {
    auto lightProj = glm::ortho(-radius, radius, -radius, radius, radius * zMult, -radius * zMult);
    // }

    return {lightView, lightProj};
}

std::array<glm::vec3, 8> GetPerspectiveFrustumCornersWorldSpace(const float nearPlane, const float farPlane, const float fov, const float aspect, const glm::vec3 position, const glm::vec3 viewDir)
{
    std::array<glm::vec3, 8> output{};
    constexpr glm::vec3 up{0.0f, 1.0f, 0.0f};

    const glm::vec3 right = normalize(cross(viewDir, up));
    const glm::vec3 upCorrected = normalize(cross(right, viewDir));

    const float nearHeight = glm::tan(fov * 0.5f) * nearPlane;
    const float nearWidth = nearHeight * aspect;
    const float farHeight = glm::tan(fov * 0.5f) * farPlane;
    const float farWidth = farHeight * aspect;

    const glm::vec3 near_center = position + viewDir * nearPlane;
    output[0] = glm::vec3(near_center - upCorrected * nearHeight - right * nearWidth); // bottom-left
    output[1] = glm::vec3(near_center + upCorrected * nearHeight - right * nearWidth); // top-left
    output[2] = glm::vec3(near_center + upCorrected * nearHeight + right * nearWidth); // top-right
    output[3] = glm::vec3(near_center - upCorrected * nearHeight + right * nearWidth); // bottom-right

    const glm::vec3 farCenter = position + viewDir * farPlane;
    output[4] = glm::vec3(farCenter - upCorrected * farHeight - right * farWidth); // bottom-left
    output[5] = glm::vec3(farCenter + upCorrected * farHeight - right * farWidth); // top-left
    output[6] = glm::vec3(farCenter + upCorrected * farHeight + right * farWidth); // top-right
    output[7] = glm::vec3(farCenter - upCorrected * farHeight + right * farWidth); // bottom-right

    return output;
}
} // Render
