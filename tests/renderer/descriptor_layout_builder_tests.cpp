//
// Created by William on 2025-12-25.
//
// Tests for DescriptorLayoutBuilder to ensure proper binding management
// and CreateInfo generation.
//

#include <catch2/catch_test_macros.hpp>

#include "render/descriptors/vk_descriptors.h"

using namespace Render;

TEST_CASE("DescriptorLayoutBuilder construction", "[renderer][descriptor-builder]") {
    SECTION("Default construction") {
        DescriptorLayoutBuilder builder;

        REQUIRE(builder.bindings.empty());
    }

    SECTION("Construction with reserved size") {
        DescriptorLayoutBuilder builder(10);

        REQUIRE(builder.bindings.capacity() >= 10);
        REQUIRE(builder.bindings.empty());
    }
}

TEST_CASE("DescriptorLayoutBuilder AddBinding", "[renderer][descriptor-builder]") {
    SECTION("Add single binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        REQUIRE(builder.bindings.size() == 1);
        REQUIRE(builder.bindings[0].binding == 0);
        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        REQUIRE(builder.bindings[0].descriptorCount == 1);
    }

    SECTION("Add binding with count") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4);

        REQUIRE(builder.bindings.size() == 1);
        REQUIRE(builder.bindings[0].binding == 0);
        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        REQUIRE(builder.bindings[0].descriptorCount == 4);
    }

    SECTION("Add multiple bindings") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8);

        REQUIRE(builder.bindings.size() == 3);
        REQUIRE(builder.bindings[0].binding == 0);
        REQUIRE(builder.bindings[1].binding == 1);
        REQUIRE(builder.bindings[2].binding == 2);
        REQUIRE(builder.bindings[2].descriptorCount == 8);
    }

    SECTION("Bindings preserve order") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLER);

        REQUIRE(builder.bindings.size() == 3);
        // Order is preserved as added, not sorted by binding number
        REQUIRE(builder.bindings[0].binding == 2);
        REQUIRE(builder.bindings[1].binding == 0);
        REQUIRE(builder.bindings[2].binding == 1);
    }
}

TEST_CASE("DescriptorLayoutBuilder descriptor types", "[renderer][descriptor-builder]") {
    SECTION("Uniform buffer binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }

    SECTION("Storage buffer binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    SECTION("Combined image sampler binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    SECTION("Sampled image binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }

    SECTION("Storage image binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    SECTION("Sampler binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER);
    }

    SECTION("Input attachment binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    }

    SECTION("Uniform buffer dynamic binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
    }

    SECTION("Storage buffer dynamic binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);

        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
    }
}

TEST_CASE("DescriptorLayoutBuilder array bindings", "[renderer][descriptor-builder]") {
    SECTION("Small array") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4);

        REQUIRE(builder.bindings[0].descriptorCount == 4);
    }

    SECTION("Large array") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096);

        REQUIRE(builder.bindings[0].descriptorCount == 4096);
    }

    SECTION("Multiple array bindings") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, 128);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096);
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8);

        REQUIRE(builder.bindings[0].descriptorCount == 128);
        REQUIRE(builder.bindings[1].descriptorCount == 4096);
        REQUIRE(builder.bindings[2].descriptorCount == 8);
    }
}

TEST_CASE("DescriptorLayoutBuilder Build CreateInfo", "[renderer][descriptor-builder]") {
    SECTION("Build create info with single binding") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        auto createInfo = builder.Build(VK_SHADER_STAGE_VERTEX_BIT, 0);

        REQUIRE(createInfo.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        REQUIRE(createInfo.bindingCount == 1);
        REQUIRE(createInfo.pBindings != nullptr);
        REQUIRE(createInfo.pBindings[0].binding == 0);
        REQUIRE(createInfo.pBindings[0].stageFlags == VK_SHADER_STAGE_VERTEX_BIT);
    }

    SECTION("Build create info with multiple bindings") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4);

        auto createInfo = builder.Build(VK_SHADER_STAGE_FRAGMENT_BIT, 0);

        REQUIRE(createInfo.bindingCount == 2);
        REQUIRE(createInfo.pBindings[0].stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT);
        REQUIRE(createInfo.pBindings[1].stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    SECTION("Build with all shader stages") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        auto createInfo = builder.Build(VK_SHADER_STAGE_ALL, 0);

        REQUIRE(createInfo.pBindings[0].stageFlags == VK_SHADER_STAGE_ALL);
    }

    SECTION("Build with multiple shader stages") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        auto createInfo = builder.Build(
            static_cast<VkShaderStageFlagBits>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            0
        );

        REQUIRE((createInfo.pBindings[0].stageFlags & VK_SHADER_STAGE_VERTEX_BIT) != 0);
        REQUIRE((createInfo.pBindings[0].stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) != 0);
    }

    SECTION("Build with compute shader stage") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        auto createInfo = builder.Build(VK_SHADER_STAGE_COMPUTE_BIT, 0);

        REQUIRE(createInfo.pBindings[0].stageFlags == VK_SHADER_STAGE_COMPUTE_BIT);
    }
}

TEST_CASE("DescriptorLayoutBuilder Clear", "[renderer][descriptor-builder]") {
    SECTION("Clear removes all bindings") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8);

        REQUIRE(builder.bindings.size() == 3);

        builder.Clear();

        REQUIRE(builder.bindings.empty());
    }

    SECTION("Can add bindings after clear") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.Clear();
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        REQUIRE(builder.bindings.size() == 1);
        REQUIRE(builder.bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }
}

TEST_CASE("DescriptorLayoutBuilder reuse", "[renderer][descriptor-builder]") {
    SECTION("Builder can be reused multiple times") {
        DescriptorLayoutBuilder builder;

        // First use
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        auto createInfo1 = builder.Build(VK_SHADER_STAGE_VERTEX_BIT, 0);
        REQUIRE(createInfo1.bindingCount == 1);

        builder.Clear();

        // Second use
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4);
        auto createInfo2 = builder.Build(VK_SHADER_STAGE_FRAGMENT_BIT, 0);
        REQUIRE(createInfo2.bindingCount == 2);

        builder.Clear();

        // Third use
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8);
        auto createInfo3 = builder.Build(VK_SHADER_STAGE_COMPUTE_BIT, 0);
        REQUIRE(createInfo3.bindingCount == 1);
    }
}

TEST_CASE("DescriptorLayoutBuilder complex layouts", "[renderer][descriptor-builder]") {
    SECTION("Typical bindless layout") {
        DescriptorLayoutBuilder builder;

        // Samplers array
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLER, 128);
        // Textures array
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096);

        auto createInfo = builder.Build(VK_SHADER_STAGE_FRAGMENT_BIT, 0);

        REQUIRE(createInfo.bindingCount == 2);
        REQUIRE(createInfo.pBindings[0].descriptorCount == 128);
        REQUIRE(createInfo.pBindings[1].descriptorCount == 4096);
    }

    SECTION("Typical compute layout") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // Input buffer
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // Output buffer
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);  // Storage image

        auto createInfo = builder.Build(VK_SHADER_STAGE_COMPUTE_BIT, 0);

        REQUIRE(createInfo.bindingCount == 3);
        REQUIRE(createInfo.pBindings[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        REQUIRE(createInfo.pBindings[1].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        REQUIRE(createInfo.pBindings[2].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    SECTION("Typical graphics layout") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);           // Scene data
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4); // Textures
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);            // Instance data

        auto createInfo = builder.Build(
            static_cast<VkShaderStageFlagBits>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            0
        );

        REQUIRE(createInfo.bindingCount == 3);
    }
}

TEST_CASE("DescriptorLayoutBuilder edge cases", "[renderer][descriptor-builder]") {
    SECTION("Empty builder") {
        DescriptorLayoutBuilder builder;

        auto createInfo = builder.Build(VK_SHADER_STAGE_VERTEX_BIT, 0);

        REQUIRE(createInfo.bindingCount == 0);
        // pBindings might be nullptr or point to empty data
    }

    SECTION("High binding numbers") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(100, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.AddBinding(200, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        REQUIRE(builder.bindings.size() == 2);
        REQUIRE(builder.bindings[0].binding == 100);
        REQUIRE(builder.bindings[1].binding == 200);
    }

    SECTION("Duplicate binding numbers") {
        // This is technically invalid Vulkan usage, but the builder doesn't validate
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        // Builder will have both, validation would catch this later
        REQUIRE(builder.bindings.size() == 2);
        REQUIRE(builder.bindings[0].binding == 0);
        REQUIRE(builder.bindings[1].binding == 0);
    }

    SECTION("Non-sequential binding numbers") {
        DescriptorLayoutBuilder builder;

        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        builder.AddBinding(10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

        REQUIRE(builder.bindings.size() == 3);
        REQUIRE(builder.bindings[0].binding == 0);
        REQUIRE(builder.bindings[1].binding == 5);
        REQUIRE(builder.bindings[2].binding == 10);
    }
}
