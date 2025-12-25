//
// Created by William on 2025-12-25.
//
// Tests for frustum creation and plane extraction from view-projection matrices.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "render/types/render_types.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

using namespace Render;

bool IsPointInsideFrustum(const Frustum& frustum, const glm::vec3& point)
{
    for (const auto& plane : frustum.planes) {
        if (glm::dot(glm::vec3(plane), point) + plane.w < 0) {
            return false;
        }
    }
    return true;
}

TEST_CASE("CreateFrustum plane normalization", "[renderer][frustum]")
{
    SECTION("All planes are normalized") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        for (int i = 0; i < 6; i++) {
            glm::vec3 normal(frustum.planes[i].x, frustum.planes[i].y, frustum.planes[i].z);
            float length = glm::length(normal);
            REQUIRE_THAT(length, Catch::Matchers::WithinRel(1.0f, 0.0001f));
        }
    }
}

TEST_CASE("CreateFrustum with identity matrix", "[renderer][frustum]")
{
    SECTION("Identity matrix creates frustum at origin") {
        glm::mat4 identity = glm::mat4(1.0f);
        Frustum frustum = CreateFrustum(identity);

        // With identity, the frustum should be the unit cube [-1, 1] in NDC space
        // All planes should be normalized

        for (int i = 0; i < 6; i++) {
            glm::vec3 normal(frustum.planes[i].x, frustum.planes[i].y, frustum.planes[i].z);
            float length = glm::length(normal);
            REQUIRE_THAT(length, Catch::Matchers::WithinRel(1.0f, 0.0001f));
        }
    }
}

TEST_CASE("CreateFrustum with orthographic projection", "[renderer][frustum]")
{
    SECTION("Orthographic frustum contains points in view volume") {
        // Orthographic projection: left=-10, right=10, bottom=-10, top=10, near=1, far=100
        // With identity view, visible Z range is [-100, -1] (camera looks down -Z)
        glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 100.0f);
        glm::mat4 view = glm::mat4(1.0f); // Identity view
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Point in the middle of the view volume should be inside
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, -50)));
    }

    SECTION("Orthographic frustum rejects points outside bounds") {
        glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 100.0f);
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Points outside the frustum bounds
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(15, 0, -50))); // Too far right
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(-15, 0, -50))); // Too far left
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, 15, -50))); // Too far up
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, -15, -50))); // Too far down
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, -0.5))); // Before near plane (z > -1)
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, -110))); // Beyond far plane (z < -100)
    }

    SECTION("Orthographic frustum accepts points inside bounds") {
        glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 100.0f);
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Points clearly inside
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, -50)));
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(5, 5, -50)));
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(-5, -5, -50)));
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(9, 9, -10)));
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(-9, -9, -90)));
    }
}

TEST_CASE("CreateFrustum with perspective projection", "[renderer][frustum]")
{
    SECTION("Perspective frustum contains points in view") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Camera is at (0, 0, 10) looking at origin
        // Origin should be inside frustum
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, 0)));

        // Points near the origin along the view direction
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, 1)));
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, 5)));
    }

    SECTION("Perspective frustum rejects points closer than near plane") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);

        Frustum frustum = CreateFrustum(proj * view);

        // Near plane is at z = 10 - 0.1 = 9.9
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, 9.94f)));
    }

    SECTION("Perspective frustum rejects points behind camera") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Point behind the camera
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, 15)));
    }

    SECTION("Perspective frustum rejects points beyond far plane") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Point way beyond far plane (camera at z=10, looking at z=0, far=100)
        // So far plane is at z = 10 - 100 = -90
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, -200)));
    }

    SECTION("Perspective frustum rejects points outside FOV") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Points way off to the side (outside FOV)
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(50, 0, 0)));
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(-50, 0, 0)));
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, 50, 0)));
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(0, -50, 0)));
    }
}

TEST_CASE("CreateFrustum plane ordering", "[renderer][frustum]")
{
    SECTION("Planes follow standard ordering (left, right, bottom, top, near, far)") {
        // Create a simple orthographic frustum to test plane ordering
        glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 100.0f);
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Test that a point on the left edge is rejected by left plane but accepted by others
        glm::vec3 leftPoint(-11, 0, -50);
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, leftPoint));

        // Test that a point on the right edge is rejected by right plane
        glm::vec3 rightPoint(11, 0, -50);
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, rightPoint));

        // Test that a point on the bottom edge is rejected by bottom plane
        glm::vec3 bottomPoint(0, -11, -50);
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, bottomPoint));

        // Test that a point on the top edge is rejected by top plane
        glm::vec3 topPoint(0, 11, -50);
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, topPoint));
    }
}

TEST_CASE("CreateFrustum with different camera positions", "[renderer][frustum]")
{
    SECTION("Camera at different position looking at origin") {
        glm::mat4 view = glm::lookAt(glm::vec3(5, 5, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Origin should be visible from this camera position
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, 0)));

        // Camera position itself should NOT be in its own frustum (behind near plane)
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(5, 5, 5)));
    }

    SECTION("Camera looking in different direction") {
        // Camera at origin looking down positive X axis
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(10, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 100.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Point along the view direction should be inside
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(5, 0, 0)));

        // Point behind the camera should be outside
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(-5, 0, 0)));
    }
}

TEST_CASE("CreateFrustum edge cases", "[renderer][frustum]")
{
    SECTION("Narrow FOV frustum") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(10.0f), 1.0f, 0.1f, 100.0f); // Very narrow FOV
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Point straight ahead should be inside
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, 0)));

        // Point slightly off-axis should be outside due to narrow FOV
        REQUIRE_FALSE(IsPointInsideFrustum(frustum, glm::vec3(2, 0, 0)));
    }

    SECTION("Wide FOV frustum") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(120.0f), 1.0f, 0.1f, 100.0f); // Very wide FOV
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Point straight ahead should be inside
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(0, 0, 0)));

        // Point off to the side should still be inside due to wide FOV
        REQUIRE(IsPointInsideFrustum(frustum, glm::vec3(3, 0, 5)));
    }

    SECTION("Extreme aspect ratio") {
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 3.0f, 0.1f, 100.0f); // Wide aspect ratio
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // All planes should still be normalized
        for (int i = 0; i < 6; i++) {
            glm::vec3 normal(frustum.planes[i].x, frustum.planes[i].y, frustum.planes[i].z);
            float length = glm::length(normal);
            REQUIRE_THAT(length, Catch::Matchers::WithinRel(1.0f, 0.0001f));
        }
    }
}

TEST_CASE("CreateFrustum plane distance calculations", "[renderer][frustum]")
{
    SECTION("Plane distances are correct for simple case") {
        // Axis-aligned orthographic frustum centered at origin
        glm::mat4 proj = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 1.0f, 10.0f);
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 viewProj = proj * view;

        Frustum frustum = CreateFrustum(viewProj);

        // Point exactly in the center should be equidistant from left/right and top/bottom
        glm::vec3 center(0, 0, -5);

        for (int i = 0; i < 6; i++) {
            const glm::vec4& plane = frustum.planes[i];
            float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
            // Distance should be positive (inside)
            REQUIRE(distance > 0);
        }
    }
}
