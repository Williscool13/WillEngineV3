//
// Created by William on 2025-12-13.
//

#include "vk_pipeline.h"
#include <cassert>

namespace Render
{
RenderPipelineBuilder& RenderPipelineBuilder::SetShaders(VkPipelineShaderStageCreateInfo* _shaderStages, uint32_t shaderCount)
{
    shaderStages = _shaderStages;
    shaderStageCount = shaderCount;
    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupVertexInput(const VkVertexInputBindingDescription* bindings, uint32_t bindingCount,
                                                               const VkVertexInputAttributeDescription* attributes, uint32_t attributeCount)
{
    vertexBindings = bindings;
    vertexBindingCount = bindingCount;
    vertexAttributes = attributes;
    vertexAttributeCount = attributeCount;

    vertexInputInfo.pVertexBindingDescriptions = vertexBindings;
    vertexInputInfo.vertexBindingDescriptionCount = vertexBindingCount;
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;
    vertexInputInfo.vertexAttributeDescriptionCount = vertexAttributeCount;

    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupInputAssembly(VkPrimitiveTopology topology, bool enablePrimitiveRestart)
{
    inputAssembly.topology = topology;
    inputAssembly.primitiveRestartEnable = enablePrimitiveRestart;
    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupRasterization(VkPolygonMode polygonMode, VkCullModeFlags cullMode,
                                                                 VkFrontFace frontFace, float lineWidth, bool rasterizerDiscardEnable)
{
    rasterizer.polygonMode = polygonMode;
    rasterizer.lineWidth = lineWidth;
    rasterizer.cullMode = cullMode;
    rasterizer.frontFace = frontFace;
    rasterizer.rasterizerDiscardEnable = rasterizerDiscardEnable;
    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::EnableDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
{
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = depthBiasConstantFactor;
    rasterizer.depthBiasClamp = depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = depthBiasSlopeFactor;
    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupMultisampling(VkBool32 sampleShadingEnable, VkSampleCountFlagBits rasterizationSamples,
                                                                 float minSampleShading, const VkSampleMask* pSampleMask,
                                                                 VkBool32 alphaToCoverageEnable, VkBool32 alphaToOneEnable)
{
    multisampling.sampleShadingEnable = sampleShadingEnable;
    multisampling.rasterizationSamples = rasterizationSamples;
    multisampling.minSampleShading = minSampleShading;
    multisampling.pSampleMask = pSampleMask;
    multisampling.alphaToCoverageEnable = alphaToCoverageEnable;
    multisampling.alphaToOneEnable = alphaToOneEnable;
    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupRenderer(const VkFormat* _colorAttachmentFormats, uint32_t colorAttachmentCount,
                                                            VkFormat depthAttachmentFormat, VkFormat stencilAttachmentFormat)
{
    this->colorAttachmentFormats = _colorAttachmentFormats;
    this->colorAttachmentFormatCount = colorAttachmentCount;

    renderInfo.colorAttachmentCount = colorAttachmentCount;
    renderInfo.pColorAttachmentFormats = _colorAttachmentFormats;
    renderInfo.depthAttachmentFormat = depthAttachmentFormat;
    renderInfo.stencilAttachmentFormat = stencilAttachmentFormat;

    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupBlending(const VkPipelineColorBlendAttachmentState* blendAttachmentStates, uint32_t count)
{
    this->blendAttachmentStates = blendAttachmentStates;
    this->blendAttachmentStateCount = count;

    colorBlending.pAttachments = blendAttachmentStates;
    colorBlending.attachmentCount = count;

    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupDepthStencil(VkBool32 depthTestEnable, VkBool32 depthWriteEnable,
                                                                VkCompareOp compareOp, VkBool32 depthBoundsTestEnable,
                                                                VkBool32 stencilTestEnable, const VkStencilOpState& front,
                                                                const VkStencilOpState& back, float minDepthBounds, float maxDepthBounds)
{
    depthStencil.depthTestEnable = depthTestEnable;
    depthStencil.depthWriteEnable = depthWriteEnable;
    depthStencil.depthCompareOp = compareOp;
    depthStencil.depthBoundsTestEnable = depthBoundsTestEnable;
    depthStencil.stencilTestEnable = stencilTestEnable;
    depthStencil.front = front;
    depthStencil.back = back;
    depthStencil.minDepthBounds = minDepthBounds;
    depthStencil.maxDepthBounds = maxDepthBounds;
    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::EnableDepthTest(VkBool32 depthWriteEnable, VkCompareOp op)
{
    return SetupDepthStencil(VK_TRUE, depthWriteEnable, op, VK_FALSE, VK_FALSE, {}, {}, 0.0f, 1.0f);
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupPipelineLayout(VkPipelineLayout pipelineLayout_)
{
    pipelineLayout = pipelineLayout_;
    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::SetupTessellation(int32_t controlPoints)
{
    bIsTessellationEnabled = true;
    tessellation.patchControlPoints = controlPoints;
    return *this;
}

RenderPipelineBuilder& RenderPipelineBuilder::AddDynamicState(VkDynamicState dynamicState)
{
    dynamicStates.push_back(dynamicState);
    dynamicInfo.pDynamicStates = dynamicStates.data();
    dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    return *this;
}

VkGraphicsPipelineCreateInfo RenderPipelineBuilder::GeneratePipelineCreateInfo(VkPipelineCreateFlagBits flags)
{
    // Auto-fill blend states if not provided
    static const VkPipelineColorBlendAttachmentState defaultBlendAttachment{
        VK_FALSE,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    if (blendAttachmentStates == nullptr && colorAttachmentFormatCount > 0) {
        // User didn't provide blend states, use default for all attachments
        static std::vector<VkPipelineColorBlendAttachmentState> defaultBlends;
        defaultBlends.clear();
        defaultBlends.resize(colorAttachmentFormatCount, defaultBlendAttachment);

        blendAttachmentStates = defaultBlends.data();
        blendAttachmentStateCount = colorAttachmentFormatCount;

        colorBlending.pAttachments = blendAttachmentStates;
        colorBlending.attachmentCount = blendAttachmentStateCount;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderInfo;
    pipelineInfo.flags = flags;

    pipelineInfo.stageCount = shaderStageCount;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicInfo;

    if (bIsTessellationEnabled) {
        pipelineInfo.pTessellationState = &tessellation;
    }

    pipelineInfo.layout = pipelineLayout;

    return pipelineInfo;
}

void RenderPipelineBuilder::Clear()
{
    shaderStages = nullptr;
    shaderStageCount = 0;

    vertexBindings = nullptr;
    vertexBindingCount = 0;
    vertexAttributes = nullptr;
    vertexAttributeCount = 0;

    colorAttachmentFormats = nullptr;
    colorAttachmentFormatCount = 0;

    blendAttachmentStates = nullptr;
    blendAttachmentStateCount = 0;

    pipelineLayout = VK_NULL_HANDLE;

    inputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_NEVER,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };
    colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
    };
    renderInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    tessellation = {.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    bIsTessellationEnabled = false;

    dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicInfo.pDynamicStates = dynamicStates.data();
    dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
}
} // Render
