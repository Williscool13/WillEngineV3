//
// Created by William on 2026-01-19.
//

#ifndef WILL_ENGINE_PIPELINE_MANAGER_H
#define WILL_ENGINE_PIPELINE_MANAGER_H


#include <chrono>
#include <string>
#include <volk.h>

#include "pipeline_category.h"
#include "pipeline_data.h"
#include "graphics_pipeline_builder.h"
#include "core/containers/array.h"
#include "core/containers/inline_path.h"
#include "core/containers/map.h"
#include "core/containers/span.h"
#include "core/string_id.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_resources.h"

namespace AssetLoad
{
class AsyncAssetLoadManager;
}

namespace Render
{
struct ResourceManager;
struct VulkanContext;

class PipelineManager
{
public: // Thread-Safe
    void RequestReload() { bReloadRequested.store(true, std::memory_order_relaxed); }

public:
    explicit PipelineManager(VulkanContext* context,
                             ResourceManager* resourceManager,
                             Core::TlsfAllocator& renderAlloc,
                             Core::TlsfAllocator& assetScratchAlloc,
                             const Core::Array<VkDescriptorSetLayout, 3>& globalLayouts);

    ~PipelineManager();

    PipelineManager(const PipelineManager&) = delete;

    PipelineManager& operator=(const PipelineManager&) = delete;

    void RegisterComputePipeline(StringID pipelineId, Core::Path shaderPath, const char* entryPoint, uint32_t pushConstantSize, PipelineCategory category);

    void RegisterComputePipelineCustomLayout(StringID pipelineId, Core::Path shaderPath, const char* entryPoint, uint32_t pushConstantSize, PipelineCategory category,
                                             Core::Span<const VkDescriptorSetLayout> customLayouts);

    void RegisterGraphicsPipeline(StringID pipelineId, GraphicsPipelineBuilder& builder, uint32_t pushConstantSize, VkShaderStageFlags pushConstantStages, PipelineCategory category);

    const PipelineEntry* GetPipelineEntry(StringID pipelineId);

    void ReloadModified();

    void Update(uint32_t frameNumber);

    bool IsCategoryReady(PipelineCategory category) const;

    void SetAssetLoadThread(AssetLoad::AsyncAssetLoadManager* _asyncAssetLoadManager);

    void RegisterPipelines();

    void LogRegistrationSummary();

#ifdef ENABLE_VULKAN_VALIDATION
    /**
     * Dumps per-pipeline shader executable statistics (VGPR/SGPR/LDS/occupancy/spills, as reported by the driver) to a plaintext file.
     * No-op unless VK_KHR_pipeline_executable_properties is enabled; pipelines must have been created with the capture-statistics flag.
     * @param outputPath
     */
    void DumpExecutableStats(const Core::Path& outputPath);
#endif

    VkPipelineLayout GetGlobalPipelineLayout() const { return globalPipelineLayout.handle; }

    VkPipelineCache GetPipelineCache() const { return pipelineCache; }

    Core::Span<const StringID> GetShadingPipelines() const { return Core::Span<const StringID>{shadingPipelines.Data(), shadingPipelines.Size()}; }
    Core::Span<const StringID> GetLightingPipelines() const { return Core::Span<const StringID>{lightingPipelines.Data(), lightingPipelines.Size()}; }

private:
    void SubmitPipelineLoad(PipelineData* data) const;

    void HandlePipelineCompletion(PipelineData& pipeline, bool bSuccess) const;

    template<typename PipelineMap>
    void CleanupRetiredPipelines(PipelineMap& pipelines)
    {
        for (auto [name, pipeline] : pipelines) {
            if (pipeline.retirementFrame != 0 && currentFrame > pipeline.retirementFrame) {
                if (pipeline.retiredEntry.pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(context->device, pipeline.retiredEntry.pipeline, nullptr);
                    pipeline.retiredEntry.pipeline = VK_NULL_HANDLE;
                }
                if (pipeline.retiredEntry.layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(context->device, pipeline.retiredEntry.layout, nullptr);
                    pipeline.retiredEntry.layout = VK_NULL_HANDLE;
                }
                pipeline.retirementFrame = 0;
            }
        }
    }

private:
    // Non-Owning
    VulkanContext* context;
    ResourceManager* resourceManager;
    Core::TlsfAllocator* renderAlloc{nullptr};
    Core::TlsfAllocator* assetScratchAlloc{nullptr};
    AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager{nullptr};

    // Owning
    Core::Map<StringID, GraphicsPipelineData> graphicsPipelines;
    Core::Map<StringID, ComputePipelineData> computePipelines;

    // // Geometry pipelines
    Core::InlineVector<StringID, 128> shadingPipelines;
    Core::InlineVector<StringID, 128> lightingPipelines;

    PipelineLayout globalPipelineLayout;

    uint32_t currentFrame;
    Core::Array<VkDescriptorSetLayout, 3> globalDescriptorSetLayouts;
    VkPipelineCache pipelineCache{VK_NULL_HANDLE};

    std::atomic<bool> bReloadRequested{false};

    int32_t registeredComputeCount{0};
    int32_t registeredGraphicsCount{0};

    int32_t pendingPipelineLogCount{0};
    std::chrono::steady_clock::time_point pipelineLastActivity{};

public:
    std::atomic<bool> bVerbosePipelineLoading{false};
};
} // Render

#endif //WILL_ENGINE_PIPELINE_MANAGER_H
