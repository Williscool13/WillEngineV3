//
// Created by William on 2026-06-03.
//

#include "render/passes/anti_aliasing_passes.h"

#include <tracy/Tracy.hpp>

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
StringID SetupSubpixelMorphologicalAntiAliasing(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent,
                                                const RenderTargets& targets)
{
    ZoneScoped;
    graph.CreateTexture("smaa_edges"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture("smaa_blend"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture("smaa_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    const Core::SMAAConfiguration& smaaConfig = viewFamily.aaConfig.smaa;

    // Pass 1: Edge Detection
    RenderPass& edgePass = graph.AddPass("SMAA Edge Detection"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    edgePass.ReadBuffer("scene_data"_sid);
    edgePass.ReadSampledImage(targets.colorOutput);
    edgePass.ReadSampledImage(targets.depthCopy);
    edgePass.WriteStorageImage("smaa_edges"_sid);
    edgePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy,
            smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaEdgeDetectionPushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputEdgeIndex = graph.GetStorageImageViewDescriptorIndex("smaa_edges"_sid),
                .threshold = smaaConfig.threshold,
                .localContrastAdaptation = smaaConfig.localContrastAdaptation,
            };

            StringID pipelineID;
            switch (smaaConfig.edgeDetectionMode) {
                case Core::SMAAEdgeDetectionMode::Color: pipelineID = "smaa_color_edge_detection"_sid;
                    break;
                case Core::SMAAEdgeDetectionMode::Depth: pipelineID = "smaa_depth_edge_detection"_sid;
                    break;
                default: pipelineID = "smaa_luma_edge_detection"_sid;
                    break;
            }

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(pipelineID);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaEdgeDetectionPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    // Pass 2: Blend Weight Calculation
    RenderPass& blendPass = graph.AddPass("SMAA Blend Weight"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    blendPass.ReadBuffer("scene_data"_sid);
    blendPass.ReadSampledImage("smaa_edges"_sid);
    blendPass.WriteStorageImage("smaa_blend"_sid);
    blendPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        SmaaBlendWeightPushConstant pushData{
            .sceneData = graph.GetBufferAddress("scene_data"_sid),
            .edgeIndex = graph.GetSampledImageViewDescriptorIndex("smaa_edges"_sid),
            .outputBlendIndex = graph.GetStorageImageViewDescriptorIndex("smaa_blend"_sid),
            .maxSearchSteps = smaaConfig.maxSearchSteps,
            .maxSearchStepsDiag = smaaConfig.maxSearchStepsDiag,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_blend_weight"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaBlendWeightPushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Pass 3: Neighborhood Blending
    RenderPass& neighborhoodPass = graph.AddPass("SMAA Neighborhood Blend"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    neighborhoodPass.ReadBuffer("scene_data"_sid);
    neighborhoodPass.ReadSampledImage(targets.colorOutput);
    neighborhoodPass.ReadSampledImage("smaa_blend"_sid);
    neighborhoodPass.WriteStorageImage("smaa_output"_sid);
    neighborhoodPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaNeighborhoodBlendPushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .blendWeightIndex = graph.GetSampledImageViewDescriptorIndex("smaa_blend"_sid),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("smaa_output"_sid),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_neighborhood_blend"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaNeighborhoodBlendPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return "smaa_output"_sid;
}

StringID SetupSMAA_T2X(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets)
{
    ZoneScoped;
    graph.CreateTexture("smaa_edges"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture("smaa_blend"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateVersionedTexture("smaa_t2x_current"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT, false, CLEAR_COLOR_EMPTY);

    const Core::SMAAConfiguration& smaaConfig = viewFamily.aaConfig.smaa;

    // Pass 1: Edge Detection
    RenderPass& edgePass = graph.AddPass("SMAA T2X Edge Detection"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    edgePass.ReadBuffer("scene_data"_sid);
    edgePass.ReadSampledImage(targets.colorOutput);
    edgePass.ReadSampledImage(targets.depthCopy);
    edgePass.WriteStorageImage("smaa_edges"_sid);
    edgePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy,
            smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaEdgeDetectionPushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputEdgeIndex = graph.GetStorageImageViewDescriptorIndex("smaa_edges"_sid),
                .threshold = smaaConfig.threshold,
                .localContrastAdaptation = smaaConfig.localContrastAdaptation,
            };

            StringID pipelineID;
            switch (smaaConfig.edgeDetectionMode) {
                case Core::SMAAEdgeDetectionMode::Color: pipelineID = "smaa_color_edge_detection"_sid;
                    break;
                case Core::SMAAEdgeDetectionMode::Depth: pipelineID = "smaa_depth_edge_detection"_sid;
                    break;
                default: pipelineID = "smaa_luma_edge_detection"_sid;
                    break;
            }

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(pipelineID);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaEdgeDetectionPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    // Pass 2: Blend Weight Calculation
    RenderPass& blendPass = graph.AddPass("SMAA T2X Blend Weight"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    blendPass.ReadBuffer("scene_data"_sid);
    blendPass.ReadSampledImage("smaa_edges"_sid);
    blendPass.WriteStorageImage("smaa_blend"_sid);
    blendPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        SmaaBlendWeightPushConstant pushData{
            .sceneData = graph.GetBufferAddress("scene_data"_sid),
            .edgeIndex = graph.GetSampledImageViewDescriptorIndex("smaa_edges"_sid),
            .outputBlendIndex = graph.GetStorageImageViewDescriptorIndex("smaa_blend"_sid),
            .maxSearchSteps = smaaConfig.maxSearchSteps,
            .maxSearchStepsDiag = smaaConfig.maxSearchStepsDiag,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_blend_weight"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaBlendWeightPushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Pass 3: Neighborhood Blending
    RenderPass& neighborhoodPass = graph.AddPass("SMAA T2X Neighborhood Blend"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    neighborhoodPass.ReadBuffer("scene_data"_sid);
    neighborhoodPass.ReadSampledImage(targets.colorOutput);
    neighborhoodPass.ReadSampledImage("smaa_blend"_sid);
    neighborhoodPass.WriteStorageImage("smaa_t2x_current"_sid);
    neighborhoodPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaNeighborhoodBlendPushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .blendWeightIndex = graph.GetSampledImageViewDescriptorIndex("smaa_blend"_sid),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("smaa_t2x_current"_sid),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_neighborhood_blend"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaNeighborhoodBlendPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    if (!graph.ResourceHasVersion("smaa_t2x_current"_sid, 1)) {
        return "smaa_t2x_current"_sid;
    }

    // Pass 4: Temporal Resolve
    graph.CreateTexture("smaa_t2x_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& resolvePass = graph.AddPass("SMAA T2X Temporal Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    resolvePass.ReadBuffer("scene_data"_sid);
    resolvePass.ReadSampledImage("smaa_t2x_current"_sid);
    resolvePass.ReadSampledImage(graph.ResourceVersionID("smaa_t2x_current"_sid, 1));
    resolvePass.ReadSampledImage(targets.gbufferOne);
    resolvePass.WriteStorageImage("smaa_t2x_output"_sid);
    resolvePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            gbufferOne = targets.gbufferOne, historyId = graph.ResourceVersionID("smaa_t2x_current"_sid, 1)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaTemporalResolvePushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .currentColorIndex = graph.GetSampledImageViewDescriptorIndex("smaa_t2x_current"_sid),
                .previousColorIndex = graph.GetSampledImageViewDescriptorIndex(historyId),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("smaa_t2x_output"_sid),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_temporal_resolve"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaTemporalResolvePushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return "smaa_t2x_output"_sid;
}

StringID SetupTemporalAntiAliasing(RenderGraph& graph,
                                   PipelineManager* pipelineManager,
                                   const Core::ViewFamily& viewFamily,
                                   Core::Array<uint32_t, 2> renderExtent,
                                   const RenderTargets& targets,
                                   StringID pipelineSID)
{
    ZoneScoped;
    graph.CreateVersionedTexture("taa_current"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT, false, CLEAR_COLOR_EMPTY);

    const StringID depthHistory = graph.ResourceVersionID(targets.depthCopy, 1);
    const StringID gbufferOneHistory = graph.ResourceVersionID(targets.gbufferOne, 1);

    if (!graph.ResourceHasVersion("taa_current"_sid, 1) || !graph.ResourceHasVersion(targets.gbufferOne, 1) || !graph.ResourceHasVersion(targets.depthCopy, 1)) {
        RenderPass& taaPass = graph.AddPass("TAA Copy Deferred"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::AntiAliasing);
        taaPass.ReadCopyImage(targets.colorOutput);
        taaPass.WriteCopyImage("taa_current"_sid);
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], outputColor = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkImage drawImage = graph.GetImageHandle(outputColor);
            VkImage taaImage = graph.GetImageHandle("taa_current"_sid);

            VkImageCopy2 copyRegion{};
            copyRegion.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent = {width, height, 1};

            VkCopyImageInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
            copyInfo.srcImage = drawImage;
            copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyInfo.dstImage = taaImage;
            copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &copyRegion;

            vkCmdCopyImage2(cmd, &copyInfo);
        });
        return targets.colorOutput;
    }

    // taa_current doubles as next frame's history, so downstream passes get their own copy written by the same dispatch
    graph.CreateTexture("taa_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    const Core::TAAConfiguration& taaConfig = viewFamily.aaConfig.taa;

    const bool bExposure = viewFamily.postProcessConfig.bExposureEnabled && graph.HasBuffer("luminance_buffer"_sid);
    const float exposureTarget = bExposure ? viewFamily.postProcessConfig.exposureTargetLuminance : 0.0f;

    RenderPass& taaPass = graph.AddPass("TAA Main"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    taaPass.ReadBuffer("scene_data"_sid);
    if (bExposure) {
        taaPass.ReadBuffer("luminance_buffer"_sid);
    }
    taaPass.ReadSampledImage(targets.colorOutput);
    taaPass.ReadSampledImage(targets.depthCopy);
    taaPass.ReadSampledImage(depthHistory);
    taaPass.ReadSampledImage(graph.ResourceVersionID("taa_current"_sid, 1));
    taaPass.ReadSampledImage(targets.gbufferOne);
    taaPass.ReadSampledImage(gbufferOneHistory);
    taaPass.WriteStorageImage("taa_current"_sid);
    taaPass.WriteStorageImage("taa_output"_sid);
    taaPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy,
            gbufferOne = targets.gbufferOne, pipelineSID, taaConfig, bExposure, exposureTarget,
            depthHistory, gbufferOneHistory,
            historyId = graph.ResourceVersionID("taa_current"_sid, 1)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            TemporalAntialiasingPushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .depthHistoryIndex = graph.GetSampledImageViewDescriptorIndex(depthHistory),
                .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex(historyId),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferOneHistoryIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory),
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex("taa_current"_sid),
                .outputCopyIndex = graph.GetStorageImageViewDescriptorIndex("taa_output"_sid),
                .baseBlendAlpha = taaConfig.baseBlendAlpha,
                .disocclusionThreshold = taaConfig.disocclusionThreshold,
                .varianceGammaLuma = taaConfig.varianceGammaLuma,
                .varianceGammaChroma = taaConfig.varianceGammaChroma,
                .karisStrength = taaConfig.karisStrength,
                .invalidHistoryBlend = taaConfig.invalidHistoryBlend,
                .lumaBoostCap = taaConfig.lumaBoostCap,
                .grazingTurnoverStrength = taaConfig.grazingTurnoverStrength,
                .exposureLuminance = bExposure ? graph.GetBufferAddress("luminance_buffer"_sid) : 0,
                .exposureTarget = exposureTarget,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(pipelineSID);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TemporalAntialiasingPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return "taa_output"_sid;
}

StringID SetupDonutTemporalAntiAliasing(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> inputExtent,
                                        Core::Array<uint32_t, 2> outputExtent,
                                        const RenderTargets& targets)
{
    ZoneScoped;
    graph.CreateVersionedTexture("donut_taa_feedback"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, outputExtent[0], outputExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT, false, CLEAR_COLOR_EMPTY);
    graph.CreateTexture("donut_taa_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, outputExtent[0], outputExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    const bool bHasHistory = graph.ResourceHasVersion("donut_taa_feedback"_sid, 1);
    const Core::DonutTAAConfiguration& donutConfig = viewFamily.aaConfig.donutTaa;

    RenderPass& taaPass = graph.AddPass("Donut TAA Main"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    taaPass.ReadBuffer("scene_data"_sid);
    taaPass.ReadSampledImage(targets.colorOutput);
    taaPass.ReadSampledImage(targets.gbufferOne);
    if (bHasHistory) {
        taaPass.ReadSampledImage(graph.ResourceVersionID("donut_taa_feedback"_sid, 1));
    }
    taaPass.WriteStorageImage("donut_taa_feedback"_sid);
    taaPass.WriteStorageImage("donut_taa_output"_sid);
    taaPass.Execute([&, pipelineManager, bHasHistory,
            inWidth = static_cast<float>(inputExtent[0]), inHeight = static_cast<float>(inputExtent[1]),
            outWidth = static_cast<float>(outputExtent[0]), outHeight = static_cast<float>(outputExtent[1]),
            dispatchW = outputExtent[0], dispatchH = outputExtent[1],
            outputColor = targets.colorOutput, gbufferOne = targets.gbufferOne, donutConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            float pqC = donutConfig.maxRadiance;
            if (pqC < 1e-4f) { pqC = 1e-4f; }
            if (pqC > 1e8f) { pqC = 1e8f; }

            const uint32_t colorInputIdx = graph.GetSampledImageViewDescriptorIndex(outputColor);

            const float newFrameWeight = bHasHistory ? donutConfig.newFrameWeight : 1.0f;
            const uint32_t feedbackInputIdx = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(graph.ResourceVersionID("donut_taa_feedback"_sid, 1)) : colorInputIdx;

            DonutTaaPushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .colorInputIndex = colorInputIdx,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .feedbackInputIndex = feedbackInputIdx,
                .historyClampRelaxIndex = colorInputIdx,
                .colorOutputIndex = graph.GetStorageImageViewDescriptorIndex("donut_taa_output"_sid),
                .feedbackOutputIndex = graph.GetStorageImageViewDescriptorIndex("donut_taa_feedback"_sid),
                .clampingFactor = donutConfig.clampingFactor,
                .newFrameWeight = newFrameWeight,
                .pqC = pqC,
                .invPqC = 1.0f / pqC,
                .useHistoryClampRelax = donutConfig.bUseHistoryClampRelax ? 1u : 0u,
                .useCatmullRom = donutConfig.bUseCatmullRom ? 1u : 0u,
                .inputViewOrigin = {0.0f, 0.0f},
                .inputViewSize = {inWidth, inHeight},
                .outputViewOrigin = {0.0f, 0.0f},
                .outputViewSize = {outWidth, outHeight},
                .outputTextureSizeInv = {1.0f / outWidth, 1.0f / outHeight},
                .inputOverOutputViewSize = {inWidth / outWidth, inHeight / outHeight},
                .outputOverInputViewSize = {outWidth / inWidth, outHeight / inHeight},
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("taa_donut"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DonutTaaPushConstant), &pushData);

            uint32_t xDispatch = (dispatchW + 15) / 16;
            uint32_t yDispatch = (dispatchH + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return "donut_taa_output"_sid;
}
} // Render
