//
// Created by William on 2025-12-25.
//
// Tests for RenderPipelineBuilder to ensure proper builder pattern usage,
// state management, and CreateInfo generation.
//

#include <catch2/catch_test_macros.hpp>

#include "render/pipelines/graphics_pipeline_builder.h"

using namespace Render;

TEST_CASE("RenderPipelineBuilder method chaining", "[renderer][pipeline-builder]") {
    SECTION("All methods return reference for chaining") {
        GraphicsPipelineBuilder builder;

        VkPipelineShaderStageCreateInfo shaderStages[2] = {};
        VkVertexInputBindingDescription binding = {};
        VkVertexInputAttributeDescription attribute = {};
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineColorBlendAttachmentState blendState = {};
        VkStencilOpState stencilOp = {};

        // Test that all methods return a reference for chaining
        GraphicsPipelineBuilder& ref1 = builder.SetShaders(shaderStages, 2);
        GraphicsPipelineBuilder& ref2 = ref1.SetupVertexInput(&binding, 1, &attribute, 1);
        GraphicsPipelineBuilder& ref3 = ref2.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        GraphicsPipelineBuilder& ref4 = ref3.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        GraphicsPipelineBuilder& ref5 = ref4.SetupMultisampling(VK_FALSE, VK_SAMPLE_COUNT_1_BIT, 1.0f, nullptr, VK_FALSE, VK_FALSE);
        GraphicsPipelineBuilder& ref6 = ref5.SetupRenderer(&colorFormat, 1);
        GraphicsPipelineBuilder& ref7 = ref6.SetupBlending(&blendState, 1);
        GraphicsPipelineBuilder& ref8 = ref7.SetupDepthStencil(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_FALSE, VK_FALSE, stencilOp, stencilOp, 0.0f, 1.0f);
        GraphicsPipelineBuilder& ref9 = ref8.EnableDepthTest(VK_TRUE, VK_COMPARE_OP_LESS);
        GraphicsPipelineBuilder& ref10 = ref9.SetupPipelineLayout(VK_NULL_HANDLE);
        GraphicsPipelineBuilder& ref11 = ref10.SetupTessellation(4);
        GraphicsPipelineBuilder& ref12 = ref11.AddDynamicState(VK_DYNAMIC_STATE_LINE_WIDTH);

        // All references should point to the same builder
        REQUIRE(&ref1 == &builder);
        REQUIRE(&ref2 == &builder);
        REQUIRE(&ref3 == &builder);
        REQUIRE(&ref4 == &builder);
        REQUIRE(&ref5 == &builder);
        REQUIRE(&ref6 == &builder);
        REQUIRE(&ref7 == &builder);
        REQUIRE(&ref8 == &builder);
        REQUIRE(&ref9 == &builder);
        REQUIRE(&ref10 == &builder);
        REQUIRE(&ref11 == &builder);
        REQUIRE(&ref12 == &builder);
    }
}

TEST_CASE("RenderPipelineBuilder shader stages", "[renderer][pipeline-builder]") {
    SECTION("Set shader stages") {
        GraphicsPipelineBuilder builder;
        VkPipelineShaderStageCreateInfo shaderStages[2] = {
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT}
        };

        builder.SetShaders(shaderStages, 2);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.stageCount == 2);
        REQUIRE(createInfo.pStages == shaderStages);
    }

    SECTION("Single shader stage") {
        GraphicsPipelineBuilder builder;
        VkPipelineShaderStageCreateInfo shaderStage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT
        };

        builder.SetShaders(&shaderStage, 1);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.stageCount == 1);
    }
}

TEST_CASE("RenderPipelineBuilder vertex input", "[renderer][pipeline-builder]") {
    SECTION("With vertex bindings and attributes") {
        GraphicsPipelineBuilder builder;

        VkVertexInputBindingDescription binding = {
            .binding = 0,
            .stride = 32,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        };

        VkVertexInputAttributeDescription attributes[2] = {
            {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},
            {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 12}
        };

        builder.SetupVertexInput(&binding, 1, attributes, 2);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pVertexInputState != nullptr);
        REQUIRE(createInfo.pVertexInputState->vertexBindingDescriptionCount == 1);
        REQUIRE(createInfo.pVertexInputState->vertexAttributeDescriptionCount == 2);
    }

    SECTION("No vertex input (mesh shaders)") {
        GraphicsPipelineBuilder builder;

        builder.SetupVertexInput(nullptr, 0, nullptr, 0);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pVertexInputState != nullptr);
        REQUIRE(createInfo.pVertexInputState->vertexBindingDescriptionCount == 0);
        REQUIRE(createInfo.pVertexInputState->vertexAttributeDescriptionCount == 0);
    }
}

TEST_CASE("RenderPipelineBuilder input assembly", "[renderer][pipeline-builder]") {
    SECTION("Triangle list topology") {
        GraphicsPipelineBuilder builder;

        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pInputAssemblyState != nullptr);
        REQUIRE(createInfo.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        REQUIRE(createInfo.pInputAssemblyState->primitiveRestartEnable == VK_FALSE);
    }

    SECTION("Triangle strip with primitive restart") {
        GraphicsPipelineBuilder builder;

        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, true);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        REQUIRE(createInfo.pInputAssemblyState->primitiveRestartEnable == VK_TRUE);
    }

    SECTION("Line list topology") {
        GraphicsPipelineBuilder builder;

        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    }
}

TEST_CASE("RenderPipelineBuilder rasterization", "[renderer][pipeline-builder]") {
    SECTION("Fill mode with backface culling") {
        GraphicsPipelineBuilder builder;

        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pRasterizationState != nullptr);
        REQUIRE(createInfo.pRasterizationState->polygonMode == VK_POLYGON_MODE_FILL);
        REQUIRE(createInfo.pRasterizationState->cullMode == VK_CULL_MODE_BACK_BIT);
        REQUIRE(createInfo.pRasterizationState->frontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE);
        REQUIRE(createInfo.pRasterizationState->lineWidth == 1.0f);
    }

    SECTION("Wireframe mode with no culling") {
        GraphicsPipelineBuilder builder;

        builder.SetupRasterization(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, 2.0f);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pRasterizationState->polygonMode == VK_POLYGON_MODE_LINE);
        REQUIRE(createInfo.pRasterizationState->cullMode == VK_CULL_MODE_NONE);
        REQUIRE(createInfo.pRasterizationState->lineWidth == 2.0f);
    }

    SECTION("With depth bias") {
        GraphicsPipelineBuilder builder;

        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        builder.EnableDepthBias(1.0f, 0.0f, 1.5f);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pRasterizationState->depthBiasEnable == VK_TRUE);
        REQUIRE(createInfo.pRasterizationState->depthBiasConstantFactor == 1.0f);
        REQUIRE(createInfo.pRasterizationState->depthBiasClamp == 0.0f);
        REQUIRE(createInfo.pRasterizationState->depthBiasSlopeFactor == 1.5f);
    }
}

TEST_CASE("RenderPipelineBuilder multisampling", "[renderer][pipeline-builder]") {
    SECTION("No multisampling") {
        GraphicsPipelineBuilder builder;

        builder.SetupMultisampling(VK_FALSE, VK_SAMPLE_COUNT_1_BIT, 1.0f, nullptr, VK_FALSE, VK_FALSE);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pMultisampleState != nullptr);
        REQUIRE(createInfo.pMultisampleState->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT);
        REQUIRE(createInfo.pMultisampleState->sampleShadingEnable == VK_FALSE);
    }

    SECTION("4x MSAA with sample shading") {
        GraphicsPipelineBuilder builder;

        builder.SetupMultisampling(VK_TRUE, VK_SAMPLE_COUNT_4_BIT, 0.25f, nullptr, VK_FALSE, VK_FALSE);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pMultisampleState->rasterizationSamples == VK_SAMPLE_COUNT_4_BIT);
        REQUIRE(createInfo.pMultisampleState->sampleShadingEnable == VK_TRUE);
        REQUIRE(createInfo.pMultisampleState->minSampleShading == 0.25f);
    }
}

TEST_CASE("RenderPipelineBuilder depth and stencil", "[renderer][pipeline-builder]") {
    SECTION("Depth test enabled with write enabled") {
        GraphicsPipelineBuilder builder;

        builder.EnableDepthTest(VK_TRUE, VK_COMPARE_OP_LESS);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pDepthStencilState != nullptr);
        REQUIRE(createInfo.pDepthStencilState->depthTestEnable == VK_TRUE);
        REQUIRE(createInfo.pDepthStencilState->depthWriteEnable == VK_TRUE);
        REQUIRE(createInfo.pDepthStencilState->depthCompareOp == VK_COMPARE_OP_LESS);
    }

    SECTION("Depth test enabled with write disabled") {
        GraphicsPipelineBuilder builder;

        builder.EnableDepthTest(VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pDepthStencilState->depthTestEnable == VK_TRUE);  // Test is enabled
        REQUIRE(createInfo.pDepthStencilState->depthWriteEnable == VK_FALSE); // Write is disabled
        REQUIRE(createInfo.pDepthStencilState->depthCompareOp == VK_COMPARE_OP_LESS_OR_EQUAL);
    }

    SECTION("Depth test with different compare ops") {
        GraphicsPipelineBuilder builder;

        builder.EnableDepthTest(VK_TRUE, VK_COMPARE_OP_GREATER);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pDepthStencilState->depthTestEnable == VK_TRUE);
        REQUIRE(createInfo.pDepthStencilState->depthWriteEnable == VK_TRUE);
        REQUIRE(createInfo.pDepthStencilState->depthCompareOp == VK_COMPARE_OP_GREATER);
    }
}

TEST_CASE("RenderPipelineBuilder color blending", "[renderer][pipeline-builder]") {
    SECTION("Alpha blending") {
        GraphicsPipelineBuilder builder;

        VkPipelineColorBlendAttachmentState blendState = {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        builder.SetupBlending(&blendState, 1);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pColorBlendState != nullptr);
        REQUIRE(createInfo.pColorBlendState->attachmentCount == 1);
        REQUIRE(createInfo.pColorBlendState->pAttachments == &blendState);
    }

    SECTION("Multiple render targets") {
        GraphicsPipelineBuilder builder;

        VkPipelineColorBlendAttachmentState blendStates[2] = {
            {.blendEnable = VK_FALSE, .colorWriteMask = 0xF},
            {.blendEnable = VK_TRUE, .colorWriteMask = 0xF}
        };

        builder.SetupBlending(blendStates, 2);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pColorBlendState->attachmentCount == 2);
    }
}

TEST_CASE("RenderPipelineBuilder rendering setup", "[renderer][pipeline-builder]") {
    SECTION("Single color attachment") {
        GraphicsPipelineBuilder builder;

        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        builder.SetupRenderer(&format, 1);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        // The rendering create info is in the pNext chain
        REQUIRE(createInfo.pNext != nullptr);
        auto* renderInfo = static_cast<const VkPipelineRenderingCreateInfo*>(createInfo.pNext);
        REQUIRE(renderInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        REQUIRE(renderInfo->colorAttachmentCount == 1);
        REQUIRE(renderInfo->pColorAttachmentFormats == &format);
    }

    SECTION("Multiple color attachments with depth") {
        GraphicsPipelineBuilder builder;

        VkFormat formats[2] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
        builder.SetupRenderer(formats, 2, VK_FORMAT_D32_SFLOAT);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        auto* renderInfo = static_cast<const VkPipelineRenderingCreateInfo*>(createInfo.pNext);
        REQUIRE(renderInfo->colorAttachmentCount == 2);
        REQUIRE(renderInfo->depthAttachmentFormat == VK_FORMAT_D32_SFLOAT);
    }
}

TEST_CASE("RenderPipelineBuilder dynamic states", "[renderer][pipeline-builder]") {
    SECTION("Default dynamic states") {
        GraphicsPipelineBuilder builder;

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pDynamicState != nullptr);
        // Default has viewport and scissor
        REQUIRE(createInfo.pDynamicState->dynamicStateCount >= 2);
    }

    SECTION("Add custom dynamic state") {
        GraphicsPipelineBuilder builder;

        builder.AddDynamicState(VK_DYNAMIC_STATE_LINE_WIDTH);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pDynamicState->dynamicStateCount >= 3);
    }

    SECTION("Multiple custom dynamic states") {
        GraphicsPipelineBuilder builder;

        builder.AddDynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS);
        builder.AddDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pDynamicState->dynamicStateCount >= 4);
    }
}

TEST_CASE("RenderPipelineBuilder tessellation", "[renderer][pipeline-builder]") {
    SECTION("Tessellation disabled by default") {
        GraphicsPipelineBuilder builder;

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pTessellationState == nullptr);
    }

    SECTION("Enable tessellation") {
        GraphicsPipelineBuilder builder;

        builder.SetupTessellation(4);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pTessellationState != nullptr);
        REQUIRE(createInfo.pTessellationState->patchControlPoints == 4);
    }

    SECTION("Custom control points") {
        GraphicsPipelineBuilder builder;

        builder.SetupTessellation(16);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.pTessellationState->patchControlPoints == 16);
    }
}

TEST_CASE("RenderPipelineBuilder clear and reuse", "[renderer][pipeline-builder]") {
    SECTION("Clear resets builder state") {
        GraphicsPipelineBuilder builder;

        VkPipelineShaderStageCreateInfo shaderStages[2] = {};
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

        builder.SetShaders(shaderStages, 2);
        builder.SetupRenderer(&format, 1);
        builder.SetupTessellation(4);

        builder.Clear();

        auto createInfo = builder.GeneratePipelineCreateInfo();

        // After clear, shader count should be 0 and tessellation disabled
        REQUIRE(createInfo.stageCount == 0);
        REQUIRE(createInfo.pTessellationState == nullptr);
    }

    SECTION("Builder can be reused after clear") {
        GraphicsPipelineBuilder builder;

        VkPipelineShaderStageCreateInfo shaderStages[1] = {};
        builder.SetShaders(shaderStages, 1);
        builder.Clear();

        VkPipelineShaderStageCreateInfo newShaderStages[2] = {};
        builder.SetShaders(newShaderStages, 2);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.stageCount == 2);
    }
}

TEST_CASE("RenderPipelineBuilder pipeline layout", "[renderer][pipeline-builder]") {
    SECTION("Set pipeline layout") {
        GraphicsPipelineBuilder builder;

        VkPipelineLayout layout = reinterpret_cast<VkPipelineLayout>(0x12345678);
        builder.SetupPipelineLayout(layout);

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.layout == layout);
    }
}

TEST_CASE("RenderPipelineBuilder create info structure", "[renderer][pipeline-builder]") {
    SECTION("Create info has correct sType") {
        GraphicsPipelineBuilder builder;

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE(createInfo.sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    }

    SECTION("Create info has descriptor buffer flag") {
        GraphicsPipelineBuilder builder;

        auto createInfo = builder.GeneratePipelineCreateInfo();

        REQUIRE((createInfo.flags & VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) != 0);
    }

    SECTION("Custom flags can be passed") {
        GraphicsPipelineBuilder builder;

        auto createInfo = builder.GeneratePipelineCreateInfo(
            static_cast<VkPipelineCreateFlagBits>(VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT)
        );

        REQUIRE((createInfo.flags & VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT) != 0);
    }
}
