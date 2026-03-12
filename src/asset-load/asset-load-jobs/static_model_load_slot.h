//
// Created by William on 2025-12-23.
//

#ifndef WILL_ENGINE_STATIC_MODEL_LOAD_JOB_H
#define WILL_ENGINE_STATIC_MODEL_LOAD_JOB_H
#include <semaphore>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"

namespace enki
{
class TaskScheduler;
}

namespace Render
{
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class StaticModelLoadSlot
{
public:
    StaticModelLoadSlot();

    ~StaticModelLoadSlot();

    void Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, Render::ResourceManager* _resourceManager,
                    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _requestDispatchCallback,
                    std::function<void(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback);

    void Launch(ModelSlotHandle _modelSlotHandle, UploadStagingSlotHandle _uploadStagingSlotHandle, UploadStaging* _uploadStaging, Engine::StaticModel* _outputModel);

    void Clear();

    bool LoadModelFromDisk();

    bool AllocateGPUResources() const;

    void PrepareUploadData();

    void UploadGeometry(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait);

    ModelSlotHandle modelSlotHandle{};
    UploadStagingSlotHandle uploadStagingSlotHandle{};

    Engine::StaticModel* outputModel{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    struct LoadModelTask : enki::ITaskSet
    {
        StaticModelLoadSlot* loadSlot{nullptr};

        explicit LoadModelTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    std::unique_ptr<LoadModelTask> task{nullptr};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    RawStaticModel rawData{};
    /**
     * Cached vector to store 3x uint8_t->1x uint32_t for meshlet triangles.
     */
    std::vector<uint32_t> packedTriangles;

    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestDispatchCallback;
    std::function<void(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_STATIC_MODEL_LOAD_JOB_H
