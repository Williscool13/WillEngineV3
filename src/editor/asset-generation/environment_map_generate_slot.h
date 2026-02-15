//
// Created by William on 2026-02-09.
//

#ifndef WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H
#define WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H

#include <filesystem>
#include <functional>
#include <vector>
#include <semaphore>
#include <TaskScheduler.h>

#include "asset_generation_types.h"
#include "dds_defs.h"
#include "environment_map_generate_resources.h"
#include "core/allocators/handle.h"
#include "core/allocators/linear_allocator.h"
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
        int32_t slotIndex,
        enki::TaskScheduler* _scheduler,
        Render::VulkanContext* _context,
        Render::PipelineManager* _pipelineManager,
        Render::ResourceManager* _resourceManager,
        std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> graphicsDispatchCallback,
        std::function<void(bool success, EnvironmentMapGenerateSlotHandle slotHandle)> notifyCallback
    );

    void Launch(EnvironmentMapGenerateSlotHandle _slotHandle, const std::filesystem::path& _imagePath, const std::filesystem::path& _outputPath);
    void Clear();

    struct GenerateTask : enki::ITaskSet
    {
        EnvironmentMapGenerateSlot* taskSlot = nullptr;
        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    std::filesystem::path imagePath;
    std::filesystem::path outputPath;

private:
    bool LoadEquirectangularAndGenerate(VkCommandBuffer cmd, const std::function<void()>& startRecording, const std::function<void(bool)>& submitAndWait);
    bool WriteKTXFile();

    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::PipelineManager* pipelineManager{nullptr};
    Render::ResourceManager* resourceManager{nullptr};
    std::filesystem::path temporaryPath;
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _graphicsDispatchCallback;
    std::function<void(bool success, EnvironmentMapGenerateSlotHandle slotHandle)> _notifyCallback;

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

    std::vector<std::array<std::vector<uint8_t>, ENVIRONMENT_MAP_MIPS>> mipData; // [mip][face]

    Render::AllocatedBuffer imageStagingBuffer;
    Render::AllocatedBuffer imageReceivingBuffer;
    Core::LinearAllocator imageStagingAllocator{ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE};
    Core::LinearAllocator imageReceivingAllocator{ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE};

    std::unique_ptr<GenerateTask> task;
};

} // namespace Editor

#endif //WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H