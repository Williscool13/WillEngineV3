//
// Created by William on 2026-01-19.
//

#include "pipeline_manager.h"

#include "pipeline_config.h"

#include <fstream>

#include "graphics_pipeline_builder.h"
#include "core/containers/vector.h"
#include "core/containers/string.h"
#include "core/containers/inline_string.h"
#include "asset-load/async_asset_load_manager.h"
#include "core/memory/tlsf_allocator.h"
#include "engine/logging/engine_log.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_config.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
PipelineManager::PipelineManager(VulkanContext* context, ResourceManager* resourceManager, Core::TlsfAllocator& renderAlloc, Core::TlsfAllocator& assetScratchAlloc,
                                 const Core::Array<VkDescriptorSetLayout, 3>& globalLayouts)
    : context(context),
      resourceManager(resourceManager),
      renderAlloc(&renderAlloc),
      assetScratchAlloc(&assetScratchAlloc),
      graphicsPipelines(&renderAlloc, Core::AllocTag::Render, 256),
      computePipelines(&renderAlloc, Core::AllocTag::Render, 1024),
      currentFrame(0),
      globalDescriptorSetLayouts(globalLayouts)
{
    Core::Path cachePath = Platform::GetCachePath() / "pipeline.cache";

    Core::Vector<char> cacheData(&assetScratchAlloc, Core::AllocTag::Render);
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
            Core::Vector<char> cacheData(assetScratchAlloc, Core::AllocTag::Render);
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

void PipelineManager::RegisterComputePipeline(StringID pipelineId, Core::Path shaderPath, const char* entryPoint, uint32_t pushConstantSize, PipelineCategory category)
{
    if (computePipelines.Contains(pipelineId)) {
        LOG_WARN(Renderer, "Pipeline '{}' already registered, skipping", pipelineId.ToString());
        return;
    }

    ComputePipelineData& data = computePipelines[pipelineId];
    data.pipelineId = pipelineId;
    data.category = category;
    data.shaderPath = shaderPath;
    data.entryPoint = Core::InlineString<64>(entryPoint);
    data.retirementFrame = 0;
    data.pushConstantRange.offset = 0;
    data.pushConstantRange.size = pushConstantSize;
    data.pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    data.layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    data.layoutCreateInfo.pSetLayouts = globalDescriptorSetLayouts.Data();
    data.layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(globalDescriptorSetLayouts.Size());
    data.layoutCreateInfo.pPushConstantRanges = &data.pushConstantRange;
    data.layoutCreateInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;

    data.bLoading = true;
    SubmitPipelineLoad(&data);

    registeredComputeCount++;
    if constexpr (VERBOSE_PIPELINE_REGISTRATION) {
        LOG_INFO(Renderer, "Registered compute pipeline: {}", pipelineId.ToString());
    }
}

void PipelineManager::RegisterComputePipelineCustomLayout(StringID pipelineId, Core::Path shaderPath, const char* entryPoint, uint32_t pushConstantSize, PipelineCategory category,
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
    data.entryPoint = Core::InlineString<64>(entryPoint);
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

    data.bLoading = true;
    SubmitPipelineLoad(&data);

    registeredComputeCount++;
    if constexpr (VERBOSE_PIPELINE_REGISTRATION) {
        LOG_INFO(Renderer, "Registered compute pipeline (custom layout): {}", pipelineId.ToString());
    }
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
        data.entryPoints.PushBack(builder.entryPoints[i]);
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

    data.bLoading = true;
    SubmitPipelineLoad(&data);

    registeredGraphicsCount++;
    if constexpr (VERBOSE_PIPELINE_REGISTRATION) {
        LOG_INFO(Renderer, "Registered graphics pipeline: {}", pipelineId.ToString());
    }
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

PipelineEntry PipelineManager::GetPipelineEntrySnapshot(StringID pipelineId)
{
    std::scoped_lock lock(activeEntryMutex);
    if (const auto* data = computePipelines.Find(pipelineId)) {
        return data->activeEntry;
    }

    if (const auto* data = graphicsPipelines.Find(pipelineId)) {
        return data->activeEntry;
    }

    LOG_ERROR(Renderer, "Pipeline '{}' not found", pipelineId.ToString());
    return {};
}

void PipelineManager::SubmitPipelineLoad(PipelineData* data) const
{
    asyncAssetLoadManager->RequestPipelineLoad(data);
}

void PipelineManager::HandlePipelineCompletion(PipelineData& pipeline, bool bSuccess)
{
    if (bSuccess) {
        pipeline.retiredEntry = pipeline.activeEntry;
        pipeline.retirementFrame = currentFrame + 3;
        {
            std::scoped_lock lock(activeEntryMutex);
            pipeline.activeEntry = pipeline.loadingEntry;
        }
        pipeline.loadingEntry = {};
        pipeline.lastModified = pipeline.loadingLastModified;
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

    AssetLoad::PipelineLoadComplete complete{};
    int32_t pipelinesThisTick{0};
    while (asyncAssetLoadManager->TryDequeuePipelineComplete(complete)) {
        if (auto* data = computePipelines.Find(complete.pipelineData->pipelineId)) {
            if (complete.bSuccess) {
                pipelinesThisTick++;
                if (bVerbosePipelineLoading.load(std::memory_order_relaxed)) {
                    LOG_INFO(Renderer, "Compute pipeline '{}' loaded", complete.pipelineData->pipelineId.ToString());
                }
            }
            HandlePipelineCompletion(*data, complete.bSuccess);
        }
        else if (auto* data = graphicsPipelines.Find(complete.pipelineData->pipelineId)) {
            if (complete.bSuccess) {
                pipelinesThisTick++;
                if (bVerbosePipelineLoading.load(std::memory_order_relaxed)) {
                    LOG_INFO(Renderer, "Graphics pipeline '{}' loaded", complete.pipelineData->pipelineId.ToString());
                }
            }
            HandlePipelineCompletion(*data, complete.bSuccess);
        }
        else {
            LOG_ERROR(Renderer, "Pipeline '{}' not found", complete.pipelineData->pipelineId.ToString());
        }
    }

    if (pipelinesThisTick > 0) {
        pendingPipelineLogCount += pipelinesThisTick;
        pipelineLastActivity = std::chrono::steady_clock::now();
    }

    if (pendingPipelineLogCount > 0 && pipelinesThisTick == 0 && (std::chrono::steady_clock::now() - pipelineLastActivity) >= std::chrono::seconds(PIPELINE_LOG_IDLE_SECONDS)) {
        LOG_INFO(Renderer, "{} pipeline(s) loaded", pendingPipelineLogCount);
        pendingPipelineLogCount = 0;
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

void PipelineManager::LogRegistrationSummary()
{
    LOG_INFO(Renderer, "Registered {} compute + {} graphics pipelines", registeredComputeCount, registeredGraphicsCount);
}

void PipelineManager::RegisterPipelines()
{
    Core::Array<VkDescriptorSetLayout, 3> layouts{
        resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGRTDescriptorBuffer.descriptorSetLayout.handle
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pSetLayouts = layouts.Data();
    layoutInfo.setLayoutCount = layouts.Size();
    globalPipelineLayout = PipelineLayout::CreatePipelineLayout(context, layoutInfo);

    auto src = Platform::GetShaderPath();

    RegisterComputePipeline(SID("instancing_instance_lod"), src / "instancing_lod.spv", "ComputeInstanceLOD",
                            sizeof(InstanceLODPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_prefix_sum_up_1"), src / "instancing_prefix_sum.spv", "ComputePrefixSumUpsweep1",
                            sizeof(PrefixSumUpsweep1PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_prefix_sum_up_2"), src / "instancing_prefix_sum.spv", "ComputePrefixSumUpsweep2",
                            sizeof(PrefixSumUpsweep2PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_scan_blocks"), src / "instancing_prefix_sum.spv", "ComputePrefixSumScanBlocks",
                            sizeof(PrefixSumScanBlocksPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_prefix_sum_down_1"), src / "instancing_prefix_sum.spv", "ComputePrefixSumDownsweep1",
                            sizeof(PrefixSumDownsweep1PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_prefix_sum_down_2"), src / "instancing_prefix_sum.spv", "ComputePrefixSumDownsweep2",
                            sizeof(PrefixSumDownsweep2PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_total_meshlet_count"), src / "instancing_meshlets.spv", "ComputeTotalMeshletCount",
                            sizeof(TotalMeshletCountPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_expand_instance_to_meshlet"), src / "instancing_meshlets.spv", "ComputeExpandInstancesToMeshlets",
                            sizeof(ExpandMeshletsPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_meshlet_visibility_prefix_sum_up_1"), src / "instancing_prefix_sum.spv", "ComputeMeshletVisibilityPrefixSumUpsweep1",
                            sizeof(MeshletVisibilityPrefixSumUpsweep1PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_meshlet_visibility_prefix_sum_down_2"), src / "instancing_prefix_sum.spv", "ComputeMeshletVisibilityPrefixSumDownsweep2",
                            sizeof(MeshletVisibilityPrefixSumDownsweep2PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_compacted_meshlet_dispatch"), src / "instancing_meshlets.spv", "ComputeCompactedMeshletDispatch",
                            sizeof(CompactedMeshletDispatchPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_max_meshlet_count"), src / "instancing_meshlets.spv", "ComputeMaxMeshletCount",
                            sizeof(MaxMeshletCountPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline("visibility_buffer_barycentric_derivative"_sid, src / "visibility_barycentric_derivative.spv", "ComputeVisibilityBarycentricDerivative",
                            sizeof(VisibilityBufferResolvePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("visibility_bucketing_bounds_calculation"_sid, src / "visibility_bucketing_bounds.spv", "ComputeShadeDispatchBucketing",
                            sizeof(ShadeBucketingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("visibility_shading_bucketing_resolve"_sid, src / "visibility_bucketing_shade_resolve.spv", "ComputeShadeDispatchBucketingResolve",
                            sizeof(ShadeBucketingResolvePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("visibility_lighting_bucketing_resolve"_sid, src / "visibility_bucketing_light_resolve.spv", "ComputeLightDispatchBucketingResolve",
                            sizeof(LightingBucketingResolvePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("visibility_bucketing_dispatch_count"_sid, src / "visibility_bucketing_dispatch_count.spv", "ComputeBucketDispatchCount",
                            sizeof(BucketDispatchCountPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline("shading_bucket_visualize"_sid, src / "shading_bucket_visualize.spv", "ComputeShadingBucketVisualize",
                            sizeof(VisibilityShadingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("lighting_bucket_visualize"_sid, src / "lighting_bucket_visualize.spv", "ComputeLightingBucketVisualize",
                            sizeof(LightingBucketVisualizePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("default_lit"_sid, src / "shading_default_lit.spv", "ComputeShadingDefaultLit",
                            sizeof(VisibilityShadingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("error_unlit"_sid, src / "shading_error_unlit.spv", "ComputeShadingErrorUnlit",
                            sizeof(VisibilityShadingPushConstant), PipelineCategory::Critical);

    shadingPipelines.PushBack("shading_bucket_visualize"_sid);
    shadingPipelines.PushBack("default_lit"_sid);
    shadingPipelines.PushBack("error_unlit"_sid);


    RegisterComputePipeline(SID("restir_di_transform_lights"), src / "restir_di_transform_lights.spv", "ComputeReSTIRDITransformLights",
                            sizeof(ReSTIRTransformLightsPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("regir_touch"), src / "regir_touch.spv", "ComputeReGIRTouch",
                            sizeof(ReGIRTouchPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("regir_build_indirect"), src / "regir_build_indirect.spv", "ComputeReGIRBuildIndirect",
                            sizeof(ReGIRBuildIndirectPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("regir_presample_tiles"), src / "regir_presample_tiles.spv", "ComputeReGIRPresampleTiles",
                            sizeof(ReGIRPresampleTilesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("regir_fill"), src / "regir_fill.spv", "ComputeReGIRFill",
                            sizeof(ReGIRFillPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_di_spatial"), src / "restir_di_spatial.spv", "ComputeReSTIRDISpatial",
                            sizeof(ReSTIRDISpatialPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_di_base_regir"), src / "restir_di_base_regir.spv", "ComputeReSTIRDIBaseReGIR",
                            sizeof(ReSTIRDICombinedTemporalPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_di_base_bin"), src / "restir_di_base_regir.spv", "ComputeReSTIRDIBaseBin",
                            sizeof(ReSTIRDICombinedTemporalPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_di_temporal"), src / "restir_di_temporal.spv", "ComputeReSTIRDITemporal",
                            sizeof(ReSTIRDICombinedTemporalPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_di_sun"), src / "restir_di_sun.spv", "ComputeReSTIRDISun",
                            sizeof(ReSTIRDISunPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_boiling_filter"), src / "restir_boiling_filter.spv", "ComputeReSTIRBoilingFilter",
                            sizeof(ReSTIRBoilingFilterPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_confidence_gradient"), src / "restir_confidence_gradient.spv", "ComputeReSTIRConfidenceGradient",
                            sizeof(ReSTIRConfidenceGradientPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_confidence_resolve"), src / "restir_confidence_resolve.spv", "ComputeReSTIRConfidenceResolve",
                            sizeof(ReSTIRConfidenceResolvePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_remodulate"), src / "restir_remodulate.spv", "ComputeReSTIRRemodulate",
                            sizeof(ReSTIRRemodulatePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reflection_trace"), src / "reflection_trace.spv", "ComputeReflectionTrace",
                            sizeof(ReflectionTracePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("ssr_trace"), src / "ssr_trace.spv", "ComputeSSRTrace",
                            sizeof(SSRTracePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reflection_shade"), src / "reflection_shade.spv", "ComputeReflectionShade",
                            sizeof(ReflectionShadePushConstant), PipelineCategory::Critical);

    RegisterComputePipeline("default_pbr"_sid, src / "lighting_pbr.spv", "ComputeLightingPBR",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);
    lightingPipelines.PushBack("default_pbr"_sid);
    RegisterComputePipeline("default_pbr_restir"_sid, src / "lighting_pbr_restir.spv", "ComputeLightingPBRRestir",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);
    lightingPipelines.PushBack("default_pbr_restir"_sid);
    RegisterComputePipeline("default_toon"_sid, src / "lighting_toon.spv", "ComputeLightingToon",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);
    lightingPipelines.PushBack("default_toon"_sid);
    RegisterComputePipeline("default_unlit"_sid, src / "lighting_unlit.spv", "ComputeLightingUnlit",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);
    lightingPipelines.PushBack("default_unlit"_sid);
    RegisterComputePipeline(SID("lighting_ground_truth"), src / "lighting_ground_truth.spv", "ComputeLightingGroundTruth",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("directional_light"), src / "directional_light.spv", "ComputeDirectionalLight",
                            sizeof(DirectionalLightPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("frustum_binning"), src / "frustum_binning.spv", "ComputeFrustumBinning",
                            sizeof(FrustumBinningPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("world_grid_binning"), src / "world_grid_binning.spv", "ComputeWorldGridBinning",
                            sizeof(WorldGridBinningPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("radiance_cache_carry_forward"), src / "radiance_cache_carry_forward.spv", "ComputeRadianceCacheCarryForward",
                            sizeof(RadianceCacheCarryForwardPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("radiance_cache_shade"), src / "radiance_cache_shade.spv", "ComputeRadianceCacheShade",
                            sizeof(RadianceCacheShadePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("radiance_cache_build_indirect"), src / "radiance_cache_build_indirect.spv", "ComputeRadianceCacheBuildIndirect",
                            sizeof(RadianceCacheBuildIndirectPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gi_gather"), src / "gi_gather.spv", "ComputeGIGather",
                            sizeof(GIGatherPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gi_denoise"), src / "gi_denoise.spv", "ComputeGIDenoise",
                            sizeof(GIDenoisePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gi_denoise_chroma"), src / "gi_denoise.spv", "ComputeGIDenoiseChroma",
                            sizeof(GIDenoisePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gi_upscale"), src / "gi_upscale.spv", "ComputeGIUpscale",
                            sizeof(GIUpscalePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gi_motion_tile_max"), src / "gi_upscale.spv", "ComputeGIMotionTileMax",
                            sizeof(GIMotionTileMaxPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("gpu_debug_build_indirect"), src / "gpu_debug_build_indirect.spv", "ComputeGPUDebugBuildIndirect",
                            sizeof(GPUDebugBuildIndirectPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gpu_debug_cluster_grid"), src / "gpu_debug_cluster_grid.spv", "ComputeGPUDebugClusterGrid",
                            sizeof(ClusterGridDebugPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gpu_debug_world_grid"), src / "gpu_debug_world_grid.spv", "ComputeGPUDebugWorldGrid",
                            sizeof(WorldGridDebugPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gpu_debug_radiance_cache"), src / "gpu_debug_radiance_cache.spv", "ComputeGPUDebugRadianceCache",
                            sizeof(RadianceCacheDebugPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("ddgi_probe_debug"), src / "ddgi_probe_debug.spv", "ComputeDDGIProbeDebug",
                            sizeof(DDGIProbeDebugPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gi_deconstruct"), src / "gi_deconstruct.spv", "ComputeGIDeconstruct",
                            sizeof(GIDeconstructPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("ddgi_probe_trace"), src / "ddgi_probe_trace.spv", "ComputeDDGIProbeTrace",
                            sizeof(DDGIProbeTracePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("ddgi_blend_irradiance"), src / "ddgi_blend_irradiance.spv", "ComputeDDGIBlendIrradiance",
                            sizeof(DDGIProbeBlendPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("ddgi_blend_visibility"), src / "ddgi_blend_visibility.spv", "ComputeDDGIBlendVisibility",
                            sizeof(DDGIProbeBlendPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("ddgi_probe_relocate"), src / "ddgi_probe_relocate.spv", "ComputeDDGIProbeRelocate",
                            sizeof(DDGIProbeRelocatePushConstant), PipelineCategory::Critical);


    RegisterComputePipeline(SID("rt_shadow_test"), src / "rt_shadow_test.spv", "ComputeRTShadowTest",
                            sizeof(RTShadowTestPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("rt_ground_truth_di"), src / "rt_ground_truth_di.spv", "ComputeRTGroundTruthDI",
                            sizeof(RTGroundTruthDIPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("rt_ground_truth_gi"), src / "rt_ground_truth_gi.spv", "ComputeRTGroundTruthGI",
                            sizeof(RTGroundTruthGIPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("rt_ground_truth_full"), src / "rt_ground_truth_full.spv", "ComputeRTGroundTruthFull",
                            sizeof(RTGroundTruthGIPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("rt_sun_shadow"), src / "rt_sun_shadow.spv", "ComputeRTSunShadow",
                            sizeof(RTSunShadowPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sigma_classify_tiles"), src / "sigma_classify_tiles.spv", "ComputeSigmaClassifyTiles",
                            sizeof(SigmaClassifyPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sigma_smooth_tiles"), src / "sigma_smooth_tiles.spv", "ComputeSigmaSmoothTiles",
                            sizeof(SigmaSmoothTilesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sigma_shadow_blur"), src / "sigma_shadow_blur.spv", "ComputeSigmaShadowBlur",
                            sizeof(SigmaBlurPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sigma_shadow_temporal"), src / "sigma_shadow_temporal.spv", "ComputeSigmaShadowTemporal",
                            sizeof(SigmaTemporalPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("shadows_resolve"), src / "shadows_resolve.spv", "ComputeShadowsResolve",
                            sizeof(ShadowsResolvePushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("taa_main"), src / "taa.spv", "ComputeTemporalAntialiasing",
                            sizeof(TemporalAntialiasingPushConstant), PipelineCategory::Legacy);
    RegisterComputePipeline(SID("taa_naive"), src / "taa_naive.spv", "ComputeNaiveTemporalAntialiasing",
                            sizeof(TemporalAntialiasingPushConstant), PipelineCategory::Legacy);
    RegisterComputePipeline(SID("taa_donut"), src / "taa_donut.spv", "ComputeDonutTaa",
                            sizeof(DonutTaaPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("smaa_luma_edge_detection"), src / "smaa.spv", "LumaEdgeDetectionMain",
                            sizeof(SmaaEdgeDetectionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_color_edge_detection"), src / "smaa.spv", "ColorEdgeDetectionMain",
                            sizeof(SmaaEdgeDetectionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_depth_edge_detection"), src / "smaa.spv", "DepthEdgeDetectionMain",
                            sizeof(SmaaEdgeDetectionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_blend_weight"), src / "smaa.spv", "BlendWeightMain",
                            sizeof(SmaaBlendWeightPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_neighborhood_blend"), src / "smaa.spv", "NeighborhoodBlendMain",
                            sizeof(SmaaNeighborhoodBlendPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_temporal_resolve"), src / "smaa.spv", "TemporalResolveMain",
                            sizeof(SmaaTemporalResolvePushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("depth_copy"), src / "depth_copy.spv", "ComputeDepthCopy",
                            sizeof(DepthCopyPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("color_copy"), src / "color_copy.spv", "ComputeColorCopy",
                            sizeof(ColorCopyPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("gtao_depth_prepass"), src / "gtao.spv", "ComputeGTAODepthPrepass",
                            sizeof(GTAODepthPrepassPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gtao_main"), src / "gtao.spv", "ComputeGTAOMain",
                            sizeof(GTAOMainPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gtao_denoise"), src / "gtao.spv", "ComputeGTAODenoise",
                            sizeof(GTAODenoisePushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("relax_generate_viewz"), src / "relax_generate_viewz.spv", "RelaxGenerateViewZMain",
                            sizeof(RelaxGenerateViewZPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_classify_tiles"), src / "relax_classify_tiles.spv", "RelaxClassifyTilesMain",
                            sizeof(RelaxClassifyTilesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_prepass"), src / "relax_prepass.spv", "RelaxPrepassMain",
                            sizeof(RelaxPrepassPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_temporal_accumulation"), src / "relax_temporal_accumulation.spv", "RelaxTemporalAccumulationMain",
                            sizeof(RelaxTemporalAccumulationPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_history_fix"), src / "relax_history_fix.spv", "RelaxHistoryFixMain",
                            sizeof(RelaxHistoryFixPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_history_clamping"), src / "relax_history_clamping.spv", "RelaxHistoryClampingMain",
                            sizeof(RelaxHistoryClampingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_atrous"), src / "relax_atrous.spv", "RelaxAtrousMain",
                            sizeof(RelaxAtrousPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_atrous_chroma"), src / "relax_atrous.spv", "RelaxAtrousChromaMain",
                            sizeof(RelaxAtrousPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_antifirefly"), src / "relax_antifirefly.spv", "RelaxAntiFireflyMain",
                            sizeof(RelaxAntiFireflyPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("nrd_prep_guides"), src / "nrd_prep.spv", "NrdPrepGuidesMain",
                            sizeof(NrdPrepGuidesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("nrd_radiance_copy"), src / "nrd_prep.spv", "NrdRadianceCopyMain",
                            sizeof(NrdRadianceCopyPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("nrd_reblur_radiance_pack"), src / "nrd_prep.spv", "NrdReblurRadiancePackMain",
                            sizeof(NrdReblurRadiancePackPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("nrd_reblur_output_copy"), src / "nrd_prep.spv", "NrdReblurOutputCopyMain",
                            sizeof(NrdRadianceCopyPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("reblur_pack"), src / "reblur_pack.spv", "ReblurPackMain",
                            sizeof(ReblurPackPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_generate_viewz"), src / "reblur_generate_viewz.spv", "ReblurGenerateViewZMain",
                            sizeof(ReblurGenerateViewZPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_classify_tiles"), src / "reblur_classify_tiles.spv", "ReblurClassifyTilesMain",
                            sizeof(ReblurClassifyTilesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_prepass"), src / "reblur_prepass.spv", "ReblurPrepassMain",
                            sizeof(ReblurPrepassPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_temporal_accumulation"), src / "reblur_temporal_accumulation.spv", "ReblurTemporalAccumulationMain",
                            sizeof(ReblurTemporalAccumulationPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_history_fix"), src / "reblur_history_fix.spv", "ReblurHistoryFixMain",
                            sizeof(ReblurHistoryFixPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_blur"), src / "reblur_blur.spv", "ReblurBlurMain",
                            sizeof(ReblurBlurPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_stabilization"), src / "reblur_stabilization.spv", "ReblurStabilizationMain",
                            sizeof(ReblurStabilizationPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_chroma"), src / "reblur_chroma.spv", "ReblurChromaMain",
                            sizeof(ReblurChromaPushConstant), PipelineCategory::Critical);


    RegisterComputePipeline(SID("exposure_build_histogram"), src / "exposure.spv", "ComputeExposureHistogram",
                            sizeof(HistogramBuildPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("exposure_calculate_average"), src / "exposure.spv", "ComputeExposureAverage",
                            sizeof(ExposureCalculatePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("post_process_finalize"), src / "post_process_finalize.spv", "ComputeFinalize",
                            sizeof(PostProcessFinalizePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("post_process_compose"), src / "post_process_compose.spv", "ComputeCompose",
                            sizeof(PostProcessComposePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("screen_fade"), src / "screen_fade.spv", "ComputeScreenFade",
                            sizeof(ScreenFadePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("motion_blur_velocity_extract"), src / "motion_blur.spv", "ComputeMotionBlurVelocityExtract",
                            sizeof(MotionBlurVelocityExtractPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("motion_blur_tile_max"), src / "motion_blur.spv", "ComputeMotionBlurTileMax",
                            sizeof(MotionBlurTileVelocityPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("motion_blur_neighbor_max"), src / "motion_blur.spv", "ComputeMotionBlurNeighborMax",
                            sizeof(MotionBlurNeighborMaxPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("motion_blur_reconstruction"), src / "motion_blur.spv", "ComputeMotionBlurReconstruction",
                            sizeof(MotionBlurReconstructionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("bloom_threshold"), src / "bloom.spv", "ComputeBloomThreshold",
                            sizeof(BloomThresholdPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("bloom_downsample"), src / "bloom.spv", "ComputeBloomDownsample",
                            sizeof(BloomDownsamplePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("bloom_upsample"), src / "bloom.spv", "ComputeBloomUpsample",
                            sizeof(BloomUpsamplePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("selection_outline"_sid, src / "selection_outline.spv", "ComputeSelectionOutline",
                            sizeof(SelectionOutlinePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("debug_visualize"), src / "debug_visualize.spv", "ComputeDebugVisualize",
                            sizeof(DebugVisualizePushConstant), PipelineCategory::Critical);

    VkDescriptorSetLayout proceduralTexLayout = resourceManager->proceduralTextureGenerateResources.descriptorSetLayout.handle;
    RegisterComputePipelineCustomLayout("yellow_texture"_sid, src / "yellow_texture.spv", "ComputeYellowTexture",
                                        sizeof(ProceduralTextureBasePushConstant), PipelineCategory::Critical, Core::Span(&proceduralTexLayout, 1));
    RegisterComputePipelineCustomLayout("domain_warp"_sid, src / "domain_warp.spv", "ComputeDomainWarp",
                                        sizeof(ProceduralTextureBasePushConstant), PipelineCategory::Critical, Core::Span(&proceduralTexLayout, 1));

#if WILL_EDITOR
    VkDescriptorSetLayout emapLayout = resourceManager->environmentMapGenerateResources.descriptorSetLayout.handle;
    RegisterComputePipelineCustomLayout(SID("ibl_equirect_to_cubemap"), src / "ibl_bake.spv", "ComputeEquirectToCubemap",
                                        sizeof(EquirectToCubemapPushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    RegisterComputePipelineCustomLayout(SID("ibl_convolve_diffuse"), src / "ibl_bake.spv", "ComputeConvolveDiffuse",
                                        sizeof(ConvolveDiffusePushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    RegisterComputePipelineCustomLayout(SID("ibl_prefilter_specular"), src / "ibl_bake.spv", "ComputePrefilterSpecular",
                                        sizeof(PrefilterSpecularPushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    VkDescriptorSetLayout brdfLutLayout = resourceManager->brdfLutGenerateResources.descriptorSetLayout.handle;
    RegisterComputePipelineCustomLayout(SID("ibl_brdf_lut"), src / "brdf_lut.spv", "ComputeBRDFLUT",
                                        sizeof(BRDFLUTPushConstant), PipelineCategory::AssetGeneration, Core::Span(&brdfLutLayout, 1));
#endif

    GraphicsPipelineBuilder builder;

    constexpr Core::Array<VkFormat, 2> graphicsColorFormats{
        VISIBILITY_BUFFER_FORMAT,
        GBUFFER_STABLE_ID_FORMAT,
    };

    // Visibility Buffer
    {
        builder.AddShaderStage(src / "geometry_visibility_buffer.spv", VK_SHADER_STAGE_MESH_BIT_EXT, "MeshGeometryVisibilityBuffer");
        builder.AddShaderStage(src / "geometry_visibility_buffer.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentGeometryVisibilityBuffer");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        builder.AddDynamicState(VK_DYNAMIC_STATE_POLYGON_MODE_EXT);

        builder.SetupRenderer(graphicsColorFormats.Data(), graphicsColorFormats.Size(), DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);

        RegisterGraphicsPipeline(
            SID("visibility_buffer_accumulate"),
            builder,
            sizeof(VisibilityBufferAccumulatePushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Portal Graphics Pipeline
    {
        builder.AddShaderStage(src / "geometry_visibility_buffer.spv", VK_SHADER_STAGE_MESH_BIT_EXT, "MeshGeometryVisibilityBuffer");
        builder.AddShaderStage(src / "geometry_visibility_buffer.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentGeometryVisibilityBuffer");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        builder.SetupStencilState(VK_TRUE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
        builder.SetupRenderer(graphicsColorFormats.Data(), graphicsColorFormats.Size(), DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);
        builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

        RegisterGraphicsPipeline(
            SID("portal_rendering"),
            builder,
            sizeof(VisibilityBufferAccumulatePushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Portal Composite
    {
        builder.AddShaderStage(src / "fullscreen_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT, "FullscreenPassVertexMain");
        builder.AddShaderStage(src / "portal_rendering.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentPortalComposite");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_ALWAYS);
        builder.SetupStencilState(VK_TRUE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL);

        VkFormat colorFormats[2] = {
            COLOR_ATTACHMENT_FORMAT,
            GBUFFER_TARGET_TWO
        };
        builder.SetupRenderer(colorFormats, 2, DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);
        builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

        RegisterGraphicsPipeline(
            SID("portal_composite"),
            builder,
            sizeof(PortalCompositePushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Skybox Rendering
    {
        builder.AddShaderStage(src / "fullscreen_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT, "FullscreenPassVertexMain");
        builder.AddShaderStage(src / "environment_map.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentSkybox");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);

        VkFormat colorFormats[1] = {
            COLOR_ATTACHMENT_FORMAT,
        };
        builder.SetupRenderer(colorFormats, 1, DEPTH_ATTACHMENT_FORMAT, VK_FORMAT_UNDEFINED);

        RegisterGraphicsPipeline(
            SID("environment_skybox"),
            builder,
            sizeof(EnvironmentSkyboxPushConstant),
            VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Sprites
    {
        builder.AddShaderStage(src / "sprites.spv", VK_SHADER_STAGE_MESH_BIT_EXT, "MeshSprites");
        builder.AddShaderStage(src / "sprites.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentSprites");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_FALSE, VK_COMPARE_OP_GREATER_OR_EQUAL);

        VkPipelineColorBlendAttachmentState colorBlend{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
        VkPipelineColorBlendAttachmentState stableIdBlend{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
        VkPipelineColorBlendAttachmentState blendStates[] = {colorBlend, stableIdBlend};
        builder.SetupBlending(blendStates, 2);

        VkFormat colorFormats[2] = {COLOR_ATTACHMENT_FORMAT, GBUFFER_STABLE_ID_FORMAT};
        builder.SetupRenderer(colorFormats, 2, DEPTH_ATTACHMENT_FORMAT);

        RegisterGraphicsPipeline(
            SID("sprites"),
            builder,
            sizeof(SpritePushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Default Text
    {
        builder.AddShaderStage(src / "text_default.spv", VK_SHADER_STAGE_MESH_BIT_EXT, "MeshTextDefault");
        builder.AddShaderStage(src / "text_default.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentTextDefault");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_FALSE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        VkPipelineColorBlendAttachmentState blendState{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        VkPipelineColorBlendAttachmentState stableIdBlendState{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
        VkPipelineColorBlendAttachmentState blendStates[] = {blendState, stableIdBlendState};
        builder.SetupBlending(blendStates, 2);

        VkFormat colorFormats[2] = {
            COLOR_ATTACHMENT_FORMAT,
            GBUFFER_STABLE_ID_FORMAT,
        };
        builder.SetupRenderer(colorFormats, 2, DEPTH_ATTACHMENT_FORMAT);

        RegisterGraphicsPipeline(
            SID("text_default"),
            builder,
            sizeof(TextRenderPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // UI Rect
    {
        builder.AddShaderStage(src / "ui_rect_default.spv", VK_SHADER_STAGE_VERTEX_BIT, "VertexUIRectDefault");
        builder.AddShaderStage(src / "ui_rect_default.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentUIRectDefault");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
        VkPipelineColorBlendAttachmentState uiRectBlend{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
        builder.SetupBlending(&uiRectBlend, 1);
        VkFormat colorFormats[1] = {COLOR_ATTACHMENT_FORMAT};
        builder.SetupRenderer(colorFormats, 1);
        RegisterGraphicsPipeline(
            SID("ui_rect_default"),
            builder,
            sizeof(UIRectRenderPushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // UI Image
    {
        builder.AddShaderStage(src / "ui_image_default.spv", VK_SHADER_STAGE_VERTEX_BIT, "VertexUIImageDefault");
        builder.AddShaderStage(src / "ui_image_default.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentUIImageDefault");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
        VkPipelineColorBlendAttachmentState blendState{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
        builder.SetupBlending(&blendState, 1);

        VkFormat colorFormats[1] = {COLOR_ATTACHMENT_FORMAT};
        builder.SetupRenderer(colorFormats, 1);

        RegisterGraphicsPipeline(
            SID("ui_image_default"),
            builder,
            sizeof(UIImagePushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // UI Text
    {
        builder.AddShaderStage(src / "ui_text_default.spv", VK_SHADER_STAGE_VERTEX_BIT, "VertexUITextDefault");
        builder.AddShaderStage(src / "ui_text_default.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentUITextDefault");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
        VkPipelineColorBlendAttachmentState uiTextBlend{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
        builder.SetupBlending(&uiTextBlend, 1);

        VkFormat colorFormats[1] = {COLOR_ATTACHMENT_FORMAT};
        builder.SetupRenderer(colorFormats, 1);

        RegisterGraphicsPipeline(
            SID("ui_text_default"),
            builder,
            sizeof(UITextRenderPushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // UI Border
    {
        builder.AddShaderStage(src / "ui_border_default.spv", VK_SHADER_STAGE_VERTEX_BIT, "VertexUIBorderDefault");
        builder.AddShaderStage(src / "ui_border_default.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentUIBorderDefault");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
        VkPipelineColorBlendAttachmentState uiBorderBlend{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
        builder.SetupBlending(&uiBorderBlend, 1);
        VkFormat colorFormats[1] = {COLOR_ATTACHMENT_FORMAT};
        builder.SetupRenderer(colorFormats, 1);
        RegisterGraphicsPipeline(
            SID("ui_border_default"),
            builder,
            sizeof(UIBorderPushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Debug Render
    {
        builder.AddShaderStage(src / "debug_render.spv", VK_SHADER_STAGE_MESH_BIT_EXT, "MeshDebugRender");
        builder.AddShaderStage(src / "debug_render.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentDebugRender");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_FALSE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        VkPipelineColorBlendAttachmentState blendState{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        builder.SetupBlending(&blendState, 1);

        VkFormat colorFormats[1] = {
            COLOR_ATTACHMENT_FORMAT,
        };
        builder.SetupRenderer(colorFormats, 1, DEPTH_ATTACHMENT_FORMAT);

        RegisterGraphicsPipeline(
            SID("debug_render"),
            builder,
            sizeof(DebugDrawPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // GPU Debug Render (indirect, GPU-appended segments)
    {
        builder.AddShaderStage(src / "debug_render.spv", VK_SHADER_STAGE_MESH_BIT_EXT, "MeshDebugRenderGPU");
        builder.AddShaderStage(src / "debug_render.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentDebugRender");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_FALSE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        VkPipelineColorBlendAttachmentState blendState{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        builder.SetupBlending(&blendState, 1);

        VkFormat colorFormats[1] = {
            COLOR_ATTACHMENT_FORMAT,
        };
        builder.SetupRenderer(colorFormats, 1, DEPTH_ATTACHMENT_FORMAT);

        RegisterGraphicsPipeline(
            SID("debug_render_gpu"),
            builder,
            sizeof(GPUDebugDrawPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Debug Sphere Render (indirect, GPU-appended instances)
    {
        builder.AddShaderStage(src / "debug_sphere.spv", VK_SHADER_STAGE_VERTEX_BIT, "VertexDebugSphere");
        builder.AddShaderStage(src / "debug_sphere.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentDebugSphere");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        VkPipelineColorBlendAttachmentState blendState{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        builder.SetupBlending(&blendState, 1);

        VkFormat colorFormats[1] = {
            COLOR_ATTACHMENT_FORMAT,
        };
        builder.SetupRenderer(colorFormats, 1, DEPTH_ATTACHMENT_FORMAT);

        RegisterGraphicsPipeline(
            SID("debug_sphere"),
            builder,
            sizeof(GPUDebugSphereDrawPushConstant),
            VK_SHADER_STAGE_VERTEX_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Reflection probe preview sphere (editor, single instance, cubemap-shaded)
    {
        builder.AddShaderStage(src / "probe_preview_sphere.spv", VK_SHADER_STAGE_VERTEX_BIT, "VertexProbePreviewSphere");
        builder.AddShaderStage(src / "probe_preview_sphere.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentProbePreviewSphere");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        VkPipelineColorBlendAttachmentState blendState{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        builder.SetupBlending(&blendState, 1);

        VkFormat colorFormats[1] = {
            COLOR_ATTACHMENT_FORMAT,
        };
        builder.SetupRenderer(colorFormats, 1, DEPTH_ATTACHMENT_FORMAT);

        RegisterGraphicsPipeline(
            SID("probe_preview_sphere"),
            builder,
            sizeof(ProbePreviewSpherePushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Debug Cube Render (indirect, GPU-appended instances)
    {
        builder.AddShaderStage(src / "debug_cube.spv", VK_SHADER_STAGE_VERTEX_BIT, "VertexDebugCube");
        builder.AddShaderStage(src / "debug_cube.spv", VK_SHADER_STAGE_FRAGMENT_BIT, "FragmentDebugCube");
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        VkPipelineColorBlendAttachmentState cubeBlendState{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        builder.SetupBlending(&cubeBlendState, 1);

        VkFormat cubeColorFormats[1] = {
            COLOR_ATTACHMENT_FORMAT,
        };
        builder.SetupRenderer(cubeColorFormats, 1, DEPTH_ATTACHMENT_FORMAT);

        RegisterGraphicsPipeline(
            SID("debug_cube"),
            builder,
            sizeof(GPUDebugCubeDrawPushConstant),
            VK_SHADER_STAGE_VERTEX_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    LogRegistrationSummary();
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

#ifdef ENABLE_VULKAN_VALIDATION
static void AppendPipelineExecutableStats(VkDevice device, VkPipeline pipeline, const char* label, Core::String& out)
{
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }

    VkPipelineInfoKHR pipelineInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
    pipelineInfo.pipeline = pipeline;

    uint32_t execCount = 0;
    vkGetPipelineExecutablePropertiesKHR(device, &pipelineInfo, &execCount, nullptr);
    if (execCount == 0) {
        return;
    }
    if (execCount > 8) {
        execCount = 8;
    }

    Core::InlineVector<VkPipelineExecutablePropertiesKHR, 8> execProps;
    for (uint32_t i = 0; i < execCount; ++i) {
        execProps.PushBack(VkPipelineExecutablePropertiesKHR{.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
    }
    vkGetPipelineExecutablePropertiesKHR(device, &pipelineInfo, &execCount, execProps.Data());

    out.Append(Core::InlineString<512>::Format("\n=== %s ===\n", label).View());

    for (uint32_t e = 0; e < execCount; ++e) {
        const VkPipelineExecutablePropertiesKHR& ep = execProps[e];
        out.Append(Core::InlineString<512>::Format("  [%s] subgroupSize=%u\n", ep.name, ep.subgroupSize).View());

        VkPipelineExecutableInfoKHR execInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
        execInfo.pipeline = pipeline;
        execInfo.executableIndex = e;

        uint32_t statCount = 0;
        vkGetPipelineExecutableStatisticsKHR(device, &execInfo, &statCount, nullptr);
        if (statCount == 0) {
            continue;
        }
        if (statCount > 64) {
            statCount = 64;
        }

        Core::InlineVector<VkPipelineExecutableStatisticKHR, 64> stats;
        for (uint32_t i = 0; i < statCount; ++i) {
            stats.PushBack(VkPipelineExecutableStatisticKHR{.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
        }
        vkGetPipelineExecutableStatisticsKHR(device, &execInfo, &statCount, stats.Data());

        for (uint32_t s = 0; s < statCount; ++s) {
            const VkPipelineExecutableStatisticKHR& st = stats[s];
            switch (st.format) {
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                out.Append(Core::InlineString<512>::Format("    %-40s = %s\n", st.name, st.value.b32 ? "true" : "false").View());
                break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                out.Append(Core::InlineString<512>::Format("    %-40s = %lld\n", st.name, static_cast<long long>(st.value.i64)).View());
                break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                out.Append(Core::InlineString<512>::Format("    %-40s = %llu\n", st.name, static_cast<unsigned long long>(st.value.u64)).View());
                break;
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                out.Append(Core::InlineString<512>::Format("    %-40s = %f\n", st.name, st.value.f64).View());
                break;
            default:
                break;
            }
        }
    }
}

void PipelineManager::DumpExecutableStats(const Core::Path& outputPath)
{
    if (!context->bPipelineExecutablePropertiesEnabled) {
        LOG_WARN(Renderer, "DumpExecutableStats: VK_KHR_pipeline_executable_properties not enabled");
        return;
    }

    Core::String report(renderAlloc, Core::AllocTag::Render, "Pipeline Executable Statistics\n");

    for (auto [name, pipeline] : computePipelines) {
        AppendPipelineExecutableStats(context->device, pipeline.activeEntry.pipeline, name.ToString(), report);
    }
    for (auto [name, pipeline] : graphicsPipelines) {
        AppendPipelineExecutableStats(context->device, pipeline.activeEntry.pipeline, name.ToString(), report);
    }

    if (Platform::WriteFile(outputPath, report.View())) {
        LOG_INFO(Renderer, "Pipeline executable stats written to {}", outputPath.c_str());
    }
    else {
        LOG_ERROR(Renderer, "Failed to write pipeline executable stats to {}", outputPath.c_str());
    }
}
#endif
} // Render
