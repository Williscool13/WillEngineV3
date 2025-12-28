//
// Created by William on 2025-12-28.
//

#include "compute_pipeline.h"

#include <filesystem>

#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace Render
{
ComputePipeline::ComputePipeline() = default;

ComputePipeline::~ComputePipeline() = default;

ComputePipeline::ComputePipeline(VulkanContext* context, VkPipelineLayoutCreateInfo layoutCreateInfo, std::filesystem::path shaderSource) : context(context)
{
    pipelineLayout = PipelineLayout::CreatePipelineLayout(context, layoutCreateInfo);
    pipelineLayout.SetDebugName("Basic Compute Pipeline Layout");

    VkShaderModule computeShader;
    if (!VkHelpers::LoadShaderModule(shaderSource, context->device, &computeShader)) {
        SPDLOG_ERROR("Failed to load basicCompute_comp.spv");
        return;
    }

    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = VkHelpers::PipelineShaderStageCreateInfo(computeShader, VK_SHADER_STAGE_COMPUTE_BIT);
    VkComputePipelineCreateInfo computePipelineCreateInfo = VkHelpers::ComputePipelineCreateInfo(pipelineLayout.handle, shaderStageCreateInfo);
    pipeline = Pipeline::CreateComputePipeline(context, computePipelineCreateInfo);
    pipeline.SetDebugName(shaderSource.filename().string().c_str());

    vkDestroyShaderModule(context->device, computeShader, nullptr);
}

ComputePipeline::ComputePipeline(ComputePipeline&& other) noexcept
{
    pipelineLayout = std::move(other.pipelineLayout);
    pipeline = std::move(other.pipeline);
    context = other.context;
    other.context = nullptr;
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept
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