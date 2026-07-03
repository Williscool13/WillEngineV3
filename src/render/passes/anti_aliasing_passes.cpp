//
// Created by William on 2026-06-03.
//

#include "render/passes/anti_aliasing_passes.h"

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
    graph.CreateTexture(SID("smaa_edges"), TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("smaa_blend"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("smaa_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    const Core::SMAAConfiguration& smaaConfig = viewFamily.aaConfig.smaa;

    // Pass 1: Edge Detection
    RenderPass& edgePass = graph.AddPass(SID("SMAA Edge Detection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    edgePass.ReadBuffer(SID("scene_data"));
    edgePass.ReadSampledImage(targets.colorOutput);
    edgePass.ReadSampledImage(targets.depthCopy);
    edgePass.WriteStorageImage(SID("smaa_edges"));
    edgePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy,
            smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaEdgeDetectionPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputEdgeIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_edges")),
                .threshold = smaaConfig.threshold,
                .localContrastAdaptation = smaaConfig.localContrastAdaptation,
            };

            StringID pipelineID;
            switch (smaaConfig.edgeDetectionMode) {
                case Core::SMAAEdgeDetectionMode::Color: pipelineID = SID("smaa_color_edge_detection");
                    break;
                case Core::SMAAEdgeDetectionMode::Depth: pipelineID = SID("smaa_depth_edge_detection");
                    break;
                default: pipelineID = SID("smaa_luma_edge_detection");
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
    RenderPass& blendPass = graph.AddPass(SID("SMAA Blend Weight"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    blendPass.ReadBuffer(SID("scene_data"));
    blendPass.ReadSampledImage(SID("smaa_edges"));
    blendPass.WriteStorageImage(SID("smaa_blend"));
    blendPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        SmaaBlendWeightPushConstant pushData{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .edgeIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_edges")),
            .outputBlendIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_blend")),
            .maxSearchSteps = smaaConfig.maxSearchSteps,
            .maxSearchStepsDiag = smaaConfig.maxSearchStepsDiag,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_blend_weight"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaBlendWeightPushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Pass 3: Neighborhood Blending
    RenderPass& neighborhoodPass = graph.AddPass(SID("SMAA Neighborhood Blend"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    neighborhoodPass.ReadBuffer(SID("scene_data"));
    neighborhoodPass.ReadSampledImage(targets.colorOutput);
    neighborhoodPass.ReadSampledImage(SID("smaa_blend"));
    neighborhoodPass.WriteStorageImage(SID("smaa_output"));
    neighborhoodPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaNeighborhoodBlendPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .blendWeightIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_blend")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_output")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_neighborhood_blend"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaNeighborhoodBlendPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return SID("smaa_output");
}

StringID SetupSMAA_T2X(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets)
{
    graph.CreateTexture(SID("smaa_edges"), TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("smaa_blend"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("smaa_t2x_current"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CarryTextureToNextFrame(SID("smaa_t2x_current"), SID("smaa_t2x_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    const Core::SMAAConfiguration& smaaConfig = viewFamily.aaConfig.smaa;

    // Pass 1: Edge Detection
    RenderPass& edgePass = graph.AddPass(SID("SMAA T2X Edge Detection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    edgePass.ReadBuffer(SID("scene_data"));
    edgePass.ReadSampledImage(targets.colorOutput);
    edgePass.ReadSampledImage(targets.depthCopy);
    edgePass.WriteStorageImage(SID("smaa_edges"));
    edgePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy,
            smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaEdgeDetectionPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputEdgeIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_edges")),
                .threshold = smaaConfig.threshold,
                .localContrastAdaptation = smaaConfig.localContrastAdaptation,
            };

            StringID pipelineID;
            switch (smaaConfig.edgeDetectionMode) {
                case Core::SMAAEdgeDetectionMode::Color: pipelineID = SID("smaa_color_edge_detection");
                    break;
                case Core::SMAAEdgeDetectionMode::Depth: pipelineID = SID("smaa_depth_edge_detection");
                    break;
                default: pipelineID = SID("smaa_luma_edge_detection");
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
    RenderPass& blendPass = graph.AddPass(SID("SMAA T2X Blend Weight"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    blendPass.ReadBuffer(SID("scene_data"));
    blendPass.ReadSampledImage(SID("smaa_edges"));
    blendPass.WriteStorageImage(SID("smaa_blend"));
    blendPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        SmaaBlendWeightPushConstant pushData{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .edgeIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_edges")),
            .outputBlendIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_blend")),
            .maxSearchSteps = smaaConfig.maxSearchSteps,
            .maxSearchStepsDiag = smaaConfig.maxSearchStepsDiag,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_blend_weight"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaBlendWeightPushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Pass 3: Neighborhood Blending
    RenderPass& neighborhoodPass = graph.AddPass(SID("SMAA T2X Neighborhood Blend"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    neighborhoodPass.ReadBuffer(SID("scene_data"));
    neighborhoodPass.ReadSampledImage(targets.colorOutput);
    neighborhoodPass.ReadSampledImage(SID("smaa_blend"));
    neighborhoodPass.WriteStorageImage(SID("smaa_t2x_current"));
    neighborhoodPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaNeighborhoodBlendPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .blendWeightIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_blend")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_t2x_current")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_neighborhood_blend"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaNeighborhoodBlendPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    if (!graph.HasTexture(SID("smaa_t2x_history"))) {
        return SID("smaa_t2x_current");
    }

    // Pass 4: Temporal Resolve
    graph.CreateTexture(SID("smaa_t2x_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& resolvePass = graph.AddPass(SID("SMAA T2X Temporal Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    resolvePass.ReadBuffer(SID("scene_data"));
    resolvePass.ReadSampledImage(SID("smaa_t2x_current"));
    resolvePass.ReadSampledImage(SID("smaa_t2x_history"));
    resolvePass.ReadSampledImage(targets.gbufferOne);
    resolvePass.WriteStorageImage(SID("smaa_t2x_output"));
    resolvePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            gbufferOne = targets.gbufferOne](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaTemporalResolvePushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .currentColorIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_t2x_current")),
                .previousColorIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_t2x_history")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_t2x_output")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_temporal_resolve"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaTemporalResolvePushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return SID("smaa_t2x_output");
}

StringID SetupTemporalAntiAliasing(RenderGraph& graph,
                                   PipelineManager* pipelineManager,
                                   const Core::ViewFamily& viewFamily,
                                   Core::Array<uint32_t, 2> renderExtent,
                                   const RenderTargets& targets,
                                   StringID pipelineSID)
{
    graph.CreateTexture(SID("taa_current"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CarryTextureToNextFrame(SID("taa_current"), SID("taa_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    if (!graph.HasTexture(SID("taa_history")) || !graph.HasTexture(SID("gbuffer_one_history")) || !graph.HasTexture(SID("depth_history"))) {
        RenderPass& taaPass = graph.AddPass(SID("TAA Copy Deferred"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::ResourceCategory::AntiAliasing);
        taaPass.ReadCopyImage(targets.colorOutput);
        taaPass.WriteCopyImage(SID("taa_current"));
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], outputColor = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkImage drawImage = graph.GetImageHandle(outputColor);
            VkImage taaImage = graph.GetImageHandle(SID("taa_current"));

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
    graph.CreateTexture(SID("taa_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    const Core::TAAConfiguration& taaConfig = viewFamily.aaConfig.taa;

    RenderPass& taaPass = graph.AddPass(SID("TAA Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    taaPass.ReadBuffer(SID("scene_data"));
    taaPass.ReadSampledImage(targets.colorOutput);
    taaPass.ReadSampledImage(targets.depthCopy);
    taaPass.ReadSampledImage(SID("depth_history"));
    taaPass.ReadSampledImage(SID("taa_history"));
    taaPass.ReadSampledImage(targets.gbufferOne);
    taaPass.ReadSampledImage(SID("gbuffer_one_history"));
    taaPass.WriteStorageImage(SID("taa_current"));
    taaPass.WriteStorageImage(SID("taa_output"));
    taaPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy,
            gbufferOne = targets.gbufferOne, pipelineSID, taaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            TemporalAntialiasingPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .depthHistoryIndex = graph.GetSampledImageViewDescriptorIndex(SID("depth_history")),
                .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex(SID("taa_history")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferOneHistoryIndex = graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")),
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("taa_current")),
                .outputCopyIndex = graph.GetStorageImageViewDescriptorIndex(SID("taa_output")),
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

    return SID("taa_output");
}

StringID SetupDonutTemporalAntiAliasing(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> inputExtent,
                                        Core::Array<uint32_t, 2> outputExtent,
                                        const RenderTargets& targets)
{
    graph.CreateTexture(SID("donut_taa_feedback"), TextureInfo{COLOR_ATTACHMENT_FORMAT, outputExtent[0], outputExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CarryTextureToNextFrame(SID("donut_taa_feedback"), SID("donut_taa_feedback_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateTexture(SID("donut_taa_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, outputExtent[0], outputExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    const bool bHasHistory = graph.HasTexture(SID("donut_taa_feedback_history"));
    const Core::DonutTAAConfiguration& donutConfig = viewFamily.aaConfig.donutTaa;

    RenderPass& taaPass = graph.AddPass(SID("Donut TAA Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AntiAliasing);
    taaPass.ReadBuffer(SID("scene_data"));
    taaPass.ReadSampledImage(targets.colorOutput);
    taaPass.ReadSampledImage(targets.gbufferOne);
    if (bHasHistory) {
        taaPass.ReadSampledImage(SID("donut_taa_feedback_history"));
    }
    taaPass.WriteStorageImage(SID("donut_taa_feedback"));
    taaPass.WriteStorageImage(SID("donut_taa_output"));
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
            const uint32_t feedbackInputIdx = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("donut_taa_feedback_history")) : colorInputIdx;

            DonutTaaPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorInputIndex = colorInputIdx,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .feedbackInputIndex = feedbackInputIdx,
                .historyClampRelaxIndex = colorInputIdx,
                .colorOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("donut_taa_output")),
                .feedbackOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("donut_taa_feedback")),
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

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("taa_donut"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DonutTaaPushConstant), &pushData);

            uint32_t xDispatch = (dispatchW + 15) / 16;
            uint32_t yDispatch = (dispatchH + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return SID("donut_taa_output");
}
} // Render
