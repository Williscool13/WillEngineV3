//
// Created by William on 2025-12-13.
//

#ifndef WILL_ENGINE_VK_GRAPHICS_PIPELINE_BUILDER_H
#define WILL_ENGINE_VK_GRAPHICS_PIPELINE_BUILDER_H

#include <filesystem>
#include <volk.h>

namespace Render
{
class GraphicsPipelineBuilder
{
public:
    enum class BlendMode
    {
        ALPHA_BLEND,
        ADDITIVE_BLEND,
        NO_BLEND
    };

    static constexpr uint32_t MAX_SHADER_STAGES = 5;
    static constexpr uint32_t MAX_VERTEX_BINDINGS = 8;
    static constexpr uint32_t MAX_VERTEX_ATTRIBUTES = 16;
    static constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;
    static constexpr uint32_t MAX_DYNAMIC_STATES = 16;

    GraphicsPipelineBuilder& AddShaderStage(const std::filesystem::path& shaderPath, VkShaderStageFlagBits stage);

    GraphicsPipelineBuilder& SetupVertexInput(const VkVertexInputBindingDescription* bindings, uint32_t bindingCount,
                                              const VkVertexInputAttributeDescription* attributes, uint32_t attributeCount);

    GraphicsPipelineBuilder& SetupInputAssembly(VkPrimitiveTopology topology, bool enablePrimitiveRestart = false);

    GraphicsPipelineBuilder& SetupRasterization(VkPolygonMode polygonMode, VkCullModeFlags cullMode, VkFrontFace frontFace,
                                                float lineWidth = 1.0f, bool rasterizerDiscardEnable = false);

    GraphicsPipelineBuilder& EnableDepthBias(
        float depthBiasConstantFactor = 0.0f,
        float depthBiasClamp = 0.0f,
        float depthBiasSlopeFactor = 0.0f);

    GraphicsPipelineBuilder& SetupMultisampling(VkBool32 sampleShadingEnable, VkSampleCountFlagBits rasterizationSamples,
                                                float minSampleShading, const VkSampleMask* pSampleMask,
                                                VkBool32 alphaToCoverageEnable, VkBool32 alphaToOneEnable);

    GraphicsPipelineBuilder& SetupRenderer(const VkFormat* _colorAttachmentFormats, uint32_t colorAttachmentCount,
                                           VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED,
                                           VkFormat stencilAttachmentFormat = VK_FORMAT_UNDEFINED);

    GraphicsPipelineBuilder& SetupBlending(const VkPipelineColorBlendAttachmentState* blendAttachmentStates, uint32_t count);

    GraphicsPipelineBuilder& SetupDepthState(
        VkBool32 depthTestEnable = VK_TRUE,
        VkBool32 depthWriteEnable = VK_TRUE,
        VkCompareOp compareOp = VK_COMPARE_OP_GREATER,
        VkBool32 depthBoundsTestEnable = VK_FALSE
    );

    GraphicsPipelineBuilder& SetupStencilState(
        VkBool32 stencilTestEnable = VK_FALSE,
        VkStencilOp failOp = VK_STENCIL_OP_KEEP,
        VkStencilOp passOp = VK_STENCIL_OP_KEEP,
        VkStencilOp depthFailOp = VK_STENCIL_OP_KEEP,
        VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS,
        uint32_t compareMask = 0xFF,
        uint32_t writeMask = 0xFF,
        uint32_t reference = 0
    );

    GraphicsPipelineBuilder& SetupTessellation(int32_t controlPoints = 4);

    GraphicsPipelineBuilder& AddDynamicState(VkDynamicState dynamicState);

    void Clear();

    std::filesystem::path shaderPaths[MAX_SHADER_STAGES];
    VkPipelineShaderStageCreateInfo shaderStages[MAX_SHADER_STAGES];
    uint32_t shaderStageCount{0};

    VkVertexInputBindingDescription vertexBindings[MAX_VERTEX_BINDINGS];
    uint32_t vertexBindingCount{0};
    VkVertexInputAttributeDescription vertexAttributes[MAX_VERTEX_ATTRIBUTES];
    uint32_t vertexAttributeCount{0};

    VkFormat colorAttachmentFormats[MAX_COLOR_ATTACHMENTS];
    uint32_t colorAttachmentFormatCount{0};

    VkPipelineColorBlendAttachmentState blendAttachmentStates[MAX_COLOR_ATTACHMENTS];
    uint32_t blendAttachmentStateCount{0};

    VkDynamicState dynamicStates[MAX_DYNAMIC_STATES]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    uint32_t dynamicStateCount{2}; // viewport + scissor

    VkPushConstantRange pushConstantRange{};

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
    };

    VkPipelineRenderingCreateInfo renderInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    };

    VkPipelineTessellationStateCreateInfo tessellation{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
    };

    VkPipelineDynamicStateCreateInfo dynamicInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    };

    bool bIsTessellationEnabled{false};
};
} // Render

#endif //WILL_ENGINE_VK_GRAPHICS_PIPELINE_BUILDER_H
