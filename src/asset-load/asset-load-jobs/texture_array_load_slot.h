//
// Created by William on 2026-10-08.
//

#ifndef WILL_ENGINE_TEXTURE_ARRAY_LOAD_SLOT_H
#define WILL_ENGINE_TEXTURE_ARRAY_LOAD_SLOT_H

#include <semaphore>
#include <volk.h>

#include <enkiTS/src/TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"
#include "engine/resources/wimage_format.h"

namespace Core
{
class MemoryManager;
}

namespace Render
{
struct TextureArray;
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class TextureArrayLoadSlot
{
public:
    TextureArrayLoadSlot();

    ~TextureArrayLoadSlot();

    void Initialize(
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        Render::ResourceManager* _resourceManager,
        Core::MemoryManager* _memoryManager,
        Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
        Core::InlineFunction<void(bool success, TextureArraySlotHandle slotHandle)> notifyCallback);

    void Launch(TextureArraySlotHandle _slotHandle, UploadStaging* _uploadStaging, Render::TextureArray* _outputTextureArray);

    void Clear();

    TextureArraySlotHandle slotHandle{};

    Render::TextureArray* outputTextureArray{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    bool LoadFromDisk();

    void AllocateGPUResources();

    struct LoadTextureArrayTask : enki::ITaskSet
    {
        TextureArrayLoadSlot* loadSlot{nullptr};
        explicit LoadTextureArrayTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    LoadTextureArrayTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    SubmitContext transferSubmit{};

    Core::HeapArray<uint8_t> blobData{};
    Engine::WImageView blobView{};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestDispatchCallback;
    Core::InlineFunction<void(bool success, TextureArraySlotHandle slotHandle)> _notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_TEXTURE_ARRAY_LOAD_SLOT_H
