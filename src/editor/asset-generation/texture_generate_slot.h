//
// Created by William on 2026-02-03.
//

#ifndef WILL_ENGINE_TEXTURE_GENERATE_SLOT_H
#define WILL_ENGINE_TEXTURE_GENERATE_SLOT_H

#include <functional>
#include <vector>

#include <semaphore>
#include <TaskScheduler.h>

#include "asset_generation_types.h"
#include "dds_defs.h"
#include "core/containers/inline_path.h"
#include "core/memory/handle.h"
#include "core/memory/linear_allocator.h"
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

    void Launch(TextureGenerateSlotHandle _slotHandle, const Core::Path& _imagePath, const Core::Path& _outputPath, Engine::TextureID _textureId, bool _mipmapped, DXGI_FORMAT _targetFormat);
    void LaunchFromMemory(TextureGenerateSlotHandle _slotHandle, Core::HeapArray<uint8_t> pixels, uint32_t w, uint32_t h, uint32_t bytesPerPixel, const Core::Path& _outputPath, Engine::TextureID _textureId, bool _mipmapped, DXGI_FORMAT _targetFormat);
    void Clear();

    struct GenerateTask : enki::ITaskSet
    {
        TextureGenerateSlot* taskSlot = nullptr;
        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    Core::Path imagePath;
    Core::Path outputPath;
    Engine::TextureID textureId{};

private:
    bool LoadImageAndGenerate(VkCommandBuffer cmd, const std::function<void()>& startRecording, const std::function<void(bool)>& submitAndWait);
    bool WriteWTextureFile();

    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _graphicsDispatchCallback;
    std::function<void(bool success, TextureGenerateSlotHandle slotHandle)> _notifyCallback;

    TextureGenerateSlotHandle slotHandle{TextureGenerateSlotHandle::INVALID};
    bool mipmapped = true;
    DXGI_FORMAT targetFormat = DXGI_FORMAT_BC7_UNORM;

    Core::HeapArray<uint8_t> preloadedPixels;
    uint32_t preloadedWidth{0};
    uint32_t preloadedHeight{0};
    uint32_t preloadedBytesPerPixel{0};

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