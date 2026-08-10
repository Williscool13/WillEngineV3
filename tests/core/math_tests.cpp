//
// Foundational math test suite. Drafted by Claude.
//
// Covers Transform TRS composition, transform hierarchy compose/decompose, octahedral normal
// codec, vertex compression round-trips (against DequantizeVertexPosition), and the frustum
// intersection tests. The frustum SSE implementations are additionally fuzz-compared against
// scalar reference copies so the two can never silently diverge.

#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "asset-load/asset_load_utils.h"
#include "core/types/math.h"
#include "core/types/transform.h"
#include "game/components/core_components.h"
#include "render/render_utils.h"
#include "render/types/render_types.h"

namespace
{
constexpr float EPS = 1e-4f;

bool NearlyEqual(float a, float b, float eps = EPS) { return std::fabs(a - b) <= eps; }

bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float eps = EPS)
{
    return NearlyEqual(a.x, b.x, eps) && NearlyEqual(a.y, b.y, eps) && NearlyEqual(a.z, b.z, eps);
}

bool NearlyEqual(const glm::mat4& a, const glm::mat4& b, float eps = EPS)
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!NearlyEqual(a[c][r], b[c][r], eps)) { return false; }
        }
    }
    return true;
}

// Same rotation regardless of quaternion sign
bool SameRotation(const glm::quat& a, const glm::quat& b, float eps = EPS)
{
    return std::fabs(std::fabs(glm::dot(a, b)) - 1.0f) <= eps;
}

struct Lcg
{
    uint64_t state{0x853C49E6748FEA9BULL};

    uint32_t NextU32()
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(state >> 33);
    }

    // [-range, range]
    float NextFloat(float range) { return (static_cast<float>(NextU32()) / 2147483647.5f - 1.0f) * range; }

    glm::vec3 NextVec3(float range) { return {NextFloat(range), NextFloat(range), NextFloat(range)}; }

    glm::quat NextRotation()
    {
        const glm::vec3 axis = glm::normalize(glm::vec3{NextFloat(1.0f) + 0.01f, NextFloat(1.0f), NextFloat(1.0f)});
        return glm::angleAxis(NextFloat(3.0f), axis);
    }
};

// Scalar reference copies of the pre-SSE frustum tests; the fuzz tests compare against these
bool RefIntersectsSphere(const Frustum& frustum, const glm::vec3& center, float radius)
{
    for (const auto& plane : frustum.planes) {
        if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) { return false; }
    }
    return true;
}

bool RefIntersectsAABB(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max)
{
    for (const auto& plane : frustum.planes) {
        const glm::vec3 normal = glm::vec3(plane);
        glm::vec3 pVertex;
        pVertex.x = normal.x >= 0 ? max.x : min.x;
        pVertex.y = normal.y >= 0 ? max.y : min.y;
        pVertex.z = normal.z >= 0 ? max.z : min.z;
        if (glm::dot(normal, pVertex) + plane.w < 0) { return false; }
    }
    return true;
}

bool RefIntersectsOBB(const Frustum& frustum, const glm::vec3& center, const glm::vec3& extents, const glm::mat3& rotation)
{
    for (const auto& plane : frustum.planes) {
        const glm::vec3 normal = glm::vec3(plane);
        const float r = extents.x * glm::abs(glm::dot(normal, rotation[0])) +
                        extents.y * glm::abs(glm::dot(normal, rotation[1])) +
                        extents.z * glm::abs(glm::dot(normal, rotation[2]));
        if (glm::dot(normal, center) + plane.w < -r) { return false; }
    }
    return true;
}

Frustum TestFrustum()
{
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3{0.0f, 0.0f, 5.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
    return Render::CreateFrustum(proj * view);
}
} // namespace

// ================================================================================
// Transform
// ================================================================================

TEST_CASE("Transform::GetMatrix equals translate * rotate * scale", "[math][transform]")
{
    const Transform t{
        {1.0f, -2.0f, 3.0f},
        glm::normalize(glm::quat{0.8f, 0.2f, -0.4f, 0.1f}),
        {2.0f, 0.5f, 3.0f}
    };
    const glm::mat4 reference = glm::translate(glm::mat4{1.0f}, t.translation) * glm::mat4_cast(t.rotation) * glm::scale(glm::mat4{1.0f}, t.scale);
    CHECK(NearlyEqual(t.GetMatrix(), reference));
    CHECK(NearlyEqual(Transform::IDENTITY.GetMatrix(), glm::mat4{1.0f}));
}

TEST_CASE("ComposeWorldTransform matches matrix composition", "[math][transform]")
{
    // Non-uniform parent scale with rotated children shears in matrix land, which TRS cannot
    // represent; equality holds for uniform parent scale (any rotations) and for non-uniform
    // parent scale with an unrotated child.
    Lcg rng;
    for (int i = 0; i < 50; ++i) {
        const float uniformScale = 0.25f + std::fabs(rng.NextFloat(2.0f));
        const Transform parent{rng.NextVec3(10.0f), rng.NextRotation(), {uniformScale, uniformScale, uniformScale}};
        const Transform local{rng.NextVec3(10.0f), rng.NextRotation(), {1.5f, 1.5f, 1.5f}};
        const Transform composed = Game::Component::ComposeWorldTransform(parent, local);
        CHECK(NearlyEqual(composed.GetMatrix(), parent.GetMatrix() * local.GetMatrix(), 1e-3f));
    }

    const Transform parent{{1.0f, 2.0f, 3.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, {2.0f, 3.0f, 4.0f}};
    const Transform local{{-1.0f, 0.5f, 2.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 0.5f}};
    const Transform composed = Game::Component::ComposeWorldTransform(parent, local);
    CHECK(NearlyEqual(composed.GetMatrix(), parent.GetMatrix() * local.GetMatrix()));
}

TEST_CASE("ComposeLocalFromWorld inverts ComposeWorldTransform", "[math][transform]")
{
    Lcg rng;
    for (int i = 0; i < 50; ++i) {
        const float uniformScale = 0.25f + std::fabs(rng.NextFloat(2.0f));
        const Transform parent{rng.NextVec3(10.0f), rng.NextRotation(), {uniformScale, uniformScale, uniformScale}};
        const Transform local{rng.NextVec3(10.0f), rng.NextRotation(), {0.5f, 2.0f, 1.25f}};
        const Transform world = Game::Component::ComposeWorldTransform(parent, local);
        const Transform recovered = Game::Component::ComposeLocalFromWorld(parent, world);
        CHECK(NearlyEqual(recovered.translation, local.translation, 1e-3f));
        CHECK(SameRotation(recovered.rotation, local.rotation, 1e-4f));
        CHECK(NearlyEqual(recovered.scale, local.scale, 1e-3f));
    }
}

// ================================================================================
// Octahedral codec + vertex compression
// ================================================================================

TEST_CASE("OctEncode/OctDecode round-trips unit vectors", "[math][oct]")
{
    const glm::vec3 fixed[] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        glm::normalize(glm::vec3{1, 1, 1}), glm::normalize(glm::vec3{-1, 2, -3}),
    };
    for (const glm::vec3& n : fixed) {
        CHECK(glm::dot(OctDecode(OctEncode(n)), n) > 0.9999f);
    }

    Lcg rng;
    for (int i = 0; i < 200; ++i) {
        const glm::vec3 n = glm::normalize(rng.NextVec3(1.0f) + glm::vec3{0.01f, 0.0f, 0.0f});
        CHECK(glm::dot(OctDecode(OctEncode(n)), n) > 0.9999f);
    }
}

TEST_CASE("CompressVertex position round-trips within quantization step", "[math][vertex]")
{
    Engine::MeshBounds bounds{};
    bounds.aabb.min = {-8.0f, -4.0f, -2.0f};
    bounds.aabb.max = {8.0f, 4.0f, 2.0f};
    bounds.aabbExtents = bounds.aabb.max - bounds.aabb.min;

    Lcg rng;
    for (int i = 0; i < 200; ++i) {
        Engine::FullVertex v{};
        v.position = {rng.NextFloat(8.0f), rng.NextFloat(4.0f), rng.NextFloat(2.0f)};
        v.normal = glm::normalize(rng.NextVec3(1.0f) + glm::vec3{0.01f, 0.0f, 0.0f});
        v.tangent = glm::vec4{glm::normalize(rng.NextVec3(1.0f) + glm::vec3{0.0f, 0.01f, 0.0f}), 1.0f};

        const Engine::Vertex packed = CompressVertex(v, bounds);
        const Vec3 decoded = AssetLoad::DequantizeVertexPosition(packed, bounds.aabb.min, bounds.aabbExtents);
        // Half a unorm16 step per axis
        CHECK(std::fabs(decoded.x - v.position.x) <= bounds.aabbExtents.x / 65535.0f);
        CHECK(std::fabs(decoded.y - v.position.y) <= bounds.aabbExtents.y / 65535.0f);
        CHECK(std::fabs(decoded.z - v.position.z) <= bounds.aabbExtents.z / 65535.0f);

        // Oct-encoded normal survives 8-bit snorm quantization
        const float nx = static_cast<float>(static_cast<int8_t>(packed.normalOct & 0xFF)) / 127.0f;
        const float ny = static_cast<float>(static_cast<int8_t>(packed.normalOct >> 8 & 0xFF)) / 127.0f;
        CHECK(glm::dot(OctDecode({nx, ny}), v.normal) > 0.99f);
    }
}

TEST_CASE("CompressVertex color and tangent sign pack exactly", "[math][vertex]")
{
    Engine::MeshBounds bounds{};
    bounds.aabb.min = {0.0f, 0.0f, 0.0f};
    bounds.aabb.max = {1.0f, 1.0f, 1.0f};
    bounds.aabbExtents = {1.0f, 1.0f, 1.0f};

    Engine::FullVertex v{};
    v.position = {0.5f, 0.5f, 0.5f};
    v.normal = {0.0f, 1.0f, 0.0f};
    v.tangent = {1.0f, 0.0f, 0.0f, -1.0f};
    v.color = {1.0f, 0.0f, 0.5f, 1.0f};

    const Engine::Vertex packed = CompressVertex(v, bounds);
    CHECK((packed.color & 0xFF) == 255);
    CHECK((packed.color >> 8 & 0xFF) == 0);
    CHECK((packed.color >> 16 & 0xFF) == 127);
    CHECK((packed.color >> 24 & 0xFF) == 255);
    CHECK((packed.tangentOct >> 16 & 0x1) == 0);

    v.tangent.w = 1.0f;
    CHECK((CompressVertex(v, bounds).tangentOct >> 16 & 0x1) == 1);
}

// ================================================================================
// Frustum
// ================================================================================

TEST_CASE("Frustum sphere tests: inside, outside, straddling, enclosing", "[math][frustum]")
{
    const Frustum f = TestFrustum();
    CHECK(Render::IntersectsSphere(f, {0.0f, 0.0f, 0.0f}, 1.0f));
    CHECK(Render::IntersectsSphere(f, {0.0f, 0.0f, -80.0f}, 1.0f));
    CHECK_FALSE(Render::IntersectsSphere(f, {0.0f, 0.0f, 50.0f}, 1.0f));
    CHECK_FALSE(Render::IntersectsSphere(f, {0.0f, 0.0f, -200.0f}, 1.0f));
    CHECK_FALSE(Render::IntersectsSphere(f, {500.0f, 0.0f, 0.0f}, 1.0f));
    CHECK(Render::IntersectsSphere(f, {0.0f, 0.0f, 0.0f}, 1000.0f));
    // Straddles the left plane
    CHECK(Render::IntersectsSphere(f, {-6.0f, 0.0f, 0.0f}, 3.0f));
}

TEST_CASE("Frustum AABB tests: inside, outside, straddling", "[math][frustum]")
{
    const Frustum f = TestFrustum();
    CHECK(Render::IntersectsAABB(f, {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}));
    CHECK_FALSE(Render::IntersectsAABB(f, {100.0f, 100.0f, 100.0f}, {101.0f, 101.0f, 101.0f}));
    CHECK(Render::IntersectsAABB(f, {-50.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}));
    CHECK(Render::IntersectsAABB(f, {-1000.0f, -1000.0f, -1000.0f}, {1000.0f, 1000.0f, 1000.0f}));
}

TEST_CASE("Frustum OBB rotation shrinks and grows the projected radius correctly", "[math][frustum]")
{
    const Frustum f = TestFrustum();
    const glm::mat3 identity{1.0f};
    CHECK(Render::IntersectsOBB(f, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, identity));
    CHECK_FALSE(Render::IntersectsOBB(f, {0.0f, 200.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, identity));
    // A long thin box rotated to poke into the frustum from outside
    const glm::mat3 rot = glm::mat3_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f}));
    CHECK(Render::IntersectsOBB(f, {-30.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 40.0f}, rot));
}

TEST_CASE("Frustum SSE implementations match the scalar reference", "[math][frustum][fuzz]")
{
    const Frustum f = TestFrustum();
    Lcg rng;
    for (int i = 0; i < 500; ++i) {
        const glm::vec3 center = rng.NextVec3(60.0f);
        const float radius = std::fabs(rng.NextFloat(20.0f));
        CHECK(Render::IntersectsSphere(f, center, radius) == RefIntersectsSphere(f, center, radius));

        const glm::vec3 half = {std::fabs(rng.NextFloat(15.0f)), std::fabs(rng.NextFloat(15.0f)), std::fabs(rng.NextFloat(15.0f))};
        CHECK(Render::IntersectsAABB(f, center - half, center + half) == RefIntersectsAABB(f, center - half, center + half));

        const glm::mat3 rot = glm::mat3_cast(rng.NextRotation());
        CHECK(Render::IntersectsOBB(f, center, half, rot) == RefIntersectsOBB(f, center, half, rot));
    }
}
