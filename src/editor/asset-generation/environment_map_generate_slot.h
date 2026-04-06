//
// Created by William on 2026-02-09.
//

#ifndef WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H
#define WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H

#include <semaphore>
#include <TaskScheduler.h>

#include "asset_generation_types.h"
#include "core/containers/array.h"
#include "core/containers/inline_function.h"
#include "core/containers/heap_array.h"
#include "core/containers/inline_path.h"
#include "core/memory/handle.h"
#include "core/memory/linear_allocator.h"
#include "core/memory/memory_manager.h"
#include "render/shaders/constants_interop.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct ResourceManager;
class PipelineManager;
struct VulkanContext;
}

namespace Editor
{
using EnvironmentMapGenerateSlotHandle = Core::Handle<struct EnvironmentMapGenerateSlot>;

struct EnvironmentMapGenerateSlot
{
    EnvironmentMapGenerateSlot();

    ~EnvironmentMapGenerateSlot();

    void Initialize(
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        Render::PipelineManager* _pipelineManager,
        Render::ResourceManager* _resourceManager,
        Core::MemoryManager* _memoryManager,
        Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback,
        Core::InlineFunction<void(bool success, EnvironmentMapGenerateSlotHandle cubemapSlotHandle)> notifyCallback
    );

    void Launch(EnvironmentMapGenerateSlotHandle _slotHandle, const Core::Path& _imagePath, const Core::Path& _outputPath);

    void Clear();

    struct GenerateTask : enki::ITaskSet
    {
        EnvironmentMapGenerateSlot* taskSlot = nullptr;

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    Core::Path imagePath;
    Core::Path outputPath;

private:
    bool LoadEquirectangularAndGenerate(VkCommandBuffer cmd, const Core::InlineFunction<void()>& startRecording, const Core::InlineFunction<void(bool)>& submitAndWait);

    bool WriteKTXFile();

    enki::TaskScheduler* scheduler{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::PipelineManager* pipelineManager{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _graphicsDispatchCallback;
    Core::InlineFunction<void(bool success, EnvironmentMapGenerateSlotHandle cubemapSlotHandle)> _notifyCallback;

    EnvironmentMapGenerateSlotHandle slotHandle{EnvironmentMapGenerateSlotHandle::INVALID};


    Render::AllocatedImage equiImage;
    Render::ImageView equiImageView;
    Render::AllocatedImage mipmappedCubemapImage;
    Render::ImageView mipmappedCubemapImageView;
    Render::AllocatedImage finalCubemapImage;
    Render::ImageView finalCubemapMipViews[ENVIRONMENT_MAP_MIPS];

    static constexpr uint32_t EQUI_IMAGE_SAMPLER_INDEX = 0;
    static constexpr uint32_t CUBEMAP_IMAGE_SAMPLER_INDEX = 1;
    Render::Sampler equiSampler;
    Render::Sampler cubemapSampler;


    // Array of mips, containing an array of faces
    Core::Array<Core::Array<Core::HeapArray<uint8_t>, 13>, 6> mipData; // [mip][face]

    Render::AllocatedBuffer imageStagingBuffer;
    Render::AllocatedBuffer imageReceivingBuffer;
    Core::LinearAllocator imageStagingAllocator{ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE};
    Core::LinearAllocator imageReceivingAllocator{ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE};

    GenerateTask task{};
};
} // namespace Editor

#endif //WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H
