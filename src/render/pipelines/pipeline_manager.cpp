//
// Created by William on 2026-01-19.
//

#include "pipeline_manager.h"

#include <fstream>
#include <ranges>

#include "platform/paths.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
PipelineManager::PipelineManager(VulkanContext* context, const std::array<VkDescriptorSetLayout, 2>& globalLayouts)
    : context(context), currentFrame(0), globalDescriptorSetLayouts(globalLayouts)
{
    std::filesystem::path cachePath = Platform::GetCachePath() / "pipeline.cache";

    std::vector<char> cacheData;
    if (std::filesystem::exists(cachePath)) {
        std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
        if (file) {
            size_t fileSize = file.tellg();
            file.seekg(0);
            cacheData.resize(fileSize);
            file.read(cacheData.data(), fileSize);
            SPDLOG_INFO("Loaded pipeline cache: {} bytes", fileSize);
        }
    }

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData = cacheData.data();

    VK_CHECK(vkCreatePipelineCache(context->device, &cacheInfo, nullptr, &pipelineCache));
}

PipelineManager::~PipelineManager()
{
    if (pipelineCache != VK_NULL_HANDLE) {
        size_t cacheSize = 0;
        vkGetPipelineCacheData(context->device, pipelineCache, &cacheSize, nullptr);

        if (cacheSize > 0) {
            std::vector<char> cacheData(cacheSize);
            vkGetPipelineCacheData(context->device, pipelineCache, &cacheSize, cacheData.data());

            std::filesystem::path cachePath = Platform::GetCachePath() / "pipeline.cache";
            std::ofstream file(cachePath, std::ios::binary);
            if (file) {
                file.write(cacheData.data(), cacheSize);
                SPDLOG_INFO("Saved pipeline cache: {} bytes", cacheSize);
            }
        }

        vkDestroyPipelineCache(context->device, pipelineCache, nullptr);
    }

    for (auto& pipeline : pipelines) {
        if (pipeline.second.activeEntry.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context->device, pipeline.second.activeEntry.pipeline, nullptr);
            pipeline.second.activeEntry.pipeline = VK_NULL_HANDLE;
        }
        if (pipeline.second.activeEntry.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context->device, pipeline.second.activeEntry.layout, nullptr);
            pipeline.second.activeEntry.layout = VK_NULL_HANDLE;
        }

        if (pipeline.second.loadingEntry.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context->device, pipeline.second.loadingEntry.pipeline, nullptr);
            pipeline.second.loadingEntry.pipeline = VK_NULL_HANDLE;
        }
        if (pipeline.second.loadingEntry.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context->device, pipeline.second.loadingEntry.layout, nullptr);
            pipeline.second.loadingEntry.layout = VK_NULL_HANDLE;
        }

        if (pipeline.second.retiredEntry.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context->device, pipeline.second.retiredEntry.pipeline, nullptr);
            pipeline.second.retiredEntry.pipeline = VK_NULL_HANDLE;
        }
        if (pipeline.second.retiredEntry.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context->device, pipeline.second.retiredEntry.layout, nullptr);
            pipeline.second.retiredEntry.layout = VK_NULL_HANDLE;
        }
    }
}

void PipelineManager::RegisterComputePipeline(const std::string& name, const std::filesystem::path& shaderPath, uint32_t pushConstantSize, PipelineCategory category)
{
    if (pipelines.contains(name)) {
        SPDLOG_WARN("Pipeline '{}' already registered, skipping", name);
        return;
    }

    pipelines[name] = {};
    PipelineData& data = pipelines[name];
    data.category = category;
    data.shaderPath = shaderPath;
    data.retirementFrame = 0;
    data.pushConstantRange.offset = 0;
    data.pushConstantRange.size = pushConstantSize;
    data.pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    data.layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    data.layoutCreateInfo.pSetLayouts = globalDescriptorSetLayouts.data();
    data.layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(globalDescriptorSetLayouts.size());
    data.layoutCreateInfo.pPushConstantRanges = &data.pushConstantRange;
    data.layoutCreateInfo.pushConstantRangeCount = 1;

    data.bIsCompute = true;
    data.loadingEntry.pipeline = VK_NULL_HANDLE;
    data.loadingEntry.layout = VK_NULL_HANDLE;
    data.activeEntry.pipeline = VK_NULL_HANDLE;
    data.activeEntry.layout = VK_NULL_HANDLE;


    SubmitPipelineLoad(name, &pipelines[name]);

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

    return &it->second.activeEntry;
}

void PipelineManager::SubmitPipelineLoad(const std::string& name, PipelineData* data) const
{
    assetLoadThread->RequestPipelineLoad(name, data);
}

void PipelineManager::Update(uint32_t frameNumber)
{
    currentFrame = frameNumber;

    AssetLoad::PipelineComplete complete;
    while (assetLoadThread->ResolvePipelineLoads(complete)) {
        if (complete.success) {
            SPDLOG_INFO("Pipeline '{}' async load completed", complete.name);
            pipelines[complete.name].retiredEntry = pipelines[complete.name].activeEntry;
            pipelines[complete.name].retirementFrame = currentFrame + 3;

            pipelines[complete.name].activeEntry = pipelines[complete.name].loadingEntry;

            pipelines[complete.name].loadingEntry = {};
            pipelines[complete.name].bLoading = false;
        }
        else {
            SPDLOG_ERROR("Pipeline '{}' async load failed", complete.name);
            pipelines[complete.name].loadingEntry = {};
            pipelines[complete.name].bLoading = false;
        }
    }

    if (bReloadRequested.exchange(false, std::memory_order_relaxed)) {
        ReloadModified();
    }

    for (auto& pipeline : pipelines) {
        if (currentFrame > pipeline.second.retirementFrame) {
            if (pipeline.second.retiredEntry.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(context->device, pipeline.second.retiredEntry.pipeline, nullptr);
                pipeline.second.retiredEntry.pipeline = VK_NULL_HANDLE;
            }
            if (pipeline.second.retiredEntry.layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(context->device, pipeline.second.retiredEntry.layout, nullptr);
                pipeline.second.retiredEntry.layout = VK_NULL_HANDLE;
            }
            pipeline.second.retirementFrame = 0;
        }
    }
}

bool PipelineManager::IsCategoryReady(PipelineCategory category) const
{
    for (auto& pipeline : pipelines) {
        if (static_cast<uint32_t>(pipeline.second.category & category) != 0) {
            if (pipeline.second.activeEntry.layout == VK_NULL_HANDLE || pipeline.second.activeEntry.pipeline == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    return true;
}

void PipelineManager::SetAssetLoadThread(AssetLoad::AssetLoadThread* _assetLoadThread)
{
    assetLoadThread = _assetLoadThread;
}

void PipelineManager::ReloadModified()
{
    for (auto& [name, data] : pipelines) {
        if (data.bLoading) { continue; }
        if (data.retirementFrame != 0) { continue; }

        auto currentTime = std::filesystem::last_write_time(data.shaderPath);

        if (currentTime != data.lastModified) {
            SPDLOG_INFO("Shader modified, rebuilding pipeline: {}", name);
            data.bLoading = true;
            SubmitPipelineLoad(name, &data);
        }
    }
}
} // Render
