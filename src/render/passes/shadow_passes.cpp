//
// Created by William on 2026-06-03.
//

#include "render/passes/shadow_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupShadowsResolve(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Array<uint32_t, 2> renderExtent,
                         const RenderTargets& targets,
                         uint32_t sceneIndex)
{
    graph.CreateTexture(SID("shadows_resolve_target"), TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    RenderPass& shadowsResolvePass = graph.AddPass(SID("Shadows Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);

    bool bHasGTAO = graph.HasTexture(SID("gtao_filtered"));
    if (bHasGTAO) {
        shadowsResolvePass.ReadSampledImage(SID("gtao_filtered"));
    }

    shadowsResolvePass.ReadBuffer(SID("scene_data"));
    shadowsResolvePass.WriteStorageImage(SID("shadows_resolve_target"));
    shadowsResolvePass.Execute([&, pipelineManager, bHasGTAO,
            width = renderExtent[0], height = renderExtent[1], sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("shadows_resolve"));

            int32_t gtaoIndex = bHasGTAO ? static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("gtao_filtered"))) : -1;

            ShadowsResolvePushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
                .gtaoFilteredIndex = gtaoIndex,
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("shadows_resolve_target")),
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}

static void AddSigmaBlurPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::SIGMAParams& sigma,
                             Core::Array<uint32_t, 2> renderExtent,
                             uint32_t sceneIndex,
                             uint64_t frameNumber,
                             StringID passName,
                             StringID inputTex,
                             StringID outputTex,
                             StringID tilesTex,
                             uint32_t passIndex,
                             StringID depth,
                             StringID gbufferOne)
{
    RenderPass& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    pass.ReadBuffer(SID("scene_data"));
    pass.ReadBuffer(SID("light_data"));
    pass.ReadSampledImage(inputTex);
    pass.ReadSampledImage(tilesTex);
    pass.ReadSampledImage(depth);
    pass.ReadSampledImage(gbufferOne);
    pass.WriteStorageImage(outputTex);
    pass.Execute([pipelineManager, sigma, sceneIndex, renderExtent, frameNumber, passIndex,
            inputTex, outputTex, tilesTex, depth, gbufferOne](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("sigma_shadow_blur"));
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaBlurPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(inputTex),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(outputTex),
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tilesTex),
                .passIndex = passIndex,
                .maxKernelPixels = sigma.maxKernelPixels,
                .penumbraScale = sigma.penumbraScale,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 7) / 8;
            const uint32_t groupsY = (renderExtent[1] + 7) / 8;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
}

void SetupSigmaShadowDenoise(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Array<uint32_t, 2> renderExtent,
                             const RenderTargets& targets,
                             uint32_t sceneIndex,
                             uint64_t frameNumber)
{
    if (!graph.HasTexture(SID("rt_sun_shadow"))) { return; }

    const Core::SIGMAParams& sigma = viewFamily.sigmaParams;
    // Half res denoises in half-res space against the trace's aux guides; full res uses the full gbuffer.
    const StringID sigmaDepth = sigma.bHalfRes ? SID("rt_sun_depth") : targets.depthCopy;
    const StringID sigmaGbuffer = sigma.bHalfRes ? SID("rt_sun_gbuffer") : targets.gbufferOne;

    // R = denoised visibility, G = penumbra (world units)
    graph.CreateTexture(SID("sigma_shadow"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    const uint32_t tilesX = (renderExtent[0] + 15) / 16;
    const uint32_t tilesY = (renderExtent[1] + 15) / 16;
    graph.CreateTexture(SID("sigma_tiles"), TextureInfo{VK_FORMAT_R8G8B8A8_UNORM, tilesX, tilesY, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("sigma_tiles_smoothed"), TextureInfo{VK_FORMAT_R8G8_UNORM, tilesX, tilesY, 1}, {std::nullopt}, true);

    RenderPass& classify = graph.AddPass(SID("[SIGMA] Classify Tiles"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    classify.ReadBuffer(SID("scene_data"));
    classify.ReadSampledImage(SID("rt_sun_shadow"));
    classify.ReadSampledImage(sigmaDepth);
    classify.WriteStorageImage(SID("sigma_tiles"));
    classify.Execute([pipelineManager, sigma, sceneIndex, renderExtent, tilesX, tilesY, sigmaDepth](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("sigma_classify_tiles"));
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaClassifyPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(SID("rt_sun_shadow")),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(sigmaDepth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("sigma_tiles")),
                .sceneDataIndex = sceneIndex,
                .maxKernelPixels = sigma.maxKernelPixels,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesX, tilesY, 1);
        });

    RenderPass& smooth = graph.AddPass(SID("[SIGMA] Smooth Tiles"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    smooth.ReadSampledImage(SID("sigma_tiles"));
    smooth.WriteStorageImage(SID("sigma_tiles_smoothed"));
    smooth.Execute([pipelineManager, tilesX, tilesY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("sigma_smooth_tiles"));
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaSmoothTilesPushConstant pc{
                .tilesExtent = {tilesX, tilesY},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("sigma_tiles")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("sigma_tiles_smoothed")),
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (tilesX + 7) / 8, (tilesY + 7) / 8, 1);
        });

    AddSigmaBlurPass(graph, pipelineManager, sigma, renderExtent, sceneIndex, frameNumber,
        SID("[SIGMA] Shadow Blur"), SID("rt_sun_shadow"), SID("sigma_shadow"), SID("sigma_tiles_smoothed"), 0u,
        sigmaDepth, sigmaGbuffer);

    if (sigma.enablePostBlur) {
        graph.CreateTexture(SID("sigma_shadow_2"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
        AddSigmaBlurPass(graph, pipelineManager, sigma, renderExtent, sceneIndex, frameNumber,
            SID("[SIGMA] Shadow Post-Blur"), SID("sigma_shadow"), SID("sigma_shadow_2"), SID("sigma_tiles_smoothed"), 1u,
            sigmaDepth, sigmaGbuffer);
    }
}

void SetupSigmaShadowTemporal(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex)
{
    if (!graph.HasTexture(SID("sigma_shadow"))) { return; }

    const Core::SIGMAParams& sigma = viewFamily.sigmaParams;
    const StringID shadowTex = graph.HasTexture(SID("sigma_shadow_2")) ? SID("sigma_shadow_2") : SID("sigma_shadow");
    const StringID sigmaDepth = sigma.bHalfRes ? SID("rt_sun_depth") : targets.depthCopy;
    const StringID sigmaGbuffer = sigma.bHalfRes ? SID("rt_sun_gbuffer") : targets.gbufferOne;

    graph.CreateTexture(SID("sigma_stabilized"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("sigma_history_length"), TextureInfo{VK_FORMAT_R32_UINT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    const bool bHasHistory = graph.HasTexture(SID("sigma_stabilized_prev")) && graph.HasTexture(SID("sigma_history_length_prev"));

    RenderPass& pass = graph.AddPass(SID("[SIGMA] Shadow Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    pass.ReadBuffer(SID("scene_data"));
    pass.ReadSampledImage(shadowTex);
    pass.ReadSampledImage(SID("sigma_tiles_smoothed"));
    pass.ReadSampledImage(sigmaDepth);
    pass.ReadSampledImage(sigmaGbuffer);
    if (bHasHistory) {
        pass.ReadSampledImage(SID("sigma_stabilized_prev"));
        pass.ReadSampledImage(SID("sigma_history_length_prev"));
    }
    pass.WriteStorageImage(SID("sigma_stabilized"));
    pass.WriteStorageImage(SID("sigma_history_length"));
    pass.Execute([pipelineManager, sigma, sceneIndex, renderExtent, bHasHistory, shadowTex,
            depth = sigmaDepth, gbufferOne = sigmaGbuffer](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("sigma_shadow_temporal"));
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(shadowTex),
                .historyIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("sigma_stabilized_prev")) : ~0x0u,
                .historyLengthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("sigma_history_length_prev")) : ~0x0u,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("sigma_stabilized")),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(SID("sigma_history_length")),
                .sceneDataIndex = sceneIndex,
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("sigma_tiles_smoothed")),
                .stabilizationStrength = sigma.historyWeight,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 7) / 8;
            const uint32_t groupsY = (renderExtent[1] + 7) / 8;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

    graph.CarryTextureToNextFrame(SID("sigma_stabilized"), SID("sigma_stabilized_prev"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("sigma_history_length"), SID("sigma_history_length_prev"), VK_IMAGE_USAGE_SAMPLED_BIT);
}
} // Render
