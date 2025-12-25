//
// Created by William on 2025-12-25.
//
// Tests for Vulkan resource wrappers (AllocatedBuffer, AllocatedImage, etc.)
// to ensure proper move semantics and resource ownership.
//
// Note: These tests verify type traits and compile-time properties.
// Runtime tests with actual Vulkan resources would require a valid context.
//

#include <catch2/catch_test_macros.hpp>

#include "render/vulkan/vk_resources.h"

using namespace Render;

TEST_CASE("AllocatedBuffer type traits", "[renderer][resources]") {
    SECTION("AllocatedBuffer is move-only") {
        REQUIRE_FALSE(std::is_copy_constructible_v<AllocatedBuffer>);
        REQUIRE_FALSE(std::is_copy_assignable_v<AllocatedBuffer>);
        REQUIRE(std::is_move_constructible_v<AllocatedBuffer>);
        REQUIRE(std::is_move_assignable_v<AllocatedBuffer>);
    }

    SECTION("AllocatedBuffer is default constructible") {
        REQUIRE(std::is_default_constructible_v<AllocatedBuffer>);
    }

    SECTION("AllocatedBuffer is destructible") {
        REQUIRE(std::is_destructible_v<AllocatedBuffer>);
    }
}

TEST_CASE("AllocatedImage type traits", "[renderer][resources]") {
    SECTION("AllocatedImage is move-only") {
        REQUIRE_FALSE(std::is_copy_constructible_v<AllocatedImage>);
        REQUIRE_FALSE(std::is_copy_assignable_v<AllocatedImage>);
        REQUIRE(std::is_move_constructible_v<AllocatedImage>);
        REQUIRE(std::is_move_assignable_v<AllocatedImage>);
    }

    SECTION("AllocatedImage is default constructible") {
        REQUIRE(std::is_default_constructible_v<AllocatedImage>);
    }

    SECTION("AllocatedImage is destructible") {
        REQUIRE(std::is_destructible_v<AllocatedImage>);
    }
}

TEST_CASE("ImageView type traits", "[renderer][resources]") {
    SECTION("ImageView is move-only") {
        REQUIRE_FALSE(std::is_copy_constructible_v<ImageView>);
        REQUIRE_FALSE(std::is_copy_assignable_v<ImageView>);
        REQUIRE(std::is_move_constructible_v<ImageView>);
        REQUIRE(std::is_move_assignable_v<ImageView>);
    }

    SECTION("ImageView is default constructible") {
        REQUIRE(std::is_default_constructible_v<ImageView>);
    }
}

TEST_CASE("Sampler type traits", "[renderer][resources]") {
    SECTION("Sampler is move-only") {
        REQUIRE_FALSE(std::is_copy_constructible_v<Sampler>);
        REQUIRE_FALSE(std::is_copy_assignable_v<Sampler>);
        REQUIRE(std::is_move_constructible_v<Sampler>);
        REQUIRE(std::is_move_assignable_v<Sampler>);
    }

    SECTION("Sampler is default constructible") {
        REQUIRE(std::is_default_constructible_v<Sampler>);
    }
}

TEST_CASE("DescriptorSetLayout type traits", "[renderer][resources]") {
    SECTION("DescriptorSetLayout is move-only") {
        REQUIRE_FALSE(std::is_copy_constructible_v<DescriptorSetLayout>);
        REQUIRE_FALSE(std::is_copy_assignable_v<DescriptorSetLayout>);
        REQUIRE(std::is_move_constructible_v<DescriptorSetLayout>);
        REQUIRE(std::is_move_assignable_v<DescriptorSetLayout>);
    }

    SECTION("DescriptorSetLayout is default constructible") {
        REQUIRE(std::is_default_constructible_v<DescriptorSetLayout>);
    }
}

TEST_CASE("PipelineLayout type traits", "[renderer][resources]") {
    SECTION("PipelineLayout is move-only") {
        REQUIRE_FALSE(std::is_copy_constructible_v<PipelineLayout>);
        REQUIRE_FALSE(std::is_copy_assignable_v<PipelineLayout>);
        REQUIRE(std::is_move_constructible_v<PipelineLayout>);
        REQUIRE(std::is_move_assignable_v<PipelineLayout>);
    }

    SECTION("PipelineLayout is default constructible") {
        REQUIRE(std::is_default_constructible_v<PipelineLayout>);
    }
}

TEST_CASE("Pipeline type traits", "[renderer][resources]") {
    SECTION("Pipeline is move-only") {
        REQUIRE_FALSE(std::is_copy_constructible_v<Pipeline>);
        REQUIRE_FALSE(std::is_copy_assignable_v<Pipeline>);
        REQUIRE(std::is_move_constructible_v<Pipeline>);
        REQUIRE(std::is_move_assignable_v<Pipeline>);
    }

    SECTION("Pipeline is default constructible") {
        REQUIRE(std::is_default_constructible_v<Pipeline>);
    }
}

TEST_CASE("Resource wrapper struct sizes", "[renderer][resources]") {
    SECTION("AllocatedBuffer size is reasonable") {
        // Should contain: context ptr, handle, address, size, allocation, allocationInfo
        // This is just a sanity check that the struct isn't unexpectedly large
        REQUIRE(sizeof(AllocatedBuffer) < 256);
    }

    SECTION("AllocatedImage size is reasonable") {
        // Should contain: context ptr, handle, format, extent, layout, mipLevels, allocation
        REQUIRE(sizeof(AllocatedImage) < 256);
    }
}
