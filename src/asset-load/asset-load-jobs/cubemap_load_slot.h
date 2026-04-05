//
// Created by William on 2026-02-16.
//

#ifndef WILL_ENGINE_CUBEMAP_LOAD_SLOT_H
#define WILL_ENGINE_CUBEMAP_LOAD_SLOT_H

#include <semaphore>
#include <volk.h>
#include <ktx.h>

#include <enkiTS/src/TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"
#include "engine/asset_manager_types.h"


namespace Render
{
struct Cubemap;
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class CubemapLoadSlot
{
public:
    CubemapLoadSlot();

    ~CubemapLoadSlot();

    void Initialize(
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        Render::ResourceManager* _resourceManager,
        Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
        Core::InlineFunction<void(bool success, CubemapSlotHandle cubemapSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback);

    void Launch(
        CubemapSlotHandle _cubemapSlotHandle,
        UploadStagingSlotHandle _uploadStagingSlotHandle,
        UploadStaging* _uploadStaging,
        Render::Cubemap* _outputCubemap);

    void Clear();

    bool LoadCubemapFromDisk();

    bool AllocateGPUResources();

    void UploadCubemap(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);

    void PostUploadSetup();

    CubemapSlotHandle cubemapSlotHandle{};
    UploadStagingSlotHandle uploadStagingSlotHandle{};

    Render::Cubemap* outputCubemap{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    struct LoadCubemapTask : enki::ITaskSet
    {
        CubemapLoadSlot* loadSlot{nullptr};
        explicit LoadCubemapTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    LoadCubemapTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    ktxTexture2* texture{nullptr};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestDispatchCallback;
    Core::InlineFunction<void(bool success, CubemapSlotHandle cubemapSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_CUBEMAP_LOAD_SLOT_H
