//
// Local light shadow selection and view math. Drafted by Claude.
//

#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "render/render-view/local_shadow_views.h"
#include "render/types/render_types.h"

static LightInfo MakeSpot(const glm::vec3& position, const glm::vec3& normal, float coneOuterDegrees, float intensity = 1000.0f, float range = 10.0f)
{
    LightInfo light{};
    light.position = glm::vec4(position, std::cos(glm::radians(coneOuterDegrees)));
    light.normal = glm::vec4(normal, 1.0f);
    light.right = glm::vec4(1.0f, 0.0f, 0.0f, 0.5f);
    light.up = glm::vec4(0.0f, 1.0f, 0.0f, 0.5f);
    light.packedColor = 0xFFFFFFFFu;
    light.intensity = intensity;
    light.range = range;
    light.type = LIGHT_TYPE_AREA;
    light.coneScale = 1.0f;
    light.flags = LIGHT_FLAG_CAST_SHADOWS;
    return light;
}

static glm::vec3 ToNdc(const glm::mat4& viewProj, const glm::vec3& world)
{
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    return glm::vec3(clip) / clip.w;
}

static glm::mat4 CameraViewProj(const glm::vec3& eye, const glm::vec3& target)
{
    return glm::perspectiveZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f) * glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("Local shadows: view counts per light shape", "[localshadow]")
{
    const Frustum outside = Render::CreateFrustum(CameraViewProj({0, 0, 30}, {0, 0, 0}));
    CHECK(Render::LocalShadowViewCount(MakeSpot({}, {0, -1, 0}, 45.0f), outside) == 1);
    // Range sphere on screen, cone pointing straight back past the camera's side.
    const Frustum aside = Render::CreateFrustum(CameraViewProj({0, 0, 0}, {0, 0, -1}));
    CHECK(Render::LocalShadowViewCount(MakeSpot({0, 0, -2}, {0, 0, 1}, 20.0f, 1000.0f, 10.0f), aside) == 1);
    CHECK(Render::LocalShadowViewCount(MakeSpot({8, 0, -2}, {1, 0, 0}, 20.0f, 1000.0f, 10.0f), aside) == 0);

    LightInfo unflagged = MakeSpot({}, {0, -1, 0}, 45.0f);
    unflagged.flags = 0;
    CHECK(Render::LocalShadowViewCount(unflagged, outside) == 0);

    LightInfo sphere = MakeSpot({}, {0, -1, 0}, 45.0f, 1000.0f, 5.0f);
    sphere.type = LIGHT_TYPE_SPHERE;
    CHECK(Render::LocalShadowViewCount(sphere, outside) == 6);

    // Camera inside the range looking down -Z: the +Z face is behind it.
    const Frustum inside = Render::CreateFrustum(CameraViewProj({0, 0, 0}, {0, 0, -1}));
    CHECK(Render::LocalShadowFaceMask(sphere, inside) == 0b101111u);

    // One-sided hemisphere emitter facing down: the +Y face is all behind it.
    CHECK(Render::LocalShadowViewCount(MakeSpot({}, {0, -1, 0}, 90.0f, 1000.0f, 5.0f), outside) == 5);
}

TEST_CASE("Local shadows: cube faces cover their axis and start past a sphere light's surface", "[localshadow]")
{
    LightInfo sphere = MakeSpot({1, 2, 3}, {0, -1, 0}, 45.0f, 1000.0f, 8.0f);
    sphere.type = LIGHT_TYPE_SPHERE;
    sphere.right.w = 0.5f;
    const Frustum outside = Render::CreateFrustum(CameraViewProj({0, 0, 40}, {0, 0, 0}));
    REQUIRE(Render::LocalShadowFaceMask(sphere, outside) == 0b111111u);
    ShadowViewGPU views[6];
    for (uint32_t f = 0; f < 6; ++f) {
        views[f] = Render::BuildLocalShadowView(sphere, f, f, Render::LocalShadowAtlasTiles(16), 512);
    }
    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (uint32_t f = 0; f < 6; ++f) {
        const glm::vec3 onAxis = ToNdc(views[f].viewProj, glm::vec3(1, 2, 3) + axes[f] * 4.0f);
        CHECK(std::fabs(onAxis.x) < 1e-4f);
        CHECK(std::fabs(onAxis.y) < 1e-4f);
        CHECK(onAxis.z > 0.0f);
        CHECK(onAxis.z < 1.0f);
        CHECK(ToNdc(views[f].viewProj, glm::vec3(1, 2, 3) + axes[f] * 0.5f).z > 1.0f);
    }
}

TEST_CASE("Local shadows: spot view is reverse-Z between near and range, centred on the axis, in its tile", "[localshadow]")
{
    const glm::vec3 position{2.0f, 5.0f, -1.0f};
    for (const glm::vec3& normal : {glm::vec3(0, -1, 0), glm::normalize(glm::vec3(1, -1, 0.5f)), glm::vec3(0, 1, 0)}) {
        const LightInfo light = MakeSpot(position, normal, 40.0f);
        CHECK(Render::IsLocalShadowSpot(light));
        const ShadowViewGPU view = Render::BuildLocalShadowView(light, 0, 5, Render::LocalShadowAtlasTiles(16), 512);

        const glm::vec3 onAxis = ToNdc(view.viewProj, position + normal * 4.0f);
        CHECK(std::fabs(onAxis.x) < 1e-4f);
        CHECK(std::fabs(onAxis.y) < 1e-4f);
        CHECK(onAxis.z > 0.0f);
        CHECK(onAxis.z < 1.0f);
        CHECK(ToNdc(view.viewProj, position + normal * 0.05f).z > 0.999f);
        CHECK(std::fabs(ToNdc(view.viewProj, position + normal * light.range).z) < 1e-4f);
        CHECK(ToNdc(view.viewProj, position + normal * 2.0f).z > ToNdc(view.viewProj, position + normal * 6.0f).z);

        CHECK(view.atlasScaleOffset == glm::vec4(0.25f, 0.25f, 0.25f, 0.25f));
        CHECK(view.eye.w == 1.0f);
    }
}

TEST_CASE("Local shadows: atlas tile grid fits the budget", "[localshadow]")
{
    CHECK(Render::LocalShadowAtlasTiles(1) == glm::uvec2(1, 1));
    CHECK(Render::LocalShadowAtlasTiles(3) == glm::uvec2(2, 2));
    CHECK(Render::LocalShadowAtlasTiles(5) == glm::uvec2(3, 2));
    CHECK(Render::LocalShadowAtlasTiles(16) == glm::uvec2(4, 4));
}

TEST_CASE("Local shadows: selection skips lights out of view, prefers strong near lights, keeps last frame's picks", "[localshadow]")
{
    const glm::mat4 proj = glm::perspectiveZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 viewProj = proj * view;

    const LightInfo lights[] = {
        MakeSpot({0, 3, -10}, {0, -1, 0}, 45.0f, 1000.0f),
        MakeSpot({0, 3, 40}, {0, -1, 0}, 45.0f, 1000.0f),
        MakeSpot({0, 3, -30}, {0, -1, 0}, 45.0f, 1000.0f),
        MakeSpot({0, 3, -5}, {0, -1, 0}, 45.0f, 1.0f),
    };
    uint32_t picks[LOCAL_SHADOW_MAX_VIEWS];

    REQUIRE(Render::SelectLocalShadowLights(lights, 4, viewProj, glm::vec3(0.0f), nullptr, 0, 16, picks) == 3);
    CHECK(picks[0] == 0);
    CHECK(picks[1] == 2);
    CHECK(picks[2] == 3);

    REQUIRE(Render::SelectLocalShadowLights(lights, 4, viewProj, glm::vec3(0.0f), nullptr, 0, 1, picks) == 1);
    CHECK(picks[0] == 0);

    LightInfo close[] = {MakeSpot({0, 3, -10}, {0, -1, 0}, 45.0f, 1000.0f), MakeSpot({0, 3, -10}, {0, -1, 0}, 45.0f, 900.0f)};
    const uint32_t previous = 1;
    REQUIRE(Render::SelectLocalShadowLights(close, 2, viewProj, glm::vec3(0.0f), &previous, 1, 1, picks) == 1);
    CHECK(picks[0] == 1);
}
