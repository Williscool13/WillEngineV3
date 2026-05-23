//
// Created by William on 2026-02-03.
//

#ifndef WILL_ENGINE_TEXTURE_GENERATE_SLOT_H
#define WILL_ENGINE_TEXTURE_GENERATE_SLOT_H

#include <semaphore>
#include <TaskScheduler.h>

#include "asset_generation_types.h"
#include "dds_defs.h"
#include "core/containers/array.h"
#include "core/containers/inline_function.h"
#include "core/containers/inline_path.h"
#include "core/memory/handle.h"
#include "core/memory/linear_allocator.h"
#include "engine/core/texture_id.h"
#include "render/vulkan/vk_resources.h"

namespace Core
{
class MemoryManager;
}

namespace Render
{
struct VulkanContext;
}

namespace Editor
{
constexpr int32_t BC7_UBER_LEVEL = 4;
constexpr float RDO_LAMBDA = 0.5f;

class AssetGenerator;

using TextureGenerateSlotHandle = Core::Handle<struct TextureGenerateSlot>;

struct TextureGenerateSlot
{
    TextureGenerateSlot();

    ~TextureGenerateSlot();

    void Initialize(
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        Core::MemoryManager* _memoryManager,
        AssetGenerator* _assetGenerator,
        Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback,
        Core::InlineFunction<void(bool success, TextureGenerateSlotHandle slotHandle)> notifyCallback
    );

    void Launch(TextureGenerateSlotHandle _slotHandle, const Core::Path& _imagePath, const Core::Path& _outputPath, Engine::TextureID _textureId, bool _mipmapped, DXGI_FORMAT _targetFormat, bool _flipY = true);

    void LaunchFromMemory(TextureGenerateSlotHandle _slotHandle, Core::HeapArray<uint8_t> pixels, uint32_t w, uint32_t h, uint32_t bytesPerPixel, const Core::Path& _outputPath,
                          Engine::TextureID _textureId, bool _mipmapped, DXGI_FORMAT _targetFormat);

    void Clear();

    struct GenerateTask : enki::ITaskSet
    {
        TextureGenerateSlot* taskSlot{nullptr};

        explicit GenerateTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    Core::Path imagePath;
    Core::Path outputPath;
    Engine::TextureID textureId{};

private:
    bool LoadImageAndGenerate(VkCommandBuffer cmd, const Core::InlineFunction<void()>& startRecording, const Core::InlineFunction<void(bool)>& submitAndWait);

    bool WriteWTextureFile();

    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    AssetGenerator* assetGenerator{nullptr};
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _graphicsDispatchCallback;
    Core::InlineFunction<void(bool success, TextureGenerateSlotHandle slotHandle)> _notifyCallback;

    TextureGenerateSlotHandle slotHandle{TextureGenerateSlotHandle::INVALID};
    bool mipmapped = true;
    bool flipY = true;
    DXGI_FORMAT targetFormat = DXGI_FORMAT_BC7_UNORM;

    Core::HeapArray<uint8_t> preloadedPixels;
    uint32_t preloadedWidth{0};
    uint32_t preloadedHeight{0};
    uint32_t preloadedBytesPerPixel{0};

    Render::AllocatedImage sourceImage;
    Core::Array<Core::HeapArray<uint8_t>, 13> mipData;
    uint32_t mipLevels = 1;

    Render::AllocatedBuffer imageStagingBuffer;
    Render::AllocatedBuffer imageReceivingBuffer;
    Core::LinearAllocator imageStagingAllocator{TEXTURE_GENERATION_STAGING_BUFFER_SIZE};
    Core::LinearAllocator imageReceivingAllocator{TEXTURE_GENERATION_STAGING_BUFFER_SIZE};

    GenerateTask task{};
};
} // namespace Editor

#endif //WILL_ENGINE_TEXTURE_GENERATE_SLOT_H
