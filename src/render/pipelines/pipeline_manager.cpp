//
// Created by William on 2026-01-19.
//

#include "pipeline_manager.h"

#include <fstream>

#include "graphics_pipeline_builder.h"
#include "core/containers/vector.h"
#include "asset-load/async_asset_load_manager.h"
#include "core/memory/tlsf_allocator.h"
#include "engine/logging/engine_log.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
PipelineManager::PipelineManager(VulkanContext* context, Core::TlsfAllocator& renderAlloc, const Core::Array<VkDescriptorSetLayout, 2>& globalLayouts)
    : context(context), renderAlloc(&renderAlloc),
      graphicsPipelines(&renderAlloc, Core::AllocTag::Render, 256),
      computePipelines(&renderAlloc, Core::AllocTag::Render, 1024),
      currentFrame(0),
      globalDescriptorSetLayouts(globalLayouts)
{
    Core::Path cachePath = Platform::GetCachePath() / "pipeline.cache";

    Core::Vector<char> cacheData(&renderAlloc, Core::AllocTag::Render);
    if (cachePath.Exists()) {
        std::ifstream file(cachePath.c_str(), std::ios::binary | std::ios::ate);
        if (file) {
            size_t fileSize = file.tellg();
            file.seekg(0);
            cacheData.Resize(fileSize);
            file.read(cacheData.Data(), fileSize);
            LOG_INFO(Renderer, "Loaded pipeline cache: {} bytes", fileSize);
        }
    }

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cacheInfo.initialDataSize = cacheData.Size();
    cacheInfo.pInitialData = cacheData.Data();

    VK_CHECK(vkCreatePipelineCache(context->device, &cacheInfo, nullptr, &pipelineCache));
}

PipelineManager::~PipelineManager()
{
    if (pipelineCache != VK_NULL_HANDLE) {
        size_t cacheSize = 0;
        vkGetPipelineCacheData(context->device, pipelineCache, &cacheSize, nullptr);

        if (cacheSize > 0) {
            Core::Vector<char> cacheData(renderAlloc, Core::AllocTag::Render);
            cacheData.Resize(cacheSize);
            vkGetPipelineCacheData(context->device, pipelineCache, &cacheSize, cacheData.Data());

            Core::Path cachePath = Platform::GetCachePath() / "pipeline.cache";
            std::ofstream file(cachePath.c_str(), std::ios::binary);
            if (file) {
                file.write(cacheData.Data(), cacheSize);
                LOG_INFO(Renderer, "Saved pipeline cache: {} bytes", cacheSize);
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

    for (auto [id, pipeline] : graphicsPipelines) {
        cleanupPipeline(pipeline);
    }

    for (auto [id, pipeline] : computePipelines) {
        cleanupPipeline(pipeline);
    }
}

void PipelineManager::RegisterComputePipeline(StringID pipelineId, Core::Path shaderPath, uint32_t pushConstantSize, PipelineCategory category)
{
    if (computePipelines.Contains(pipelineId)) {
        LOG_WARN(Renderer, "Pipeline '{}' already registered, skipping", pipelineId.ToString());
        return;
    }

    ComputePipelineData& data = computePipelines[pipelineId];
    data.pipelineId = pipelineId;
    data.category = category;
    data.shaderPath = shaderPath;
    data.retirementFrame = 0;
    data.pushConstantRange.offset = 0;
    data.pushConstantRange.size = pushConstantSize;
    data.pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    data.layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    data.layoutCreateInfo.pSetLayouts = globalDescriptorSetLayouts.Data();
    data.layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(globalDescriptorSetLayouts.Size());
    data.layoutCreateInfo.pPushConstantRanges = &data.pushConstantRange;
    data.layoutCreateInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;

    SubmitPipelineLoad(&data);

    LOG_INFO(Renderer, "Registered compute pipeline: {}", pipelineId.ToString());
}

void PipelineManager::RegisterComputePipelineCustomLayout(StringID pipelineId, Core::Path shaderPath, uint32_t pushConstantSize, PipelineCategory category,
                                                          Core::Span<const VkDescriptorSetLayout> customLayouts)
{
    if (computePipelines.Contains(pipelineId)) {
        LOG_WARN(Renderer, "Pipeline '{}' already registered, skipping", pipelineId.ToString());
        return;
    }

    ComputePipelineData& data = computePipelines[pipelineId];
    data.pipelineId = pipelineId;
    data.category = category;
    data.shaderPath = shaderPath;
    data.retirementFrame = 0;
    data.pushConstantRange.offset = 0;
    data.pushConstantRange.size = pushConstantSize;
    data.pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    for (const VkDescriptorSetLayout layout : customLayouts) {
        data.customLayout.PushBack(layout);
    }
    data.layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    data.layoutCreateInfo.pSetLayouts = data.customLayout.Data();
    data.layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(data.customLayout.Size());
    data.layoutCreateInfo.pPushConstantRanges = &data.pushConstantRange;
    data.layoutCreateInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;

    SubmitPipelineLoad(&data);

    LOG_INFO(Renderer, "Registered compute pipeline (custom layout): {}", pipelineId.ToString());
}

void PipelineManager::RegisterGraphicsPipeline(StringID pipelineId, GraphicsPipelineBuilder& builder, uint32_t pushConstantSize, VkShaderStageFlags pushConstantStages, PipelineCategory category)
{
    if (graphicsPipelines.Contains(pipelineId)) {
        LOG_WARN(Renderer, "Pipeline '{}' already registered, skipping", pipelineId.ToString());
        return;
    }

    GraphicsPipelineData& data = graphicsPipelines[pipelineId];
    data.pipelineId = pipelineId;
    data.category = category;
    data.retirementFrame = 0;

    for (uint32_t i = 0; i < builder.shaderStages.Size(); ++i) {
        data.shaderPaths.PushBack(builder.shaderPaths[i]);
        data.shaderStages.PushBack(builder.shaderStages[i]);
    }
    for (const auto& b : builder.vertexBindings) { data.vertexBindings.PushBack(b); }
    for (const auto& a : builder.vertexAttributes) { data.vertexAttributes.PushBack(a); }
    for (const auto& f : builder.colorAttachmentFormats) { data.colorAttachmentFormats.PushBack(f); }
    for (const auto& s : builder.blendAttachmentStates) { data.blendAttachmentStates.PushBack(s); }
    for (const auto& d : builder.dynamicStates) { data.dynamicStates.PushBack(d); }

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
    data.layoutCreateInfo.pSetLayouts = globalDescriptorSetLayouts.Data();
    data.layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(globalDescriptorSetLayouts.Size());
    data.layoutCreateInfo.pPushConstantRanges = &data.pushConstantRange;
    data.layoutCreateInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;

    SubmitPipelineLoad(&data);

    LOG_INFO(Renderer, "Registered graphics pipeline: {}", pipelineId.ToString());
}

const PipelineEntry* PipelineManager::GetPipelineEntry(StringID pipelineId)
{
    if (auto* data = computePipelines.Find(pipelineId)) {
        return &data->activeEntry;
    }

    if (auto* data = graphicsPipelines.Find(pipelineId)) {
        return &data->activeEntry;
    }

    LOG_ERROR(Renderer, "Pipeline '{}' not found", pipelineId.ToString());
    return nullptr;
}

void PipelineManager::SubmitPipelineLoad(PipelineData* data) const
{
    asyncAssetLoadManager->RequestPipelineLoad(data);
}

void PipelineManager::HandlePipelineCompletion(PipelineData& pipeline, bool bSuccess) const
{
    if (bSuccess) {
        pipeline.retiredEntry = pipeline.activeEntry;
        pipeline.retirementFrame = currentFrame + 3;
        pipeline.activeEntry = pipeline.loadingEntry;
        pipeline.loadingEntry = {};
        pipeline.bLoading = false;
    }
    else {
        LOG_ERROR(Renderer, "Pipeline '{}' async load failed", pipeline.pipelineId.ToString());
        pipeline.loadingEntry = {};
        pipeline.bLoading = false;
    }
}

void PipelineManager::Update(uint32_t frameNumber)
{
    currentFrame = frameNumber;

    AssetLoad::PipelineLoadComplete complete;
    while (asyncAssetLoadManager->TryDequeuePipelineComplete(complete)) {
        if (auto* data = computePipelines.Find(complete.pipelineData->pipelineId)) {
            if (complete.bSuccess) {
                LOG_INFO(Renderer, "Compute pipeline '{}' loaded", complete.pipelineData->pipelineId.ToString());
            }
            HandlePipelineCompletion(*data, complete.bSuccess);
        }
        else if (auto* data = graphicsPipelines.Find(complete.pipelineData->pipelineId)) {
            if (complete.bSuccess) {
                LOG_INFO(Renderer, "Graphics pipeline '{}' loaded", complete.pipelineData->pipelineId.ToString());
            }
            HandlePipelineCompletion(*data, complete.bSuccess);
        }
        else {
            LOG_ERROR(Renderer, "Pipeline '{}' not found", complete.pipelineData->pipelineId.ToString());
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
    for (auto [id, pipeline] : computePipelines) {
        if (static_cast<uint32_t>(pipeline.category & category) != 0) {
            if (pipeline.activeEntry.layout == VK_NULL_HANDLE || pipeline.activeEntry.pipeline == VK_NULL_HANDLE) {
                return false;
            }
        }
    }

    for (auto [id, pipeline] : graphicsPipelines) {
        if (static_cast<uint32_t>(pipeline.category & category) != 0) {
            if (pipeline.activeEntry.layout == VK_NULL_HANDLE || pipeline.activeEntry.pipeline == VK_NULL_HANDLE) {
                return false;
            }
        }
    }

    return true;
}

void PipelineManager::SetAssetLoadThread(AssetLoad::AsyncAssetLoadManager* _asyncAssetLoadManager)
{
    asyncAssetLoadManager = _asyncAssetLoadManager;
}

void PipelineManager::ReloadModified()
{
    for (auto [pipelineId, data] : computePipelines) {
        if (data.bLoading || data.retirementFrame != 0) { continue; }

        uint64_t currentTime = Platform::GetFileWriteTime(data.shaderPath.c_str());
        if (currentTime != data.lastModified) {
            LOG_INFO(Renderer, "Compute shader modified, rebuilding pipeline: {}", pipelineId.ToString());
            data.bLoading = true;
            SubmitPipelineLoad(&data);
        }
    }

    for (auto [pipelineId, data] : graphicsPipelines) {
        if (data.bLoading || data.retirementFrame != 0) { continue; }

        uint64_t currentTime = 0;
        for (uint32_t i = 0; i < data.shaderPaths.Size(); ++i) {
            uint64_t modTime = Platform::GetFileWriteTime(data.shaderPaths[i].c_str());
            if (modTime > currentTime) {
                currentTime = modTime;
            }
        }

        if (currentTime != data.lastModified) {
            LOG_INFO(Renderer, "Graphics shader modified, rebuilding pipeline: {}", pipelineId.ToString());
            data.bLoading = true;
            SubmitPipelineLoad(&data);
        }
    }
}
} // Render
