//
// Created by William on 2025-12-13.
//

#include "graphics_pipeline_builder.h"
#include <cassert>

namespace Render
{
GraphicsPipelineBuilder& GraphicsPipelineBuilder::AddShaderStage(const std::filesystem::path& shaderPath, VkShaderStageFlagBits stage)
{
    assert(shaderStageCount < MAX_SHADER_STAGES && "Too many shader stages");

    shaderPaths[shaderStageCount] = shaderPath;
    shaderStages[shaderStageCount] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = VK_NULL_HANDLE, // Filled in creation
        .pName = "main",
    };
    shaderStageCount++;

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupVertexInput(const VkVertexInputBindingDescription* bindings, uint32_t bindingCount,
                                                                   const VkVertexInputAttributeDescription* attributes, uint32_t attributeCount)
{
    assert(bindingCount <= MAX_VERTEX_BINDINGS && "Too many vertex bindings");
    assert(attributeCount <= MAX_VERTEX_ATTRIBUTES && "Too many vertex attributes");

    vertexBindingCount = bindingCount;
    for (uint32_t i = 0; i < bindingCount; ++i) {
        vertexBindings[i] = bindings[i];
    }

    vertexAttributeCount = attributeCount;
    for (uint32_t i = 0; i < attributeCount; ++i) {
        vertexAttributes[i] = attributes[i];
    }

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupInputAssembly(VkPrimitiveTopology topology, bool enablePrimitiveRestart)
{
    inputAssembly.topology = topology;
    inputAssembly.primitiveRestartEnable = enablePrimitiveRestart;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupRasterization(VkPolygonMode polygonMode, VkCullModeFlags cullMode,
                                                                     VkFrontFace frontFace, float lineWidth, bool rasterizerDiscardEnable)
{
    rasterizer.polygonMode = polygonMode;
    rasterizer.lineWidth = lineWidth;
    rasterizer.cullMode = cullMode;
    rasterizer.frontFace = frontFace;
    rasterizer.rasterizerDiscardEnable = rasterizerDiscardEnable;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::EnableDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
{
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = depthBiasConstantFactor;
    rasterizer.depthBiasClamp = depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = depthBiasSlopeFactor;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupMultisampling(VkBool32 sampleShadingEnable, VkSampleCountFlagBits rasterizationSamples,
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

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupRenderer(const VkFormat* _colorAttachmentFormats, uint32_t colorAttachmentCount,
                                                                VkFormat depthAttachmentFormat, VkFormat stencilAttachmentFormat)
{
    assert(colorAttachmentCount <= MAX_COLOR_ATTACHMENTS && "Too many color attachments");

    this->colorAttachmentFormatCount = colorAttachmentCount;
    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
        this->colorAttachmentFormats[i] = _colorAttachmentFormats[i];
    }

    renderInfo.colorAttachmentCount = colorAttachmentCount;
    renderInfo.depthAttachmentFormat = depthAttachmentFormat;
    renderInfo.stencilAttachmentFormat = stencilAttachmentFormat;

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupBlending(const VkPipelineColorBlendAttachmentState* _blendAttachmentStates, uint32_t count)
{
    assert(count <= MAX_COLOR_ATTACHMENTS && "Too many blend attachment states");

    this->blendAttachmentStateCount = count;
    for (uint32_t i = 0; i < count; ++i) {
        this->blendAttachmentStates[i] = _blendAttachmentStates[i];
    }

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupDepthState(VkBool32 depthTestEnable, VkBool32 depthWriteEnable, VkCompareOp compareOp, VkBool32 depthBoundsTestEnable)
{
    depthStencil.depthTestEnable = depthTestEnable;
    depthStencil.depthWriteEnable = depthWriteEnable;
    depthStencil.depthCompareOp = compareOp;
    depthStencil.depthBoundsTestEnable = depthBoundsTestEnable;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupStencilState(
    VkBool32 stencilTestEnable,
    VkStencilOp failOp,
    VkStencilOp passOp,
    VkStencilOp depthFailOp,
    VkCompareOp compareOp,
    uint32_t compareMask,
    uint32_t writeMask,
    uint32_t reference)
{
    depthStencil.stencilTestEnable = stencilTestEnable;

    depthStencil.front.failOp = failOp;
    depthStencil.front.passOp = passOp;
    depthStencil.front.depthFailOp = depthFailOp;
    depthStencil.front.compareOp = compareOp;
    depthStencil.front.compareMask = compareMask;
    depthStencil.front.writeMask = writeMask;
    depthStencil.front.reference = reference;

    depthStencil.back = depthStencil.front;

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetupTessellation(int32_t controlPoints)
{
    bIsTessellationEnabled = true;
    tessellation.patchControlPoints = controlPoints;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::AddDynamicState(VkDynamicState dynamicState)
{
    assert(dynamicStateCount < MAX_DYNAMIC_STATES && "Too many dynamic states");

    dynamicStates[dynamicStateCount++] = dynamicState;

    return *this;
}

void GraphicsPipelineBuilder::Clear()
{
    shaderStageCount = 0;
    vertexBindingCount = 0;
    vertexAttributeCount = 0;
    colorAttachmentFormatCount = 0;
    blendAttachmentStateCount = 0;

    dynamicStateCount = 2;
    dynamicStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
    dynamicStates[1] = VK_DYNAMIC_STATE_SCISSOR;

    pushConstantRange = {};

    inputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
    };
    depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };
    colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
    };
    renderInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    tessellation = {.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    vertexInputInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    dynamicInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};

    bIsTessellationEnabled = false;
}
} // Render