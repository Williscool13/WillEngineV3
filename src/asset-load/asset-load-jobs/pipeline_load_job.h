//
// Created by William on 2026-01-19.
//

#ifndef WILL_ENGINE_PIPELINE_LOAD_JOB_H
#define WILL_ENGINE_PIPELINE_LOAD_JOB_H

#include <filesystem>
#include <volk.h>

#include "asset_load_job.h"

namespace Render
{
struct PipelineData;
class VulkanContext;
class ResourceManager;
}

namespace AssetLoad
{
class PipelineLoadJob : public AssetLoadJob
{
public:
    PipelineLoadJob(Render::VulkanContext* context, Render::ResourceManager* resourceManager, VkPipelineCache pipelineCache);
    ~PipelineLoadJob() override = default;

    void StartJob() override;
    TaskState TaskExecute(enki::TaskScheduler* scheduler) override;
    bool PreThreadExecute() override;
    ThreadState ThreadExecute() override;
    bool PostThreadExecute() override;
    void Reset() override;
    uint32_t GetUploadCount() override { return 0; }

    Render::PipelineData* outputDate{nullptr};

private:
    struct LoadPipelineTask : enki::ITaskSet
    {
        PipelineLoadJob* loadJob{nullptr};

        explicit LoadPipelineTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
    };

    // Task
    TaskState taskState{TaskState::NotStarted};
    std::unique_ptr<LoadPipelineTask> task;


    Render::VulkanContext* context;
    Render::ResourceManager* resourceManager;
    VkPipelineCache pipelineCache{VK_NULL_HANDLE};
};
} // AssetLoad

#endif //WILL_ENGINE_PIPELINE_LOAD_JOB_H