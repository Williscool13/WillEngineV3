//
// Created by William on 2026-06-03.
//

#include "render/passes/shadow_passes.h"

#include <tracy/Tracy.hpp>

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
    ZoneScoped;
    graph.CreateTexture("shadows_resolve_target"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    RenderPass& shadowsResolvePass = graph.AddPass("Shadows Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);

    bool bHasGTAO = graph.HasTexture("gtao_filtered"_sid);
    if (bHasGTAO) {
        shadowsResolvePass.ReadSampledImage("gtao_filtered"_sid);
    }

    const float temporalMaxAccum = viewFamily.gtaoConfig.temporalMaxAccum;
    const float temporalClampScale = viewFamily.gtaoConfig.temporalClampScale;
    const bool bTemporal = bHasGTAO && temporalMaxAccum > 0.0f;
    if (bTemporal) {
        graph.CreateVersionedTexture("gtao_temporal"_sid, TextureInfo{VK_FORMAT_R16G16_UNORM, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    const bool bHistoryValid = bTemporal && graph.ResourceHasVersion("gtao_temporal"_sid, 1) && graph.ResourceHasVersion(targets.depthCopy, 1) && graph.ResourceHasVersion(targets.gbufferOne, 1);
    const StringID gtaoTemporalPrev = bHistoryValid ? graph.ResourceVersionID("gtao_temporal"_sid, 1) : StringID{};
    const StringID depthHistory = graph.ResourceVersionID(targets.depthCopy, 1);
    const StringID gbufferOneHistory = graph.ResourceVersionID(targets.gbufferOne, 1);
    if (bTemporal) {
        shadowsResolvePass.ReadSampledImage(targets.depthCopy);
        shadowsResolvePass.ReadSampledImage(targets.gbufferOne);
        shadowsResolvePass.WriteStorageImage("gtao_temporal"_sid);
    }
    if (bHistoryValid) {
        shadowsResolvePass.ReadSampledImage(gtaoTemporalPrev);
        shadowsResolvePass.ReadSampledImage(depthHistory);
        shadowsResolvePass.ReadSampledImage(gbufferOneHistory);
    }

    shadowsResolvePass.ReadBuffer("scene_data"_sid);
    shadowsResolvePass.WriteStorageImage("shadows_resolve_target"_sid);
    shadowsResolvePass.Execute([&, pipelineManager, bHasGTAO, bTemporal, bHistoryValid, gtaoTemporalPrev, depthHistory, gbufferOneHistory, temporalMaxAccum, temporalClampScale,
            depth = targets.depthCopy, gbufferOne = targets.gbufferOne,
            width = renderExtent[0], height = renderExtent[1], sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("shadows_resolve"_sid);

            int32_t gtaoIndex = bHasGTAO ? static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex("gtao_filtered"_sid)) : -1;

            ShadowsResolvePushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"_sid) + sizeof(SceneData) * sceneIndex,
                .gtaoFilteredIndex = gtaoIndex,
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex("shadows_resolve_target"_sid),
                .depthIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(depth) : ~0x0u,
                .gbufferOneIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(gbufferOne) : ~0x0u,
                .historyIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(gtaoTemporalPrev) : ~0x0u,
                .depthHistoryIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0x0u,
                .gbufferOneHistoryIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0x0u,
                .temporalOutputIndex = bTemporal ? graph.GetStorageImageViewDescriptorIndex("gtao_temporal"_sid) : ~0x0u,
                .bHistoryValid = bHistoryValid ? 1u : 0u,
                .temporalMaxAccum = temporalMaxAccum,
                .temporalClampScale = temporalClampScale,
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
    ZoneScoped;
    RenderPass& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    pass.ReadBuffer("scene_data"_sid);
    pass.ReadBuffer("light_data"_sid);
    pass.ReadSampledImage(inputTex);
    pass.ReadSampledImage(tilesTex);
    pass.ReadSampledImage(depth);
    pass.ReadSampledImage(gbufferOne);
    pass.WriteStorageImage(outputTex);
    pass.Execute([pipelineManager, sigma, sceneIndex, renderExtent, frameNumber, passIndex,
            inputTex, outputTex, tilesTex, depth, gbufferOne](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("sigma_shadow_blur"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaBlurPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .lightData = graph.GetBufferAddress("light_data"_sid),
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
    ZoneScoped;
    if (!graph.HasTexture("rt_sun_shadow"_sid)) { return; }

    const Core::SIGMAParams& sigma = viewFamily.sigmaParams;
    // Half res denoises in half-res space against the trace's aux guides; full res uses the full gbuffer.
    const StringID sigmaDepth = sigma.bHalfRes ? "rt_sun_depth"_sid : targets.depthCopy;
    const StringID sigmaGbuffer = sigma.bHalfRes ? "rt_sun_gbuffer"_sid : targets.gbufferOne;

    // R = denoised visibility, G = penumbra (world units)
    graph.CreateTexture("sigma_shadow"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    const uint32_t tilesX = (renderExtent[0] + 15) / 16;
    const uint32_t tilesY = (renderExtent[1] + 15) / 16;
    graph.CreateTexture("sigma_tiles"_sid, TextureInfo{VK_FORMAT_R8G8B8A8_UNORM, tilesX, tilesY, 1}, {std::nullopt}, true);
    graph.CreateTexture("sigma_tiles_smoothed"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, tilesX, tilesY, 1}, {std::nullopt}, true);

    RenderPass& classify = graph.AddPass("[SIGMA] Classify Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    classify.ReadBuffer("scene_data"_sid);
    classify.ReadSampledImage("rt_sun_shadow"_sid);
    classify.ReadSampledImage(sigmaDepth);
    classify.WriteStorageImage("sigma_tiles"_sid);
    classify.Execute([pipelineManager, sigma, sceneIndex, renderExtent, tilesX, tilesY, sigmaDepth](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("sigma_classify_tiles"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaClassifyPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex("rt_sun_shadow"_sid),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(sigmaDepth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("sigma_tiles"_sid),
                .sceneDataIndex = sceneIndex,
                .maxKernelPixels = sigma.maxKernelPixels,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesX, tilesY, 1);
        });

    RenderPass& smooth = graph.AddPass("[SIGMA] Smooth Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    smooth.ReadSampledImage("sigma_tiles"_sid);
    smooth.WriteStorageImage("sigma_tiles_smoothed"_sid);
    smooth.Execute([pipelineManager, tilesX, tilesY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("sigma_smooth_tiles"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaSmoothTilesPushConstant pc{
                .tilesExtent = {tilesX, tilesY},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("sigma_tiles"_sid),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("sigma_tiles_smoothed"_sid),
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (tilesX + 7) / 8, (tilesY + 7) / 8, 1);
        });

    AddSigmaBlurPass(graph, pipelineManager, sigma, renderExtent, sceneIndex, frameNumber,
        "[SIGMA] Shadow Blur"_sid, "rt_sun_shadow"_sid, "sigma_shadow"_sid, "sigma_tiles_smoothed"_sid, 0u,
        sigmaDepth, sigmaGbuffer);

    if (sigma.enablePostBlur) {
        graph.CreateTexture("sigma_shadow_2"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
        AddSigmaBlurPass(graph, pipelineManager, sigma, renderExtent, sceneIndex, frameNumber,
            "[SIGMA] Shadow Post-Blur"_sid, "sigma_shadow"_sid, "sigma_shadow_2"_sid, "sigma_tiles_smoothed"_sid, 1u,
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
    ZoneScoped;
    if (!graph.HasTexture("sigma_shadow"_sid)) { return; }

    const Core::SIGMAParams& sigma = viewFamily.sigmaParams;
    const StringID shadowTex = graph.HasTexture("sigma_shadow_2"_sid) ? "sigma_shadow_2"_sid : "sigma_shadow"_sid;
    const StringID sigmaDepth = sigma.bHalfRes ? "rt_sun_depth"_sid : targets.depthCopy;
    const StringID sigmaGbuffer = sigma.bHalfRes ? "rt_sun_gbuffer"_sid : targets.gbufferOne;

    graph.CreateVersionedTexture("sigma_stabilized"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("sigma_history_length"_sid, TextureInfo{VK_FORMAT_R32_UINT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);

    const bool bHasHistory = graph.ResourceHasVersion("sigma_stabilized"_sid, 1) && graph.ResourceHasVersion("sigma_history_length"_sid, 1);
    const StringID prevStabilized = bHasHistory ? graph.ResourceVersionID("sigma_stabilized"_sid, 1) : StringID{};
    const StringID prevHistoryLength = bHasHistory ? graph.ResourceVersionID("sigma_history_length"_sid, 1) : StringID{};

    RenderPass& pass = graph.AddPass("[SIGMA] Shadow Temporal"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    pass.ReadBuffer("scene_data"_sid);
    pass.ReadSampledImage(shadowTex);
    pass.ReadSampledImage("sigma_tiles_smoothed"_sid);
    pass.ReadSampledImage(sigmaDepth);
    pass.ReadSampledImage(sigmaGbuffer);
    if (bHasHistory) {
        pass.ReadSampledImage(prevStabilized);
        pass.ReadSampledImage(prevHistoryLength);
    }
    pass.WriteStorageImage("sigma_stabilized"_sid);
    pass.WriteStorageImage("sigma_history_length"_sid);
    pass.Execute([pipelineManager, sigma, sceneIndex, renderExtent, bHasHistory, shadowTex, prevStabilized, prevHistoryLength,
            depth = sigmaDepth, gbufferOne = sigmaGbuffer](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("sigma_shadow_temporal"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"_sid),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(shadowTex),
                .historyIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(prevStabilized) : ~0x0u,
                .historyLengthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(prevHistoryLength) : ~0x0u,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("sigma_stabilized"_sid),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex("sigma_history_length"_sid),
                .sceneDataIndex = sceneIndex,
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("sigma_tiles_smoothed"_sid),
                .stabilizationStrength = sigma.historyWeight,
                .pixelScale = sigma.bHalfRes ? 2u : 1u,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 7) / 8;
            const uint32_t groupsY = (renderExtent[1] + 7) / 8;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
}
} // Render
