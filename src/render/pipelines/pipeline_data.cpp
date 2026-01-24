//
// Created by William on 2026-01-24.
//

#include "pipeline_data.h"

#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
bool ComputePipelineData::CreatePipeline(VulkanContext* context, VkPipelineCache pipelineCache)
{
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (!VkHelpers::LoadShaderModule(shaderPath, context->device, &shaderModule)) {
        SPDLOG_ERROR("Failed to load shader: {}", shaderPath.string());
        return false;
    }

    VkResult layoutResult = vkCreatePipelineLayout(context->device, &layoutCreateInfo, nullptr, &loadingEntry.layout);
    if (layoutResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create pipeline layout for: {}", shaderPath.string());
        vkDestroyShaderModule(context->device, shaderModule, nullptr);
        return false;
    }


    VkPipelineShaderStageCreateInfo shaderStage = VkHelpers::PipelineShaderStageCreateInfo(shaderModule, VK_SHADER_STAGE_COMPUTE_BIT);
    VkComputePipelineCreateInfo pipelineInfo = VkHelpers::ComputePipelineCreateInfo(loadingEntry.layout, shaderStage);
    VkResult pipelineResult = vkCreateComputePipelines(context->device, pipelineCache, 1, &pipelineInfo, nullptr, &loadingEntry.pipeline);

    if (pipelineResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create compute pipeline: {}", shaderPath.string());
        vkDestroyPipelineLayout(context->device, loadingEntry.layout, nullptr);
        vkDestroyShaderModule(context->device, shaderModule, nullptr);
        return false;
    }

    lastModified = std::filesystem::last_write_time(shaderPath);
    retirementFrame = 0;
    vkDestroyShaderModule(context->device, shaderModule, nullptr);

    return true;
}

bool GraphicsPipelineData::CreatePipeline(VulkanContext* context, VkPipelineCache pipelineCache)
{
    VkShaderModule shaderModules[MAX_SHADER_STAGES] {
        VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE
    };
    for (uint32_t i = 0; i < shaderStageCount; ++i) {
        if (!VkHelpers::LoadShaderModule(shaderPaths[i], context->device, &shaderModules[i])) {
            SPDLOG_ERROR("Failed to load shader: {}", shaderPaths[i].string());
            for (uint32_t j = 0; j < i; ++j) {
                if (shaderModules[j] != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(context->device, shaderModules[j], nullptr);
                }
            }
            return false;
        }
        shaderStages[i].module = shaderModules[i];
    }

    VkResult layoutResult = vkCreatePipelineLayout(context->device, &layoutCreateInfo, nullptr, &loadingEntry.layout);
    if (layoutResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create pipeline layout for graphics pipeline");
        for (uint32_t i = 0; i < shaderStageCount; ++i) {
            vkDestroyShaderModule(context->device, shaderModules[i], nullptr);
        }
        return false;
    }

    if (blendAttachmentStateCount == 0 && colorAttachmentFormatCount > 0) {
        VkPipelineColorBlendAttachmentState defaultBlend{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        blendAttachmentStateCount = colorAttachmentFormatCount;
        for (uint32_t i = 0; i < colorAttachmentFormatCount; ++i) {
            blendAttachmentStates[i] = defaultBlend;
        }
    }


    vertexInputInfo.vertexBindingDescriptionCount = vertexBindingCount;
    vertexInputInfo.pVertexBindingDescriptions = vertexBindingCount > 0 ? vertexBindings : nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = vertexAttributeCount;
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributeCount > 0 ? vertexAttributes : nullptr;

    colorBlending.attachmentCount = blendAttachmentStateCount;
    colorBlending.pAttachments = blendAttachmentStateCount > 0 ? blendAttachmentStates : nullptr;

    renderInfo.colorAttachmentCount = colorAttachmentFormatCount;
    renderInfo.pColorAttachmentFormats = colorAttachmentFormatCount > 0 ? colorAttachmentFormats : nullptr;

    dynamicInfo.dynamicStateCount = dynamicStateCount;
    dynamicInfo.pDynamicStates = dynamicStateCount > 0 ? dynamicStates : nullptr;

    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderInfo,
        .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .stageCount = shaderStageCount,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pTessellationState = bIsTessellationEnabled ? &tessellation : nullptr,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicInfo,
        .layout = loadingEntry.layout,
    };

    VkResult pipelineResult = vkCreateGraphicsPipelines(context->device, pipelineCache, 1, &pipelineInfo, nullptr, &loadingEntry.pipeline);

    if (pipelineResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create graphics pipeline");
        vkDestroyPipelineLayout(context->device, loadingEntry.layout, nullptr);
        for (uint32_t i = 0; i < shaderStageCount; ++i) {
            vkDestroyShaderModule(context->device, shaderModules[i], nullptr);
        }
        return false;
    }

    lastModified = std::filesystem::file_time_type::min();
    for (uint32_t i = 0; i < shaderStageCount; ++i) {
        auto modTime = std::filesystem::last_write_time(shaderPaths[i]);
        if (modTime > lastModified) {
            lastModified = modTime;
        }
    }

    retirementFrame = 0;

    for (uint32_t i = 0; i < shaderStageCount; ++i) {
        vkDestroyShaderModule(context->device, shaderModules[i], nullptr);
    }

    return true;
}
} // Render
