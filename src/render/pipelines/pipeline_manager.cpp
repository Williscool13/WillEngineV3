//
// Created by William on 2026-01-19.
//

#include "pipeline_manager.h"

#include <fstream>
#include <ranges>

#include "graphics_pipeline_builder.h"
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

    auto cleanupPipeline = [this](PipelineData& pipeline) {
        if (pipeline.activeEntry.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context->device, pipeline.activeEntry.pipeline, nullptr);
            pipeline.activeEntry.pipeline = VK_NULL_HANDLE;
        }
        if (pipeline.activeEntry.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context->device, pipeline.activeEntry.layout, nullptr);
            pipeline.activeEntry.layout = VK_NULL_HANDLE;
        }

        if (pipeline.loadingEntry.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context->device, pipeline.loadingEntry.pipeline, nullptr);
            pipeline.loadingEntry.pipeline = VK_NULL_HANDLE;
        }
        if (pipeline.loadingEntry.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context->device, pipeline.loadingEntry.layout, nullptr);
            pipeline.loadingEntry.layout = VK_NULL_HANDLE;
        }

        if (pipeline.retiredEntry.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(context->device, pipeline.retiredEntry.pipeline, nullptr);
            pipeline.retiredEntry.pipeline = VK_NULL_HANDLE;
        }
        if (pipeline.retiredEntry.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(context->device, pipeline.retiredEntry.layout, nullptr);
            pipeline.retiredEntry.layout = VK_NULL_HANDLE;
        }
    };

    for (auto& pipeline : graphicsPipelines) {
        cleanupPipeline(pipeline.second);
    }

    for (auto& pipeline : computePipelines) {
        cleanupPipeline(pipeline.second);
    }
}

void PipelineManager::RegisterComputePipeline(const std::string& name, const std::filesystem::path& shaderPath, uint32_t pushConstantSize, PipelineCategory category)
{
    if (computePipelines.contains(name)) {
        SPDLOG_WARN("Pipeline '{}' already registered, skipping", name);
        return;
    }

    ComputePipelineData& data = computePipelines[name];
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
    data.layoutCreateInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;

    SubmitPipelineLoad(name, &data);

    SPDLOG_INFO("Registered compute pipeline: {}", name);
}

void PipelineManager::RegisterGraphicsPipeline(const std::string& name, GraphicsPipelineBuilder& builder, uint32_t pushConstantSize, VkShaderStageFlags pushConstantStages, PipelineCategory category)
{
    if (graphicsPipelines.contains(name)) {
        SPDLOG_WARN("Pipeline '{}' already registered, skipping", name);
        return;
    }

    GraphicsPipelineData& data = graphicsPipelines[name];
    data.category = category;
    data.retirementFrame = 0;

    data.shaderStageCount = builder.shaderStageCount;
    for (uint32_t i = 0; i < builder.shaderStageCount; ++i) {
        data.shaderPaths[i] = builder.shaderPaths[i];
        data.shaderStages[i] = builder.shaderStages[i];
    }

    data.vertexBindingCount = builder.vertexBindingCount;
    for (uint32_t i = 0; i < builder.vertexBindingCount; ++i) {
        data.vertexBindings[i] = builder.vertexBindings[i];
    }
    data.vertexAttributeCount = builder.vertexAttributeCount;
    for (uint32_t i = 0; i < builder.vertexAttributeCount; ++i) {
        data.vertexAttributes[i] = builder.vertexAttributes[i];
    }

    data.colorAttachmentFormatCount = builder.colorAttachmentFormatCount;
    for (uint32_t i = 0; i < builder.colorAttachmentFormatCount; ++i) {
        data.colorAttachmentFormats[i] = builder.colorAttachmentFormats[i];
    }

    data.blendAttachmentStateCount = builder.blendAttachmentStateCount;
    for (uint32_t i = 0; i < builder.blendAttachmentStateCount; ++i) {
        data.blendAttachmentStates[i] = builder.blendAttachmentStates[i];
    }

    data.dynamicStateCount = builder.dynamicStateCount;
    for (uint32_t i = 0; i < builder.dynamicStateCount; ++i) {
        data.dynamicStates[i] = builder.dynamicStates[i];
    }

    data.vertexInputInfo = builder.vertexInputInfo;
    data.inputAssembly = builder.inputAssembly;
    data.viewportState = builder.viewportState;
    data.rasterizer = builder.rasterizer;
    data.multisampling = builder.multisampling;
    data.depthStencil = builder.depthStencil;
    data.colorBlending = builder.colorBlending;
    data.renderInfo = builder.renderInfo;
    data.tessellation = builder.tessellation;
    data.dynamicInfo = builder.dynamicInfo;
    data.bIsTessellationEnabled = builder.bIsTessellationEnabled;

    // Setup push constants
    data.pushConstantRange.offset = 0;
    data.pushConstantRange.size = pushConstantSize;
    data.pushConstantRange.stageFlags = pushConstantStages;

    // Setup pipeline layout
    data.layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    data.layoutCreateInfo.pSetLayouts = globalDescriptorSetLayouts.data();
    data.layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(globalDescriptorSetLayouts.size());
    data.layoutCreateInfo.pPushConstantRanges = &data.pushConstantRange;
    data.layoutCreateInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;

    SubmitPipelineLoad(name, &data);

    SPDLOG_INFO("Registered graphics pipeline: {}", name);
}

const PipelineEntry* PipelineManager::GetPipelineEntry(const std::string& name)
{
    if (auto it = computePipelines.find(name); it != computePipelines.end()) {
        return &it->second.activeEntry;
    }

    if (auto it = graphicsPipelines.find(name); it != graphicsPipelines.end()) {
        return &it->second.activeEntry;
    }

    SPDLOG_ERROR("Pipeline '{}' not found", name);
    return nullptr;
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
        if (auto it = computePipelines.find(complete.name); it != computePipelines.end()) {
            if (complete.success) SPDLOG_INFO("Compute pipeline '{}' loaded", complete.name);
            HandlePipelineCompletion(it->second, complete);
        } else if (auto it = graphicsPipelines.find(complete.name); it != graphicsPipelines.end()) {
            if (complete.success) SPDLOG_INFO("Graphics pipeline '{}' loaded", complete.name);
            HandlePipelineCompletion(it->second, complete);
        } else {
            SPDLOG_ERROR("Pipeline '{}' not found", complete.name);
        }
    }

    if (bReloadRequested.exchange(false, std::memory_order_relaxed)) {
        ReloadModified();
    }

    CleanupRetiredPipelines(computePipelines);
    CleanupRetiredPipelines(graphicsPipelines);
}

bool PipelineManager::IsCategoryReady(PipelineCategory category) const
{
    for (const auto& pipeline : computePipelines | std::views::values) {
        if (static_cast<uint32_t>(pipeline.category & category) != 0) {
            if (pipeline.activeEntry.layout == VK_NULL_HANDLE || pipeline.activeEntry.pipeline == VK_NULL_HANDLE) {
                return false;
            }
        }
    }

    for (const auto& pipeline : graphicsPipelines | std::views::values) { // NOLINT(*-use-anyofallof)
        if (static_cast<uint32_t>(pipeline.category & category) != 0) {
            if (pipeline.activeEntry.layout == VK_NULL_HANDLE || pipeline.activeEntry.pipeline == VK_NULL_HANDLE) {
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
    for (auto& [name, data] : computePipelines) {
        if (data.bLoading || data.retirementFrame != 0) { continue; }

        auto currentTime = std::filesystem::last_write_time(data.shaderPath);
        if (currentTime != data.lastModified) {
            SPDLOG_INFO("Compute shader modified, rebuilding pipeline: {}", name);
            data.bLoading = true;
            SubmitPipelineLoad(name, &data);
        }
    }

    for (auto& [name, data] : graphicsPipelines) {
        if (data.bLoading || data.retirementFrame != 0) { continue; }

        auto currentTime = std::filesystem::file_time_type::min();
        for (uint32_t i = 0; i < data.shaderStageCount; ++i) {
            auto modTime = std::filesystem::last_write_time(data.shaderPaths[i]);
            if (modTime > currentTime) {
                currentTime = modTime;
            }
        }

        if (currentTime != data.lastModified) {
            SPDLOG_INFO("Graphics shader modified, rebuilding pipeline: {}", name);
            data.bLoading = true;
            SubmitPipelineLoad(name, &data);
        }
    }
}

void PipelineManager::HandlePipelineCompletion(PipelineData& pipeline, const AssetLoad::PipelineComplete& complete) const
{
    if (complete.success) {
        pipeline.retiredEntry = pipeline.activeEntry;
        pipeline.retirementFrame = currentFrame + 3;
        pipeline.activeEntry = pipeline.loadingEntry;
        pipeline.loadingEntry = {};
        pipeline.bLoading = false;
    } else {
        SPDLOG_ERROR("Pipeline '{}' async load failed", complete.name);
        pipeline.loadingEntry = {};
        pipeline.bLoading = false;
    }
}

} // Render
