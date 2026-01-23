//
// Created by William on 2026-01-19.
//

#include "pipeline_load_job.h"

#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "spdlog/spdlog.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
PipelineLoadJob::PipelineLoadJob(Render::VulkanContext* context, Render::ResourceManager* resourceManager, VkPipelineCache pipelineCache)
    : context(context), resourceManager(resourceManager), pipelineCache(pipelineCache)
{
    task = std::make_unique<LoadPipelineTask>();
}

void PipelineLoadJob::StartJob()
{}

TaskState PipelineLoadJob::TaskExecute(enki::TaskScheduler* scheduler)
{
    if (taskState == TaskState::NotStarted) {
        task->loadJob = this;
        taskState = TaskState::InProgress;
        scheduler->AddTaskSetToPipe(task.get());
    }

    if (task->GetIsComplete()) {
        return taskState;
    }

    return TaskState::InProgress;
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
    taskState = TaskState::NotStarted;
    outputDate = nullptr;
}

void PipelineLoadJob::LoadPipelineTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum)
{
    ZoneScopedN("LoadPipelineTask");
    if (!loadJob || !loadJob->outputDate) {
        if (loadJob) {
            loadJob->taskState = TaskState::Failed;
        }
        return;
    }

    auto* outputEntry = loadJob->outputDate;
    assert(outputEntry);

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (!Render::VkHelpers::LoadShaderModule(outputEntry->shaderPath, loadJob->context->device, &shaderModule)) {
        SPDLOG_ERROR("Failed to load shader: {}", outputEntry->shaderPath.string());
        loadJob->taskState = TaskState::Failed;
        return;
    }

    VkResult layoutResult = vkCreatePipelineLayout(loadJob->context->device, &outputEntry->layoutCreateInfo, nullptr, &outputEntry->loadingEntry.layout);
    if (layoutResult != VK_SUCCESS) {
        SPDLOG_ERROR("Failed to create pipeline layout for: {}", outputEntry->shaderPath.string());
        vkDestroyShaderModule(loadJob->context->device, shaderModule, nullptr);
        loadJob->taskState = TaskState::Failed;
        return;
    }

    if (outputEntry->bIsCompute) {
        VkPipelineShaderStageCreateInfo shaderStage = Render::VkHelpers::PipelineShaderStageCreateInfo(shaderModule, VK_SHADER_STAGE_COMPUTE_BIT);
        VkComputePipelineCreateInfo pipelineInfo = Render::VkHelpers::ComputePipelineCreateInfo(outputEntry->loadingEntry.layout, shaderStage);
        VkResult pipelineResult = vkCreateComputePipelines(loadJob->context->device, loadJob->pipelineCache, 1, &pipelineInfo, nullptr, &outputEntry->loadingEntry.pipeline);

        if (pipelineResult != VK_SUCCESS) {
            SPDLOG_ERROR("Failed to create compute pipeline: {}", outputEntry->shaderPath.string());
            vkDestroyPipelineLayout(loadJob->context->device, outputEntry->loadingEntry.layout, nullptr);
            vkDestroyShaderModule(loadJob->context->device, shaderModule, nullptr);
            loadJob->taskState = TaskState::Failed;
            return;
        }
    }
    else {
        // TODO: Graphics pipeline support
        SPDLOG_ERROR("Graphics pipeline creation not yet implemented");
        vkDestroyPipelineLayout(loadJob->context->device, outputEntry->loadingEntry.layout, nullptr);
        vkDestroyShaderModule(loadJob->context->device, shaderModule, nullptr);
        loadJob->taskState = TaskState::Failed;
        return;
    }

    outputEntry->lastModified = std::filesystem::last_write_time(outputEntry->shaderPath);
    outputEntry->retirementFrame = 0;
    vkDestroyShaderModule(loadJob->context->device, shaderModule, nullptr);

    loadJob->taskState = TaskState::Complete;
}

} // AssetLoad
