//
// Created by William on 2026-01-19.
//

#include "pipeline_load_job.h"

#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"

namespace AssetLoad
{
PipelineLoadJob::PipelineLoadJob(Render::VulkanContext* context, Render::ResourceManager* resourceManager)
    : context(context), resourceManager(resourceManager)
{}

void PipelineLoadJob::StartJob()
{}

TaskState PipelineLoadJob::TaskExecute(enki::TaskScheduler* scheduler)
{
    if (!outputEntry) {
        SPDLOG_ERROR("PipelineLoadJob output entry is null");
        return TaskState::Failed;
    }

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (!Render::VkHelpers::LoadShaderModule(outputEntry->shaderPath, context->device, &shaderModule)) {
        SPDLOG_ERROR("Failed to load shader: {}", outputEntry->shaderPath.string());
        return TaskState::Failed;
    }

    VkResult layoutResult = vkCreatePipelineLayout(context->device, &outputEntry->layoutCreateInfo, nullptr, &outputEntry->layout);
    if (layoutResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create pipeline layout for: {}", outputEntry->shaderPath.string());
        vkDestroyShaderModule(context->device, shaderModule, nullptr);
        return TaskState::Failed;
    }

    if (outputEntry->bIsCompute) {
        VkPipelineShaderStageCreateInfo shaderStage = Render::VkHelpers::PipelineShaderStageCreateInfo(shaderModule, VK_SHADER_STAGE_COMPUTE_BIT);

        VkComputePipelineCreateInfo pipelineInfo = Render::VkHelpers::ComputePipelineCreateInfo(outputEntry->layout, shaderStage);

        VkResult pipelineResult = vkCreateComputePipelines(context->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outputEntry->pipeline);

        if (pipelineResult != VK_SUCCESS) {
            SPDLOG_ERROR("Failed to create compute pipeline: {}", outputEntry->shaderPath.string());
            vkDestroyPipelineLayout(context->device, outputEntry->layout, nullptr);
            vkDestroyShaderModule(context->device, shaderModule, nullptr);
            return TaskState::Failed;
        }
    }
    else {
        // TODO: Graphics pipeline support
        SPDLOG_ERROR("Graphics pipeline creation not yet implemented");
        vkDestroyPipelineLayout(context->device, outputEntry->layout, nullptr);
        vkDestroyShaderModule(context->device, shaderModule, nullptr);
        return TaskState::Failed;
    }

    outputEntry->lastModified = std::filesystem::last_write_time(outputEntry->shaderPath);
    outputEntry->retirementFrame = 0;
    vkDestroyShaderModule(context->device, shaderModule, nullptr);

    return TaskState::Complete;
}

bool PipelineLoadJob::PreThreadExecute()
{
    return true;
}

ThreadState PipelineLoadJob::ThreadExecute()
{
    return ThreadState::Complete;
}

bool PipelineLoadJob::PostThreadExecute()
{
    return true;
}

void PipelineLoadJob::Reset()
{
    outputEntry = nullptr;
}
} // AssetLoad
