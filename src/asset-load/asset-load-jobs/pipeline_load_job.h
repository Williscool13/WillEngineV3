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
struct PipelineEntry;
class VulkanContext;
class ResourceManager;
}

namespace AssetLoad
{
class PipelineLoadJob : public AssetLoadJob
{
public:
    PipelineLoadJob(Render::VulkanContext* context, Render::ResourceManager* resourceManager);
    ~PipelineLoadJob() override = default;

    void StartJob() override;
    TaskState TaskExecute(enki::TaskScheduler* scheduler) override;
    bool PreThreadExecute() override;
    ThreadState ThreadExecute() override;
    bool PostThreadExecute() override;
    void Reset() override;
    uint32_t GetUploadCount() override { return 0; }

    Render::PipelineEntry* outputEntry{nullptr};

private:
    Render::VulkanContext* context;
    Render::ResourceManager* resourceManager;
};
} // AssetLoad

#endif //WILL_ENGINE_PIPELINE_LOAD_JOB_H