//
// Created by William on 2026-06-06.
//

#include "render/passes/restir_passes.h"

#include "render/render_config.h"
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
    const uint32_t pixelScale = restirParams.bHalfRes ? 2u : 1u;

    const bool bHasTLAS = graph.HasBuffer(RT_TLAS_BUFFER);
    const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;
    const bool bCombined = restirParams.mode == Core::ReSTIRParams::Mode::CombinedTemporal;

    // Transform all area lights to view space once; every ReSTIR pass and the resolve read this instead of transforming per pixel.
    graph.CreateBuffer(SID("restir_lights_vs"), MAX_AREA_LIGHTS * sizeof(AreaLightVSData), true);

    RenderPass& transformPass = graph.AddPass(SID("ReSTIR Transform Lights"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    transformPass.ReadBuffer(SCENE_DATA_BUFFER);
    transformPass.ReadBuffer(SID("light_data"));
    transformPass.WriteBuffer(SID("restir_lights_vs"));
    transformPass.Execute([&, pipelineManager, sceneIndex](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_transform_lights"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRTransformLightsPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (MAX_AREA_LIGHTS + 63) / 64, 1, 1);
    });

    if (bCombined) {
        // Combined generate+temporal pass
        graph.CreateBuffer(SID("restir_reservoir_temporal"), pixelCount * sizeof(Reservoir), true);

        const bool bHasHistory = graph.HasBuffer(SID("restir_reservoir_history"));

        RenderPass& combinedPass = graph.AddPass(SID("ReSTIR DI Combined Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        combinedPass.ReadBuffer(SCENE_DATA_BUFFER);
        combinedPass.ReadBuffer(SID("light_data"));
        combinedPass.ReadBuffer(SID("restir_lights_vs"));
        combinedPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        if (bHasHistory) { combinedPass.ReadBuffer(SID("restir_reservoir_history")); }
        combinedPass.ReadSampledImage(targets.visibility);
        combinedPass.ReadSampledImage(targets.gbufferOne);
        combinedPass.ReadSampledImage(targets.gbufferTwo);
        combinedPass.ReadSampledImage(targets.depthCopy);
        if (bHasHistory) { combinedPass.ReadSampledImage(SID("gbuffer_one_history")); }
        if (bHasHistory) { combinedPass.ReadSampledImage(SID("depth_history")); }
        if (bHasTLAS) { combinedPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        combinedPass.WriteBuffer(SID("restir_reservoir_temporal"));
        combinedPass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, tlasIndex, bHasHistory, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_combined_temporal"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRDICombinedTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .historyBuffer = bHasHistory ? graph.GetBufferAddress(SID("restir_reservoir_history")) : 0,
                .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .prevGbufferOneIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : ~0u,
                .prevDepthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : ~0u,
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .mCap = restirParams.temporalMCap,
                .pixelScale = pixelScale,
                .tlasIndex = tlasIndex,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
    }
    else {
        // Separate generate pass
        graph.CreateBuffer(SID("restir_reservoir_buffer"), pixelCount * sizeof(Reservoir), true);

        RenderPass& genPass = graph.AddPass(SID("ReSTIR DI Generate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        genPass.ReadBuffer(SCENE_DATA_BUFFER);
        genPass.ReadBuffer(SID("light_data"));
        genPass.ReadBuffer(SID("restir_lights_vs"));
        genPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        genPass.ReadSampledImage(targets.visibility);
        genPass.ReadSampledImage(targets.gbufferOne);
        genPass.ReadSampledImage(targets.gbufferTwo);
        genPass.ReadSampledImage(targets.depthCopy);
        if (bHasTLAS) { genPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        genPass.WriteBuffer(SID("restir_reservoir_buffer"));
        genPass.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent, pixelScale, tlasIndex, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_generate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRDIGeneratePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .reservoirBuffer = graph.GetBufferAddress(SID("restir_reservoir_buffer")),
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .pixelScale = pixelScale,
                .tlasIndex = tlasIndex,
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
            temporalPass.ReadBuffer(SID("restir_lights_vs"));
            temporalPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            temporalPass.ReadBuffer(SID("restir_reservoir_buffer"));
            temporalPass.ReadBuffer(SID("restir_reservoir_history"));
            temporalPass.ReadSampledImage(targets.visibility);
            temporalPass.ReadSampledImage(targets.gbufferOne);
            temporalPass.ReadSampledImage(targets.gbufferTwo);
            temporalPass.ReadSampledImage(targets.depthCopy);
            temporalPass.ReadSampledImage(SID("gbuffer_one_history"));
            temporalPass.ReadSampledImage(SID("depth_history"));
            if (bHasTLAS) { temporalPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
            temporalPass.WriteBuffer(SID("restir_reservoir_temporal"));
            temporalPass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, tlasIndex, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_temporal"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                ReSTIRDITemporalPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .currentBuffer = graph.GetBufferAddress(SID("restir_reservoir_buffer")),
                    .historyBuffer = graph.GetBufferAddress(SID("restir_reservoir_history")),
                    .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .prevGbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")),
                    .prevDepthIndex = graph.GetSampledImageViewDescriptorIndex(SID("depth_history")),
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .sceneDataIndex = sceneIndex,
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .mCap = restirParams.temporalMCap,
                    .pixelScale = pixelScale,
                    .tlasIndex = tlasIndex,
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
    }

    // Spatial Reuse 1
    graph.CreateBuffer(SID("restir_reservoir_spatial"), pixelCount * sizeof(Reservoir), true);

    RenderPass& spatial1Pass = graph.AddPass(SID("ReSTIR DI Spatial"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    spatial1Pass.ReadBuffer(SCENE_DATA_BUFFER);
    spatial1Pass.ReadBuffer(SID("light_data"));
    spatial1Pass.ReadBuffer(SID("restir_lights_vs"));
    spatial1Pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    spatial1Pass.ReadBuffer(SID("restir_reservoir_temporal"));
    spatial1Pass.ReadSampledImage(targets.visibility);
    spatial1Pass.ReadSampledImage(targets.gbufferOne);
    spatial1Pass.ReadSampledImage(targets.gbufferTwo);
    spatial1Pass.ReadSampledImage(targets.depthCopy);
    if (bHasTLAS) { spatial1Pass.ReadTLASBuffer(RT_TLAS_BUFFER); }
    spatial1Pass.WriteBuffer(SID("restir_reservoir_spatial"));
    spatial1Pass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, tlasIndex, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_spatial"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRDISpatialPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .inputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
            .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_spatial")),
            .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .spatialRadius = restirParams.spatialRadius,
            .spatialNeighbors = restirParams.spatialNeighbors,
            .mCap = restirParams.spatialMCap,
            .pixelScale = pixelScale,
            .tlasIndex = tlasIndex,
            .passIndex = 0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 15) / 16;
        const uint32_t groupsY = (renderExtent[1] + 15) / 16;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    graph.CarryBufferToNextFrame(SID("restir_reservoir_temporal"), SID("restir_reservoir_history"), 0);

    if (restirParams.debugStop == Core::ReSTIRDebugStop::Spatial1 || !restirParams.bSpatial2) {
        graph.AliasBuffer(SID("restir_reservoir_final"), SID("restir_reservoir_spatial"));
        return;
    }

    // Spatial Reuse 2
    graph.CreateBuffer(SID("restir_reservoir_spatial2"), pixelCount * sizeof(Reservoir), true);

    RenderPass& spatial2Pass = graph.AddPass(SID("ReSTIR DI Spatial 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    spatial2Pass.ReadBuffer(SCENE_DATA_BUFFER);
    spatial2Pass.ReadBuffer(SID("light_data"));
    spatial2Pass.ReadBuffer(SID("restir_lights_vs"));
    spatial2Pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    spatial2Pass.ReadBuffer(SID("restir_reservoir_spatial"));
    spatial2Pass.ReadSampledImage(targets.visibility);
    spatial2Pass.ReadSampledImage(targets.gbufferOne);
    spatial2Pass.ReadSampledImage(targets.gbufferTwo);
    spatial2Pass.ReadSampledImage(targets.depthCopy);
    if (bHasTLAS) { spatial2Pass.ReadTLASBuffer(RT_TLAS_BUFFER); }
    spatial2Pass.WriteBuffer(SID("restir_reservoir_spatial2"));
    spatial2Pass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, tlasIndex, visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_spatial"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRDISpatialPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .inputBuffer = graph.GetBufferAddress(SID("restir_reservoir_spatial")),
            .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_spatial2")),
            .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .spatialRadius = restirParams.spatialRadius,
            .spatialNeighbors = restirParams.spatialNeighbors,
            .mCap = restirParams.spatialMCap,
            .pixelScale = pixelScale,
            .tlasIndex = tlasIndex,
            .passIndex = 1u,
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
                                    uint64_t frameNumber,
                                    uint32_t pixelScale)
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
    if (graph.HasBuffer(SID("restir_lights_vs"))) {
        lightingResolve.ReadBuffer(SID("restir_lights_vs"));
    }
    lightingResolve.ReadIndirectBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    lightingResolve.ReadSampledImage(targets.gbufferOne);
    lightingResolve.ReadSampledImage(targets.gbufferTwo);
    lightingResolve.ReadSampledImage(targets.depthCopy);
    if (targets.shadows != StringID{}) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    lightingResolve.WriteStorageImage(targets.intermediateOne);
    lightingResolve.WriteStorageImage(targets.intermediateTwo);
    lightingResolve.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent, pixelScale,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
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
                    .lightVS = graph.TryGetBufferAddress(SID("restir_lights_vs")),
                    .lightDispatchBuffer = lightDispatchAddress,
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .reservoirBuffer = graph.TryGetBufferAddress(SID("restir_reservoir_final")),
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                    .skyboxIndex = skyboxIndex,
                    .primaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(diffuseOut),
                    .secondaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(specularOut),
                    .sceneDataIndex = sceneIndex,
                    .lightingIndex = entry.bucketIndex,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .pixelScale = pixelScale,
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
                               uint32_t outputMode,
                               uint32_t pixelScale,
                               float iblIntensity,
                               uint64_t frameNumber)
{
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];

    RenderPass& pass = graph.AddPass(SID("ReSTIR Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.intermediateOne);
    pass.ReadSampledImage(targets.intermediateTwo);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    pass.WriteStorageImage(targets.colorOutput);
    const int32_t skyboxIndex = viewFamily.skyboxIndex;
    pass.Execute([&graph, pipelineManager, sceneIndex, outputMode, pixelScale, width, height, skyboxIndex, iblIntensity, frameNumber,
            diffuse = targets.intermediateOne, specular = targets.intermediateTwo,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, output = targets.colorOutput](VkCommandBuffer cmd) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .sceneDataIndex = sceneIndex,
                .diffuseIndex = graph.GetSampledImageViewDescriptorIndex(diffuse),
                .specularIndex = graph.GetSampledImageViewDescriptorIndex(specular),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .width = width,
                .height = height,
                .outputMode = outputMode,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .pixelScale = pixelScale,
                .skyboxIndex = skyboxIndex,
                .iblIntensity = iblIntensity,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("restir_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
}
} // Render
