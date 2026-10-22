//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_AABB_H
#define WILL_ENGINE_AABB_H

#include <cfloat>

#include "math.h"

namespace Core::Math
{
/** Default-constructed empty (inverted), so Encapsulate can start from it. */
struct AABB
{
    Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
    Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};

    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 HalfExtents() const { return (max - min) * 0.5f; }
    float SurfaceArea() const { glm::vec3 d = max - min; return 2.f * (d.x * d.y + d.y * d.z + d.z * d.x); }
    float Volume() const { glm::vec3 d = max - min; return d.x * d.y * d.z; }

    bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    bool Overlaps(const AABB& other) const { return glm::all(glm::lessThanEqual(min, other.max)) && glm::all(glm::greaterThanEqual(max, other.min)); }

    void Encapsulate(const AABB& other)
    {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    /** The bounds of this box after the affine transform `m`. */
    AABB Transformed(const Mat4& m) const
    {
        const Vec3 center = Vec3(m * Vec4(Center(), 1.0f));
        const Vec3 half = HalfExtents();
        const Vec3 extent = glm::abs(Vec3(m[0])) * half.x + glm::abs(Vec3(m[1])) * half.y + glm::abs(Vec3(m[2])) * half.z;
        return {center - extent, center + extent};
    }
};
} // Core::Math

using Core::Math::AABB;

#endif //WILL_ENGINE_AABB_H
