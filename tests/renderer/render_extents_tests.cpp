//
// Created by William on 2025-12-25.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "render/vulkan/vk_render_extents.h"

using namespace Render;

TEST_CASE("RenderExtents construction with scale", "[renderer][extents]") {
    SECTION("Scale 1.0 produces same dimensions") {
        RenderExtents extents(1920, 1080, 1.0f);
        auto [width, height] = extents.GetScaledExtent();

        REQUIRE(width == 1920);
        REQUIRE(height == 1080);
    }

    SECTION("Scale 0.5 produces half dimensions") {
        RenderExtents extents(1920, 1080, 0.5f);
        auto [width, height] = extents.GetScaledExtent();

        REQUIRE(width == 960);
        REQUIRE(height == 540);
    }

    SECTION("Scale 2.0 produces double dimensions") {
        RenderExtents extents(800, 600, 2.0f);
        auto [width, height] = extents.GetScaledExtent();

        REQUIRE(width == 1600);
        REQUIRE(height == 1200);
    }

    SECTION("Scale with rounding") {
        RenderExtents extents(1921, 1081, 0.5f);
        auto [width, height] = extents.GetScaledExtent();

        // 1921 * 0.5 = 960.5 -> rounds to 961
        // 1081 * 0.5 = 540.5 -> rounds to 541
        REQUIRE(width == 961);
        REQUIRE(height == 541);
    }

    SECTION("Original extents unchanged") {
        RenderExtents extents(1920, 1080, 0.5f);
        auto [width, height] = extents.GetExtent();

        REQUIRE(width == 1920);
        REQUIRE(height == 1080);
    }
}

TEST_CASE("RenderExtents ApplyResize", "[renderer][extents]") {
    RenderExtents extents(1920, 1080, 0.5f);

    SECTION("Resize updates both extents") {
        extents.ApplyResize(2560, 1440);

        auto [width, height] = extents.GetExtent();
        REQUIRE(width == 2560);
        REQUIRE(height == 1440);

        auto [scaledWidth, scaledHeight] = extents.GetScaledExtent();
        REQUIRE(scaledWidth == 1280);
        REQUIRE(scaledHeight == 720);
    }

    SECTION("Resize with current scale") {
        extents.ApplyResize(800, 600);

        auto [scaledWidth, scaledHeight] = extents.GetScaledExtent();
        REQUIRE(scaledWidth == 400);
        REQUIRE(scaledHeight == 300);
    }

    SECTION("Resize to odd numbers with scale") {
        extents.ApplyResize(1921, 1081);

        auto [scaledWidth, scaledHeight] = extents.GetScaledExtent();
        REQUIRE(scaledWidth == 961);
        REQUIRE(scaledHeight == 541);
    }
}

TEST_CASE("RenderExtents UpdateScale", "[renderer][extents]") {
    RenderExtents extents(1920, 1080, 1.0f);

    SECTION("Update scale recalculates scaled extents") {
        extents.UpdateScale(0.5f);

        auto [width, height] = extents.GetExtent();
        REQUIRE(width == 1920);
        REQUIRE(height == 1080);

        auto [scaledWidth, scaledHeight] = extents.GetScaledExtent();
        REQUIRE(scaledWidth == 960);
        REQUIRE(scaledHeight == 540);
    }

    SECTION("Update scale to 2.0") {
        extents.UpdateScale(2.0f);

        auto [scaledWidth, scaledHeight] = extents.GetScaledExtent();
        REQUIRE(scaledWidth == 3840);
        REQUIRE(scaledHeight == 2160);
    }

    SECTION("Update scale to 0.75") {
        extents.UpdateScale(0.75f);

        auto [scaledWidth, scaledHeight] = extents.GetScaledExtent();
        REQUIRE(scaledWidth == 1440);
        REQUIRE(scaledHeight == 810);
    }
}

TEST_CASE("RenderExtents aspect ratio calculation", "[renderer][extents]") {
    SECTION("16:9 aspect ratio") {
        RenderExtents extents(1920, 1080, 1.0f);
        float aspectRatio = extents.GetAspectRatio();

        REQUIRE_THAT(aspectRatio, Catch::Matchers::WithinRel(16.0f / 9.0f, 0.0001f));
    }

    SECTION("4:3 aspect ratio") {
        RenderExtents extents(1024, 768, 1.0f);
        float aspectRatio = extents.GetAspectRatio();

        REQUIRE_THAT(aspectRatio, Catch::Matchers::WithinRel(4.0f / 3.0f, 0.0001f));
    }

    SECTION("21:9 ultrawide aspect ratio") {
        RenderExtents extents(2560, 1080, 1.0f);
        float aspectRatio = extents.GetAspectRatio();

        REQUIRE_THAT(aspectRatio, Catch::Matchers::WithinRel(2560.0f / 1080.0f, 0.0001f));
    }

    SECTION("Square aspect ratio") {
        RenderExtents extents(1024, 1024, 1.0f);
        float aspectRatio = extents.GetAspectRatio();

        REQUIRE_THAT(aspectRatio, Catch::Matchers::WithinRel(1.0f, 0.0001f));
    }

    SECTION("Aspect ratio unaffected by scale") {
        RenderExtents extents(1920, 1080, 0.5f);
        float aspectRatio = extents.GetAspectRatio();

        // Aspect ratio is calculated from original extents, not scaled
        REQUIRE_THAT(aspectRatio, Catch::Matchers::WithinRel(16.0f / 9.0f, 0.0001f));
    }
}

TEST_CASE("RenderExtents texel size calculation", "[renderer][extents]") {
    SECTION("1920x1080 texel size") {
        RenderExtents extents(1920, 1080, 1.0f);
        glm::vec2 texelSize = extents.GetTexelSize();

        REQUIRE_THAT(texelSize.x, Catch::Matchers::WithinRel(1.0f / 1920.0f, 0.00001f));
        REQUIRE_THAT(texelSize.y, Catch::Matchers::WithinRel(1.0f / 1080.0f, 0.00001f));
    }

    SECTION("800x600 texel size") {
        RenderExtents extents(800, 600, 1.0f);
        glm::vec2 texelSize = extents.GetTexelSize();

        REQUIRE_THAT(texelSize.x, Catch::Matchers::WithinRel(1.0f / 800.0f, 0.00001f));
        REQUIRE_THAT(texelSize.y, Catch::Matchers::WithinRel(1.0f / 600.0f, 0.00001f));
    }

    SECTION("Texel size calculated from original extents") {
        RenderExtents extents(1920, 1080, 0.5f);
        glm::vec2 texelSize = extents.GetTexelSize();

        // Texel size is based on original extents, not scaled
        REQUIRE_THAT(texelSize.x, Catch::Matchers::WithinRel(1.0f / 1920.0f, 0.00001f));
        REQUIRE_THAT(texelSize.y, Catch::Matchers::WithinRel(1.0f / 1080.0f, 0.00001f));
    }
}

TEST_CASE("RenderExtents edge cases", "[renderer][extents]") {
    SECTION("Very small dimensions") {
        RenderExtents extents(1, 1, 1.0f);

        auto [width, height] = extents.GetExtent();
        REQUIRE(width == 1);
        REQUIRE(height == 1);

        REQUIRE_THAT(extents.GetAspectRatio(), Catch::Matchers::WithinRel(1.0f, 0.0001f));
    }

    SECTION("Very large dimensions") {
        RenderExtents extents(7680, 4320, 1.0f); // 8K resolution

        auto [width, height] = extents.GetExtent();
        REQUIRE(width == 7680);
        REQUIRE(height == 4320);
    }

    SECTION("Scale rounds correctly near 0.5") {
        RenderExtents extents(100, 100, 1.0f);
        extents.UpdateScale(0.504f);

        auto [width, height] = extents.GetScaledExtent();
        // 100 * 0.504 + 0.5 = 50.4 + 0.5 = 50.9 -> 50
        REQUIRE(width == 50);
        REQUIRE(height == 50);
    }

    SECTION("Scale rounds correctly above 0.5") {
        RenderExtents extents(100, 100, 1.0f);
        extents.UpdateScale(0.506f);

        auto [width, height] = extents.GetScaledExtent();
        // 100 * 0.506 + 0.5 = 50.6 + 0.5 = 51.1 -> 51
        REQUIRE(width == 51);
        REQUIRE(height == 51);
    }
}
