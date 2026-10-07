//
// Cascaded shadow map math. Drafted by Claude.
//

#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include <glm/glm.hpp>

#include "render/render-view/csm_views.h"

static constexpr float EPS = 1e-4f;

static glm::vec3 ToNdc(const glm::mat4& viewProj, const glm::vec3& world)
{
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    return glm::vec3(clip) / clip.w;
}

static bool IsFinite(const glm::mat4& m)
{
    for (int32_t c = 0; c < 4; ++c) {
        for (int32_t r = 0; r < 4; ++r) {
            if (!std::isfinite(m[c][r])) { return false; }
        }
    }
    return true;
}

static const glm::vec3 SUN_DIRECTIONS[] = {
    glm::normalize(glm::vec3(0.577f, -0.577f, 0.577f)),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::normalize(glm::vec3(0.001f, -1.0f, 0.0f)),
    glm::normalize(glm::vec3(-0.3f, -0.2f, 0.9f)),
};

TEST_CASE("CSM: light basis is orthonormal and right-handed for any sun, including straight overhead", "[csm]")
{
    const Core::CSMParams params{};
    for (const glm::vec3& sun : SUN_DIRECTIONS) {
        const Render::CSMFrame frame = Render::ComputeCSMFrame(params, glm::vec3(3.0f, 2.0f, -7.0f), sun, 0.1f);
        CHECK(std::fabs(glm::length(frame.right) - 1.0f) < EPS);
        CHECK(std::fabs(glm::length(frame.up) - 1.0f) < EPS);
        CHECK(std::fabs(glm::dot(frame.right, frame.up)) < EPS);
        CHECK(std::fabs(glm::dot(frame.right, frame.toSun)) < EPS);
        CHECK(glm::length(glm::cross(frame.right, frame.up) - frame.toSun) < EPS);
        CHECK(glm::length(frame.toSun + sun) < EPS);
        for (uint32_t i = 0; i < frame.cascadeCount; ++i) {
            CHECK(IsFinite(frame.cascades[i].viewProj));
        }
    }
}

TEST_CASE("CSM: cascades grow, end at max distance, and clamp the count", "[csm]")
{
    Core::CSMParams params{};
    const Render::CSMFrame frame = Render::ComputeCSMFrame(params, glm::vec3(0.0f), SUN_DIRECTIONS[0], 0.1f);
    REQUIRE(frame.cascadeCount == 4);
    for (uint32_t i = 1; i < frame.cascadeCount; ++i) {
        CHECK(frame.cascades[i].halfExtent > frame.cascades[i - 1].halfExtent);
    }
    CHECK(std::fabs(frame.cascades[3].halfExtent - params.maxDistance) < EPS);
    CHECK(std::fabs(frame.cascades[0].texelWorldSize - 2.0f * frame.cascades[0].halfExtent / static_cast<float>(params.resolution)) < EPS);

    params.cascadeCount = 9;
    CHECK(Render::ComputeCSMFrame(params, glm::vec3(0.0f), SUN_DIRECTIONS[0], 0.1f).cascadeCount == Render::CSM_MAX_CASCADES);
    params.cascadeCount = 0;
    CHECK(Render::ComputeCSMFrame(params, glm::vec3(0.0f), SUN_DIRECTIONS[0], 0.1f).cascadeCount == 1);
}

TEST_CASE("CSM: the anchor maps inside every cascade and depth runs sunward plane 1 to far plane 0", "[csm]")
{
    const Core::CSMParams params{};
    const glm::vec3 anchor{12.3f, 4.5f, -67.8f};
    for (const glm::vec3& sun : SUN_DIRECTIONS) {
        const Render::CSMFrame frame = Render::ComputeCSMFrame(params, anchor, sun, 0.1f);
        for (uint32_t i = 0; i < frame.cascadeCount; ++i) {
            const Render::CSMCascade& cascade = frame.cascades[i];
            const glm::vec3 ndc = ToNdc(cascade.viewProj, anchor);
            CHECK(std::fabs(ndc.x) < 1.0f);
            CHECK(std::fabs(ndc.y) < 1.0f);
            CHECK(ndc.z > 0.0f);
            CHECK(ndc.z < 1.0f);

            // Snapping moves the box by under a texel, so the anchor stays within one texel of the box center.
            CHECK(std::fabs(ndc.x) <= 2.0f / static_cast<float>(params.resolution) + EPS);
            CHECK(std::fabs(ndc.y) <= 2.0f / static_cast<float>(params.resolution) + EPS);

            const float zAnchor = glm::dot(anchor, frame.toSun);
            const glm::vec3 onAxis = anchor - frame.toSun * zAnchor;
            const glm::vec3 sunward = onAxis + frame.toSun * (zAnchor + cascade.halfExtent + params.casterExtension);
            const glm::vec3 farthest = onAxis + frame.toSun * (zAnchor - cascade.halfExtent);
            CHECK(std::fabs(ToNdc(cascade.viewProj, sunward).z - 1.0f) < 1e-3f);
            CHECK(std::fabs(ToNdc(cascade.viewProj, farthest).z) < 1e-3f);
            CHECK(std::fabs(cascade.depthRange - (2.0f * cascade.halfExtent + params.casterExtension)) < 1e-3f);
        }
    }
}

TEST_CASE("CSM: a fixed world point keeps its sub-texel position as the anchor moves", "[csm]")
{
    const Core::CSMParams params{};
    const glm::vec3 point{5.25f, 1.0f, -3.5f};
    const glm::vec3 sun = SUN_DIRECTIONS[0];
    const Render::CSMFrame base = Render::ComputeCSMFrame(params, glm::vec3(0.0f), sun, 0.1f);

    const glm::vec3 moves[] = {{0.37f, 0.0f, 0.0f}, {-1.91f, 0.4f, 2.2f}, {0.0f, 0.0f, 13.7f}};
    for (const glm::vec3& move : moves) {
        const Render::CSMFrame moved = Render::ComputeCSMFrame(params, move, sun, 0.1f);
        for (uint32_t i = 0; i < base.cascadeCount; ++i) {
            const float res = static_cast<float>(params.resolution);
            const glm::vec2 texelA = (glm::vec2(ToNdc(base.cascades[i].viewProj, point)) * 0.5f + 0.5f) * res;
            const glm::vec2 texelB = (glm::vec2(ToNdc(moved.cascades[i].viewProj, point)) * 0.5f + 0.5f) * res;
            const glm::vec2 shift = texelB - texelA;
            CHECK(std::fabs(shift.x - std::round(shift.x)) < 2e-2f);
            CHECK(std::fabs(shift.y - std::round(shift.y)) < 2e-2f);
        }
    }
}
