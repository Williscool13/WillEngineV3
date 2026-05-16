//
// Created by William on 2025-12-23.
//

#ifndef WILL_ENGINE_TEXTURE_LOAD_JOB_H
#define WILL_ENGINE_TEXTURE_LOAD_JOB_H

#include <semaphore>
#include <volk.h>
#include <ktx.h>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"
#include "engine/asset_manager_types.h"

namespace Core
{
class MemoryManager;
}

namespace enki
{
class TaskScheduler;
}

namespace Engine
{
struct Texture;
}

namespace Render
{
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
        Core::MemoryManager* _memoryManager,
        Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
        Core::InlineFunction<void(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback);

    void Launch(
        TextureSlotHandle _textureSlotHandle,
        UploadStagingSlotHandle _uploadStagingSlotHandle,
        UploadStaging* _uploadStaging,
        Engine::Texture* _outputTexture);

    void Clear();

    bool LoadTextureFromDisk();

    struct AllocatedTextureResources
    {
        bool bSuccess{false};
        Render::AllocatedImage image{};
        Render::ImageView imageView{};
    };

    AllocatedTextureResources AllocateGPUResources() const;

    void UploadTexture(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);

    void PostUploadSetup();

    TextureSlotHandle textureSlotHandle{};
    UploadStagingSlotHandle uploadStagingSlotHandle{};

    Engine::Texture* outputTexture{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    struct LoadTextureTask : enki::ITaskSet
    {
        TextureLoadSlot* loadSlot{nullptr};

        explicit LoadTextureTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    LoadTextureTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};
    Core::MemoryManager* memoryManager{nullptr};

    ktxTexture2* texture{nullptr};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestDispatchCallback;
    Core::InlineFunction<void(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_TEXTURE_LOAD_JOB_H
