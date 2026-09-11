//
// Created by William on 2026-06-03.
//

#include "render/passes/anti_aliasing_passes.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>

#include "render/render_utils.h"
#include "render/render-view/render_view_helpers.h"
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

    RenderPass& taaPass = graph.AddPass("TAA Main"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    taaPass.ReadBuffer("scene_data"_sid);
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
            gbufferOne = targets.gbufferOne, pipelineSID, taaConfig,
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
    taaPass.ReadSampledImage(targets.depthCopy);
    if (bHasHistory) {
        taaPass.ReadSampledImage(graph.ResourceVersionID("donut_taa_feedback"_sid, 1));
    }
    taaPass.WriteStorageImage("donut_taa_feedback"_sid);
    taaPass.WriteStorageImage("donut_taa_output"_sid);
    taaPass.Execute([&, pipelineManager, bHasHistory,
            inWidth = static_cast<float>(inputExtent[0]), inHeight = static_cast<float>(inputExtent[1]),
            outWidth = static_cast<float>(outputExtent[0]), outHeight = static_cast<float>(outputExtent[1]),
            dispatchW = outputExtent[0], dispatchH = outputExtent[1],
            outputColor = targets.colorOutput, gbufferOne = targets.gbufferOne, depthStencil = targets.depthCopy, donutConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .colorOutputIndex = graph.GetStorageImageViewDescriptorIndex("donut_taa_output"_sid),
                .feedbackOutputIndex = graph.GetStorageImageViewDescriptorIndex("donut_taa_feedback"_sid),
                .clampingFactor = donutConfig.clampingFactor,
                .newFrameWeight = newFrameWeight,
                .pqC = pqC,
                .invPqC = 1.0f / pqC,
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

static void DispatchFsr2Pass(PipelineManager* pipelineManager, VkCommandBuffer cmd, StringID pipelineId, const void* pushData, uint32_t pushSize, uint32_t groupsX, uint32_t groupsY)
{
    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(pipelineId);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize, pushData);
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

StringID SetupFsr2(RenderGraph& graph,
                   PipelineManager* pipelineManager,
                   const Core::ViewFamily& viewFamily,
                   Core::Array<uint32_t, 2> renderExtent,
                   Core::Array<uint32_t, 2> outputExtent,
                   const RenderTargets& targets,
                   bool bHasPreOverlayColor,
                   const Core::ReflectionConfiguration& reflectionConfig,
                   float deltaTime,
                   float framerateScale,
                   uint64_t frameNumber,
                   float preExposure,
                   float prevPreExposure)
{
    ZoneScoped;
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;
    const Core::Fsr2Configuration& config = viewFamily.aaConfig.fsr2;

    const uint32_t renderW = renderExtent[0];
    const uint32_t renderH = renderExtent[1];
    const uint32_t displayW = outputExtent[0];
    const uint32_t displayH = outputExtent[1];
    const uint32_t mip4W = (renderW + 31) / 32;
    const uint32_t mip4H = (renderH + 31) / 32;
    const uint32_t mip5W = (renderW + 63) / 64;
    const uint32_t mip5H = (renderH + 63) / 64;
    const uint32_t renderGroupsX = (renderW + 7) / 8;
    const uint32_t renderGroupsY = (renderH + 7) / 8;
    const uint32_t displayGroupsX = (displayW + 7) / 8;
    const uint32_t displayGroupsY = (displayH + 7) / 8;

    const bool bAutoExposure = config.bAutoExposure;
    const bool bSharpen = config.bSharpen;
    const bool bReactive = config.bReactiveMask && bHasPreOverlayColor;

    graph.CreateVersionedTexture("fsr2_history_color"_sid, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, displayW, displayH, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("fsr2_lock_status"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, displayW, displayH, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("fsr2_dilated_motion"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderW, renderH, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("fsr2_luma_history"_sid, TextureInfo{VK_FORMAT_R8G8B8A8_UNORM, displayW, displayH, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    if (bAutoExposure) {
        graph.CreateVersionedTexture("fsr2_exposure"_sid, TextureInfo{VK_FORMAT_R32G32_SFLOAT, 1, 1, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    graph.CreateTexture("fsr2_luma_mip4"_sid, TextureInfo{VK_FORMAT_R16_SFLOAT, mip4W, mip4H, 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture("fsr2_luma_mip5"_sid, TextureInfo{VK_FORMAT_R16_SFLOAT, mip5W, mip5H, 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture("fsr2_dilated_depth"_sid, TextureInfo{VK_FORMAT_R32_SFLOAT, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    // Scatter target for InterlockedMax; must be zero before the reconstruct pass
    graph.CreateTexture("fsr2_reconstructed_depth"_sid, TextureInfo{VK_FORMAT_R32_UINT, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture("fsr2_lock_input_luma"_sid, TextureInfo{VK_FORMAT_R16_SFLOAT, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture("fsr2_prepared_color"_sid, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture("fsr2_dilated_reactive"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    // Lock pass sets, accumulate reads and clears; must be zero before the lock pass
    graph.CreateTexture("fsr2_new_locks"_sid, TextureInfo{VK_FORMAT_R8_UNORM, displayW, displayH, 1}, CLEAR_COLOR_EMPTY, true);
    if (bReactive) {
        graph.CreateTexture("fsr2_reactive_mask"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    }
    graph.CreateTexture("fsr2_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, displayW, displayH, 1}, CLEAR_COLOR_EMPTY, true);

    const bool bHasHistory = graph.ResourceHasVersion("fsr2_history_color"_sid, 1);
    const bool bHasPrevMotion = graph.ResourceHasVersion("fsr2_dilated_motion"_sid, 1);
    const bool bHasPrevExposure = bAutoExposure && graph.ResourceHasVersion("fsr2_exposure"_sid, 1);
    const StringID prevHistoryColorId = bHasHistory ? graph.ResourceVersionID("fsr2_history_color"_sid, 1) : "fsr2_prepared_color"_sid;
    const StringID prevLockStatusId = bHasHistory ? graph.ResourceVersionID("fsr2_lock_status"_sid, 1) : "fsr2_dilated_reactive"_sid;
    const StringID prevLumaHistoryId = bHasHistory ? graph.ResourceVersionID("fsr2_luma_history"_sid, 1) : "fsr2_prepared_color"_sid;
    const StringID prevDilatedMotionId = bHasPrevMotion ? graph.ResourceVersionID("fsr2_dilated_motion"_sid, 1) : "fsr2_dilated_motion"_sid;

    const glm::mat4& proj = viewFamily.mainView.currentViewData.proj;
    const uint32_t jitterPhaseCount = ComputeJitterPhaseCount(Core::AntiAliasingMode::FSR2, viewFamily.resolutionScale);
    const HaltonSample jitterSample = ComputeJitterSample(Core::AntiAliasingMode::FSR2, frameNumber, jitterPhaseCount);

    Fsr2Constants constants{
        .renderSize = {static_cast<float>(renderW), static_cast<float>(renderH)},
        .displaySize = {static_cast<float>(displayW), static_cast<float>(displayH)},
        .jitter = {-jitterSample.x, -jitterSample.y},
        .downscaleFactor = {static_cast<float>(renderW) / static_cast<float>(displayW), static_cast<float>(renderH) / static_cast<float>(displayH)},
        .lumaMipSize = {static_cast<float>(mip4W), static_cast<float>(mip4H)},
        .lumaMipClampSize = {static_cast<float>(std::max(1u, renderW / 32)), static_cast<float>(std::max(1u, renderH / 32))},
        .depthToView = {0.0f, viewFamily.mainView.currentViewData.nearPlane},
        .tanHalfFov = {1.0f / proj[0][0], 1.0f / proj[1][1]},
        .preExposure = preExposure,
        .previousPreExposure = prevPreExposure,
        .deltaTime = glm::clamp(deltaTime, 0.0f, 1.0f),
        .jitterPhaseCount = static_cast<float>(jitterPhaseCount),
        .frameIndex = bHasHistory ? 1u : 0u,
        .framerateScale = framerateScale,
    };

    if (bReactive) {
        RenderPass& reactivePass = graph.AddPass("FSR2 Reactive"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
        reactivePass.ReadSampledImage(targets.colorOutput);
        reactivePass.ReadSampledImage("lit_color_preoverlay"_sid);
        reactivePass.WriteStorageImage("fsr2_reactive_mask"_sid);
        reactivePass.Execute([pipelineManager, constants, colorOutput = targets.colorOutput, scale = config.reactiveScale, threshold = config.reactiveThreshold,
                renderGroupsX, renderGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                Fsr2ReactivePushConstant pushData{
                    .c = constants,
                    .colorIndex = graph.GetSampledImageViewDescriptorIndex(colorOutput),
                    .preOverlayColorIndex = graph.GetSampledImageViewDescriptorIndex("lit_color_preoverlay"_sid),
                    .reactiveOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_reactive_mask"_sid),
                    .scale = scale,
                    .threshold = threshold,
                    .binaryValue = 0.9f,
                };
                DispatchFsr2Pass(pipelineManager, cmd, "fsr2_reactive"_sid, &pushData, sizeof(pushData), renderGroupsX, renderGroupsY);
            });
    }

    RenderPass& luminancePass = graph.AddPass("FSR2 Luminance"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    luminancePass.ReadSampledImage(targets.colorOutput);
    luminancePass.WriteStorageImage("fsr2_luma_mip4"_sid);
    luminancePass.WriteStorageImage("fsr2_luma_mip5"_sid);
    luminancePass.Execute([pipelineManager, constants, colorOutput = targets.colorOutput, mip5W, mip5H](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        Fsr2LuminancePushConstant pushData{
            .c = constants,
            .colorIndex = graph.GetSampledImageViewDescriptorIndex(colorOutput),
            .lumaMip4OutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_luma_mip4"_sid),
            .lumaMip5OutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_luma_mip5"_sid),
        };
        DispatchFsr2Pass(pipelineManager, cmd, "fsr2_luminance"_sid, &pushData, sizeof(pushData), mip5W, mip5H);
    });

    if (bAutoExposure) {
        RenderPass& exposurePass = graph.AddPass("FSR2 Exposure"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
        exposurePass.ReadSampledImage("fsr2_luma_mip5"_sid);
        if (bHasPrevExposure) {
            exposurePass.ReadSampledImage(graph.ResourceVersionID("fsr2_exposure"_sid, 1));
        }
        exposurePass.WriteStorageImage("fsr2_exposure"_sid);
        exposurePass.Execute([pipelineManager, constants, bHasPrevExposure](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2ExposurePushConstant pushData{
                .c = constants,
                .lumaMip5Index = graph.GetSampledImageViewDescriptorIndex("fsr2_luma_mip5"_sid),
                .previousExposureIndex = bHasPrevExposure ? graph.GetSampledImageViewDescriptorIndex(graph.ResourceVersionID("fsr2_exposure"_sid, 1)) : INVALID_INDEX,
                .exposureOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_exposure"_sid),
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_exposure"_sid, &pushData, sizeof(pushData), 1, 1);
        });
    }

    RenderPass& reconstructPass = graph.AddPass("FSR2 Reconstruct"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    reconstructPass.ReadBuffer("scene_data"_sid);
    reconstructPass.ReadSampledImage(targets.depthCopy);
    reconstructPass.ReadSampledImage(targets.gbufferOne);
    reconstructPass.ReadSampledImage(targets.colorOutput);
    if (bAutoExposure) {
        reconstructPass.ReadSampledImage("fsr2_exposure"_sid);
    }
    reconstructPass.WriteStorageImage("fsr2_dilated_depth"_sid);
    reconstructPass.WriteStorageImage("fsr2_dilated_motion"_sid);
    reconstructPass.WriteStorageImage("fsr2_reconstructed_depth"_sid);
    reconstructPass.WriteStorageImage("fsr2_lock_input_luma"_sid);
    reconstructPass.Execute([pipelineManager, constants, bAutoExposure, depthCopy = targets.depthCopy, gbufferOne = targets.gbufferOne, colorOutput = targets.colorOutput,
            renderGroupsX, renderGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2ReconstructPushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .c = constants,
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthCopy),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(colorOutput),
                .exposureIndex = bAutoExposure ? graph.GetSampledImageViewDescriptorIndex("fsr2_exposure"_sid) : INVALID_INDEX,
                .dilatedDepthOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_dilated_depth"_sid),
                .dilatedMotionOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_dilated_motion"_sid),
                .reconstructedDepthOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_reconstructed_depth"_sid),
                .lockInputLumaOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_lock_input_luma"_sid),
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_reconstruct"_sid, &pushData, sizeof(pushData), renderGroupsX, renderGroupsY);
        });

    RenderPass& depthClipPass = graph.AddPass("FSR2 Depth Clip"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    depthClipPass.ReadBuffer("scene_data"_sid);
    depthClipPass.ReadSampledImage(targets.depthCopy);
    depthClipPass.ReadSampledImage(targets.gbufferOne);
    depthClipPass.ReadSampledImage(targets.colorOutput);
    if (bAutoExposure) {
        depthClipPass.ReadSampledImage("fsr2_exposure"_sid);
    }
    if (bReactive) {
        depthClipPass.ReadSampledImage("fsr2_reactive_mask"_sid);
    }
    depthClipPass.ReadSampledImage("fsr2_dilated_depth"_sid);
    depthClipPass.ReadSampledImage("fsr2_dilated_motion"_sid);
    if (bHasPrevMotion) {
        depthClipPass.ReadSampledImage(prevDilatedMotionId);
    }
    depthClipPass.ReadSampledImage("fsr2_reconstructed_depth"_sid);
    depthClipPass.WriteStorageImage("fsr2_prepared_color"_sid);
    depthClipPass.WriteStorageImage("fsr2_dilated_reactive"_sid);
    depthClipPass.Execute([pipelineManager, constants, bAutoExposure, bReactive, prevDilatedMotionId, depthCopy = targets.depthCopy, gbufferOne = targets.gbufferOne, colorOutput = targets.colorOutput,
            reflectionReactive = config.reflectionReactive, mirrorRoughnessMax = reflectionConfig.mirrorRoughnessMax, tracedRoughnessMax = reflectionConfig.tracedRoughnessMax,
            renderGroupsX, renderGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2DepthClipPushConstant pushData{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .c = constants,
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthCopy),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(colorOutput),
                .exposureIndex = bAutoExposure ? graph.GetSampledImageViewDescriptorIndex("fsr2_exposure"_sid) : INVALID_INDEX,
                .reactiveMaskIndex = bReactive ? graph.GetSampledImageViewDescriptorIndex("fsr2_reactive_mask"_sid) : INVALID_INDEX,
                .dilatedDepthIndex = graph.GetSampledImageViewDescriptorIndex("fsr2_dilated_depth"_sid),
                .dilatedMotionIndex = graph.GetSampledImageViewDescriptorIndex("fsr2_dilated_motion"_sid),
                .previousDilatedMotionIndex = graph.GetSampledImageViewDescriptorIndex(prevDilatedMotionId),
                .reconstructedDepthIndex = graph.GetSampledImageViewDescriptorIndex("fsr2_reconstructed_depth"_sid),
                .preparedColorOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_prepared_color"_sid),
                .dilatedReactiveOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_dilated_reactive"_sid),
                .reflectionReactive = reflectionReactive,
                .mirrorRoughnessMax = mirrorRoughnessMax,
                .tracedRoughnessMax = tracedRoughnessMax,
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_depth_clip"_sid, &pushData, sizeof(pushData), renderGroupsX, renderGroupsY);
        });

    RenderPass& lockPass = graph.AddPass("FSR2 Lock"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    lockPass.ReadSampledImage("fsr2_lock_input_luma"_sid);
    lockPass.WriteStorageImage("fsr2_new_locks"_sid);
    lockPass.Execute([pipelineManager, constants, renderGroupsX, renderGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        Fsr2LockPushConstant pushData{
            .c = constants,
            .lockInputLumaIndex = graph.GetSampledImageViewDescriptorIndex("fsr2_lock_input_luma"_sid),
            .newLocksOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_new_locks"_sid),
        };
        DispatchFsr2Pass(pipelineManager, cmd, "fsr2_lock"_sid, &pushData, sizeof(pushData), renderGroupsX, renderGroupsY);
    });

    RenderPass& accumulatePass = graph.AddPass("FSR2 Accumulate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    if (bAutoExposure) {
        accumulatePass.ReadSampledImage("fsr2_exposure"_sid);
    }
    accumulatePass.ReadSampledImage("fsr2_dilated_reactive"_sid);
    accumulatePass.ReadSampledImage("fsr2_dilated_motion"_sid);
    accumulatePass.ReadSampledImage("fsr2_prepared_color"_sid);
    accumulatePass.ReadSampledImage("fsr2_luma_mip4"_sid);
    if (bHasHistory) {
        accumulatePass.ReadSampledImage(prevHistoryColorId);
        accumulatePass.ReadSampledImage(prevLockStatusId);
        accumulatePass.ReadSampledImage(prevLumaHistoryId);
    }
    accumulatePass.WriteStorageImage("fsr2_new_locks"_sid);
    accumulatePass.WriteStorageImage("fsr2_history_color"_sid);
    accumulatePass.WriteStorageImage("fsr2_lock_status"_sid);
    accumulatePass.WriteStorageImage("fsr2_luma_history"_sid);
    if (!bSharpen) {
        accumulatePass.WriteStorageImage("fsr2_output"_sid);
    }
    accumulatePass.Execute([pipelineManager, constants, bAutoExposure, bSharpen, prevHistoryColorId, prevLockStatusId, prevLumaHistoryId,
            displayGroupsX, displayGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2AccumulatePushConstant pushData{
                .c = constants,
                .exposureIndex = bAutoExposure ? graph.GetSampledImageViewDescriptorIndex("fsr2_exposure"_sid) : INVALID_INDEX,
                .dilatedReactiveIndex = graph.GetSampledImageViewDescriptorIndex("fsr2_dilated_reactive"_sid),
                .dilatedMotionIndex = graph.GetSampledImageViewDescriptorIndex("fsr2_dilated_motion"_sid),
                .historyColorIndex = graph.GetSampledImageViewDescriptorIndex(prevHistoryColorId),
                .lockStatusIndex = graph.GetSampledImageViewDescriptorIndex(prevLockStatusId),
                .preparedColorIndex = graph.GetSampledImageViewDescriptorIndex("fsr2_prepared_color"_sid),
                .lumaMip4Index = graph.GetSampledImageViewDescriptorIndex("fsr2_luma_mip4"_sid),
                .lumaHistoryIndex = graph.GetSampledImageViewDescriptorIndex(prevLumaHistoryId),
                .newLocksIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_new_locks"_sid),
                .historyColorOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_history_color"_sid),
                .lockStatusOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_lock_status"_sid),
                .lumaHistoryOutIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_luma_history"_sid),
                .outputIndex = bSharpen ? INVALID_INDEX : graph.GetStorageImageViewDescriptorIndex("fsr2_output"_sid),
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_accumulate"_sid, &pushData, sizeof(pushData), displayGroupsX, displayGroupsY);
        });

    if (bSharpen) {
        // FSR2 maps sharpness [0, 1] to 2 - 2 * sharpness stops of attenuation
        const float sharpnessLinear = std::exp2(-(2.0f - 2.0f * glm::clamp(config.sharpness, 0.0f, 1.0f)));
        RenderPass& rcasPass = graph.AddPass("FSR2 RCAS"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
        rcasPass.ReadSampledImage("fsr2_history_color"_sid);
        if (bAutoExposure) {
            rcasPass.ReadSampledImage("fsr2_exposure"_sid);
        }
        rcasPass.WriteStorageImage("fsr2_output"_sid);
        rcasPass.Execute([pipelineManager, constants, bAutoExposure, sharpnessLinear, displayGroupsX, displayGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2RcasPushConstant pushData{
                .c = constants,
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("fsr2_history_color"_sid),
                .exposureIndex = bAutoExposure ? graph.GetSampledImageViewDescriptorIndex("fsr2_exposure"_sid) : INVALID_INDEX,
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("fsr2_output"_sid),
                .sharpness = sharpnessLinear,
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_rcas"_sid, &pushData, sizeof(pushData), displayGroupsX, displayGroupsY);
        });
    }

    return "fsr2_output"_sid;
}
} // Render
