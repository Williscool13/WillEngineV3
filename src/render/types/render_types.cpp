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

// Transposes the 6 planes into SoA registers (nx, ny, nz, w per lane)
#define FRUSTUM_LOAD_SOA(frustum)                                   \
    const float* planeF = reinterpret_cast<const float*>((frustum).planes); \
    __m128 nxA = _mm_loadu_ps(planeF + 0);                          \
    __m128 nyA = _mm_loadu_ps(planeF + 4);                          \
    __m128 nzA = _mm_loadu_ps(planeF + 8);                          \
    __m128 wA = _mm_loadu_ps(planeF + 12);                          \
    _MM_TRANSPOSE4_PS(nxA, nyA, nzA, wA);                           \
    __m128 nxB = _mm_loadu_ps(planeF + 16);                         \
    __m128 nyB = _mm_loadu_ps(planeF + 20);                         \
    __m128 nzB = _mm_setzero_ps();                                  \
    __m128 wB = _mm_setzero_ps();                                   \
    _MM_TRANSPOSE4_PS(nxB, nyB, nzB, wB)

bool IntersectsSphere(const Frustum& frustum, const glm::vec3& center, float radius)
{
    FRUSTUM_LOAD_SOA(frustum);

    const __m128 cx = _mm_set1_ps(center.x);
    const __m128 cy = _mm_set1_ps(center.y);
    const __m128 cz = _mm_set1_ps(center.z);
    const __m128 negRadius = _mm_set1_ps(-radius);

    const __m128 distA = _mm_add_ps(_mm_add_ps(_mm_mul_ps(nxA, cx), _mm_mul_ps(nyA, cy)), _mm_add_ps(_mm_mul_ps(nzA, cz), wA));
    const __m128 distB = _mm_add_ps(_mm_add_ps(_mm_mul_ps(nxB, cx), _mm_mul_ps(nyB, cy)), _mm_add_ps(_mm_mul_ps(nzB, cz), wB));
    const int outsideMask = _mm_movemask_ps(_mm_cmplt_ps(distA, negRadius)) | _mm_movemask_ps(_mm_cmplt_ps(distB, negRadius));
    return outsideMask == 0;
}

bool IntersectsAABB(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max)
{
    FRUSTUM_LOAD_SOA(frustum);

    const __m128 minX = _mm_set1_ps(min.x);
    const __m128 minY = _mm_set1_ps(min.y);
    const __m128 minZ = _mm_set1_ps(min.z);
    const __m128 maxX = _mm_set1_ps(max.x);
    const __m128 maxY = _mm_set1_ps(max.y);
    const __m128 maxZ = _mm_set1_ps(max.z);
    const __m128 zero = _mm_setzero_ps();

    // p-vertex: pick max where the plane normal component is >= 0, min where negative (blendv keys on the sign bit)
    const __m128 distA = _mm_add_ps(_mm_add_ps(
                             _mm_mul_ps(nxA, _mm_blendv_ps(maxX, minX, nxA)),
                             _mm_mul_ps(nyA, _mm_blendv_ps(maxY, minY, nyA))),
                         _mm_add_ps(_mm_mul_ps(nzA, _mm_blendv_ps(maxZ, minZ, nzA)), wA));
    const __m128 distB = _mm_add_ps(_mm_add_ps(
                             _mm_mul_ps(nxB, _mm_blendv_ps(maxX, minX, nxB)),
                             _mm_mul_ps(nyB, _mm_blendv_ps(maxY, minY, nyB))),
                         _mm_add_ps(_mm_mul_ps(nzB, _mm_blendv_ps(maxZ, minZ, nzB)), wB));
    const int outsideMask = _mm_movemask_ps(_mm_cmplt_ps(distA, zero)) | _mm_movemask_ps(_mm_cmplt_ps(distB, zero));
    return outsideMask == 0;
}

bool IntersectsOBB(const Frustum& frustum, const glm::vec3& center, const glm::vec3& extents, const glm::mat3& rotation)
{
    FRUSTUM_LOAD_SOA(frustum);

    const __m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    const __m128 cx = _mm_set1_ps(center.x);
    const __m128 cy = _mm_set1_ps(center.y);
    const __m128 cz = _mm_set1_ps(center.z);

    auto projectedRadius = [&](__m128 nx, __m128 ny, __m128 nz) {
        __m128 r = _mm_setzero_ps();
        for (int axis = 0; axis < 3; ++axis) {
            const __m128 dot = _mm_add_ps(_mm_add_ps(
                                   _mm_mul_ps(nx, _mm_set1_ps(rotation[axis].x)),
                                   _mm_mul_ps(ny, _mm_set1_ps(rotation[axis].y))),
                               _mm_mul_ps(nz, _mm_set1_ps(rotation[axis].z)));
            r = _mm_add_ps(r, _mm_mul_ps(_mm_set1_ps(extents[axis]), _mm_and_ps(dot, absMask)));
        }
        return r;
    };

    const __m128 distA = _mm_add_ps(_mm_add_ps(_mm_mul_ps(nxA, cx), _mm_mul_ps(nyA, cy)), _mm_add_ps(_mm_mul_ps(nzA, cz), wA));
    const __m128 distB = _mm_add_ps(_mm_add_ps(_mm_mul_ps(nxB, cx), _mm_mul_ps(nyB, cy)), _mm_add_ps(_mm_mul_ps(nzB, cz), wB));
    const int outsideMask = _mm_movemask_ps(_mm_cmplt_ps(distA, _mm_sub_ps(_mm_setzero_ps(), projectedRadius(nxA, nyA, nzA))))
                          | _mm_movemask_ps(_mm_cmplt_ps(distB, _mm_sub_ps(_mm_setzero_ps(), projectedRadius(nxB, nyB, nzB))));
    return outsideMask == 0;
}

#undef FRUSTUM_LOAD_SOA

int32_t GetSphereSegments(const glm::vec3& center, const glm::vec3& viewPos, float radius)
{
    float dist = glm::distance(center, viewPos);
    float screenSize = radius / dist;

    if (screenSize > 0.1f) return 32;
    if (screenSize > 0.05f) return 16;
    return 8;
}
} // Render
