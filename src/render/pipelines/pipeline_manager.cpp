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

    registeredComputeCount++;
    if constexpr (VERBOSE_PIPELINE_REGISTRATION) {
        LOG_INFO(Renderer, "Registered compute pipeline: {}", pipelineId.ToString());
    }
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

    RegisterComputePipeline(SID("instancing_instance_lod"), src / "instancing_instance_lod_compute.spv",
                            sizeof(InstanceLODPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_prefix_sum_up_1"), src / "instancing_prefix_sum_up_1_compute.spv",
                            sizeof(PrefixSumUpsweep1PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_prefix_sum_up_2"), src / "instancing_prefix_sum_up_2_compute.spv",
                            sizeof(PrefixSumUpsweep2PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_scan_blocks"), src / "instancing_scan_blocks_compute.spv",
                            sizeof(PrefixSumScanBlocksPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_prefix_sum_down_1"), src / "instancing_prefix_sum_down_1_compute.spv",
                            sizeof(PrefixSumDownsweep1PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_prefix_sum_down_2"), src / "instancing_prefix_sum_down_2_compute.spv",
                            sizeof(PrefixSumDownsweep2PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_total_meshlet_count"), src / "instancing_total_meshlet_count_compute.spv",
                            sizeof(TotalMeshletCountPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_expand_instance_to_meshlet"), src / "instancing_expand_instance_to_meshlet_compute.spv",
                            sizeof(ExpandMeshletsPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_meshlet_visibility_prefix_sum_up_1"), src / "instancing_meshlet_visibility_prefix_sum_up_1_compute.spv",
                            sizeof(MeshletVisibilityPrefixSumUpsweep1PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_meshlet_visibility_prefix_sum_down_2"), src / "instancing_meshlet_visibility_prefix_sum_down_2_compute.spv",
                            sizeof(MeshletVisibilityPrefixSumDownsweep2PushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_compacted_meshlet_dispatch"), src / "instancing_compacted_meshlet_dispatch_compute.spv",
                            sizeof(CompactedMeshletDispatchPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("instancing_max_meshlet_count"), src / "instancing_max_meshlet_count_compute.spv",
                            sizeof(MaxMeshletCountPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline("visibility_buffer_barycentric_derivative"_sid, src / "visibility_buffer_barycentric_derivative_compute.spv",
                            sizeof(VisibilityBufferResolvePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("visibility_bucketing_bounds_calculation"_sid, src / "visibility_bucketing_bounds_calculation_compute.spv",
                            sizeof(ShadeBucketingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("visibility_shading_bucketing_resolve"_sid, src / "visibility_shading_bucketing_resolve_compute.spv",
                            sizeof(ShadeBucketingResolvePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("visibility_lighting_bucketing_resolve"_sid, src / "visibility_lighting_bucketing_resolve_compute.spv",
                            sizeof(LightingBucketingResolvePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("bucketing_dispatch_count"_sid, src / "bucketing_dispatch_count_compute.spv",
                            sizeof(BucketDispatchCountPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline("shading_bucket_visualize"_sid, src / "shading_bucket_visualize_compute.spv",
                            sizeof(VisibilityShadingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("lighting_bucket_visualize"_sid, src / "lighting_bucket_visualize_compute.spv",
                            sizeof(LightingBucketVisualizePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("default_lit"_sid, src / "shading_default_lit_compute.spv",
                            sizeof(VisibilityShadingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline("error_unlit"_sid, src / "shading_error_unlit_compute.spv",
                            sizeof(VisibilityShadingPushConstant), PipelineCategory::Critical);

    shadingPipelines.PushBack("shading_bucket_visualize"_sid);
    shadingPipelines.PushBack("default_lit"_sid);
    shadingPipelines.PushBack("error_unlit"_sid);


    RegisterComputePipeline(SID("restir_transform_lights"), src / "restir_di_transform_lights_compute.spv",
                            sizeof(ReSTIRTransformLightsPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("regir_touch"), src / "regir_touch_compute.spv",
                            sizeof(ReGIRTouchPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("regir_build_indirect"), src / "regir_build_indirect_compute.spv",
                            sizeof(ReGIRBuildIndirectPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("regir_presample_tiles"), src / "regir_presample_tiles_compute.spv",
                            sizeof(ReGIRPresampleTilesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("regir_fill"), src / "regir_fill_compute.spv",
                            sizeof(ReGIRFillPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_di_spatial"), src / "restir_di_spatial_compute.spv",
                            sizeof(ReSTIRDISpatialPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_di_base_regir"), src / "restir_di_base_regir_compute.spv",
                            sizeof(ReSTIRDICombinedTemporalPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_di_temporal"), src / "restir_di_temporal_compute.spv",
                            sizeof(ReSTIRDICombinedTemporalPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_boiling_filter"), src / "restir_boiling_filter_compute.spv",
                            sizeof(ReSTIRBoilingFilterPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_confidence_gradient"), src / "restir_confidence_gradient_compute.spv",
                            sizeof(ReSTIRConfidenceGradientPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_confidence_resolve"), src / "restir_confidence_resolve_compute.spv",
                            sizeof(ReSTIRConfidenceResolvePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("restir_remodulate"), src / "restir_remodulate_compute.spv",
                            sizeof(ReSTIRRemodulatePushConstant), PipelineCategory::Critical);

    RegisterComputePipeline("default_pbr"_sid, src / "lighting_pbr_compute.spv",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);
    lightingPipelines.PushBack("default_pbr"_sid);
    RegisterComputePipeline("default_pbr_restir"_sid, src / "lighting_pbr_restir_compute.spv",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);
    lightingPipelines.PushBack("default_pbr_restir"_sid);
    RegisterComputePipeline("default_toon"_sid, src / "lighting_toon_compute.spv",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);
    lightingPipelines.PushBack("default_toon"_sid);
    RegisterComputePipeline("default_unlit"_sid, src / "lighting_unlit_compute.spv",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);
    lightingPipelines.PushBack("default_unlit"_sid);
    RegisterComputePipeline(SID("lighting_ground_truth"), src / "lighting_ground_truth_compute.spv",
                            sizeof(VisibilityLightingPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("directional_light"), src / "directional_light_compute.spv",
                            sizeof(DirectionalLightPushConstant), PipelineCategory::Critical);


    RegisterComputePipeline(SID("rt_shadow_test"), src / "rt_shadow_test_compute.spv",
                            sizeof(RTShadowTestPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("rt_ground_truth_di"), src / "rt_ground_truth_di_compute.spv",
                            sizeof(RTGroundTruthDIPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("rt_sun_shadow"), src / "rt_sun_shadow_compute.spv",
                            sizeof(RTSunShadowPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sigma_classify_tiles"), src / "sigma_classify_tiles_compute.spv",
                            sizeof(SigmaClassifyPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sigma_smooth_tiles"), src / "sigma_smooth_tiles_compute.spv",
                            sizeof(SigmaSmoothTilesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sigma_shadow_blur"), src / "sigma_shadow_blur_compute.spv",
                            sizeof(SigmaBlurPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sigma_shadow_temporal"), src / "sigma_shadow_temporal_compute.spv",
                            sizeof(SigmaTemporalPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("shadows_resolve"), src / "shadows_resolve_compute.spv",
                            sizeof(ShadowsResolvePushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("temporal_antialiasing"), src / "temporal_antialiasing_compute.spv",
                            sizeof(TemporalAntialiasingPushConstant), PipelineCategory::Legacy);
    RegisterComputePipeline(SID("naive_temporal_antialiasing"), src / "naive_temporal_antialiasing_compute.spv",
                            sizeof(TemporalAntialiasingPushConstant), PipelineCategory::Legacy);
    RegisterComputePipeline(SID("donut_taa"), src / "donut_taa_compute.spv",
                            sizeof(DonutTaaPushConstant), PipelineCategory::Legacy);

    RegisterComputePipeline(SID("smaa_luma_edge_detection"), src / "smaa_luma_edge_detection_compute.spv",
                            sizeof(SmaaEdgeDetectionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_color_edge_detection"), src / "smaa_color_edge_detection_compute.spv",
                            sizeof(SmaaEdgeDetectionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_depth_edge_detection"), src / "smaa_depth_edge_detection_compute.spv",
                            sizeof(SmaaEdgeDetectionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_blend_weight"), src / "smaa_blend_weight_compute.spv",
                            sizeof(SmaaBlendWeightPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_neighborhood_blend"), src / "smaa_neighborhood_blend_compute.spv",
                            sizeof(SmaaNeighborhoodBlendPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("smaa_temporal_resolve"), src / "smaa_temporal_resolve_compute.spv",
                            sizeof(SmaaTemporalResolvePushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("depth_copy"), src / "depth_copy_compute.spv",
                            sizeof(DepthCopyPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("gtao_depth_prepass"), src / "gtao_depth_prepass_compute.spv",
                            sizeof(GTAODepthPrepassPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gtao_main"), src / "gtao_main_compute.spv",
                            sizeof(GTAOMainPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("gtao_denoise"), src / "gtao_denoise_compute.spv",
                            sizeof(GTAODenoisePushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("relax_generate_viewz"), src / "relax_generate_viewz_compute.spv",
                            sizeof(RelaxGenerateViewZPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_classify_tiles"), src / "relax_classify_tiles_compute.spv",
                            sizeof(RelaxClassifyTilesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_prepass"), src / "relax_prepass_compute.spv",
                            sizeof(RelaxPrepassPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_temporal_accumulation"), src / "relax_temporal_accumulation_compute.spv",
                            sizeof(RelaxTemporalAccumulationPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_history_fix"), src / "relax_history_fix_compute.spv",
                            sizeof(RelaxHistoryFixPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_history_clamping"), src / "relax_history_clamping_compute.spv",
                            sizeof(RelaxHistoryClampingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_atrous"), src / "relax_atrous_compute.spv",
                            sizeof(RelaxAtrousPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("relax_antifirefly"), src / "relax_antifirefly_compute.spv",
                            sizeof(RelaxAntiFireflyPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline(SID("reblur_pack"), src / "reblur_pack_compute.spv",
                            sizeof(ReblurPackPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_generate_viewz"), src / "reblur_generate_viewz_compute.spv",
                            sizeof(ReblurGenerateViewZPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_classify_tiles"), src / "reblur_classify_tiles_compute.spv",
                            sizeof(ReblurClassifyTilesPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_prepass"), src / "reblur_prepass_compute.spv",
                            sizeof(ReblurPrepassPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_temporal_accumulation"), src / "reblur_temporal_accumulation_compute.spv",
                            sizeof(ReblurTemporalAccumulationPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_history_fix"), src / "reblur_history_fix_compute.spv",
                            sizeof(ReblurHistoryFixPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_blur"), src / "reblur_blur_compute.spv",
                            sizeof(ReblurBlurPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("reblur_stabilization"), src / "reblur_stabilization_compute.spv",
                            sizeof(ReblurStabilizationPushConstant), PipelineCategory::Critical);


    RegisterComputePipeline(SID("exposure_build_histogram"), src / "exposure_build_histogram_compute.spv",
                            sizeof(HistogramBuildPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("exposure_calculate_average"), src / "exposure_calculate_average_compute.spv",
                            sizeof(ExposureCalculatePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("tonemap_sdr"), src / "tonemap_sdr_compute.spv",
                            sizeof(TonemapSDRPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("motion_blur_tile_max"), src / "motion_blur_tile_max_compute.spv",
                            sizeof(MotionBlurTileVelocityPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("motion_blur_neighbor_max"), src / "motion_blur_neighbor_max_compute.spv",
                            sizeof(MotionBlurNeighborMaxPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("motion_blur_reconstruction"), src / "motion_blur_reconstruction_compute.spv",
                            sizeof(MotionBlurReconstructionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("bloom_threshold"), src / "bloom_threshold_compute.spv",
                            sizeof(BloomThresholdPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("bloom_downsample"), src / "bloom_downsample_compute.spv",
                            sizeof(BloomDownsamplePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("bloom_upsample"), src / "bloom_upsample_compute.spv",
                            sizeof(BloomUpsamplePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("vignette_aberration"), src / "vignette_aberration_compute.spv",
                            sizeof(VignetteChromaticAberrationPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("film_grain"), src / "film_grain_compute.spv",
                            sizeof(FilmGrainPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("sharpening"), src / "sharpening_compute.spv",
                            sizeof(SharpeningPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("color_grading"), src / "color_grading_compute.spv",
                            sizeof(ColorGradingPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("panini_projection"), src / "panini_projection_compute.spv",
                            sizeof(PaniniProjectionPushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("dither"), src / "dither_compute.spv",
                            sizeof(DitherPushConstant), PipelineCategory::Critical);

    RegisterComputePipeline("selection_outline"_sid, src / "selection_outline_compute.spv",
                            sizeof(SelectionOutlinePushConstant), PipelineCategory::Critical);
    RegisterComputePipeline(SID("debug_visualize"), src / "debug_visualize_compute.spv",
                            sizeof(DebugVisualizePushConstant), PipelineCategory::Critical);

    VkDescriptorSetLayout proceduralTexLayout = resourceManager->proceduralTextureGenerateResources.descriptorSetLayout.handle;
    RegisterComputePipelineCustomLayout("yellow_texture"_sid, src / "yellow_texture_compute.spv",
                                        sizeof(ProceduralTextureBasePushConstant), PipelineCategory::Critical, Core::Span(&proceduralTexLayout, 1));
    RegisterComputePipelineCustomLayout("domain_warp"_sid, src / "domain_warp_compute.spv",
                                        sizeof(ProceduralTextureBasePushConstant), PipelineCategory::Critical, Core::Span(&proceduralTexLayout, 1));

#if WILL_EDITOR
    VkDescriptorSetLayout emapLayout = resourceManager->environmentMapGenerateResources.descriptorSetLayout.handle;
    RegisterComputePipelineCustomLayout(SID("ibl_equirect_to_cubemap"), src / "ibl_equirect_to_cubemap_compute.spv",
                                        sizeof(EquirectToCubemapPushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    RegisterComputePipelineCustomLayout(SID("ibl_convolve_diffuse"), src / "ibl_convolve_diffuse_compute.spv",
                                        sizeof(ConvolveDiffusePushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    RegisterComputePipelineCustomLayout(SID("ibl_prefilter_specular"), src / "ibl_prefilter_specular_compute.spv",
                                        sizeof(PrefilterSpecularPushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    VkDescriptorSetLayout brdfLutLayout = resourceManager->brdfLutGenerateResources.descriptorSetLayout.handle;
    RegisterComputePipelineCustomLayout(SID("ibl_brdf_lut"), src / "brdf_lut_generate_compute.spv",
                                        sizeof(BRDFLUTPushConstant), PipelineCategory::AssetGeneration, Core::Span(&brdfLutLayout, 1));
#endif

    GraphicsPipelineBuilder builder;

    constexpr Core::Array<VkFormat, 2> graphicsColorFormats{
        VISIBILITY_BUFFER_FORMAT,
        GBUFFER_STABLE_ID_FORMAT,
    };

    // Visibility Buffer
    {
        builder.AddShaderStage(src / "visibility_buffer_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(src / "visibility_buffer_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "visibility_buffer_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(src / "visibility_buffer_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "fullscreen_pass_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage(src / "portal_composite_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "fullscreen_pass_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage(src / "environment_map_skybox_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "sprites_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(src / "sprites_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "default_text_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(src / "default_text_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
            SID("default_text"),
            builder,
            sizeof(TextRenderPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // UI Rect
    {
        builder.AddShaderStage(src / "ui_rect_default_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage(src / "ui_rect_default_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "ui_image_default_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage(src / "ui_image_default_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "ui_text_default_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage(src / "ui_text_default_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "ui_border_default_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage(src / "ui_border_default_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
        builder.AddShaderStage(src / "debug_render_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(src / "debug_render_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
