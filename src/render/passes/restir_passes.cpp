//
// Created by William on 2026-06-06.
//

#include "render/passes/restir_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupReSTIRPasses(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex,
                       Core::Arena& arena,
                       uint64_t frameNumber,
                       const Core::ReSTIRParams& restirParams)
{
    const uint32_t pixelCount = renderExtent[0] * renderExtent[1];

    // Generate
    graph.CreateBuffer(SID("restir_reservoir_buffer"), pixelCount * sizeof(Reservoir), true);

    RenderPass& genPass = graph.AddPass(SID("ReSTIR DI Generate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    genPass.ReadBuffer(SCENE_DATA_BUFFER);
    genPass.ReadBuffer(SID("light_data"));
    genPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    genPass.ReadSampledImage(targets.visibility);
    genPass.ReadSampledImage(targets.gbufferOne);
    genPass.ReadSampledImage(targets.gbufferTwo);
    genPass.ReadSampledImage(targets.depthStencil);
    genPass.WriteBuffer(SID("restir_reservoir_buffer"));
    genPass.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthStencil](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_generate"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRDIGeneratePushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .reservoirBuffer = graph.GetBufferAddress(SID("restir_reservoir_buffer")),
            .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 15) / 16;
        const uint32_t groupsY = (renderExtent[1] + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    if (restirParams.debugStop == Core::ReSTIRDebugStop::Generate) {
        graph.CarryBufferToNextFrame(SID("restir_reservoir_buffer"), SID("restir_reservoir_history"), 0);
        graph.AliasBuffer(SID("restir_reservoir_final"), SID("restir_reservoir_buffer"));
        return;
    }

    // Temporal Reuse
    if (!graph.HasBuffer(SID("restir_reservoir_history"))) {
        graph.AliasBuffer(SID("restir_reservoir_temporal"), SID("restir_reservoir_buffer"));
    }
    else {
        graph.CreateBuffer(SID("restir_reservoir_temporal"), pixelCount * sizeof(Reservoir), true);

        RenderPass& temporalPass = graph.AddPass(SID("ReSTIR DI Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        temporalPass.ReadBuffer(SCENE_DATA_BUFFER);
        temporalPass.ReadBuffer(SID("light_data"));
        temporalPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        temporalPass.ReadBuffer(SID("restir_reservoir_buffer"));
        temporalPass.ReadBuffer(SID("restir_reservoir_history"));
        temporalPass.ReadSampledImage(targets.visibility);
        temporalPass.ReadSampledImage(targets.gbufferOne);
        temporalPass.ReadSampledImage(targets.gbufferTwo);
        temporalPass.ReadSampledImage(targets.depthStencil);
        temporalPass.ReadSampledImage(SID("gbuffer_one_history"));
        temporalPass.ReadSampledImage(SID("depth_history"));
        temporalPass.WriteBuffer(SID("restir_reservoir_temporal"));
        temporalPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthStencil](VkCommandBuffer cmd) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_temporal"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRDITemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .currentBuffer = graph.GetBufferAddress(SID("restir_reservoir_buffer")),
                .historyBuffer = graph.GetBufferAddress(SID("restir_reservoir_history")),
                .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .prevGbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")),
                .prevDepthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(SID("depth_history")),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .mCap = restirParams.temporalMCap,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
    }

    if (restirParams.debugStop == Core::ReSTIRDebugStop::Temporal) {
        graph.CarryBufferToNextFrame(SID("restir_reservoir_temporal"), SID("restir_reservoir_history"), 0);
        graph.AliasBuffer(SID("restir_reservoir_final"), SID("restir_reservoir_temporal"));
        return;
    }

    // Spatial Reuse 1
    graph.CreateBuffer(SID("restir_reservoir_spatial"), pixelCount * sizeof(Reservoir), true);

    RenderPass& spatial1Pass = graph.AddPass(SID("ReSTIR DI Spatial"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    spatial1Pass.ReadBuffer(SCENE_DATA_BUFFER);
    spatial1Pass.ReadBuffer(SID("light_data"));
    spatial1Pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    spatial1Pass.ReadBuffer(SID("restir_reservoir_temporal"));
    spatial1Pass.ReadSampledImage(targets.visibility);
    spatial1Pass.ReadSampledImage(targets.gbufferOne);
    spatial1Pass.ReadSampledImage(targets.gbufferTwo);
    spatial1Pass.ReadSampledImage(targets.depthStencil);
    spatial1Pass.WriteBuffer(SID("restir_reservoir_spatial"));
    spatial1Pass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthStencil](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_spatial"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRDISpatialPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .inputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
            .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_spatial")),
            .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .spatialRadius = restirParams.spatialRadius,
            .spatialNeighbors = restirParams.spatialNeighbors,
            .mCap = restirParams.spatialMCap,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 15) / 16;
        const uint32_t groupsY = (renderExtent[1] + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    if (restirParams.debugStop == Core::ReSTIRDebugStop::Spatial1) {
        graph.CarryBufferToNextFrame(SID("restir_reservoir_spatial"), SID("restir_reservoir_history"), 0);
        graph.AliasBuffer(SID("restir_reservoir_final"), SID("restir_reservoir_spatial"));
        return;
    }

    // Spatial Reuse 2
    graph.CreateBuffer(SID("restir_reservoir_spatial2"), pixelCount * sizeof(Reservoir), true);
    graph.CarryBufferToNextFrame(SID("restir_reservoir_spatial2"), SID("restir_reservoir_history"), 0);

    RenderPass& spatial2Pass = graph.AddPass(SID("ReSTIR DI Spatial 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    spatial2Pass.ReadBuffer(SCENE_DATA_BUFFER);
    spatial2Pass.ReadBuffer(SID("light_data"));
    spatial2Pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    spatial2Pass.ReadBuffer(SID("restir_reservoir_spatial"));
    spatial2Pass.ReadSampledImage(targets.visibility);
    spatial2Pass.ReadSampledImage(targets.gbufferOne);
    spatial2Pass.ReadSampledImage(targets.gbufferTwo);
    spatial2Pass.ReadSampledImage(targets.depthStencil);
    spatial2Pass.WriteBuffer(SID("restir_reservoir_spatial2"));
    spatial2Pass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthStencil](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_spatial"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRDISpatialPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .inputBuffer = graph.GetBufferAddress(SID("restir_reservoir_spatial")),
            .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_spatial2")),
            .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .spatialRadius = restirParams.spatialRadius,
            .spatialNeighbors = restirParams.spatialNeighbors,
            .mCap = restirParams.spatialMCap,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 15) / 16;
        const uint32_t groupsY = (renderExtent[1] + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    graph.AliasBuffer(SID("restir_reservoir_final"), SID("restir_reservoir_spatial2"));
}

void SetupReSTIRLightingResolvePass(RenderGraph& graph,
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

    RenderPass& lightingResolve = graph.AddPass(SID("ReSTIR Lighting Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    lightingResolve.ReadBuffer(SCENE_DATA_BUFFER);
    lightingResolve.ReadBuffer(SHADOW_DATA_BUFFER);
    lightingResolve.ReadBuffer(SID("light_data"));
    if (graph.HasBuffer(SID("restir_reservoir_final"))) {
        lightingResolve.ReadBuffer(SID("restir_reservoir_final"));
    }
    lightingResolve.ReadIndirectBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    lightingResolve.ReadSampledImage(targets.gbufferOne);
    lightingResolve.ReadSampledImage(targets.gbufferTwo);
    lightingResolve.ReadSampledImage(targets.depthStencil);
    if (targets.shadows != StringID{}) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    lightingResolve.WriteStorageImage(targets.intermediateOne);
    lightingResolve.WriteStorageImage(targets.intermediateTwo);
    lightingResolve.Execute([&, pipelineManager, sceneIndex, frameNumber,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthStencil, shadows = targets.shadows,
            diffuseOut = targets.intermediateOne, specularOut = targets.intermediateTwo, skyboxIndex = viewFamily.skyboxIndex,
            buckets, lightingCount](VkCommandBuffer cmd) {
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
                    .shadowData = graph.GetBufferAddress(SHADOW_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightDispatchBuffer = lightDispatchAddress,
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .reservoirBuffer = graph.TryGetBufferAddress(SID("restir_reservoir_final")),
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                    .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                    .skyboxIndex = skyboxIndex,
                    .primaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(diffuseOut),
                    .secondaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(specularOut),
                    .sceneDataIndex = sceneIndex,
                    .lightingIndex = entry.bucketIndex,
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                                      entry.bucketIndex * sizeof(LightingDispatchParameters) + offsetof(LightingDispatchParameters, xDispatch));
            }
        });
}

void SetupReSTIRRemodulatePass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               Core::Array<uint32_t, 2> renderExtent,
                               const RenderTargets& targets,
                               uint32_t sceneIndex,
                               uint32_t outputMode)
{
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];

    RenderPass& pass = graph.AddPass(SID("ReSTIR Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.intermediateOne);
    pass.ReadSampledImage(targets.intermediateTwo);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthStencil);
    pass.WriteStorageImage(targets.colorOutput);
    pass.Execute([&graph, pipelineManager, sceneIndex, outputMode, width, height,
            diffuse = targets.intermediateOne, specular = targets.intermediateTwo,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthStencil, output = targets.colorOutput](VkCommandBuffer cmd) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .sceneDataIndex = sceneIndex,
                .diffuseIndex = graph.GetSampledImageViewDescriptorIndex(diffuse),
                .specularIndex = graph.GetSampledImageViewDescriptorIndex(specular),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .width = width,
                .height = height,
                .outputMode = outputMode,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("restir_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
}
} // Render
