//
// Created by William on 2026-06-03.
//

#include "render/passes/ambient_occlusion_passes.h"

#include <tracy/Tracy.hpp>

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
                                      const RenderTargets& targets,
                                      uint64_t frameNumber,
                                      uint32_t sceneIndex)
{
    ZoneScoped;
    const Core::GTAOConfiguration& gtaoConfig = viewFamily.gtaoConfig;

    uint32_t denoisePassCount = static_cast<uint32_t>(gtaoConfig.denoisePasses + 0.5f);
    denoisePassCount = denoisePassCount < 1u ? 1u : (denoisePassCount > 8u ? 8u : denoisePassCount);

    graph.CreateTexture(SID("gtao_depth"), TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 5}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_ao"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_edges"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_bent_normals"), TextureInfo{VK_FORMAT_R32_UINT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_filtered"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    if (denoisePassCount >= 2) {
        graph.CreateTexture(SID("gtao_temp"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    }
    if (denoisePassCount >= 3) {
        graph.CreateTexture(SID("gtao_temp2"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    }

    RenderPass& depthPrepass = graph.AddPass(SID("GTAO Depth Prepass"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
    depthPrepass.ReadBuffer(SID("scene_data"));
    depthPrepass.ReadSampledImage(targets.depthCopy);
    depthPrepass.WriteStorageImage(SID("gtao_depth"));
    depthPrepass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            depthStencil = targets.depthCopy,
            effectRadius = gtaoConfig.effectRadius,
            effectFalloffRange = gtaoConfig.effectFalloffRange,
            radiusMultiplier = gtaoConfig.radiusMultiplier](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            GTAODepthPrepassPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
                .inputDepth = graph.GetSampledImageViewDescriptorIndex(depthStencil),
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

    RenderPass& gtaoMainPass = graph.AddPass(SID("GTAO Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
    gtaoMainPass.ReadBuffer(SCENE_DATA_BUFFER);
    gtaoMainPass.ReadSampledImage(SID("gtao_depth"));
    gtaoMainPass.ReadSampledImage(targets.gbufferOne);
    gtaoMainPass.WriteStorageImage(SID("gtao_ao"));
    gtaoMainPass.WriteStorageImage(SID("gtao_edges"));
    gtaoMainPass.WriteStorageImage(SID("gtao_bent_normals"));
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
            stepsPerSlice = gtaoConfig.stepsPerSlice](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .bentNormalIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_bent_normals")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_main"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
            uint32_t yDispatch = (height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    const StringID pingPong[2] = {SID("gtao_temp"), SID("gtao_temp2")};
    for (uint32_t i = 0; i < denoisePassCount; i++) {
        const bool bFinalPass = i == denoisePassCount - 1;
        const StringID source = i == 0 ? SID("gtao_ao") : pingPong[(i - 1) % 2];
        const StringID destination = bFinalPass ? SID("gtao_filtered") : pingPong[i % 2];

        Core::InlineString<32> passName;
        passName = Core::InlineString<32>::Format("GTAO Denoise %u", i + 1);

        RenderPass& denoise = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
        denoise.ReadBuffer(SCENE_DATA_BUFFER);
        denoise.ReadSampledImage(source);
        denoise.ReadSampledImage(SID("gtao_edges"));
        denoise.WriteStorageImage(destination);
        denoise.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex, source, destination, bFinalPass,
                denoiseBlurBeta = gtaoConfig.denoiseBlurBeta](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                GTAODenoisePushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sizeof(SceneData) * sceneIndex,
                    .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(source),
                    .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_edges")),
                    .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(destination),
                    .denoiseBlurBeta = denoiseBlurBeta,
                    .isFinalDenoisePass = bFinalPass ? 1u : 0u,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_denoise"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
                uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
    }
}
} // Render
