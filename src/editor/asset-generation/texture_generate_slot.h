//
// Created by William on 2026-02-03.
//

#ifndef WILL_ENGINE_TEXTURE_GENERATE_SLOT_H
#define WILL_ENGINE_TEXTURE_GENERATE_SLOT_H

#include <filesystem>
#include <functional>
#include <vector>

#include <semaphore>
#include <TaskScheduler.h>

#include "asset_generation_types.h"
#include "dds_defs.h"
#include "core/allocators/handle.h"
#include "core/allocators/linear_allocator.h"
#include "engine/core/texture_id.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;
}

namespace Editor
{
using TextureGenerateSlotHandle = Core::Handle<struct TextureGenerateSlot>;

struct TextureGenerateSlot
{
    TextureGenerateSlot();
    ~TextureGenerateSlot();

    void Initialize(
        int32_t slotIndex,
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback,
        std::function<void(bool success, TextureGenerateSlotHandle slotHandle)> notifyCallback
    );

    void Launch(TextureGenerateSlotHandle _slotHandle, const std::filesystem::path& _imagePath, const std::filesystem::path& _outputPath, Engine::TextureID _textureId, bool _mipmapped, DXGI_FORMAT _targetFormat);
    void Clear();

    struct GenerateTask : enki::ITaskSet
    {
        TextureGenerateSlot* taskSlot = nullptr;
        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    std::filesystem::path imagePath;
    std::filesystem::path outputPath;
    Engine::TextureID textureId{};

private:
    bool LoadImageAndGenerate(VkCommandBuffer cmd, const std::function<void()>& startRecording, const std::function<void(bool)>& submitAndWait);
    bool WriteWTextureFile();

    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    std::filesystem::path temporaryPath;
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _graphicsDispatchCallback;
    std::function<void(bool success, TextureGenerateSlotHandle slotHandle)> _notifyCallback;

    TextureGenerateSlotHandle slotHandle{TextureGenerateSlotHandle::INVALID};
    bool mipmapped = true;
    DXGI_FORMAT targetFormat = DXGI_FORMAT_BC7_UNORM;

    Render::AllocatedImage sourceImage;
    std::vector<std::vector<uint8_t>> mipData;
    uint32_t mipLevels = 1;

    Render::AllocatedBuffer imageStagingBuffer;
    Render::AllocatedBuffer imageReceivingBuffer;
    Core::LinearAllocator imageStagingAllocator{TEXTURE_GENERATION_STAGING_BUFFER_SIZE};
    Core::LinearAllocator imageReceivingAllocator{TEXTURE_GENERATION_STAGING_BUFFER_SIZE};

    std::unique_ptr<GenerateTask> task;
};

} // namespace Editor

#endif //WILL_ENGINE_TEXTURE_GENERATE_SLOT_H