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

    graph.CreateTexture("gtao_depth"_sid, TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 5}, {std::nullopt}, true);
    graph.CreateTexture("gtao_ao"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture("gtao_edges"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture("gtao_bent_normals"_sid, TextureInfo{VK_FORMAT_R32_UINT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture("gtao_filtered"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    if (denoisePassCount >= 2) {
        graph.CreateTexture("gtao_temp"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    }
    if (denoisePassCount >= 3) {
        graph.CreateTexture("gtao_temp2"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    }

    RenderPass& depthPrepass = graph.AddPass("GTAO Depth Prepass"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
    depthPrepass.ReadBuffer("scene_data"_sid);
    depthPrepass.ReadSampledImage(targets.depthCopy);
    depthPrepass.WriteStorageImage("gtao_depth"_sid);
    depthPrepass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            depthStencil = targets.depthCopy,
            effectRadius = gtaoConfig.effectRadius,
            effectFalloffRange = gtaoConfig.effectFalloffRange,
            radiusMultiplier = gtaoConfig.radiusMultiplier](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            GTAODepthPrepassPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"_sid) + sizeof(SceneData) * sceneIndex,
                .inputDepth = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputDepth0 = graph.GetStorageImageViewDescriptorIndex("gtao_depth"_sid, 0),
                .outputDepth1 = graph.GetStorageImageViewDescriptorIndex("gtao_depth"_sid, 1),
                .outputDepth2 = graph.GetStorageImageViewDescriptorIndex("gtao_depth"_sid, 2),
                .outputDepth3 = graph.GetStorageImageViewDescriptorIndex("gtao_depth"_sid, 3),
                .outputDepth4 = graph.GetStorageImageViewDescriptorIndex("gtao_depth"_sid, 4),
                .effectRadius = effectRadius,
                .effectFalloffRange = effectFalloffRange,
                .radiusMultiplier = radiusMultiplier,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_depth_prepass"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width / 2 + GTAO_DEPTH_PREPASS_DISPATCH_X - 1) / GTAO_DEPTH_PREPASS_DISPATCH_X;
            uint32_t yDispatch = (height / 2 + GTAO_DEPTH_PREPASS_DISPATCH_Y - 1) / GTAO_DEPTH_PREPASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    RenderPass& gtaoMainPass = graph.AddPass("GTAO Main"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
    gtaoMainPass.ReadBuffer(SCENE_DATA_BUFFER);
    gtaoMainPass.ReadSampledImage("gtao_depth"_sid);
    gtaoMainPass.ReadSampledImage(targets.gbufferOne);
    gtaoMainPass.WriteStorageImage("gtao_ao"_sid);
    gtaoMainPass.WriteStorageImage("gtao_edges"_sid);
    gtaoMainPass.WriteStorageImage("gtao_bent_normals"_sid);
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
                .prefilteredDepthIndex = graph.GetSampledImageViewDescriptorIndex("gtao_depth"_sid),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(normal),
                .aoOutputIndex = graph.GetStorageImageViewDescriptorIndex("gtao_ao"_sid),
                .edgeDataIndex = graph.GetStorageImageViewDescriptorIndex("gtao_edges"_sid),

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
                .bentNormalIndex = graph.GetStorageImageViewDescriptorIndex("gtao_bent_normals"_sid),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_main"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
            uint32_t yDispatch = (height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    const StringID pingPong[2] = {"gtao_temp"_sid, "gtao_temp2"_sid};
    for (uint32_t i = 0; i < denoisePassCount; i++) {
        const bool bFinalPass = i == denoisePassCount - 1;
        const StringID source = i == 0 ? "gtao_ao"_sid : pingPong[(i - 1) % 2];
        const StringID destination = bFinalPass ? "gtao_filtered"_sid : pingPong[i % 2];

        Core::InlineString<32> passName;
        passName = Core::InlineString<32>::Format("GTAO Denoise %u", i + 1);

        RenderPass& denoise = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
        denoise.ReadBuffer(SCENE_DATA_BUFFER);
        denoise.ReadSampledImage(source);
        denoise.ReadSampledImage("gtao_edges"_sid);
        denoise.WriteStorageImage(destination);
        denoise.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex, source, destination, bFinalPass,
                denoiseBlurBeta = gtaoConfig.denoiseBlurBeta](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                GTAODenoisePushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sizeof(SceneData) * sceneIndex,
                    .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(source),
                    .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex("gtao_edges"_sid),
                    .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(destination),
                    .denoiseBlurBeta = denoiseBlurBeta,
                    .isFinalDenoisePass = bFinalPass ? 1u : 0u,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_denoise"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
                uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
    }
}
} // Render
