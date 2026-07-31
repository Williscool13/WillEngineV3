//
// Created by William on 2026-01-24.
//

#include "pipeline_data.h"

#include "core/containers/array.h"
#include "platform/file_utils.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
bool ComputePipelineData::CreatePipeline(VulkanContext* context, Core::MemoryManager* memoryManager, VkPipelineCache pipelineCache)
{
    loadingLastModified = GetLatestShaderWriteTime();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (!VkHelpers::LoadShaderModule(&memoryManager->AssetsScratch(), shaderPath, context->device, &shaderModule)) {
        SPDLOG_ERROR("Failed to load shader: {}", shaderPath.c_str());
        return false;
    }

    VkResult layoutResult = vkCreatePipelineLayout(context->device, &layoutCreateInfo, nullptr, &loadingEntry.layout);
    if (layoutResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create pipeline layout for: {}", shaderPath.c_str());
        vkDestroyShaderModule(context->device, shaderModule, nullptr);
        return false;
    }


    VkPipelineShaderStageCreateInfo shaderStage = VkHelpers::PipelineShaderStageCreateInfo(shaderModule, VK_SHADER_STAGE_COMPUTE_BIT, entryPoint.c_str());
    Core::Array<uint32_t, 6> specConstantData{
        VulkanContext::deviceInfo.meshShaderProps.maxTaskWorkGroupCount[0],
        VulkanContext::deviceInfo.meshShaderProps.maxTaskWorkGroupCount[1],
        VulkanContext::deviceInfo.meshShaderProps.maxTaskWorkGroupCount[2],
        VulkanContext::deviceInfo.meshShaderProps.maxMeshWorkGroupCount[0],
        VulkanContext::deviceInfo.meshShaderProps.maxMeshWorkGroupCount[1],
        VulkanContext::deviceInfo.meshShaderProps.maxMeshWorkGroupCount[2],
    };

    Core::Array<VkSpecializationMapEntry, 6> entries{};
    for (uint32_t i = 0; i < 6; i++) {
        entries[i].constantID = i;
        entries[i].offset = i * sizeof(uint32_t);
        entries[i].size = sizeof(uint32_t);
    }

    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 6;
    specInfo.pMapEntries = entries.Data();
    specInfo.dataSize = specConstantData.Size() * sizeof(uint32_t);
    specInfo.pData = specConstantData.Data();
    shaderStage.pSpecializationInfo = &specInfo;

    VkComputePipelineCreateInfo pipelineInfo = VkHelpers::ComputePipelineCreateInfo(loadingEntry.layout, shaderStage);
#ifdef ENABLE_VULKAN_VALIDATION
    if (context->bPipelineExecutablePropertiesEnabled) {
        pipelineInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    }
#endif
    VkResult pipelineResult = vkCreateComputePipelines(context->device, pipelineCache, 1, &pipelineInfo, nullptr, &loadingEntry.pipeline);

    if (pipelineResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create compute pipeline: {}", shaderPath.c_str());
        vkDestroyPipelineLayout(context->device, loadingEntry.layout, nullptr);
        vkDestroyShaderModule(context->device, shaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(context->device, shaderModule, nullptr);

    return true;
}

uint64_t ComputePipelineData::GetLatestShaderWriteTime() const
{
    return Platform::GetFileWriteTime(shaderPath.c_str());
}

bool GraphicsPipelineData::CreatePipeline(VulkanContext* context, Core::MemoryManager* memoryManager, VkPipelineCache pipelineCache)
{
    loadingLastModified = GetLatestShaderWriteTime();

    Core::Array<VkShaderModule, MAX_SHADER_STAGES> shaderModules{};
    for (uint32_t i = 0; i < shaderStages.Size(); ++i) {
        if (!VkHelpers::LoadShaderModule(&memoryManager->AssetsScratch(), shaderPaths[i], context->device, &shaderModules[i])) {
            SPDLOG_ERROR("Failed to load shader: {}", shaderPaths[i].c_str());
            for (uint32_t j = 0; j < i; ++j) {
                if (shaderModules[j] != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(context->device, shaderModules[j], nullptr);
                }
            }
            return false;
        }
        shaderStages[i].module = shaderModules[i];
        shaderStages[i].pName = entryPoints[i].c_str();
    }

    VkResult layoutResult = vkCreatePipelineLayout(context->device, &layoutCreateInfo, nullptr, &loadingEntry.layout);
    if (layoutResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create pipeline layout for graphics pipeline");
        for (uint32_t i = 0; i < shaderStages.Size(); ++i) {
            vkDestroyShaderModule(context->device, shaderModules[i], nullptr);
        }
        return false;
    }

    if (blendAttachmentStates.IsEmpty() && !colorAttachmentFormats.IsEmpty()) {
        VkPipelineColorBlendAttachmentState defaultBlend{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        for (uint32_t i = 0; i < colorAttachmentFormats.Size(); ++i) {
            blendAttachmentStates.PushBack(defaultBlend);
        }
    }

    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.Size());
    vertexInputInfo.pVertexBindingDescriptions = !vertexBindings.IsEmpty() ? vertexBindings.Data() : nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.Size());
    vertexInputInfo.pVertexAttributeDescriptions = !vertexAttributes.IsEmpty() ? vertexAttributes.Data() : nullptr;

    colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.Size());
    colorBlending.pAttachments = !blendAttachmentStates.IsEmpty() ? blendAttachmentStates.Data() : nullptr;

    renderInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentFormats.Size());
    renderInfo.pColorAttachmentFormats = !colorAttachmentFormats.IsEmpty() ? colorAttachmentFormats.Data() : nullptr;

    dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.Size());
    dynamicInfo.pDynamicStates = !dynamicStates.IsEmpty() ? dynamicStates.Data() : nullptr;

    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderInfo,
        .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .stageCount = static_cast<uint32_t>(shaderStages.Size()),
        .pStages = shaderStages.Data(),
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
#ifdef ENABLE_VULKAN_VALIDATION
    if (context->bPipelineExecutablePropertiesEnabled) {
        pipelineInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    }
#endif

    Core::Array<uint32_t, 6> specConstantData{
        VulkanContext::deviceInfo.meshShaderProps.maxTaskWorkGroupCount[0],
        VulkanContext::deviceInfo.meshShaderProps.maxTaskWorkGroupCount[1],
        VulkanContext::deviceInfo.meshShaderProps.maxTaskWorkGroupCount[2],
        VulkanContext::deviceInfo.meshShaderProps.maxMeshWorkGroupCount[0],
        VulkanContext::deviceInfo.meshShaderProps.maxMeshWorkGroupCount[1],
        VulkanContext::deviceInfo.meshShaderProps.maxMeshWorkGroupCount[2],
    };

    Core::Array<VkSpecializationMapEntry, 6> entries{};
    for (uint32_t i = 0; i < 6; i++) {
        entries[i].constantID = i;
        entries[i].offset = i * sizeof(uint32_t);
        entries[i].size = sizeof(uint32_t);
    }

    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 6;
    specInfo.pMapEntries = entries.Data();
    specInfo.dataSize = specConstantData.Size() * sizeof(uint32_t);
    specInfo.pData = specConstantData.Data();
    for (VkPipelineShaderStageCreateInfo& stage : shaderStages) {
        stage.pSpecializationInfo = &specInfo;
    }

    VkResult pipelineResult = vkCreateGraphicsPipelines(context->device, pipelineCache, 1, &pipelineInfo, nullptr, &loadingEntry.pipeline);

    if (pipelineResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create graphics pipeline");
        vkDestroyPipelineLayout(context->device, loadingEntry.layout, nullptr);
        for (uint32_t i = 0; i < shaderStages.Size(); ++i) {
            vkDestroyShaderModule(context->device, shaderModules[i], nullptr);
        }
        return false;
    }

    for (uint32_t i = 0; i < shaderStages.Size(); ++i) {
        vkDestroyShaderModule(context->device, shaderModules[i], nullptr);
    }

    return true;
}

uint64_t GraphicsPipelineData::GetLatestShaderWriteTime() const
{
    uint64_t latest = 0;
    for (uint32_t i = 0; i < shaderPaths.Size(); ++i) {
        uint64_t modTime = Platform::GetFileWriteTime(shaderPaths[i].c_str());
        if (modTime > latest) {
            latest = modTime;
        }
    }
    return latest;
}
} // Render
