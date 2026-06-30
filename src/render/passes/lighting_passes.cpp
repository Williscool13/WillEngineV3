//
// Created by William on 2026-06-03.
//

#include "render/passes/lighting_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupVisibilityLightingResolvePass(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> renderExtent,
                                        const RenderTargets& targets,
                                        uint32_t sceneIndex,
                                        Core::Arena& arena,
                                        uint64_t frameNumber)
{
    if (!graph.HasBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER)) { return; }

    struct LightingEntry
    {
        uint32_t bucketIndex{};
        StringID lightingShader;
    };

    const auto lightingCount = static_cast<uint32_t>(viewFamily.lightingBuckets.Size());
    auto buckets = arena.AllocArray<LightingEntry>(lightingCount);
    uint32_t idx = 0;
    for (const auto& [shader, bucketIndex] : viewFamily.lightingBuckets) {
        buckets[idx++] = {bucketIndex, shader};
    }

    RenderPass& lightingResolve = graph.AddPass(SID("Visibility Lighting Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Lighting);
    lightingResolve.ReadBuffer(SCENE_DATA_BUFFER);
    lightingResolve.ReadBuffer(SID("light_data"));
    if (graph.HasBuffer(SID("restir_reservoir_final"))) {
        lightingResolve.ReadBuffer(SID("restir_reservoir_final"));
    }
    lightingResolve.ReadIndirectBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    lightingResolve.ReadSampledImage(targets.gbufferOne);
    lightingResolve.ReadSampledImage(targets.gbufferTwo);
    lightingResolve.ReadSampledImage(targets.depthCopy);
    if (targets.shadows != StringID{}) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    lightingResolve.WriteStorageImage(targets.colorOutput);
    lightingResolve.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            output = targets.colorOutput, skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity,
            buckets, lightingCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkDeviceAddress lightDispatchAddress = graph.GetBufferAddress(LIGHTING_DISPATCH_BUCKETING_BUFFER);

            for (uint32_t i = 0; i < lightingCount; ++i) {
                const LightingEntry& entry = buckets[i];
                if (!entry.lightingShader) { continue; }

                StringID shaderToUse = viewFamily.lightingShaderOverride ? viewFamily.lightingShaderOverride : entry.lightingShader;
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(shaderToUse);
                if (!pipelineEntry) { continue; }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                VisibilityLightingPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightDispatchBuffer = lightDispatchAddress,
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .reservoirBuffer = graph.TryGetBufferAddress(SID("restir_reservoir_final")),
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                    .skyboxIndex = skyboxIndex,
                    .primaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(output),
                    .secondaryOutputImageIndex = ~0x0u,
                    .sceneDataIndex = sceneIndex,
                    .lightingIndex = entry.bucketIndex,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .iblIntensity = iblIntensity,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                                      entry.bucketIndex * sizeof(LightingDispatchParameters) + offsetof(LightingDispatchParameters, xDispatch));
            }
        });
}

void SetupGroundTruthLightingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  const RenderTargets& targets,
                                  uint32_t sceneIndex,
                                  bool bReset,
                                  uint32_t accumulationCount,
                                  uint64_t frameNumber)
{
    const uint32_t pixelCount = renderExtent[0] * renderExtent[1];
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    if (!graph.HasBuffer(SID("gt_accum"))) {
        graph.CreateBuffer(SID("gt_accum"), bufferSize, false);
        bReset = true;
    }

    if (bReset) {
        RenderPass& clearPass = graph.AddPass(SID("GT Accum Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::ResourceCategory::Lighting);
        clearPass.WriteTransferBuffer(SID("gt_accum"));
        clearPass.Execute([&](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("gt_accum")), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass(SID("Ground Truth Lighting"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Lighting);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(SID("light_data"));
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadWriteBuffer(SID("gt_accum"));
    pass.ReadSampledImage(targets.visibility);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (targets.shadows != StringID{}) {
        pass.ReadSampledImage(targets.shadows);
    }
    pass.WriteStorageImage(targets.colorOutput);
    pass.Execute([&, pipelineManager, sceneIndex, frameNumber, accumulationCount,
            visibility = targets.visibility,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            output = targets.colorOutput, skyboxIndex = viewFamily.skyboxIndex, renderExtent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("lighting_ground_truth"));
            if (!pipelineEntry) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            VisibilityLightingPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightDispatchBuffer = 0,
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .reservoirBuffer = 0,
                .accumulationBuffer = graph.GetBufferAddress(SID("gt_accum")),
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0u,
                .skyboxIndex = skyboxIndex,
                .primaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .secondaryOutputImageIndex = ~0x0u,
                .sceneDataIndex = sceneIndex,
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .accumulationCount = accumulationCount,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

    graph.CarryBufferToNextFrame(SID("gt_accum"), SID("gt_accum"), 0);
}

void SetupDirectionalLightingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  Core::Array<uint32_t, 2> shadowExtent,
                                  const RenderTargets& targets,
                                  uint32_t sceneIndex,
                                  uint32_t pixelScale)
{
    if (!graph.HasTexture(SID("rt_sun_shadow"))) { return; }

    const bool bHalfRes = pixelScale > 1u;

    // Prefer the most-processed shadow available: temporally stabilized > spatially denoised > raw trace.
    StringID shadowTex = SID("rt_sun_shadow");
    if (graph.HasTexture(SID("sigma_shadow"))) { shadowTex = SID("sigma_shadow"); }
    if (graph.HasTexture(SID("sigma_stabilized"))) { shadowTex = SID("sigma_stabilized"); }

    RenderPass& pass = graph.AddPass(SID("Directional Lighting"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Lighting);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(shadowTex);
    if (bHalfRes) {
        pass.ReadSampledImage(SID("rt_sun_depth"));
        pass.ReadSampledImage(SID("rt_sun_gbuffer"));
    }
    pass.ReadWriteImage(targets.colorOutput);
    pass.Execute([&, pipelineManager, sceneIndex, renderExtent, shadowExtent, pixelScale, bHalfRes, shadowTex,
            depth = targets.depthCopy, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("directional_light"));
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            DirectionalLightPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(shadowTex),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .sceneDataIndex = sceneIndex,
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .shadowExtent = {shadowExtent[0], shadowExtent[1]},
                .pixelScale = pixelScale,
                .shadowDepthIndex = bHalfRes ? graph.GetSampledImageViewDescriptorIndex(SID("rt_sun_depth")) : ~0x0u,
                .shadowNormalIndex = bHalfRes ? graph.GetSampledImageViewDescriptorIndex(SID("rt_sun_gbuffer")) : ~0x0u,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
}
} // Render
