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
} // Render
