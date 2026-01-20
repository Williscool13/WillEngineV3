//
// Created by William on 2026-01-19.
//

#include "pipeline_manager.h"

#include <ranges>

namespace Render
{
PipelineManager::PipelineManager(VulkanContext* context, AssetLoad::AssetLoadThread* assetLoadThread, const std::array<VkDescriptorSetLayout, 2>& globalLayouts)
    : context(context), assetLoadThread(assetLoadThread), currentFrame(0), globalDescriptorSetLayouts(globalLayouts)
{}

PipelineManager::~PipelineManager()
{
    for (auto& pipeline : pipelines) {
        for (auto& entry : pipeline.second.versions) {
            if (entry.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(context->device, entry.pipeline, nullptr);
            }
            if (entry.layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(context->device, entry.layout, nullptr);
            }
        }
    }
}

void PipelineManager::RegisterComputePipeline(const std::string& name, const std::filesystem::path& shaderPath, uint32_t pushConstantSize, PipelineCategory category)
{
    if (pipelines.contains(name)) {
        SPDLOG_WARN("Pipeline '{}' already registered, skipping", name);
        return;
    }

    PipelineData data;
    data.category = category;

    data.versions.emplace_back();
    PipelineEntry& entry = data.versions.back();
    entry.shaderPath = shaderPath;

    entry.pushConstantRange.offset = 0;
    entry.pushConstantRange.size = pushConstantSize;
    entry.pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    entry.layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    entry.layoutCreateInfo.pSetLayouts = globalDescriptorSetLayouts.data();
    entry.layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(globalDescriptorSetLayouts.size());
    entry.layoutCreateInfo.pPushConstantRanges = &entry.pushConstantRange;
    entry.layoutCreateInfo.pushConstantRangeCount = 1;

    entry.bIsCompute = true;
    entry.pipeline = VK_NULL_HANDLE;
    entry.layout = VK_NULL_HANDLE;
    entry.retirementFrame = 0;

    pipelines[name] = std::move(data);

    SubmitPipelineLoad(name, pipelines[name].versions[0]);

    SPDLOG_INFO("Registered compute pipeline: {}", name);
}

void PipelineManager::RegisterGraphicsPipeline(const std::string& name, const std::filesystem::path& shaderPath, const VkGraphicsPipelineCreateInfo& pipelineInfo)
{
    // todo
}

const PipelineEntry* PipelineManager::GetPipelineEntry(const std::string& name)
{
    const auto it = pipelines.find(name);
    if (it == pipelines.end()) {
        SPDLOG_ERROR("Pipeline '{}' not found", name);
        return nullptr;
    }

    if (it->second.versions.empty()) {
        SPDLOG_ERROR("Pipeline '{}' has no versions", name);
        return nullptr;
    }

    return &it->second.versions[0];
}

void PipelineManager::SubmitPipelineLoad(const std::string& name, PipelineEntry& entry) const
{
    assetLoadThread->RequestPipelineLoad(name, &entry);
}

void PipelineManager::Update(uint32_t frameNumber)
{
    currentFrame = frameNumber;

    AssetLoad::PipelineComplete complete;
    while (assetLoadThread->ResolvePipelineLoads(complete)) {
        if (complete.success) {
            SPDLOG_INFO("Pipeline '{}' async load completed", complete.name);
        }
        else {
            SPDLOG_ERROR("Pipeline '{}' async load failed", complete.name);
        }
    }

    for (auto& [name, data] : pipelines) {
        auto& versions = data.versions;

        versions.erase(
            std::remove_if(versions.begin() + 1, versions.end(),
                           [this](const PipelineEntry& entry) {
                               if (entry.retirementFrame > 0 && currentFrame >= entry.retirementFrame) {
                                   vkDestroyPipeline(context->device, entry.pipeline, nullptr);
                                   vkDestroyPipelineLayout(context->device, entry.layout, nullptr);
                                   return true;
                               }
                               return false;
                           }),
            versions.end()
        );
    }
}

bool PipelineManager::IsCategoryReady(PipelineCategory category) const
{
    for (auto& pipeline : pipelines){
        if (static_cast<uint32_t>(pipeline.second.category & category) != 0) {
            if (pipeline.second.versions.empty() || pipeline.second.versions[0].pipeline == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    return true;
}

void PipelineManager::ReloadModified()
{
    SPDLOG_INFO("Checking for modified shaders...");

    for (auto& [name, data] : pipelines) {
        if (data.versions.empty()) { continue; }

        auto& active = data.versions[0];
        if (active.pipeline == VK_NULL_HANDLE) { continue; }

        auto currentTime = std::filesystem::last_write_time(active.shaderPath);

        if (currentTime != active.lastModified) {
            SPDLOG_INFO("Shader modified, rebuilding pipeline: {}", name);

            active.retirementFrame = currentFrame + 3;

            PipelineEntry newEntry;
            newEntry.shaderPath = active.shaderPath;
            newEntry.layoutCreateInfo = active.layoutCreateInfo;
            newEntry.graphicsCreateInfo = active.graphicsCreateInfo;
            newEntry.bIsCompute = active.bIsCompute;
            newEntry.pipeline = VK_NULL_HANDLE;
            newEntry.layout = VK_NULL_HANDLE;
            newEntry.retirementFrame = 0;

            data.versions.insert(data.versions.begin(), newEntry);
            SubmitPipelineLoad(name, data.versions[0]);
        }
    }
}
} // Render
