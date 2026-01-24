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

    //
    {
        ZoneScopedN("CreatePipeline");
        bool res = outputEntry->CreatePipeline(loadJob->context, loadJob->pipelineCache);
        loadJob->taskState = res ? TaskState::Complete : TaskState::Failed;
    }
}
} // AssetLoad
