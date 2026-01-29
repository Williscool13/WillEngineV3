//
// Created by William on 2025-12-23.
//

#ifndef WILL_ENGINE_TEXTURE_LOAD_JOB_H
#define WILL_ENGINE_TEXTURE_LOAD_JOB_H

#include <semaphore>
#include <volk.h>
#include <ktx.h>

#include "asset-load/asset_load_types.h"
#include "engine/asset_manager_types.h"

namespace enki
{
class TaskScheduler;
}

namespace Render
{
struct Texture;
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class TextureLoadSlot
{
public:
    TextureLoadSlot();

    ~TextureLoadSlot();

    void Initialize(
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        Render::ResourceManager* _resourceManager,
        std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
        std::function<void(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback);

    void Launch(
        TextureSlotHandle _textureSlotHandle,
        UploadStagingSlotHandle _uploadStagingSlotHandle,
        UploadStaging* _uploadStaging,
        Render::Texture* _outputTexture);

    void Clear();

    bool LoadTextureFromDisk();

    bool AllocateGPUResources();

    void UploadTexture(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait);

    void PostUploadSetup();

    TextureSlotHandle textureSlotHandle{};
    UploadStagingSlotHandle uploadStagingSlotHandle{};

    Render::Texture* outputTexture{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    struct LoadTextureTask : enki::ITaskSet
    {
        TextureLoadSlot* loadSlot{nullptr};

        explicit LoadTextureTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    std::unique_ptr<LoadTextureTask> task{nullptr};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    ktxTexture2* texture{nullptr};

    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestDispatchCallback;
    std::function<void(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_TEXTURE_LOAD_JOB_H