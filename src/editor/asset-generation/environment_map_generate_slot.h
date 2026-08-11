//
// Created by William on 2026-02-09.
//

#ifndef WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H
#define WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H

#include <semaphore>
#include <TaskScheduler.h>

#include "asset_generation_types.h"
#include "asset-load/asset_load_types.h"
#include "core/containers/array.h"
#include "core/containers/inline_function.h"
#include "core/containers/heap_array.h"
#include "core/containers/inline_path.h"
#include "core/memory/handle.h"
#include "core/memory/linear_allocator.h"
#include "core/memory/memory_manager.h"
#include "engine/core/environment_map_id.h"
#include "engine/resources/environment_map/probe_format.h"
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

    void Launch(EnvironmentMapGenerateSlotHandle _slotHandle, const Core::Path& _imagePath, const Core::Path& _outputPath, Engine::EnvironmentMapID _environmentMapId, uint64_t _contentVersion);

    /** Assembles a prefiltered cubemap asset from 6 captured RGBA16F probe faces (S x S) downsampled to targetResolution, writing a .wprobe at _outputPath. Faces are moved in. */
    void LaunchProbe(EnvironmentMapGenerateSlotHandle _slotHandle, Core::HeapArray<uint16_t>* faces, uint32_t captureSize, uint32_t targetResolution, const Core::Path& _outputPath, Engine::EnvironmentMapID _environmentMapId, uint64_t _probeId, const Engine::ProbeBakeSnapshot& _snapshot, uint64_t _contentVersion);

    void Clear();

    struct GenerateTask : enki::ITaskSet
    {
        EnvironmentMapGenerateSlot* taskSlot = nullptr;

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    Core::Path imagePath;
    Core::Path outputPath;
    Engine::EnvironmentMapID environmentMapId{};
    uint64_t contentVersion{1};

private:
    bool LoadEquirectangularAndGenerate(VkCommandBuffer cmd, const Core::InlineFunction<void()>& startRecording, const Core::InlineFunction<void(bool)>& submitAndWait);

    bool AssembleProbeCubemapAndGenerate(VkCommandBuffer cmd, const Core::InlineFunction<void()>& startRecording, const Core::InlineFunction<void(bool)>& submitAndWait);

    /** Shared tail: consumes a populated float cubemap (SetCubemap index 0, SHADER_READ, full mip chain) into GGX-prefiltered specular mips 0-4 + diffuse irradiance mip 5, then reads back to mipData. Resolution driven by baseResolution. */
    bool BuildFilteredMipsAndCopy(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);

    bool WriteWEnvMapFile();

    bool WriteWProbeFile();

    enki::TaskScheduler* scheduler{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::PipelineManager* pipelineManager{nullptr};
    Render::ResourceManager* resourceManager{nullptr};
    AssetLoad::SubmitContext graphicsSubmit{};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _graphicsDispatchCallback;
    Core::InlineFunction<void(bool success, EnvironmentMapGenerateSlotHandle cubemapSlotHandle)> _notifyCallback;

    EnvironmentMapGenerateSlotHandle slotHandle{EnvironmentMapGenerateSlotHandle::INVALID};


    bool bProbeMode{false};
    uint32_t baseResolution{ENVIRONMENT_MAP_RESOLUTION};
    uint32_t probeCaptureSize{0};
    uint64_t probeId{0};
    Engine::ProbeBakeSnapshot probeSnapshot{};
    Core::HeapArray<uint16_t> probeFaces[6]{};

    Render::AllocatedImage probeSourceImage;
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
    Core::LinearAllocator imageStagingAllocator{ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE, "EnvMapGenStaging"};
    Core::LinearAllocator imageReceivingAllocator{ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE, "EnvMapGenReceiving"};

    GenerateTask task{};
};
} // namespace Editor

#endif //WILL_ENGINE_ENVIRONMENT_MAP_GENERATE_SLOT_H
