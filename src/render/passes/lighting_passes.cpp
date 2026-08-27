//
// Created by William on 2026-06-03.
//

#include "render/passes/lighting_passes.h"

#include <tracy/Tracy.hpp>

#include "ddgi_passes.h"
#include "final_gather_passes.h"
#include "reflection_passes.h"
#include "shadow_passes.h"
#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupFrustumBinningPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             uint32_t sceneIndex,
                             float clusterZNear,
                             float clusterZFar)
{
    ZoneScoped;
    if (!graph.HasBuffer(LIGHT_DATA_BUFFER)) { return; }

    const VkDeviceSize gridBytes = static_cast<VkDeviceSize>(CLUSTER_COUNT) * 2u * sizeof(uint32_t);
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(CLUSTER_COUNT) * MAX_LIGHTS_PER_CLUSTER * sizeof(uint32_t);
    graph.CreateBuffer(SID("cluster_light_grid"), gridBytes, false);
    graph.CreateBuffer(SID("cluster_light_index_list"), indexBytes, false);

    RenderPass& cull = graph.AddPass(SID("Frustum Binning"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::FrustumBinning);
    cull.ReadBuffer(SCENE_DATA_BUFFER);
    cull.ReadBuffer(LIGHT_DATA_BUFFER);
    cull.WriteBuffer(SID("cluster_light_grid"));
    cull.WriteBuffer(SID("cluster_light_index_list"));
    cull.Execute([pipelineManager, sceneIndex, clusterZNear, clusterZFar](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("frustum_binning"));
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        FrustumBinningPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .lightGrid = graph.GetBufferAddress(SID("cluster_light_grid")),
            .lightIndexList = graph.GetBufferAddress(SID("cluster_light_index_list")),
            .zNear = clusterZNear,
            .zFar = clusterZFar,
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (CLUSTER_COUNT + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });
}

void SetupWorldGridBinningPass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               uint32_t sceneIndex,
                               Core::Arena& arena,
                               const DDGICascades& ddgiCascades)
{
    ZoneScoped;
    if (!graph.HasBuffer(LIGHT_DATA_BUFFER)) { return; }

    const VkDeviceSize gridBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * 2u * sizeof(uint32_t);
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * MAX_LIGHTS_PER_WORLD_GRID_CELL * sizeof(uint32_t);
    const VkDeviceSize emissiveIndexBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * MAX_EMISSIVE_GROUPS_PER_WORLD_GRID_CELL * sizeof(uint32_t);
    graph.CreateBuffer(SID("world_grid_light_grid"), gridBytes, false);
    graph.CreateBuffer(SID("world_grid_index_list"), indexBytes, false);
    graph.CreateBuffer(SID("world_grid_emissive_grid"), gridBytes, false);
    graph.CreateBuffer(SID("world_grid_emissive_index_list"), emissiveIndexBytes, false);
    graph.CreateBuffer(SID("world_grid_cell_power"), static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * 2u * sizeof(float), false);
    graph.CreateBuffer(SID("world_grid_probe_grid"), static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * sizeof(uint32_t), false);

    const VkDeviceSize ddgiIndexBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * MAX_DDGI_VOLUMES_PER_WORLD_GRID_CELL * sizeof(uint32_t);
    graph.CreateBuffer(SID("world_grid_ddgi_grid"), gridBytes, false);
    graph.CreateBuffer(SID("world_grid_ddgi_index_list"), ddgiIndexBytes, false);
    graph.CreateBuffer(SID("ddgi_volume_windows"), sizeof(DDGIVolumeParams) * DDGI_MAX_RESIDENT_LOCAL_VOLUMES, false);

    // Cascades occupy volumes[0, count); the resident world volumes follow, and the bin emits their absolute slot so the sampler indexes cascades[] with no remap.
    const uint32_t volumeSlotBase = ddgiCascades.count;
    const uint32_t volumeCount = glm::min(ddgiCascades.localCount, DDGI_MAX_RESIDENT_LOCAL_VOLUMES);
    if (volumeCount > 0u) {
        DDGIVolumeParams* windows = arena.AllocArray<DDGIVolumeParams>(volumeCount);
        for (uint32_t i = 0; i < volumeCount; ++i) {
            windows[i] = ddgiCascades.volumes[volumeSlotBase + i];
        }
        const VkDeviceSize windowBytes = sizeof(DDGIVolumeParams) * volumeCount;
        RenderPass& upload = graph.AddPass(SID("DDGI Volume Windows"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::WorldGridBinning);
        upload.WriteTransferBuffer(SID("ddgi_volume_windows"));
        upload.Execute([windows, windowBytes](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(SID("ddgi_volume_windows")), 0, windowBytes, windows);
        });
    }

    const uint32_t probeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());

    RenderPass& binning = graph.AddPass(SID("World Grid Binning"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::WorldGridBinning);
    binning.ReadBuffer(SCENE_DATA_BUFFER);
    binning.ReadBuffer(LIGHT_DATA_BUFFER);
    binning.ReadBuffer(REFLECTION_PROBE_BUFFER);
    binning.WriteBuffer(SID("world_grid_light_grid"));
    binning.WriteBuffer(SID("world_grid_index_list"));
    binning.WriteBuffer(SID("world_grid_emissive_grid"));
    binning.WriteBuffer(SID("world_grid_emissive_index_list"));
    binning.WriteBuffer(SID("world_grid_cell_power"));
    binning.WriteBuffer(SID("world_grid_probe_grid"));
    binning.WriteBuffer(SID("world_grid_ddgi_grid"));
    binning.WriteBuffer(SID("world_grid_ddgi_index_list"));
    if (volumeCount > 0u) { binning.ReadBuffer(SID("ddgi_volume_windows")); }
    binning.Execute([pipelineManager, sceneIndex, probeCount, volumeCount, volumeSlotBase](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("world_grid_binning"));
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        WorldGridBinningPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .worldGridBuffer = graph.GetBufferAddress(SID("world_grid_light_grid")),
            .worldGridIndexList = graph.GetBufferAddress(SID("world_grid_index_list")),
            .worldGridEmissiveGrid = graph.GetBufferAddress(SID("world_grid_emissive_grid")),
            .worldGridEmissiveIndexList = graph.GetBufferAddress(SID("world_grid_emissive_index_list")),
            .worldGridCellPower = graph.GetBufferAddress(SID("world_grid_cell_power")),
            .sceneDataIndex = sceneIndex,
            .reflectionProbes = probeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
            .worldGridProbeGrid = graph.GetBufferAddress(SID("world_grid_probe_grid")),
            .reflectionProbeCount = probeCount,
            .ddgiVolumes = volumeCount > 0u ? graph.GetBufferAddress(SID("ddgi_volume_windows")) : 0,
            .worldGridDDGIGrid = graph.GetBufferAddress(SID("world_grid_ddgi_grid")),
            .worldGridDDGIIndexList = graph.GetBufferAddress(SID("world_grid_ddgi_index_list")),
            .ddgiVolumeCount = volumeCount,
            .ddgiVolumeSlotBase = volumeSlotBase,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (WORLD_GRID_CELL_COUNT + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });
}

void SetupVisibilityLightingResolvePass(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> renderExtent,
                                        const RenderTargets& targets,
                                        uint32_t sceneIndex,
                                        Core::Arena& arena,
                                        uint64_t frameNumber,
                                        bool bDDGIApply,
                                        uint32_t giGatherMode,
                                        const Core::ReflectionConfiguration& reflectionConfig)
{
    ZoneScoped;
    if (!graph.HasBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER)) { return; }

    const bool bWorldGrid = graph.HasBuffer(SID("world_grid_light_grid")) && graph.HasBuffer(SID("world_grid_index_list"));

    const bool bDDGI = bDDGIApply && graph.HasBuffer(DDGI_CASCADES_BUFFER);
    const bool bGIGather = giGatherMode != 0u && graph.HasTexture(GI_GATHER_RESOLVED);

    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    const StringID reflectionTarget = REFLECTION_SPEC_NOISY_TARGET;
    const bool bReflection = reflectionRoughnessMax >= 0.0f && graph.HasTexture(reflectionTarget);


    RenderPass& lightingResolve = graph.AddPass(SID("Visibility Lighting Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::LightingResolve);
    lightingResolve.ReadBuffer(SCENE_DATA_BUFFER);
    lightingResolve.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    lightingResolve.ReadBuffer(SID("light_data"));
    lightingResolve.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (bWorldGrid) {
        lightingResolve.ReadBuffer(SID("world_grid_light_grid"));
        lightingResolve.ReadBuffer(SID("world_grid_index_list"));
    }
    if (graph.HasBuffer(SID("world_grid_probe_grid"))) {
        lightingResolve.ReadBuffer(SID("world_grid_probe_grid"));
    }
    if (graph.HasBuffer(SID("restir_reservoir_final"))) {
        lightingResolve.ReadBuffer(SID("restir_reservoir_final"));
    }
    lightingResolve.ReadIndirectBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    lightingResolve.ReadSampledImage(targets.visibility);
    lightingResolve.ReadSampledImage(targets.gbufferOne);
    lightingResolve.ReadSampledImage(targets.gbufferTwo);
    lightingResolve.ReadSampledImage(targets.depthCopy);
    if (targets.shadows != StringID{}) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    if (bDDGI) {
        AddDDGISampleDependencies(graph, lightingResolve);
    }
    if (bGIGather) {
        lightingResolve.ReadSampledImage(GI_GATHER_RESOLVED);
        lightingResolve.ReadSampledImage(GI_GATHER_DATA);
    }
    if (bReflection) {
        lightingResolve.ReadSampledImage(reflectionTarget);
    }
    lightingResolve.WriteStorageImage(targets.colorOutput);
    lightingResolve.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            output = targets.colorOutput, skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity,
            bDDGI, bWorldGrid, bGIGather, giGatherMode, bReflection, reflectionTarget, reflectionRoughnessMax, lightSpecularFromReflectionsMax = reflectionConfig.lightSpecularFromReflectionsMax
            ](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkDeviceAddress lightDispatchAddress = graph.GetBufferAddress(LIGHTING_DISPATCH_BUCKETING_BUFFER);

            for (const LightingPipelineInfo& entry : pipelineManager->GetLightingPipelines()) {
                if (!entry.id) { continue; }

                StringID shaderToUse = viewFamily.lightingShaderOverride ? viewFamily.lightingShaderOverride : entry.id;
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
                    .lightingIndex = entry.index,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .iblIntensity = iblIntensity,
                    .reflectionIndex = bReflection ? graph.GetSampledImageViewDescriptorIndex(reflectionTarget) : ~0x0u,
                    .ddgiCascades = bDDGI ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
                    .bDDGIApply = bDDGI ? 1u : 0u,
                    .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_light_grid")) : 0,
                    .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_index_list")) : 0,
                    .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_RESOLVED) : ~0x0u,
                    .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA) : ~0x0u,
                    .giGatherMode = bGIGather ? giGatherMode : 0u,
                    .reflectionRoughnessMax = reflectionRoughnessMax,
                    .lightSpecularFromReflectionsMax = lightSpecularFromReflectionsMax,
                    .reflectionProbes = viewFamily.reflectionProbes.Size() > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
                    .reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size()),
                    .worldGridProbeGrid = (!viewFamily.bReflectionProbeBruteForce && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                                      entry.index * sizeof(LightingDispatchParameters) + offsetof(LightingDispatchParameters, xDispatch));
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
    ZoneScoped;
    const uint32_t pixelCount = renderExtent[0] * renderExtent[1];
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    if (!graph.HasBuffer(SID("gt_accum"))) {
        graph.CreateBuffer(SID("gt_accum"), bufferSize, false);
        bReset = true;
    }

    if (bReset) {
        RenderPass& clearPass = graph.AddPass(SID("GT Accum Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer(SID("gt_accum"));
        clearPass.Execute([&](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("gt_accum")), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass(SID("Ground Truth Lighting"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::GroundTruth);
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
    ZoneScoped;
    if (!graph.HasTexture(SID("rt_sun_shadow"))) { return; }

    const bool bHalfRes = pixelScale > 1u;

    // Prefer the most-processed shadow available: temporally stabilized > spatially denoised > raw trace.
    StringID shadowTex = SID("rt_sun_shadow");
    if (graph.HasTexture(SID("sigma_shadow"))) { shadowTex = SID("sigma_shadow"); }
    if (graph.HasTexture(SID("sigma_stabilized"))) { shadowTex = SID("sigma_stabilized"); }

    RenderPass& pass = graph.AddPass(SID("Directional Lighting"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
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
