//
// Created by William on 2026-06-03.
//

#include "render/passes/anti_aliasing_passes.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>

#include "render/passes/reflection_passes.h"
#include "render/render_utils.h"
#include "render/render-view/render_view_helpers.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
RDGTexture SetupSubpixelMorphologicalAntiAliasing(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Extent2D renderExtent,
                                                  const RenderTargets& targets, const SceneResources& scene)
{
    ZoneScoped;
    const RDGTexture smaaEdges = graph.CreateTexture("smaa_edges"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent.width, renderExtent.height, 1}, CLEAR_COLOR_EMPTY, true);
    const RDGTexture smaaBlend = graph.CreateTexture("smaa_blend"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, CLEAR_COLOR_EMPTY, true);
    const RDGTexture smaaOutput = graph.CreateTexture("smaa_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, CLEAR_COLOR_EMPTY, true);

    const Core::SMAAConfiguration& smaaConfig = viewFamily.aaConfig.smaa;

    // Pass 1: Edge Detection
    RenderPass& edgePass = graph.AddPass("SMAA Edge Detection"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    edgePass.ReadBuffer(scene.sceneData);
    edgePass.ReadSampledImage(targets.colorOutput);
    edgePass.ReadSampledImage(targets.depthCopy);
    edgePass.WriteStorageImage(smaaEdges);
    edgePass.Execute([&scene, pipelineManager, renderExtent,
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy, smaaEdges,
            smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaEdgeDetectionPushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputEdgeIndex = graph.GetStorageImageViewDescriptorIndex(smaaEdges),
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

            uint32_t xDispatch = (renderExtent.width + 15) / 16;
            uint32_t yDispatch = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    // Pass 2: Blend Weight Calculation
    RenderPass& blendPass = graph.AddPass("SMAA Blend Weight"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    blendPass.ReadBuffer(scene.sceneData);
    blendPass.ReadSampledImage(smaaEdges);
    blendPass.WriteStorageImage(smaaBlend);
    blendPass.Execute([&scene, pipelineManager, renderExtent, smaaConfig, smaaEdges, smaaBlend](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        SmaaBlendWeightPushConstant pushData{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .edgeIndex = graph.GetSampledImageViewDescriptorIndex(smaaEdges),
            .outputBlendIndex = graph.GetStorageImageViewDescriptorIndex(smaaBlend),
            .maxSearchSteps = smaaConfig.maxSearchSteps,
            .maxSearchStepsDiag = smaaConfig.maxSearchStepsDiag,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_blend_weight"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaBlendWeightPushConstant), &pushData);

        uint32_t xDispatch = (renderExtent.width + 15) / 16;
        uint32_t yDispatch = (renderExtent.height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Pass 3: Neighborhood Blending
    RenderPass& neighborhoodPass = graph.AddPass("SMAA Neighborhood Blend"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    neighborhoodPass.ReadBuffer(scene.sceneData);
    neighborhoodPass.ReadSampledImage(targets.colorOutput);
    neighborhoodPass.ReadSampledImage(smaaBlend);
    neighborhoodPass.WriteStorageImage(smaaOutput);
    neighborhoodPass.Execute([&scene, pipelineManager, renderExtent,
            outputColor = targets.colorOutput, smaaBlend, smaaOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaNeighborhoodBlendPushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .blendWeightIndex = graph.GetSampledImageViewDescriptorIndex(smaaBlend),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(smaaOutput),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_neighborhood_blend"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaNeighborhoodBlendPushConstant), &pushData);

            uint32_t xDispatch = (renderExtent.width + 15) / 16;
            uint32_t yDispatch = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return smaaOutput;
}

RDGTexture SetupSMAA_T2X(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Extent2D renderExtent,
                         const RenderTargets& targets,
                         const SceneResources& scene)
{
    ZoneScoped;
    const RDGTexture smaaEdges = graph.CreateTexture("smaa_edges"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent.width, renderExtent.height, 1}, CLEAR_COLOR_EMPTY, true);
    const RDGTexture smaaBlend = graph.CreateTexture("smaa_blend"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, CLEAR_COLOR_EMPTY, true);
    const RDGTextureRing currentRing = graph.CreateVersionedTexture("smaa_t2x_current"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true,
                                                                    VK_IMAGE_USAGE_SAMPLED_BIT, false, CLEAR_COLOR_EMPTY);
    const RDGTexture current = currentRing.Current();
    const RDGTexture history = currentRing.Version(1);

    const Core::SMAAConfiguration& smaaConfig = viewFamily.aaConfig.smaa;

    // Pass 1: Edge Detection
    RenderPass& edgePass = graph.AddPass("SMAA T2X Edge Detection"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    edgePass.ReadBuffer(scene.sceneData);
    edgePass.ReadSampledImage(targets.colorOutput);
    edgePass.ReadSampledImage(targets.depthCopy);
    edgePass.WriteStorageImage(smaaEdges);
    edgePass.Execute([&scene, pipelineManager, renderExtent,
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy, smaaEdges,
            smaaConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaEdgeDetectionPushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputEdgeIndex = graph.GetStorageImageViewDescriptorIndex(smaaEdges),
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

            uint32_t xDispatch = (renderExtent.width + 15) / 16;
            uint32_t yDispatch = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    // Pass 2: Blend Weight Calculation
    RenderPass& blendPass = graph.AddPass("SMAA T2X Blend Weight"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    blendPass.ReadBuffer(scene.sceneData);
    blendPass.ReadSampledImage(smaaEdges);
    blendPass.WriteStorageImage(smaaBlend);
    blendPass.Execute([&scene, pipelineManager, renderExtent, smaaConfig, smaaEdges, smaaBlend](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        SmaaBlendWeightPushConstant pushData{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .edgeIndex = graph.GetSampledImageViewDescriptorIndex(smaaEdges),
            .outputBlendIndex = graph.GetStorageImageViewDescriptorIndex(smaaBlend),
            .maxSearchSteps = smaaConfig.maxSearchSteps,
            .maxSearchStepsDiag = smaaConfig.maxSearchStepsDiag,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_blend_weight"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaBlendWeightPushConstant), &pushData);

        uint32_t xDispatch = (renderExtent.width + 15) / 16;
        uint32_t yDispatch = (renderExtent.height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Pass 3: Neighborhood Blending
    RenderPass& neighborhoodPass = graph.AddPass("SMAA T2X Neighborhood Blend"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    neighborhoodPass.ReadBuffer(scene.sceneData);
    neighborhoodPass.ReadSampledImage(targets.colorOutput);
    neighborhoodPass.ReadSampledImage(smaaBlend);
    neighborhoodPass.WriteStorageImage(current);
    neighborhoodPass.Execute([&scene, pipelineManager, renderExtent,
            outputColor = targets.colorOutput, smaaBlend, current](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaNeighborhoodBlendPushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .blendWeightIndex = graph.GetSampledImageViewDescriptorIndex(smaaBlend),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(current),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_neighborhood_blend"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaNeighborhoodBlendPushConstant), &pushData);

            uint32_t xDispatch = (renderExtent.width + 15) / 16;
            uint32_t yDispatch = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    if (!history.IsValid()) {
        return current;
    }

    // Pass 4: Temporal Resolve
    const RDGTexture t2xOutput = graph.CreateTexture("smaa_t2x_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& resolvePass = graph.AddPass("SMAA T2X Temporal Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    resolvePass.ReadBuffer(scene.sceneData);
    resolvePass.ReadSampledImage(current);
    resolvePass.ReadSampledImage(history);
    resolvePass.ReadSampledImage(targets.gbufferOne);
    resolvePass.WriteStorageImage(t2xOutput);
    resolvePass.Execute([&scene, pipelineManager, renderExtent,
            gbufferOne = targets.gbufferOne, current, history, t2xOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SmaaTemporalResolvePushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .currentColorIndex = graph.GetSampledImageViewDescriptorIndex(current),
                .previousColorIndex = graph.GetSampledImageViewDescriptorIndex(history),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(t2xOutput),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("smaa_temporal_resolve"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaTemporalResolvePushConstant), &pushData);

            uint32_t xDispatch = (renderExtent.width + 15) / 16;
            uint32_t yDispatch = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return t2xOutput;
}

RDGTexture SetupTemporalAntiAliasing(RenderGraph& graph,
                                     PipelineManager* pipelineManager,
                                     const Core::ViewFamily& viewFamily,
                                     Core::Extent2D renderExtent,
                                     const RenderTargets& targets,
                                     const SceneResources& scene,
                                     uint64_t frameNumber,
                                     StringID pipelineSID)
{
    ZoneScoped;
    const RDGTextureRing taaRing = graph.CreateVersionedTexture("taa_current"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true,
                                                                VK_IMAGE_USAGE_SAMPLED_BIT, false, CLEAR_COLOR_EMPTY);
    const RDGTexture taaCurrent = taaRing.Current();
    const RDGTexture taaHistory = taaRing.Version(1);

    const RDGTexture depthHistory = targets.depthCopyHistory;
    const RDGTexture gbufferOneHistory = targets.gbufferOneHistory;

    if (!taaHistory.IsValid() || !gbufferOneHistory.IsValid() || !depthHistory.IsValid()) {
        RenderPass& taaPass = graph.AddPass("TAA Copy Deferred"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::AntiAliasing);
        taaPass.ReadCopyImage(targets.colorOutput);
        taaPass.WriteCopyImage(taaCurrent);
        taaPass.Execute([renderExtent, outputColor = targets.colorOutput, taaCurrent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkImage drawImage = graph.GetImageHandle(outputColor);
            VkImage taaImage = graph.GetImageHandle(taaCurrent);

            VkImageCopy2 copyRegion{};
            copyRegion.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent = {renderExtent.width, renderExtent.height, 1};

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
    const RDGTexture taaOutput = graph.CreateTexture("taa_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, CLEAR_COLOR_EMPTY, true);

    const Core::TAAConfiguration& taaConfig = viewFamily.aaConfig.taa;

    RenderPass& taaPass = graph.AddPass("TAA Main"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    taaPass.ReadBuffer(scene.sceneData);
    taaPass.ReadSampledImage(targets.colorOutput);
    taaPass.ReadSampledImage(targets.depthCopy);
    taaPass.ReadSampledImage(depthHistory);
    taaPass.ReadSampledImage(taaHistory);
    taaPass.ReadSampledImage(targets.gbufferOne);
    taaPass.ReadSampledImage(gbufferOneHistory);
    taaPass.WriteStorageImage(taaCurrent);
    taaPass.WriteStorageImage(taaOutput);
    taaPass.Execute([&scene, pipelineManager, renderExtent, frameNumber,
            outputColor = targets.colorOutput, depthStencil = targets.depthCopy,
            gbufferOne = targets.gbufferOne, pipelineSID, taaConfig,
            depthHistory, gbufferOneHistory, taaHistory, taaCurrent, taaOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            TemporalAntialiasingPushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .depthHistoryIndex = graph.GetSampledImageViewDescriptorIndex(depthHistory),
                .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex(taaHistory),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferOneHistoryIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory),
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(taaCurrent),
                .outputCopyIndex = graph.GetStorageImageViewDescriptorIndex(taaOutput),
                .baseBlendAlpha = taaConfig.baseBlendAlpha,
                .disocclusionThreshold = taaConfig.disocclusionThreshold,
                .varianceGammaLuma = taaConfig.varianceGammaLuma,
                .varianceGammaChroma = taaConfig.varianceGammaChroma,
                .karisStrength = taaConfig.karisStrength,
                .invalidHistoryBlend = taaConfig.invalidHistoryBlend,
                .lumaBoostCap = taaConfig.lumaBoostCap,
                .grazingTurnoverStrength = taaConfig.grazingTurnoverStrength,
                .frameIndex = static_cast<uint32_t>(frameNumber),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(pipelineSID);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TemporalAntialiasingPushConstant), &pushData);

            uint32_t xDispatch = (renderExtent.width + 15) / 16;
            uint32_t yDispatch = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return taaOutput;
}

RDGTexture SetupDonutTemporalAntiAliasing(RenderGraph& graph,
                                          PipelineManager* pipelineManager,
                                          const Core::ViewFamily& viewFamily,
                                          Core::Extent2D inputExtent,
                                          Core::Extent2D outputExtent,
                                          const RenderTargets& targets,
                                          const SceneResources& scene)
{
    ZoneScoped;
    const RDGTextureRing feedbackRing = graph.CreateVersionedTexture("donut_taa_feedback"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, outputExtent.width, outputExtent.height, 1}, 1, VersionSource::Fresh, true,
                                                                     VK_IMAGE_USAGE_SAMPLED_BIT, false, CLEAR_COLOR_EMPTY);
    const RDGTexture feedback = feedbackRing.Current();
    const RDGTexture feedbackHistory = feedbackRing.Version(1);
    const RDGTexture donutOutput = graph.CreateTexture("donut_taa_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, outputExtent.width, outputExtent.height, 1}, CLEAR_COLOR_EMPTY, true);

    const bool bHasHistory = feedbackHistory.IsValid();
    const Core::DonutTAAConfiguration& donutConfig = viewFamily.aaConfig.donutTaa;

    RenderPass& taaPass = graph.AddPass("Donut TAA Main"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    taaPass.ReadBuffer(scene.sceneData);
    taaPass.ReadSampledImage(targets.colorOutput);
    taaPass.ReadSampledImage(targets.gbufferOne);
    taaPass.ReadSampledImage(targets.depthCopy);
    if (bHasHistory) {
        taaPass.ReadSampledImage(feedbackHistory);
    }
    taaPass.WriteStorageImage(feedback);
    taaPass.WriteStorageImage(donutOutput);
    taaPass.Execute([&scene, pipelineManager, bHasHistory,
            inWidth = static_cast<float>(inputExtent.width), inHeight = static_cast<float>(inputExtent.height),
            outWidth = static_cast<float>(outputExtent.width), outHeight = static_cast<float>(outputExtent.height),
            outputExtent, feedback, feedbackHistory, donutOutput,
            outputColor = targets.colorOutput, gbufferOne = targets.gbufferOne, depthStencil = targets.depthCopy, donutConfig](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            float pqC = donutConfig.maxRadiance;
            if (pqC < 1e-4f) { pqC = 1e-4f; }
            if (pqC > 1e8f) { pqC = 1e8f; }

            const uint32_t colorInputIdx = graph.GetSampledImageViewDescriptorIndex(outputColor);

            const float newFrameWeight = bHasHistory ? donutConfig.newFrameWeight : 1.0f;
            const uint32_t feedbackInputIdx = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(feedbackHistory) : colorInputIdx;

            DonutTaaPushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .colorInputIndex = colorInputIdx,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .feedbackInputIndex = feedbackInputIdx,
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .colorOutputIndex = graph.GetStorageImageViewDescriptorIndex(donutOutput),
                .feedbackOutputIndex = graph.GetStorageImageViewDescriptorIndex(feedback),
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

            uint32_t xDispatch = (outputExtent.width + 15) / 16;
            uint32_t yDispatch = (outputExtent.height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return donutOutput;
}

static void DispatchFsr2Pass(PipelineManager* pipelineManager, VkCommandBuffer cmd, StringID pipelineId, const void* pushData, uint32_t pushSize, uint32_t groupsX, uint32_t groupsY)
{
    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(pipelineId);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize, pushData);
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

RDGTexture SetupFsr2(RenderGraph& graph,
                     PipelineManager* pipelineManager,
                     const Core::ViewFamily& viewFamily,
                     Core::Extent2D renderExtent,
                     Core::Extent2D outputExtent,
                     const RenderTargets& targets,
                     const SceneResources& scene,
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
    const RDGTexture preOverlayColor = targets.preOverlayColor;
    const bool bHasPreOverlayColor = preOverlayColor.IsValid();

    const uint32_t renderW = renderExtent.width;
    const uint32_t renderH = renderExtent.height;
    const uint32_t displayW = outputExtent.width;
    const uint32_t displayH = outputExtent.height;
    const uint32_t mip4W = (renderW + 31) / 32;
    const uint32_t mip4H = (renderH + 31) / 32;
    const uint32_t mip5W = (renderW + 63) / 64;
    const uint32_t mip5H = (renderH + 63) / 64;
    const uint32_t renderGroupsX = (renderW + 7) / 8;
    const uint32_t renderGroupsY = (renderH + 7) / 8;
    const uint32_t displayGroupsX = (displayW + 7) / 8;
    const uint32_t displayGroupsY = (displayH + 7) / 8;

    const bool bSharpen = config.bSharpen;
    const bool bReactive = config.bReactiveMask && bHasPreOverlayColor;
    const RDGTexture virtualMotion = targets.reflectionVirtualMotion;
    const bool bVirtualMotion = virtualMotion.IsValid();

    const RDGTextureRing historyColorRing = graph.CreateVersionedTexture("fsr2_history_color"_sid, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, displayW, displayH, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing lockStatusRing = graph.CreateVersionedTexture("fsr2_lock_status"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, displayW, displayH, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing dilatedMotionRing = graph.CreateVersionedTexture("fsr2_dilated_motion"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderW, renderH, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing lumaHistoryRing = graph.CreateVersionedTexture("fsr2_luma_history"_sid, TextureInfo{VK_FORMAT_R8G8B8A8_UNORM, displayW, displayH, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTexture historyColor = historyColorRing.Current();
    const RDGTexture lockStatus = lockStatusRing.Current();
    const RDGTexture dilatedMotion = dilatedMotionRing.Current();
    const RDGTexture lumaHistory = lumaHistoryRing.Current();

    const RDGTexture lumaMip4 = graph.CreateTexture("fsr2_luma_mip4"_sid, TextureInfo{VK_FORMAT_R16_SFLOAT, mip4W, mip4H, 1}, CLEAR_COLOR_EMPTY, true);
    const RDGTexture dilatedDepth = graph.CreateTexture("fsr2_dilated_depth"_sid, TextureInfo{VK_FORMAT_R32_SFLOAT, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    // Scatter target for InterlockedMax; must be zero before the reconstruct pass
    const RDGTexture reconstructedDepth = graph.CreateTexture("fsr2_reconstructed_depth"_sid, TextureInfo{VK_FORMAT_R32_UINT, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    const RDGTexture lockInputLuma = graph.CreateTexture("fsr2_lock_input_luma"_sid, TextureInfo{VK_FORMAT_R16_SFLOAT, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    const RDGTexture preparedColor = graph.CreateTexture("fsr2_prepared_color"_sid, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    const RDGTexture dilatedReactive = graph.CreateTexture("fsr2_dilated_reactive"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    // Lock pass sets, accumulate reads and clears; must be zero before the lock pass
    const RDGTexture newLocks = graph.CreateTexture("fsr2_new_locks"_sid, TextureInfo{VK_FORMAT_R8_UNORM, displayW, displayH, 1}, CLEAR_COLOR_EMPTY, true);
    RDGTexture reactiveMask{};
    if (bReactive) {
        reactiveMask = graph.CreateTexture("fsr2_reactive_mask"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderW, renderH, 1}, CLEAR_COLOR_EMPTY, true);
    }
    const RDGTexture fsr2Output = graph.CreateTexture("fsr2_output"_sid, TextureInfo{COLOR_ATTACHMENT_FORMAT, displayW, displayH, 1}, CLEAR_COLOR_EMPTY, true);

    const bool bHasHistory = historyColorRing.Version(1).IsValid();
    const bool bHasPrevMotion = dilatedMotionRing.Version(1).IsValid();
    const RDGTexture prevHistoryColor = bHasHistory ? historyColorRing.Version(1) : preparedColor;
    const RDGTexture prevLockStatus = bHasHistory ? lockStatusRing.Version(1) : dilatedReactive;
    const RDGTexture prevLumaHistory = bHasHistory ? lumaHistoryRing.Version(1) : preparedColor;
    const RDGTexture prevDilatedMotion = bHasPrevMotion ? dilatedMotionRing.Version(1) : dilatedMotion;

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
        reactivePass.ReadSampledImage(preOverlayColor);
        reactivePass.WriteStorageImage(reactiveMask);
        reactivePass.Execute([pipelineManager, constants, preOverlayColor, reactiveMask, colorOutput = targets.colorOutput, scale = config.reactiveScale, threshold = config.reactiveThreshold,
                renderGroupsX, renderGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                Fsr2ReactivePushConstant pushData{
                    .c = constants,
                    .colorIndex = graph.GetSampledImageViewDescriptorIndex(colorOutput),
                    .preOverlayColorIndex = graph.GetSampledImageViewDescriptorIndex(preOverlayColor),
                    .reactiveOutIndex = graph.GetStorageImageViewDescriptorIndex(reactiveMask),
                    .scale = scale,
                    .threshold = threshold,
                    .binaryValue = 0.9f,
                };
                DispatchFsr2Pass(pipelineManager, cmd, "fsr2_reactive"_sid, &pushData, sizeof(pushData), renderGroupsX, renderGroupsY);
            });
    }

    RenderPass& luminancePass = graph.AddPass("FSR2 Luminance"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    luminancePass.ReadSampledImage(targets.colorOutput);
    luminancePass.WriteStorageImage(lumaMip4);
    luminancePass.Execute([pipelineManager, constants, colorOutput = targets.colorOutput, lumaMip4, mip5W, mip5H](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        Fsr2LuminancePushConstant pushData{
            .c = constants,
            .colorIndex = graph.GetSampledImageViewDescriptorIndex(colorOutput),
            .lumaMip4OutIndex = graph.GetStorageImageViewDescriptorIndex(lumaMip4),
        };
        DispatchFsr2Pass(pipelineManager, cmd, "fsr2_luminance"_sid, &pushData, sizeof(pushData), mip5W, mip5H);
    });

    RenderPass& reconstructPass = graph.AddPass("FSR2 Reconstruct"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    reconstructPass.ReadBuffer(scene.sceneData);
    reconstructPass.ReadSampledImage(targets.depthCopy);
    reconstructPass.ReadSampledImage(targets.gbufferOne);
    reconstructPass.ReadSampledImage(targets.colorOutput);
    reconstructPass.WriteStorageImage(dilatedDepth);
    reconstructPass.WriteStorageImage(dilatedMotion);
    reconstructPass.WriteStorageImage(reconstructedDepth);
    reconstructPass.WriteStorageImage(lockInputLuma);
    reconstructPass.Execute([&scene, pipelineManager, constants, depthCopy = targets.depthCopy, gbufferOne = targets.gbufferOne, colorOutput = targets.colorOutput,
            dilatedDepth, dilatedMotion, reconstructedDepth, lockInputLuma, renderGroupsX, renderGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2ReconstructPushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .c = constants,
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthCopy),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(colorOutput),
                .dilatedDepthOutIndex = graph.GetStorageImageViewDescriptorIndex(dilatedDepth),
                .dilatedMotionOutIndex = graph.GetStorageImageViewDescriptorIndex(dilatedMotion),
                .reconstructedDepthOutIndex = graph.GetStorageImageViewDescriptorIndex(reconstructedDepth),
                .lockInputLumaOutIndex = graph.GetStorageImageViewDescriptorIndex(lockInputLuma),
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_reconstruct"_sid, &pushData, sizeof(pushData), renderGroupsX, renderGroupsY);
        });

    RenderPass& depthClipPass = graph.AddPass("FSR2 Depth Clip"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    depthClipPass.ReadBuffer(scene.sceneData);
    depthClipPass.ReadSampledImage(targets.depthCopy);
    depthClipPass.ReadSampledImage(targets.gbufferOne);
    depthClipPass.ReadSampledImage(targets.colorOutput);
    if (bReactive) {
        depthClipPass.ReadSampledImage(reactiveMask);
    }
    depthClipPass.ReadSampledImage(dilatedDepth);
    depthClipPass.ReadSampledImage(dilatedMotion);
    if (bHasPrevMotion) {
        depthClipPass.ReadSampledImage(prevDilatedMotion);
    }
    depthClipPass.ReadSampledImage(reconstructedDepth);
    if (bVirtualMotion) {
        depthClipPass.WriteStorageImage(virtualMotion);
        if (bHasPreOverlayColor) {
            depthClipPass.ReadSampledImage(preOverlayColor);
        }
    }
    depthClipPass.WriteStorageImage(preparedColor);
    depthClipPass.WriteStorageImage(dilatedReactive);
    depthClipPass.Execute([&scene, pipelineManager, constants, bReactive, bVirtualMotion, bHasPreOverlayColor, preOverlayColor, prevDilatedMotion, depthCopy = targets.depthCopy, gbufferOne = targets.gbufferOne,
            colorOutput = targets.colorOutput, reactiveMask, dilatedDepth, dilatedMotion, reconstructedDepth, preparedColor, dilatedReactive, virtualMotion,
            reflectionReactive = config.reflectionReactive, mirrorRoughnessMax = reflectionConfig.mirrorRoughnessMax, tracedRoughnessMax = reflectionConfig.tracedRoughnessMax,
            renderGroupsX, renderGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2DepthClipPushConstant pushData{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .c = constants,
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthCopy),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(colorOutput),
                .reactiveMaskIndex = bReactive ? graph.GetSampledImageViewDescriptorIndex(reactiveMask) : INVALID_INDEX,
                .dilatedDepthIndex = graph.GetSampledImageViewDescriptorIndex(dilatedDepth),
                .dilatedMotionIndex = graph.GetSampledImageViewDescriptorIndex(dilatedMotion),
                .previousDilatedMotionIndex = graph.GetSampledImageViewDescriptorIndex(prevDilatedMotion),
                .reconstructedDepthIndex = graph.GetSampledImageViewDescriptorIndex(reconstructedDepth),
                .preparedColorOutIndex = graph.GetStorageImageViewDescriptorIndex(preparedColor),
                .dilatedReactiveOutIndex = graph.GetStorageImageViewDescriptorIndex(dilatedReactive),
                .reflectionReactive = reflectionReactive,
                .mirrorRoughnessMax = mirrorRoughnessMax,
                .tracedRoughnessMax = tracedRoughnessMax,
                .virtualMotionIndex = bVirtualMotion ? graph.GetStorageImageViewDescriptorIndex(virtualMotion) : INVALID_INDEX,
                .preOverlayColorIndex = bVirtualMotion && bHasPreOverlayColor ? graph.GetSampledImageViewDescriptorIndex(preOverlayColor) : INVALID_INDEX,
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_depth_clip"_sid, &pushData, sizeof(pushData), renderGroupsX, renderGroupsY);
        });

    RenderPass& lockPass = graph.AddPass("FSR2 Lock"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    lockPass.ReadSampledImage(lockInputLuma);
    lockPass.WriteStorageImage(newLocks);
    lockPass.Execute([pipelineManager, constants, lockInputLuma, newLocks, renderGroupsX, renderGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        Fsr2LockPushConstant pushData{
            .c = constants,
            .lockInputLumaIndex = graph.GetSampledImageViewDescriptorIndex(lockInputLuma),
            .newLocksOutIndex = graph.GetStorageImageViewDescriptorIndex(newLocks),
        };
        DispatchFsr2Pass(pipelineManager, cmd, "fsr2_lock"_sid, &pushData, sizeof(pushData), renderGroupsX, renderGroupsY);
    });

    RenderPass& accumulatePass = graph.AddPass("FSR2 Accumulate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
    accumulatePass.ReadSampledImage(dilatedReactive);
    accumulatePass.ReadSampledImage(dilatedMotion);
    accumulatePass.ReadSampledImage(preparedColor);
    accumulatePass.ReadSampledImage(lumaMip4);
    if (bVirtualMotion) {
        accumulatePass.ReadSampledImage(virtualMotion);
    }
    if (bHasHistory) {
        accumulatePass.ReadSampledImage(prevHistoryColor);
        accumulatePass.ReadSampledImage(prevLockStatus);
        accumulatePass.ReadSampledImage(prevLumaHistory);
    }
    accumulatePass.WriteStorageImage(newLocks);
    accumulatePass.WriteStorageImage(historyColor);
    accumulatePass.WriteStorageImage(lockStatus);
    accumulatePass.WriteStorageImage(lumaHistory);
    if (!bSharpen) {
        accumulatePass.WriteStorageImage(fsr2Output);
    }
    accumulatePass.Execute([pipelineManager, constants, bSharpen, bVirtualMotion, prevHistoryColor, prevLockStatus, prevLumaHistory, dilatedReactive, dilatedMotion, preparedColor, lumaMip4, newLocks,
            historyColor, lockStatus, lumaHistory, fsr2Output, virtualMotion, displayGroupsX, displayGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2AccumulatePushConstant pushData{
                .c = constants,
                .dilatedReactiveIndex = graph.GetSampledImageViewDescriptorIndex(dilatedReactive),
                .dilatedMotionIndex = graph.GetSampledImageViewDescriptorIndex(dilatedMotion),
                .historyColorIndex = graph.GetSampledImageViewDescriptorIndex(prevHistoryColor),
                .lockStatusIndex = graph.GetSampledImageViewDescriptorIndex(prevLockStatus),
                .preparedColorIndex = graph.GetSampledImageViewDescriptorIndex(preparedColor),
                .lumaMip4Index = graph.GetSampledImageViewDescriptorIndex(lumaMip4),
                .lumaHistoryIndex = graph.GetSampledImageViewDescriptorIndex(prevLumaHistory),
                .newLocksIndex = graph.GetStorageImageViewDescriptorIndex(newLocks),
                .historyColorOutIndex = graph.GetStorageImageViewDescriptorIndex(historyColor),
                .lockStatusOutIndex = graph.GetStorageImageViewDescriptorIndex(lockStatus),
                .lumaHistoryOutIndex = graph.GetStorageImageViewDescriptorIndex(lumaHistory),
                .outputIndex = bSharpen ? INVALID_INDEX : graph.GetStorageImageViewDescriptorIndex(fsr2Output),
                .virtualMotionIndex = bVirtualMotion ? graph.GetSampledImageViewDescriptorIndex(virtualMotion) : INVALID_INDEX,
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_accumulate"_sid, &pushData, sizeof(pushData), displayGroupsX, displayGroupsY);
        });

    if (bSharpen) {
        // FSR2 maps sharpness [0, 1] to 2 - 2 * sharpness stops of attenuation
        const float sharpnessLinear = std::exp2(-(2.0f - 2.0f * glm::clamp(config.sharpness, 0.0f, 1.0f)));
        RenderPass& rcasPass = graph.AddPass("FSR2 RCAS"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AntiAliasing);
        rcasPass.ReadSampledImage(historyColor);
        rcasPass.WriteStorageImage(fsr2Output);
        rcasPass.Execute([pipelineManager, constants, sharpnessLinear, historyColor, fsr2Output, displayGroupsX, displayGroupsY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Fsr2RcasPushConstant pushData{
                .c = constants,
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(historyColor),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(fsr2Output),
                .sharpness = sharpnessLinear,
            };
            DispatchFsr2Pass(pipelineManager, cmd, "fsr2_rcas"_sid, &pushData, sizeof(pushData), displayGroupsX, displayGroupsY);
        });
    }

    return fsr2Output;
}
} // Render
