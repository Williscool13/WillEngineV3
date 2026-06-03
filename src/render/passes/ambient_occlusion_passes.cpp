//
// Created by William on 2026-06-03.
//

#include "render/passes/ambient_occlusion_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupGroundTruthAmbientOcclusion(RenderGraph& graph,
                                      PipelineManager* pipelineManager,
                                      const Core::ViewFamily& viewFamily,
                                      Core::Array<uint32_t, 2> renderExtent,
                                      const MainRenderTargets& targets,
                                      uint64_t frameNumber,
                                      uint32_t sceneIndex)
{
    const Core::GTAOConfiguration& gtaoConfig = viewFamily.gtaoConfig;

    graph.CreateTexture(SID("gtao_depth"), TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 5}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_ao"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_edges"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    // Denoise pass(es) - typically run 2-3 times for better quality
    graph.CreateTexture(SID("gtao_temp"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_filtered"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& depthPrepass = graph.AddPass(SID("GTAO Depth Prepass"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AmbientOcclusion);
    depthPrepass.ReadBuffer(SID("scene_data"));
    depthPrepass.ReadSampledImage(targets.depthStencil);
    depthPrepass.WriteStorageImage(SID("gtao_depth"));
    depthPrepass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            depthStencil = targets.depthStencil,
            effectRadius = gtaoConfig.effectRadius,
            effectFalloffRange = gtaoConfig.effectFalloffRange,
            radiusMultiplier = gtaoConfig.radiusMultiplier](VkCommandBuffer cmd) {
            GTAODepthPrepassPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
                .inputDepth = graph.GetDepthOnlySampledImageViewDescriptorIndex(depthStencil),
                .outputDepth0 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 0),
                .outputDepth1 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 1),
                .outputDepth2 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 2),
                .outputDepth3 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 3),
                .outputDepth4 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 4),
                .effectRadius = effectRadius,
                .effectFalloffRange = effectFalloffRange,
                .radiusMultiplier = radiusMultiplier,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_depth_prepass"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width / 2 + GTAO_DEPTH_PREPASS_DISPATCH_X - 1) / GTAO_DEPTH_PREPASS_DISPATCH_X;
            uint32_t yDispatch = (height / 2 + GTAO_DEPTH_PREPASS_DISPATCH_Y - 1) / GTAO_DEPTH_PREPASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    RenderPass& gtaoMainPass = graph.AddPass(SID("GTAO Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AmbientOcclusion);
    gtaoMainPass.ReadSampledImage(SID("gtao_depth"));
    gtaoMainPass.ReadSampledImage(targets.gbufferOne);
    gtaoMainPass.WriteStorageImage(SID("gtao_ao"));
    gtaoMainPass.WriteStorageImage(SID("gtao_edges"));
    gtaoMainPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex, frameNumber,
            normal = targets.gbufferOne,
            effectRadius = gtaoConfig.effectRadius,
            radiusMultiplier = gtaoConfig.radiusMultiplier,
            effectFalloffRange = gtaoConfig.effectFalloffRange,
            sampleDistributionPower = gtaoConfig.sampleDistributionPower,
            thinOccluderCompensation = gtaoConfig.thinOccluderCompensation,
            finalValuePower = gtaoConfig.finalValuePower,
            depthMipSamplingOffset = gtaoConfig.depthMipSamplingOffset,
            sliceCount = gtaoConfig.sliceCount,
            stepsPerSlice = gtaoConfig.stepsPerSlice](VkCommandBuffer cmd) {
            GTAOMainPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sizeof(SceneData) * sceneIndex,
                .prefilteredDepthIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_depth")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(normal),
                .aoOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_ao")),
                .edgeDataIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_edges")),

                .effectRadius = effectRadius,
                .radiusMultiplier = radiusMultiplier,
                .effectFalloffRange = effectFalloffRange,
                .sampleDistributionPower = sampleDistributionPower,
                .thinOccluderCompensation = thinOccluderCompensation,
                .finalValuePower = finalValuePower,
                .depthMipSamplingOffset = depthMipSamplingOffset,
                .sliceCount = sliceCount,
                .stepsPerSlice = stepsPerSlice,
                .noiseIndex = static_cast<uint32_t>(frameNumber % 64),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_main"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
            uint32_t yDispatch = (height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    RenderPass& denoise1 = graph.AddPass(SID("GTAO Denoise 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AmbientOcclusion);
    denoise1.ReadSampledImage(SID("gtao_ao"));
    denoise1.ReadSampledImage(SID("gtao_edges"));
    denoise1.WriteStorageImage(SID("gtao_temp"));
    denoise1.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex](VkCommandBuffer cmd) {
        GTAODenoisePushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
            .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_ao")),
            .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_edges")),
            .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_temp")),
            .denoiseBlurBeta = 1e4f,
            .isFinalDenoisePass = 0,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_denoise"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& denoise2 = graph.AddPass(SID("GTAO Denoise 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::AmbientOcclusion);
    denoise2.ReadSampledImage(SID("gtao_temp"));
    denoise2.ReadSampledImage(SID("gtao_edges"));
    denoise2.WriteStorageImage(SID("gtao_filtered"));
    denoise2.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            denoiseBlurBeta = gtaoConfig.denoiseBlurBeta](VkCommandBuffer cmd) {
            GTAODenoisePushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
                .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_temp")),
                .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_edges")),
                .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_filtered")),
                .denoiseBlurBeta = denoiseBlurBeta,
                .isFinalDenoisePass = 1,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_denoise"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
            uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}
} // Render
