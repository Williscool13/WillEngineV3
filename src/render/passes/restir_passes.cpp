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
void SetupQuadSelectionPass(RenderGraph& graph,
                            PipelineManager* pipelineManager,
                            Core::Array<uint32_t, 2> renderExtent,
                            const RenderTargets& targets,
                            uint32_t sceneIndex,
                            uint64_t frameNumber)
{
    graph.CreateTexture(SID("quad_selection"), TextureInfo{VK_FORMAT_R32_UINT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass(SID("[ReSTIR DI] Quad Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.WriteStorageImage(SID("quad_selection"));
    pass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, depth = targets.depthCopy, gbufferOne = targets.gbufferOne](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_quad_selection"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        QuadSelectionPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .normalIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .outIndex = graph.GetStorageImageViewDescriptorIndex(SID("quad_selection")),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .pixelScale = 2u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 7) / 8, (renderExtent[1] + 7) / 8, 1);
    });
}

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
    const uint32_t reservoirBufferSize = pixelCount * static_cast<uint32_t>(sizeof(Reservoir));

    const bool bHasTLAS = graph.HasBuffer(RT_TLAS_BUFFER);
    const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;
    // History confidence for the RELAX denoiser; only RELAX consumes it.
    const bool bConfidence = restirParams.confidenceStrength > 0.0f && restirParams.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX;
    // Antilag reuses the shadow-flip signal but needs only the shadow-vis history, not the confidence texture or RELAX.
    const bool bAntilag = restirParams.antilagStrength > 0.0f;
    const bool bShadowVis = bConfidence || bAntilag;

    // Transform all lights (area + sphere) to view space once; every ReSTIR pass and the resolve read this instead of transforming per pixel.
    graph.CreateBuffer(SID("restir_lights_vs"), MAX_LIGHTS * sizeof(LightVSData), true);

    RenderPass& transformPass = graph.AddPass(SID("[ReSTIR DI] Transform Lights"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    transformPass.ReadBuffer(SCENE_DATA_BUFFER);
    transformPass.ReadBuffer(SID("light_data"));
    transformPass.WriteBuffer(SID("restir_lights_vs"));
    transformPass.Execute([&, pipelineManager, sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_transform_lights"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRTransformLightsPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (MAX_LIGHTS + 63) / 64, 1, 1);
    });

    graph.CreateBuffer(SID("regir_grid"), REGIR_CELL_COUNT * REGIR_RESERVOIRS_PER_CELL * static_cast<uint32_t>(sizeof(Reservoir)), true);

    const uint32_t regirHistoryCount = restirParams.regirHistoryLength < REGIR_HISTORY_LENGTH ? restirParams.regirHistoryLength : REGIR_HISTORY_LENGTH;

    StringID historyNames[REGIR_HISTORY_LENGTH];
    bool bHasHistory[REGIR_HISTORY_LENGTH] = {};
    for (uint32_t g = 0; g < regirHistoryCount; g++) {
        const Core::InlineString<32> name = Core::InlineString<32>::Format("regir_grid_history%u", g);
        historyNames[g] = StringID(name.c_str(), name.Size());
        bHasHistory[g] = graph.HasBuffer(historyNames[g]);
    }

    RenderPass& regirFillPass = graph.AddPass(SID("[ReGIR] Fill"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    regirFillPass.ReadBuffer(SCENE_DATA_BUFFER);
    regirFillPass.ReadBuffer(SID("light_data"));
    regirFillPass.ReadBuffer(SID("restir_lights_vs"));
    for (uint32_t g = 0; g < regirHistoryCount; g++) {
        if (bHasHistory[g]) { regirFillPass.ReadBuffer(historyNames[g]); }
    }
    regirFillPass.WriteBuffer(SID("regir_grid"));
    regirFillPass.Execute([&, pipelineManager, sceneIndex, frameNumber, regirHistoryCount, historyNames, bHasHistory](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("regir_fill"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReGIRFillPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
            .gridBuffer = graph.GetBufferAddress(SID("regir_grid")),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .historyCount = regirHistoryCount,
        };
        for (uint32_t g = 0; g < regirHistoryCount; g++) {
            pc.gridHistory[g] = bHasHistory[g] ? graph.GetBufferAddress(historyNames[g]) : 0;
        }
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t totalSlots = REGIR_CELL_COUNT * REGIR_RESERVOIRS_PER_CELL;
        vkCmdDispatch(cmd, (totalSlots + 63) / 64, 1, 1);
    });

    if (regirHistoryCount > 0) {
        graph.CarryBufferToNextFrame(SID("regir_grid"), historyNames[0], 0);
        for (uint32_t g = 0; g + 1 < regirHistoryCount; g++) {
            if (bHasHistory[g]) { graph.CarryBufferToNextFrame(historyNames[g], historyNames[g + 1], 0); }
        }
    }

    {
        graph.CreateBuffer(SID("restir_reservoir_temporal"), reservoirBufferSize, true);

        const bool bHasHistory = graph.HasBuffer(SID("restir_reservoir_history"));
        const bool bHasQuadHistory = restirParams.bHalfRes && graph.HasTexture(SID("quad_selection_history"));
        const bool bHasPrevVis = bShadowVis && graph.HasTexture(SID("restir_shadow_vis_prev"));
        if (bShadowVis) {
            graph.CreateTexture(SID("restir_shadow_vis"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
        }
        if (bConfidence) {
            graph.CreateTexture(SID("restir_confidence"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
        }

        RenderPass& combinedPass = graph.AddPass(SID("[ReSTIR DI] Combined Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        combinedPass.ReadBuffer(SCENE_DATA_BUFFER);
        combinedPass.ReadBuffer(SID("light_data"));
        combinedPass.ReadBuffer(SID("restir_lights_vs"));
        combinedPass.ReadBuffer(SID("regir_grid"));
        combinedPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        if (bHasHistory) { combinedPass.ReadBuffer(SID("restir_reservoir_history")); }
        combinedPass.ReadSampledImage(targets.gbufferOne);
        combinedPass.ReadSampledImage(targets.gbufferTwo);
        combinedPass.ReadSampledImage(targets.depthCopy);
        if (bHasHistory) { combinedPass.ReadSampledImage(SID("gbuffer_one_history")); }
        if (bHasHistory) { combinedPass.ReadSampledImage(SID("depth_history")); }
        if (bHasPrevVis) { combinedPass.ReadSampledImage(SID("restir_shadow_vis_prev")); }
        if (restirParams.bHalfRes) { combinedPass.ReadSampledImage(SID("quad_selection")); }
        if (bHasQuadHistory) { combinedPass.ReadSampledImage(SID("quad_selection_history")); }
        if (bHasTLAS) { combinedPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        combinedPass.WriteBuffer(SID("restir_reservoir_temporal"));
        if (bShadowVis) { combinedPass.WriteStorageImage(SID("restir_shadow_vis")); }
        if (bConfidence) { combinedPass.WriteStorageImage(SID("restir_confidence")); }
        combinedPass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, tlasIndex, bHasHistory, bHasQuadHistory, bConfidence, bShadowVis, bHasPrevVis, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_combined_temporal"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRDICombinedTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .gridBuffer = graph.GetBufferAddress(SID("regir_grid")),
                .historyBuffer = bHasHistory ? graph.GetBufferAddress(SID("restir_reservoir_history")) : 0,
                .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
                .visibilityBufferIndex = ~0u,
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
                .prevShadowVisIndex = bHasPrevVis ? graph.GetSampledImageViewDescriptorIndex(SID("restir_shadow_vis_prev")) : ~0u,
                .shadowVisIndex = bShadowVis ? graph.GetStorageImageViewDescriptorIndex(SID("restir_shadow_vis")) : ~0u,
                .confidenceIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex(SID("restir_confidence")) : ~0u,
                .confidenceStrength = restirParams.confidenceStrength,
                .bPermutationSampling = restirParams.bPermutationSampling ? 1u : 0u,
                .antilagStrength = restirParams.antilagStrength,
                .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
                .quadSelectionHistoryIndex = (pixelScale == 2u) ? (bHasQuadHistory ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection_history")) : graph.GetSampledImageViewDescriptorIndex(SID("quad_selection"))) : ~0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
    }

    StringID reuseBuffer = SID("restir_reservoir_temporal");

    graph.CarryBufferToNextFrame(reuseBuffer, SID("restir_reservoir_history"), 0);
    if (bShadowVis) {
        graph.CarryTextureToNextFrame(SID("restir_shadow_vis"), SID("restir_shadow_vis_prev"), VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    // Spatial reuse chain: N passes ping-ponging two scratch buffers, each reading the previous output. Pass 0 reads the temporal reuseBuffer.
    const uint32_t spatialPasses = restirParams.spatialPasses < 1u ? 1u : restirParams.spatialPasses;
    const StringID spatialScratch[2] = {SID("restir_reservoir_spatial"), SID("restir_reservoir_spatial2")};
    graph.CreateBuffer(spatialScratch[0], reservoirBufferSize, true);
    if (spatialPasses > 1u) {
        graph.CreateBuffer(spatialScratch[1], reservoirBufferSize, true);
    }

    for (uint32_t i = 0; i < spatialPasses; i++) {
        const StringID inputName = i == 0u ? reuseBuffer : spatialScratch[i - 1u & 1u];
        const StringID outputName = spatialScratch[i & 1u];

        const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReSTIR DI] Spatial %u", i);

        RenderPass& spatialPass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        spatialPass.ReadBuffer(SCENE_DATA_BUFFER);
        spatialPass.ReadBuffer(SID("light_data"));
        spatialPass.ReadBuffer(SID("restir_lights_vs"));
        spatialPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        spatialPass.ReadBuffer(inputName);
        spatialPass.ReadSampledImage(targets.gbufferOne);
        spatialPass.ReadSampledImage(targets.gbufferTwo);
        spatialPass.ReadSampledImage(targets.depthCopy);
        if (restirParams.bHalfRes) { spatialPass.ReadSampledImage(SID("quad_selection")); }
        if (bHasTLAS) { spatialPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        spatialPass.WriteBuffer(outputName);
        spatialPass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, tlasIndex, inputName, outputName, passIndex = i, bLastPass = (i == spatialPasses - 1u), gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_spatial"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRDISpatialPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .inputBuffer = graph.GetBufferAddress(inputName),
                .outputBuffer = graph.GetBufferAddress(outputName),
                .visibilityBufferIndex = ~0u,
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
                .passIndex = passIndex,
                .bAdaptiveSpatial = restirParams.bAdaptiveSpatial ? 1u : 0u,
                .adaptiveSpatialBoost = restirParams.adaptiveSpatialBoost,
                .adaptiveMReference = restirParams.temporalMCap,
                .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
                .bValidateVisibility = bLastPass ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
    }

    graph.AliasBuffer(SID("restir_reservoir_final"), spatialScratch[spatialPasses - 1u & 1u]);
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

    RenderPass& lightingResolve = graph.AddPass(SID("[ReSTIR DI] Lighting Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    lightingResolve.ReadBuffer(SCENE_DATA_BUFFER);
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
    if (pixelScale == 2u) { lightingResolve.ReadSampledImage(SID("quad_selection")); }
    if (targets.shadows != StringID{}) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    lightingResolve.WriteStorageImage(targets.intermediateOne);
    lightingResolve.WriteStorageImage(targets.intermediateTwo);
    lightingResolve.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent, pixelScale,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            diffuseOut = targets.intermediateOne, specularOut = targets.intermediateTwo, skyboxIndex = viewFamily.skyboxIndex,
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
                    .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
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

    RenderPass& pass = graph.AddPass(SID("[ReSTIR DI] Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.intermediateOne);
    pass.ReadSampledImage(targets.intermediateTwo);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (pixelScale == 2u) { pass.ReadSampledImage(SID("quad_selection")); }
    pass.WriteStorageImage(targets.colorOutput);
    const int32_t skyboxIndex = viewFamily.skyboxIndex;
    pass.Execute([pipelineManager, sceneIndex, outputMode, pixelScale, width, height, skyboxIndex, iblIntensity, frameNumber,
            diffuse = targets.intermediateOne, specular = targets.intermediateTwo,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("restir_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
}
} // Render
