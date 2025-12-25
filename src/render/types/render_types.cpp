//
// Created by William on 2025-12-12.
//

#include "render_types.h"

namespace Render
{
Frustum CreateFrustum(const glm::mat4& viewProj)
{
    Frustum frustum{};

    // Left
    frustum.planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );

    // Right
    frustum.planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );

    // Bottom
    frustum.planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );

    // Top
    frustum.planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );

    // Near
    // Vulkan 0->1
    //   Instead of 4 - 3, it's just 3
    frustum.planes[4] = glm::vec4(
        viewProj[0][2],
        viewProj[1][2],
        viewProj[2][2],
        viewProj[3][2]
    );

    // Far
    frustum.planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize
    for (glm::vec4& plane : frustum.planes) {
        const float length = glm::length(glm::vec3(plane));
        plane /= length;
    }

    return frustum;
}
} // Render
