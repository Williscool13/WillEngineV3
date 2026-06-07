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
    shadowsResolvePass.ReadSampledImage(targets.gbufferOne);
    shadowsResolvePass.ReadSampledImage(targets.depthCopy);

    bool bHasGTAO = graph.HasTexture(SID("gtao_filtered"));
    if (bHasGTAO) {
        shadowsResolvePass.ReadSampledImage(SID("gtao_filtered"));
    }

    bool bHasShadows = graph.HasTexture(SID("shadow_cascade_0"));
    if (bHasShadows) {
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_0"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_1"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_2"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_3"));
    }

    shadowsResolvePass.ReadBuffer(SID("scene_data"));
    shadowsResolvePass.ReadBuffer(SID("shadow_data"));
    shadowsResolvePass.ReadBuffer(SID("light_data"));
    shadowsResolvePass.WriteStorageImage(SID("shadows_resolve_target"));
    shadowsResolvePass.Execute([&, pipelineManager, bHasShadows, bHasGTAO,
            width = renderExtent[0], height = renderExtent[1], sceneIndex,
            depthStencil = targets.depthCopy, gbufferOne = targets.gbufferOne](VkCommandBuffer cmd) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("shadows_resolve"));

            glm::ivec4 csmIndices{-1, -1, -1, -1};
            if (bHasShadows) {
                csmIndices.x = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_0")));
                csmIndices.y = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_1")));
                csmIndices.z = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_2")));
                csmIndices.w = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_3")));
            }

            int32_t gtaoIndex = bHasGTAO ? static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("gtao_filtered"))) : -1;

            ShadowsResolvePushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
                .shadowData = graph.GetBufferAddress(SID("shadow_data")),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .gtaoFilteredIndex = gtaoIndex,
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("shadows_resolve_target")),
                .csmIndices = csmIndices,
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}
} // Render
