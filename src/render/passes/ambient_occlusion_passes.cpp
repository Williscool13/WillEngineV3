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
GTAOFrame SetupGroundTruthAmbientOcclusion(RenderGraph& graph,
                                           PipelineManager* pipelineManager,
                                           const Core::ViewFamily& viewFamily,
                                           Core::Extent2D renderExtent,
                                           const SceneResources& scene,
                                           const RenderTargets& targets,
                                           uint64_t frameNumber,
                                           uint32_t sceneIndex)
{
    ZoneScoped;
    const Core::GTAOConfiguration& gtaoConfig = viewFamily.gtaoConfig;

    uint32_t denoisePassCount = static_cast<uint32_t>(gtaoConfig.denoisePasses + 0.5f);
    denoisePassCount = denoisePassCount < 1u ? 1u : (denoisePassCount > 8u ? 8u : denoisePassCount);

    const RDGTexture gtaoDepth = graph.CreateTexture("gtao_depth"_sid, TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent.width, renderExtent.height, 5}, {std::nullopt}, true);
    const RDGTexture gtaoAO = graph.CreateTexture("gtao_ao"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture gtaoEdges = graph.CreateTexture("gtao_edges"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture gtaoBentNormals = graph.CreateTexture("gtao_bent_normals"_sid, TextureInfo{VK_FORMAT_R32_UINT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture gtaoFiltered = graph.CreateTexture("gtao_filtered"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    RDGTexture pingPong[2] = {};
    if (denoisePassCount >= 2) {
        pingPong[0] = graph.CreateTexture("gtao_temp"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    }
    if (denoisePassCount >= 3) {
        pingPong[1] = graph.CreateTexture("gtao_temp2"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    }

    RenderPass& depthPrepass = graph.AddPass("GTAO Depth Prepass"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
    depthPrepass.ReadBuffer(scene.sceneData);
    depthPrepass.ReadSampledImage(targets.depthCopy);
    depthPrepass.WriteStorageImage(gtaoDepth);
    depthPrepass.Execute([&scene, pipelineManager, renderExtent, sceneIndex, gtaoDepth,
            depthStencil = targets.depthCopy,
            effectRadius = gtaoConfig.effectRadius,
            effectFalloffRange = gtaoConfig.effectFalloffRange,
            radiusMultiplier = gtaoConfig.radiusMultiplier](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            GTAODepthPrepassPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData) + sizeof(SceneData) * sceneIndex,
                .inputDepth = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputDepth0 = graph.GetStorageImageViewDescriptorIndex(gtaoDepth, 0),
                .outputDepth1 = graph.GetStorageImageViewDescriptorIndex(gtaoDepth, 1),
                .outputDepth2 = graph.GetStorageImageViewDescriptorIndex(gtaoDepth, 2),
                .outputDepth3 = graph.GetStorageImageViewDescriptorIndex(gtaoDepth, 3),
                .outputDepth4 = graph.GetStorageImageViewDescriptorIndex(gtaoDepth, 4),
                .effectRadius = effectRadius,
                .effectFalloffRange = effectFalloffRange,
                .radiusMultiplier = radiusMultiplier,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_depth_prepass"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (renderExtent.width / 2 + GTAO_DEPTH_PREPASS_DISPATCH_X - 1) / GTAO_DEPTH_PREPASS_DISPATCH_X;
            uint32_t yDispatch = (renderExtent.height / 2 + GTAO_DEPTH_PREPASS_DISPATCH_Y - 1) / GTAO_DEPTH_PREPASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    RenderPass& gtaoMainPass = graph.AddPass("GTAO Main"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
    gtaoMainPass.ReadBuffer(scene.sceneData);
    gtaoMainPass.ReadSampledImage(gtaoDepth);
    gtaoMainPass.ReadSampledImage(targets.gbufferOne);
    gtaoMainPass.WriteStorageImage(gtaoAO);
    gtaoMainPass.WriteStorageImage(gtaoEdges);
    gtaoMainPass.WriteStorageImage(gtaoBentNormals);
    gtaoMainPass.Execute([&scene, pipelineManager, renderExtent, sceneIndex, frameNumber, gtaoDepth, gtaoAO, gtaoEdges, gtaoBentNormals,
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
                .sceneData = graph.GetBufferAddress(scene.sceneData) + sizeof(SceneData) * sceneIndex,
                .prefilteredDepthIndex = graph.GetSampledImageViewDescriptorIndex(gtaoDepth),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(normal),
                .aoOutputIndex = graph.GetStorageImageViewDescriptorIndex(gtaoAO),
                .edgeDataIndex = graph.GetStorageImageViewDescriptorIndex(gtaoEdges),

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
                .bentNormalIndex = graph.GetStorageImageViewDescriptorIndex(gtaoBentNormals),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_main"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (renderExtent.width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
            uint32_t yDispatch = (renderExtent.height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    for (uint32_t i = 0; i < denoisePassCount; i++) {
        const bool bFinalPass = i == denoisePassCount - 1;
        const RDGTexture source = i == 0 ? gtaoAO : pingPong[(i - 1) % 2];
        const RDGTexture destination = bFinalPass ? gtaoFiltered : pingPong[i % 2];

        Core::InlineString<32> passName;
        passName = Core::InlineString<32>::Format("GTAO Denoise %u", i + 1);

        RenderPass& denoise = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);
        denoise.ReadBuffer(scene.sceneData);
        denoise.ReadSampledImage(source);
        denoise.ReadSampledImage(gtaoEdges);
        denoise.WriteStorageImage(destination);
        denoise.Execute([&scene, pipelineManager, renderExtent, sceneIndex, source, destination, gtaoEdges, bFinalPass,
                denoiseBlurBeta = gtaoConfig.denoiseBlurBeta](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                GTAODenoisePushConstant pc{
                    .sceneData = graph.GetBufferAddress(scene.sceneData) + sizeof(SceneData) * sceneIndex,
                    .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(source),
                    .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex(gtaoEdges),
                    .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(destination),
                    .denoiseBlurBeta = denoiseBlurBeta,
                    .isFinalDenoisePass = bFinalPass ? 1u : 0u,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_denoise"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                uint32_t xDispatch = (renderExtent.width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
                uint32_t yDispatch = (renderExtent.height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
    }

    return GTAOFrame{.bentNormals = gtaoBentNormals, .filtered = gtaoFiltered};
}
} // Render
