//
// Created by William on 2025-12-19.
//

#ifndef WILL_ENGINE_WILL_MODEL_LOADER_H
#define WILL_ENGINE_WILL_MODEL_LOADER_H
#include "asset_load_types.h"
#include "ktx.h"
#include "TaskScheduler.h"
#include "render/vulkan/vk_resource_manager.h"

namespace Render
{
struct VulkanContext;
}

namespace AssetLoad
{
class UploadStaging;
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
        InProgress,
        Complete,
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
    Engine::WillModelHandle willModelHandle{Engine::WillModelHandle::INVALID};
    Render::WillModel* model{nullptr};

    UnpackedWillModel rawData{};
    std::vector<VkSamplerCreateInfo> pendingSamplerInfos;
    std::vector<ktxTexture2*> pendingTextures;
    /**
     * Cached vector to store SkinnedVertex->Vertex for non-skinned models.
     */
    std::vector<Vertex> convertedVertices;
    /**
     * Cached vector to store 3x uint8_t->1x uint32_t for meshlet triangles.
     */
    std::vector<uint32_t> paddedTriangles;
    uint32_t pendingTextureHead{0};
    uint32_t pendingVerticesHead{0};
    uint32_t pendingMeshletVerticesHead{0};
    uint32_t pendingMeshletTrianglesHead{0};
    uint32_t pendingMeshletsHead{0};
    uint32_t pendingPrimitivesHead{0};
    uint32_t pendingBufferBarrier{0};

    // Task Cache
    TaskState taskState{TaskState::NotStarted};

    TaskState TaskExecute(enki::TaskScheduler* scheduler, LoadModelTask* task);

    void TaskImplementation();

    /**
     * Will only be called once, before ThreadExecute. Use to validate what ThreadExecute will do.
     * \n Synchronous
     * @param context
     * @param resourceManager
     * @return true if all validation checks pass and ThreadExecute is safe to begin resource upload
     */
    bool PreThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager);

    /**
     * Resource upload that may take several frames. This will be called repeatedly until the upload has finished (over multiple ticks).
     * \n This call can never fail. It can either be in progress or finished with the resource upload.
     * \n Synchronous, but expected to be called over multiple frames until it returns ThreadState::Finished
     * @param context
     * @param resourceManager
     * @return
     */
    ThreadState ThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager);

    /**
     * Will only be called once, after ThreadExecute has returned ThreadState::Complete
     * \n Synchronous
     * @param context
     * @param resourceManager
     * @return true if the post thread execute succeeded and the model is successfully laoded
     */
    bool PostThreadExecute(Render::VulkanContext* context, Render::ResourceManager* resourceManager);
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_LOADER_H