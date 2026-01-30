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

bool IntersectsSphere(const Frustum& frustum, const glm::vec3& center, float radius)
{
    for (const auto& plane : frustum.planes) {
        float dist = glm::dot(glm::vec3(plane), center) + plane.w;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

bool IntersectsAABB(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max)
{
    for (const auto& plane : frustum.planes) {
        glm::vec3 normal = glm::vec3(plane);

        glm::vec3 pVertex;
        pVertex.x = normal.x >= 0 ? max.x : min.x;
        pVertex.y = normal.y >= 0 ? max.y : min.y;
        pVertex.z = normal.z >= 0 ? max.z : min.z;

        if (glm::dot(normal, pVertex) + plane.w < 0) {
            return false;
        }
    }
    return true;
}

bool IntersectsOBB(const Frustum& frustum, const glm::vec3& center, const glm::vec3& extents, const glm::mat3& rotation)
{
    for (const auto& plane : frustum.planes) {
        glm::vec3 normal = glm::vec3(plane);

        float r = extents.x * glm::abs(glm::dot(normal, rotation[0])) +
                  extents.y * glm::abs(glm::dot(normal, rotation[1])) +
                  extents.z * glm::abs(glm::dot(normal, rotation[2]));

        float dist = glm::dot(normal, center) + plane.w;
        if (dist < -r) {
            return false;
        }
    }
    return true;
}

int32_t GetSphereSegments(const glm::vec3& center, const glm::vec3& viewPos, float radius)
{
    float dist = glm::distance(center, viewPos);
    float screenSize = radius / dist;

    if (screenSize > 0.1f) return 32;
    if (screenSize > 0.05f) return 16;
    return 8;
}
} // Render
