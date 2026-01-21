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
#include "asset-load/asset_load_thread.h"

namespace AssetLoad
{
class AssetLoadThread;
}

namespace Render
{
struct VulkanContext;

struct PipelineEntry
{
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
};

struct PipelineData
{
    // Initialized once, never modified again
    PipelineCategory category{PipelineCategory::None};
    std::filesystem::path shaderPath{};
    VkPipelineLayoutCreateInfo layoutCreateInfo{};
    VkPushConstantRange pushConstantRange{};
    VkGraphicsPipelineCreateInfo graphicsCreateInfo{};

    // If true, loadingEntry is managed by asset load thead, do not touch.
    bool bLoading{false};
    PipelineEntry loadingEntry{};

    PipelineEntry activeEntry{};
    std::filesystem::file_time_type lastModified{};

    PipelineEntry retiredEntry{};
    uint32_t retirementFrame{0};

    bool bIsCompute;
};

class PipelineManager
{
public: // Thread-Safe
    void RequestReload() { bReloadRequested.store(true, std::memory_order_relaxed); }

public:
    explicit PipelineManager(VulkanContext* context, AssetLoad::AssetLoadThread* assetLoadThread, const std::array<VkDescriptorSetLayout, 2>& globalLayouts);

    ~PipelineManager();

    PipelineManager(const PipelineManager&) = delete;

    PipelineManager& operator=(const PipelineManager&) = delete;

    void RegisterComputePipeline(const std::string& name, const std::filesystem::path& shaderPath, uint32_t pushConstantSize, PipelineCategory category);

    void RegisterGraphicsPipeline(const std::string& name, const std::filesystem::path& shaderPath, const VkGraphicsPipelineCreateInfo& pipelineInfo);

    const PipelineEntry* GetPipelineEntry(const std::string& name);

    void ReloadModified();

    void Update(uint32_t frameNumber);

    bool IsCategoryReady(PipelineCategory category) const;

private:
    void SubmitPipelineLoad(const std::string& name, PipelineData* data) const;

    VulkanContext* context;
    AssetLoad::AssetLoadThread* assetLoadThread;
    std::unordered_map<std::string, PipelineData> pipelines;
    uint32_t currentFrame;
    std::array<VkDescriptorSetLayout, 2> globalDescriptorSetLayouts;

    std::atomic<bool> bReloadRequested{false};
};
} // Render

#endif //WILL_ENGINE_PIPELINE_MANAGER_H
