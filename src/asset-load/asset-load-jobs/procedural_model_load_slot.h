//
// Created by William on 2026-03-14.
//

#ifndef WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
#define WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
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
class ProceduralModelLoadSlot
{
public:
    ProceduralModelLoadSlot();

    ~ProceduralModelLoadSlot();

    void Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, Render::ResourceManager* _resourceManager,
                    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _requestDispatchCallback,
                    std::function<void(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback);

    void Launch(ProceduralModelSlotHandle _slotHandle, UploadStagingSlotHandle _uploadStagingSlotHandle, UploadStaging* _uploadStaging, Engine::StaticModel* _outputModel);

    void Clear();

    bool GenerateGeometry();

    bool AllocateGPUResources() const;


    void PrepareUploadData();

    void UploadGeometry(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait);

    ProceduralModelSlotHandle slotHandle{};
    UploadStagingSlotHandle uploadStagingSlotHandle{};

    Engine::StaticModel* outputModel{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    struct GenerateModelTask : enki::ITaskSet
    {
        ProceduralModelLoadSlot* loadSlot{nullptr};

        explicit GenerateModelTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    std::unique_ptr<GenerateModelTask> task{nullptr};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    RawStaticModel rawData{};
    std::vector<uint32_t> packedTriangles;

    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestDispatchCallback;
    std::function<void(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback;

    bool GenerateStaircase(const Engine::StaircaseParams& p);
};
} // AssetLoad

#endif //WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
