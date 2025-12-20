//
// Created by William on 2025-12-19.
//

#ifndef WILL_ENGINE_WILL_MODEL_LOADER_H
#define WILL_ENGINE_WILL_MODEL_LOADER_H
#include "asset_load_types.h"
#include "TaskScheduler.h"
#include "render/vulkan/vk_resource_manager.h"

namespace Render
{
struct VulkanContext;
}

namespace AssetLoad
{
struct UploadStaging;
struct WillModelLoader;

struct LoadModelTask : enki::ITaskSet
{
    WillModelLoader* modelLoader{nullptr};

    explicit LoadModelTask()
        : ITaskSet(1)
    {}

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
};

enum class WillModelLoadState
{
    Idle,
    TaskExecuting,
    ThreadExecuting,
    Loaded,
    Failed
};

struct WillModelLoader
{
    enum class TaskState
    {
        NotStarted,
        InProgress,
        Complete,
        Failed,
    };

    enum class ThreadState
    {
        NotStarted,
        InProgress,
        Complete,
        Failed,
    };

    WillModelLoader();

    ~WillModelLoader();

    WillModelLoader(const WillModelLoader&) = delete;

    WillModelLoader& operator=(const WillModelLoader&) = delete;

    WillModelLoader(WillModelLoader&&) noexcept = default;

    WillModelLoader& operator=(WillModelLoader&&) noexcept = default;

    void Reset();

    std::unique_ptr<LoadModelTask> loadModelTask;
    std::unique_ptr<UploadStaging> uploadStaging;

    // Transient
    WillModelLoadState loadState{WillModelLoadState::Idle};
    Render::WillModelHandle willModelHandle{Render::WillModelHandle::INVALID};
    Render::WillModel* model{nullptr};

    UnpackedWillModel rawData{};
    std::vector<VkSamplerCreateInfo> pendingSamplerInfos;
    std::vector<ktxTexture2*> pendingTextures;
    uint32_t pendingTextureHead{0};
    std::vector<uint32_t> pendingTextureIndices;

    // Task Cache
    TaskState taskState{TaskState::NotStarted};

    // Thread Cache
    ThreadState threadState{ThreadState::NotStarted};

    TaskState TaskExecute(enki::TaskScheduler* scheduler, LoadModelTask* task);

    void TaskImplementation();

    ThreadState ThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager);

    /**
     * Will only be called once, after ThreadExecute has returned ThreadState::Complete
     * @param context
     * @param resourceManager
     * @return true if the post thread execute succeeded and the model is successfully laoded
     */
    bool PostThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager);
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_LOADER_H