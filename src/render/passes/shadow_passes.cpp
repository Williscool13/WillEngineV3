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
    RenderPass& shadowsResolvePass = graph.AddPass(SID("Shadows Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Shadow);

    bool bHasGTAO = graph.HasTexture(SID("gtao_filtered"));
    if (bHasGTAO) {
        shadowsResolvePass.ReadSampledImage(SID("gtao_filtered"));
    }

    shadowsResolvePass.ReadBuffer(SID("scene_data"));
    shadowsResolvePass.WriteStorageImage(SID("shadows_resolve_target"));
    shadowsResolvePass.Execute([&, pipelineManager, bHasGTAO,
            width = renderExtent[0], height = renderExtent[1], sceneIndex](VkCommandBuffer cmd) {
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

void SetupSigmaShadowDenoise(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Array<uint32_t, 2> renderExtent,
                             const RenderTargets& targets,
                             uint32_t sceneIndex,
                             uint64_t frameNumber)
{
    if (!graph.HasTexture(SID("rt_sun_shadow"))) { return; }

    // R = denoised visibility, G = penumbra (world units)
    graph.CreateTexture(SID("sigma_shadow"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass(SID("SIGMA Shadow Blur"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Shadow);
    pass.ReadBuffer(SID("scene_data"));
    pass.ReadBuffer(SID("light_data"));
    pass.ReadSampledImage(SID("rt_sun_shadow"));
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.WriteStorageImage(SID("sigma_shadow"));
    pass.Execute([&graph, pipelineManager, sceneIndex, renderExtent, frameNumber,
            depth = targets.depthCopy, gbufferOne = targets.gbufferOne](VkCommandBuffer cmd) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("sigma_shadow_blur"));
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaBlurPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(SID("rt_sun_shadow")),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("sigma_shadow")),
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 7) / 8;
            const uint32_t groupsY = (renderExtent[1] + 7) / 8;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
}

void SetupSigmaShadowTemporal(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex)
{
    if (!graph.HasTexture(SID("sigma_shadow"))) { return; }

    graph.CreateTexture(SID("sigma_stabilized"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    const bool bHasHistory = graph.HasTexture(SID("sigma_stabilized_prev"));

    RenderPass& pass = graph.AddPass(SID("SIGMA Shadow Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Shadow);
    pass.ReadBuffer(SID("scene_data"));
    pass.ReadSampledImage(SID("sigma_shadow"));
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    if (bHasHistory) { pass.ReadSampledImage(SID("sigma_stabilized_prev")); }
    pass.WriteStorageImage(SID("sigma_stabilized"));
    pass.Execute([&graph, pipelineManager, sceneIndex, renderExtent, bHasHistory,
            depth = targets.depthCopy, gbufferOne = targets.gbufferOne](VkCommandBuffer cmd) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("sigma_shadow_temporal"));
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(SID("sigma_shadow")),
                .historyIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("sigma_stabilized_prev")) : ~0x0u,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("sigma_stabilized")),
                .sceneDataIndex = sceneIndex,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 7) / 8;
            const uint32_t groupsY = (renderExtent[1] + 7) / 8;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

    graph.CarryTextureToNextFrame(SID("sigma_stabilized"), SID("sigma_stabilized_prev"), VK_IMAGE_USAGE_SAMPLED_BIT);
}
} // Render
