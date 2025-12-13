//
// Created by William on 2025-12-13.
//

#ifndef WILL_ENGINE_VK_PIPELINE_H
#define WILL_ENGINE_VK_PIPELINE_H

#include <vector>
#include <volk.h>

namespace Render
{
class RenderPipelineBuilder
{
public:
    enum class BlendMode
    {
        ALPHA_BLEND,
        ADDITIVE_BLEND,
        NO_BLEND
    };

    RenderPipelineBuilder& SetShaders(VkPipelineShaderStageCreateInfo* _shaderStages, uint32_t shaderCount);

    RenderPipelineBuilder& SetupVertexInput(const VkVertexInputBindingDescription* bindings, uint32_t bindingCount,
                                            const VkVertexInputAttributeDescription* attributes, uint32_t attributeCount);

    RenderPipelineBuilder& SetupInputAssembly(VkPrimitiveTopology topology, bool enablePrimitiveRestart = false);

    RenderPipelineBuilder& SetupRasterization(VkPolygonMode polygonMode, VkCullModeFlags cullMode, VkFrontFace frontFace,
                                              float lineWidth = 1.0f, bool rasterizerDiscardEnable = false);

    RenderPipelineBuilder& EnableDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor);

    RenderPipelineBuilder& SetupMultisampling(VkBool32 sampleShadingEnable, VkSampleCountFlagBits rasterizationSamples,
                                              float minSampleShading, const VkSampleMask* pSampleMask,
                                              VkBool32 alphaToCoverageEnable, VkBool32 alphaToOneEnable);

    RenderPipelineBuilder& DisableMultisampling();

    RenderPipelineBuilder& SetupRenderer(const VkFormat* colorAttachmentFormats, uint32_t colorAttachmentCount,
                                         VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED,
                                         VkFormat stencilAttachmentFormat = VK_FORMAT_UNDEFINED);

    RenderPipelineBuilder& SetupBlending(const VkPipelineColorBlendAttachmentState* blendAttachmentStates, uint32_t count);

    RenderPipelineBuilder& SetupDepthStencil(VkBool32 depthTestEnable, VkBool32 depthWriteEnable,
                                             VkCompareOp compareOp, VkBool32 depthBoundsTestEnable,
                                             VkBool32 stencilTestEnable, const VkStencilOpState& front,
                                             const VkStencilOpState& back, float minDepthBounds, float maxDepthBounds);

    RenderPipelineBuilder& EnableDepthTest(VkBool32 depthWriteEnable, VkCompareOp op);

    RenderPipelineBuilder& DisableDepthTest();

    RenderPipelineBuilder& SetupPipelineLayout(VkPipelineLayout pipelineLayout_);

    RenderPipelineBuilder& SetupTessellation(int32_t controlPoints = 4);

    RenderPipelineBuilder& AddDynamicState(VkDynamicState dynamicState);

    VkGraphicsPipelineCreateInfo GeneratePipelineCreateInfo(VkPipelineCreateFlagBits flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

    void Clear();

private:
    VkPipelineShaderStageCreateInfo* shaderStages{nullptr};
    uint32_t shaderStageCount{0};

    const VkVertexInputBindingDescription* vertexBindings{nullptr};
    uint32_t vertexBindingCount{0};
    const VkVertexInputAttributeDescription* vertexAttributes{nullptr};
    uint32_t vertexAttributeCount{0};

    const VkFormat* colorAttachmentFormats{nullptr};
    uint32_t colorAttachmentFormatCount{0};

    const VkPipelineColorBlendAttachmentState* blendAttachmentStates{nullptr};
    uint32_t blendAttachmentStateCount{0};

    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
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
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_NEVER,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 0,
    };

    VkPipelineRenderingCreateInfo renderInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    };

    VkPipelineTessellationStateCreateInfo tessellation{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
    };

    bool bIsTessellationEnabled{false};

    std::vector<VkDynamicState> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates.data(),
    };
};
} // Render

#endif //WILL_ENGINE_VK_PIPELINE_H