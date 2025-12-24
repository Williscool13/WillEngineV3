//
// Created by William on 2025-12-13.
//

#include "basic_compute_pipeline.h"

#include "vk_pipeline.h"
#include "platform/paths.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
BasicComputePipeline::BasicComputePipeline() = default;

BasicComputePipeline::~BasicComputePipeline() = default;

BasicComputePipeline::BasicComputePipeline(VulkanContext* context, DescriptorSetLayout& renderTargetSetLayout) : context(context)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BasicComputePushConstant);
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo piplineLayoutCreateInfo{};
    piplineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    piplineLayoutCreateInfo.pSetLayouts = &renderTargetSetLayout.handle;
    piplineLayoutCreateInfo.setLayoutCount = 1;
    piplineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    piplineLayoutCreateInfo.pushConstantRangeCount = 1;

    pipelineLayout = PipelineLayout::CreatePipelineLayout(context, piplineLayoutCreateInfo);
    pipelineLayout.SetDebugName("Basic Compute Pipeline Layout");

    VkShaderModule computeShader;
    if (!VkHelpers::LoadShaderModule(Platform::GetShaderPath() / "basicCompute_compute.spv", context->device, &computeShader)) {
        SPDLOG_ERROR("Failed to load basicCompute_comp.spv");
        return;
    }

    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = VkHelpers::PipelineShaderStageCreateInfo(computeShader, VK_SHADER_STAGE_COMPUTE_BIT);
    VkComputePipelineCreateInfo computePipelineCreateInfo = VkHelpers::ComputePipelineCreateInfo(pipelineLayout.handle, shaderStageCreateInfo);
    pipeline = Pipeline::CreateComputePipeline(context, computePipelineCreateInfo);
    pipeline.SetDebugName("Basic Compute Pipeline");

    vkDestroyShaderModule(context->device, computeShader, nullptr);
}

BasicComputePipeline::BasicComputePipeline(BasicComputePipeline&& other) noexcept
{
    pipelineLayout = std::move(other.pipelineLayout);
    pipeline = std::move(other.pipeline);
    context = other.context;
    other.context = nullptr;
}

BasicComputePipeline& BasicComputePipeline::operator=(BasicComputePipeline&& other) noexcept
{
    if (this != &other) {
        pipelineLayout = std::move(other.pipelineLayout);
        pipeline = std::move(other.pipeline);
        context = other.context;
        other.context = nullptr;
    }
    return *this;
}
} // Render