//
// Created by William on 2026-01-19.
//

#ifndef WILL_ENGINE_PIPELINE_MANAGER_H
#define WILL_ENGINE_PIPELINE_MANAGER_H


#include <filesystem>
#include <string>
#include <unordered_map>
#include <volk.h>

#include "pipeline_category.h"
#include "pipeline_data.h"
#include "graphics_pipeline_builder.h"
#include "asset-load/asset_load_thread.h"

namespace AssetLoad
{
class AssetLoadThread;
}

namespace Render
{
struct VulkanContext;

class PipelineManager
{
public: // Thread-Safe
    void RequestReload() { bReloadRequested.store(true, std::memory_order_relaxed); }

public:
    explicit PipelineManager(VulkanContext* context, const std::array<VkDescriptorSetLayout, 2>& globalLayouts);

    ~PipelineManager();

    PipelineManager(const PipelineManager&) = delete;

    PipelineManager& operator=(const PipelineManager&) = delete;

    void RegisterComputePipeline(const std::string& name, const std::filesystem::path& shaderPath, uint32_t pushConstantSize, PipelineCategory category);

    void RegisterGraphicsPipeline(const std::string& name, GraphicsPipelineBuilder& builder, uint32_t pushConstantSize, VkShaderStageFlags pushConstantStages, PipelineCategory category);

    const PipelineEntry* GetPipelineEntry(const std::string& name);

    void ReloadModified();

    void Update(uint32_t frameNumber);

    bool IsCategoryReady(PipelineCategory category) const;

    void SetAssetLoadThread(AssetLoad::AssetLoadThread* _assetLoadThread);

    VkPipelineCache GetPipelineCache() const { return pipelineCache; }

private:
    void SubmitPipelineLoad(const std::string& name, PipelineData* data) const;


    void HandlePipelineCompletion(PipelineData& pipeline, const AssetLoad::PipelineComplete& complete) const;

    template<typename PipelineMap>
    void CleanupRetiredPipelines(PipelineMap& pipelines)
    {
        for (auto& [name, pipeline] : pipelines) {
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
    VulkanContext* context;
    AssetLoad::AssetLoadThread* assetLoadThread{nullptr};
    std::unordered_map<std::string, GraphicsPipelineData> graphicsPipelines;
    std::unordered_map<std::string, ComputePipelineData> computePipelines;

    uint32_t currentFrame;
    std::array<VkDescriptorSetLayout, 2> globalDescriptorSetLayouts;
    VkPipelineCache pipelineCache{VK_NULL_HANDLE};

    std::atomic<bool> bReloadRequested{false};
};
} // Render

#endif //WILL_ENGINE_PIPELINE_MANAGER_H
