//
// Created by William on 2026-01-19.
//

#ifndef WILL_ENGINE_PIPELINE_MANAGER_H
#define WILL_ENGINE_PIPELINE_MANAGER_H


#include <filesystem>
#include <string>
#include <unordered_map>
#include <volk.h>

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
    VkPipeline pipeline;
    VkPipelineLayout layout;
    std::filesystem::path shaderPath;
    std::filesystem::file_time_type lastModified;
    uint32_t retirementFrame;

    VkPipelineLayoutCreateInfo layoutCreateInfo;
    VkPushConstantRange pushConstantRange;
    VkGraphicsPipelineCreateInfo graphicsCreateInfo;
    bool bIsCompute;
};

struct PipelineData
{
    std::vector<PipelineEntry> versions;
    bool isCompute;
};

class PipelineManager
{
public:
    explicit PipelineManager(VulkanContext* context, AssetLoad::AssetLoadThread* assetLoadThread, const std::array<VkDescriptorSetLayout, 2>& globalLayouts);

    ~PipelineManager();

    PipelineManager(const PipelineManager&) = delete;

    PipelineManager& operator=(const PipelineManager&) = delete;

    void RegisterComputePipeline(const std::string& name, const std::filesystem::path& shaderPath, uint32_t pushConstantSize);

    void RegisterGraphicsPipeline(const std::string& name, const std::filesystem::path& shaderPath, const VkGraphicsPipelineCreateInfo& pipelineInfo);

    const PipelineEntry* GetPipelineEntry(const std::string& name);

    void ReloadModified();

    void Update(uint32_t frameNumber);

private:
    void SubmitPipelineLoad(const std::string& name, PipelineEntry& entry) const;

    VulkanContext* context;
    AssetLoad::AssetLoadThread* assetLoadThread;
    std::unordered_map<std::string, PipelineData> pipelines;
    uint32_t currentFrame;
    std::array<VkDescriptorSetLayout, 2> globalDescriptorSetLayouts;
};
} // Render

#endif //WILL_ENGINE_PIPELINE_MANAGER_H
