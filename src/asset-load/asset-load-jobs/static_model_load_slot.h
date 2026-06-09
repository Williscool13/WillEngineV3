//
// Created by William on 2025-12-23.
//

#ifndef WILL_ENGINE_STATIC_MODEL_LOAD_JOB_H
#define WILL_ENGINE_STATIC_MODEL_LOAD_JOB_H
#include <semaphore>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"
#include "core/containers/vector.h"
#include "core/memory/tlsf_allocator.h"
#include "render/vulkan/vk_resources.h"

namespace Core
{
class MemoryManager;
}

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
                    Core::MemoryManager* _memoryManager,
                    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _requestTransferDispatchCallback,
                    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _requestGraphicsDispatchCallback,
                    Core::InlineFunction<void(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback);

    void Launch(ModelSlotHandle _modelSlotHandle, UploadStagingSlotHandle _uploadStagingSlotHandle, UploadStaging* _uploadStaging, Engine::StaticModel* _outputModel);

    void Clear();

    bool LoadModelFromDisk();

    bool AllocateGPUResources();

    void PrepareUploadData();

    void UploadGeometry(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);
    void BuildBLAS(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);

    ModelSlotHandle modelSlotHandle{};
    UploadStagingSlotHandle uploadStagingSlotHandle{};

    Engine::StaticModel* outputModel{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    struct LoadModelTask : enki::ITaskSet
    {
        StaticModelLoadSlot* loadSlot{nullptr};

        explicit LoadModelTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    LoadModelTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    UnpackedStaticModel rawData{};
    BLASTransients blasTransients{};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestTransferDispatchCallback;
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestGraphicsDispatchCallback;
    Core::InlineFunction<void(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback;

};
} // AssetLoad

#endif //WILL_ENGINE_STATIC_MODEL_LOAD_JOB_H
