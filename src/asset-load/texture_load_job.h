//
// Created by William on 2025-12-23.
//

#ifndef WILL_ENGINE_TEXTURE_LOAD_JOB_H
#define WILL_ENGINE_TEXTURE_LOAD_JOB_H

#include <volk.h>
#include <ktx.h>

#include "asset_load_job.h"
#include "engine/asset_manager_types.h"

namespace Render
{
class Texture;
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class UploadStaging;

class TextureLoadJob : public AssetLoadJob
{
public:
    TextureLoadJob();
    TextureLoadJob(Render::VulkanContext* context, Render::ResourceManager* resourceManager, VkCommandBuffer commandBuffer);
    ~TextureLoadJob() override;

    TaskState TaskExecute(enki::TaskScheduler* scheduler) override;
    bool PreThreadExecute() override;
    ThreadState ThreadExecute() override;
    bool PostThreadExecute() override;
    void Reset() override;

    Engine::TextureHandle textureHandle{Engine::TextureHandle::INVALID};
    Render::Texture* outputTexture{nullptr};

private:
    struct LoadTextureTask : enki::ITaskSet
    {
        TextureLoadJob* loadJob{nullptr};

        explicit LoadTextureTask() : ITaskSet(1) {}
        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
    };

    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    // Task
    TaskState taskState{TaskState::NotStarted};
    std::unique_ptr<LoadTextureTask> task;
    ktxTexture2* texture{nullptr};

    // Thread
    std::unique_ptr<UploadStaging> uploadStaging;
    int32_t currentMip{0};
    bool bPendingInitialBarrier{true};
    bool bPendingFinalBarrier{true};
};
} // AssetLoad

#endif //WILL_ENGINE_TEXTURE_LOAD_JOB_H