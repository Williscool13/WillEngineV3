//
// Created by William on 2026-07-03.
//

#include "collider_generation.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

#include "core/containers/inline_vector.h"

namespace Engine
{
static Quat BasisToQuat(const Vec3& x, const Vec3& y, const Vec3& z)
{
    return glm::normalize(glm::quat_cast(Mat3(x, y, z)));
}

/** Orthonormal box basis whose local Z follows `dir`, local X near `rRight`, local Y near `rUp`. */
static Quat SegmentBoxRotation(const Vec3& dir, const Vec3& rRight, const Vec3& rUp)
{
    Vec3 z = dir;
    Vec3 x = rRight - z * glm::dot(rRight, z);
    if (glm::length(x) < 1e-4f) {
        x = rUp - z * glm::dot(rUp, z);
    }
    if (glm::length(x) < 1e-4f) {
        return Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    x = glm::normalize(x);
    Vec3 y = glm::normalize(glm::cross(z, x));
    x = glm::cross(y, z);
    return BasisToQuat(x, y, z);
}

void BuildSplineColliderPrimitives(const SplineParams& p, Core::Span<const SplineFrame> frames, Core::Vector<SplineColliderPrimitive>& out)
{
    const int totalRings = static_cast<int>(frames.Size());
    if (totalRings < 2) { return; }

    const bool bClosed = p.spline.bClosed;
    const bool bTube = p.profile.type == SplineProfileType::Tube;
    const float halfW = bTube ? p.radius : std::max(0.001f, p.profile.width * 0.5f);
    const float halfH = bTube ? p.radius : std::max(0.001f, p.profile.height * 0.5f);

    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    auto horizAxes = [&](const SplineFrame& f, Vec3& across, Vec3& fwd) {
        fwd = f.tangent - worldUp * glm::dot(f.tangent, worldUp);
        if (glm::length(fwd) < 1e-4f) { fwd = f.rRight - worldUp * glm::dot(f.rRight, worldUp); }
        fwd = (glm::length(fwd) < 1e-4f) ? Vec3{0.0f, 0.0f, 1.0f} : glm::normalize(fwd);
        across = glm::normalize(glm::cross(worldUp, fwd));
    };

    auto laneCenter = [&](const SplineFrame& f, Vec2 off) -> Vec3 {
        if (p.railing.bEnabled) {
            Vec3 across, fwd;
            horizAxes(f, across, fwd);
            return f.pos + (off.x + p.railing.lateralOffset) * across + off.y * worldUp;
        }
        return f.pos + off.x * f.rRight + off.y * f.rUp;
    };

    Core::InlineVector<Vec2, 8> laneOffsets{};
    if (p.railing.bEnabled) {
        for (int li = 0; li < static_cast<int>(p.railing.lanes.Size()); li++) { laneOffsets.PushBack(p.railing.lanes[li]); }
    }
    else {
        laneOffsets.PushBack(Vec2{0.0f, 0.0f});
    }

    // Main sweep: one box (or capsule for Tube) per ring-to-ring segment, per lane.
    const int stitchCount = bClosed ? totalRings : totalRings - 1;
    for (int rail = 0; rail < static_cast<int>(laneOffsets.Size()); rail++) {
        const Vec2 laneOff = laneOffsets[rail];
        for (int i = 0; i < stitchCount; i++) {
            const int nextRing = bClosed ? (i + 1) % totalRings : i + 1;
            const Vec3 c0 = laneCenter(frames[i], laneOff);
            const Vec3 c1 = laneCenter(frames[nextRing], laneOff);
            const Vec3 seg = c1 - c0;
            const float len = glm::length(seg);
            if (len < 1e-5f) { continue; }
            const Vec3 dir = seg / len;
            const Vec3 mid = (c0 + c1) * 0.5f;

            SplineColliderPrimitive prim{};
            prim.position = mid;
            if (bTube) {
                prim.type = SplineColliderPrimitiveType::Capsule;
                prim.radius = p.radius;
                prim.halfHeight = len * 0.5f;
                // Capsule axis is local Y; orient Y along the segment.
                Vec3 y = dir;
                Vec3 x = frames[i].rRight - y * glm::dot(frames[i].rRight, y);
                if (glm::length(x) < 1e-4f) { x = frames[i].rUp - y * glm::dot(frames[i].rUp, y); }
                x = glm::length(x) < 1e-4f ? Vec3{1.0f, 0.0f, 0.0f} : glm::normalize(x);
                const Vec3 z = glm::normalize(glm::cross(x, y));
                x = glm::cross(y, z);
                prim.rotation = BasisToQuat(x, y, z);
            }
            else {
                prim.type = SplineColliderPrimitiveType::Box;
                prim.halfExtents = Vec3(halfW, halfH, len * 0.5f);
                prim.rotation = SegmentBoxRotation(dir, frames[i].rRight, frames[i].rUp);
            }
            out.PushBack(prim);
        }
    }

    // Cross planks bridge exactly two lanes.
    if (p.railing.bEnabled && p.railing.lanes.Size() == 2 && p.bCrossPlanks) {
        const float plankThickness = std::max(0.001f, p.crossPlankThickness);
        const float halfLength = std::max(0.001f, p.crossPlankLength) * 0.5f;
        const int plankInterval = std::max(1, p.crossPlankInterval);
        const Vec2 laneA = p.railing.lanes[0];
        const Vec2 laneB = p.railing.lanes[1];

        const int plankMaxRing = bClosed ? totalRings : totalRings - 1;
        for (int i = 0; i < plankMaxRing; i += plankInterval) {
            Vec3 across, fwd;
            horizAxes(frames[i], across, fwd);
            const Vec3 centerA = laneCenter(frames[i], laneA) + p.crossPlankHeight * worldUp;
            const Vec3 centerB = laneCenter(frames[i], laneB) + p.crossPlankHeight * worldUp;
            const Vec3 acrossVec = centerB - centerA;
            const float acrossLen = glm::length(acrossVec);
            if (acrossLen < 1e-5f) { continue; }
            const Vec3 xAxis = acrossVec / acrossLen; // plank long axis (lane A -> lane B)
            Vec3 zAxis = fwd - xAxis * glm::dot(fwd, xAxis);
            zAxis = glm::length(zAxis) < 1e-4f ? fwd : glm::normalize(zAxis);
            const Vec3 yAxis = glm::normalize(glm::cross(zAxis, xAxis)); // thickness (~world up)

            SplineColliderPrimitive prim{};
            prim.type = SplineColliderPrimitiveType::Box;
            prim.position = (centerA + centerB) * 0.5f - plankThickness * 0.5f * yAxis;
            prim.halfExtents = Vec3(acrossLen * 0.5f, plankThickness * 0.5f, halfLength);
            prim.rotation = BasisToQuat(xAxis, yAxis, zAxis);
            out.PushBack(prim);
        }
    }

    // Vertical posts (balusters), plumb along world up.
    if (p.railing.bEnabled && p.railing.bPosts) {
        const int postInterval = std::max(1, p.railing.postInterval);
        const float pb = p.railing.postBottom;
        const float pt = p.railing.postTop;
        const float railWidth = bTube ? 2.0f * p.radius : std::max(p.profile.width, p.profile.height);
        const float hw = std::max(0.001f, std::min(p.railing.postSize.x, railWidth)) * 0.5f;
        const float hd = std::max(0.001f, std::min(p.railing.postSize.y, railWidth)) * 0.5f;

        for (int i = 0; i < totalRings; i += postInterval) {
            const SplineFrame& f = frames[i];
            Vec3 u, w;
            horizAxes(f, u, w);
            Vec3 postBase = f.pos + (p.railing.lateralOffset + p.railing.postLateral) * u;
            if (!bClosed && totalRings > 1) {
                if (i == 0) { postBase += hd * glm::normalize(frames[1].pos - frames[0].pos); }
                else if (i == totalRings - 1) { postBase += hd * glm::normalize(frames[totalRings - 2].pos - frames[totalRings - 1].pos); }
            }
            const float halfY = std::max(0.001f, std::abs(pt - pb) * 0.5f);

            SplineColliderPrimitive prim{};
            prim.type = SplineColliderPrimitiveType::Box;
            prim.position = postBase + (pb + pt) * 0.5f * worldUp;
            prim.halfExtents = Vec3(hw, halfY, hd);
            prim.rotation = BasisToQuat(u, worldUp, w);
            out.PushBack(prim);
        }
    }
}
} // Engine
