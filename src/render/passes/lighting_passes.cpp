//
// Created by William on 2026-06-03.
//

#include "render/passes/lighting_passes.h"

#include <tracy/Tracy.hpp>

#include "render/passes/ddgi_passes.h"
#include "render/passes/final_gather_passes.h"
#include "render/passes/reflection_passes.h"
#include "render/passes/shadow_passes.h"
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
                             const SceneResources& scene,
                             uint32_t sceneIndex,
                             float clusterZNear,
                             float clusterZFar)
{
    ZoneScoped;
    if (!scene.lightData.IsValid()) { return; }

    const VkDeviceSize gridBytes = static_cast<VkDeviceSize>(CLUSTER_COUNT) * 2u * sizeof(uint32_t);
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(CLUSTER_COUNT) * MAX_LIGHTS_PER_CLUSTER * sizeof(uint32_t);
    const RDGBuffer lightGrid = graph.CreateBuffer("cluster_light_grid"_sid, gridBytes, false);
    const RDGBuffer lightIndexList = graph.CreateBuffer("cluster_light_index_list"_sid, indexBytes, false);

    RenderPass& cull = graph.AddPass("Frustum Binning"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::FrustumBinning);
    cull.ReadBuffer(scene.sceneData);
    cull.ReadBuffer(scene.lightData);
    cull.WriteBuffer(lightGrid);
    cull.WriteBuffer(lightIndexList);
    cull.Execute([&scene, pipelineManager, sceneIndex, clusterZNear, clusterZFar, lightGrid, lightIndexList](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("frustum_binning"_sid);
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        FrustumBinningPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .lightGrid = graph.GetBufferAddress(lightGrid),
            .lightIndexList = graph.GetBufferAddress(lightIndexList),
            .zNear = clusterZNear,
            .zFar = clusterZFar,
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (CLUSTER_COUNT + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });
}

void SetupEmissiveTriLightPass(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, const SceneResources& scene, float emissiveTriRangeMultiplier)
{
    ZoneScoped;
    if (viewFamily.triLightCount == 0 || !scene.lightData.IsValid()) { return; }

    const auto workCount = static_cast<uint32_t>(viewFamily.emissiveTriWork.Size());
    if (workCount == 0 || !scene.emissiveTriWork.IsValid()) { return; }

    const bool bHasInstances = scene.instances.IsValid() && scene.models.IsValid() && scene.materials.IsValid();

    RenderPass& pass = graph.AddPass("Emissive Tri Lights"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::ReSTIRDI);
    pass.AsyncCompute();
    pass.ReadBuffer(scene.emissiveTriWork);
    if (bHasInstances) {
        pass.ReadBuffer(scene.instances);
        pass.ReadBuffer(scene.models);
        pass.ReadBuffer(scene.materials);
    }
    pass.ReadBuffer(scene.primitives);
    pass.ReadBuffer(scene.vertexPositions);
    pass.ReadBuffer(scene.indices);
    pass.ReadBuffer(scene.meshlets);
    pass.WriteBuffer(scene.lightData);
    pass.Execute([&scene, pipelineManager, workCount, emissiveTriRangeMultiplier, bHasInstances](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("emissive_tri_lights"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        EmissiveTriLightPushConstant pc{
            .workBuffer = graph.GetBufferAddress(scene.emissiveTriWork),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .instanceBuffer = bHasInstances ? graph.GetBufferAddress(scene.instances) : 0,
            .modelBuffer = bHasInstances ? graph.GetBufferAddress(scene.models) : 0,
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .materialBuffer = bHasInstances ? graph.GetBufferAddress(scene.materials) : 0,
            .vertexPosBuffer = graph.GetBufferAddress(scene.vertexPositions),
            .indexBuffer = graph.GetBufferAddress(scene.indices),
            .meshletBuffer = graph.GetBufferAddress(scene.meshlets),
            .workCount = workCount,
            .rangeMultiplier = emissiveTriRangeMultiplier,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, workCount, 1, 1);
    });
}

WorldGridFrame SetupWorldGridBinningPass(RenderGraph& graph,
                                         PipelineManager* pipelineManager,
                                         const Core::ViewFamily& viewFamily,
                                         const SceneResources& scene,
                                         uint32_t sceneIndex,
                                         Core::Arena& arena,
                                         const DDGICascades& ddgiCascades)
{
    ZoneScoped;
    if (!scene.lightData.IsValid()) { return {}; }

    const VkDeviceSize gridBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * 2u * sizeof(uint32_t);
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * MAX_LIGHTS_PER_WORLD_GRID_CELL * sizeof(uint32_t);
    const VkDeviceSize emissiveIndexBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * MAX_EMISSIVE_MESHLETS_PER_WORLD_GRID_CELL * sizeof(uint32_t);
    WorldGridFrame worldGrid{};
    worldGrid.lightGrid = graph.CreateBuffer("world_grid_light_grid"_sid, gridBytes, false);
    worldGrid.indexList = graph.CreateBuffer("world_grid_index_list"_sid, indexBytes, false);
    worldGrid.emissiveGrid = graph.CreateBuffer("world_grid_emissive_grid"_sid, gridBytes, false);
    worldGrid.emissiveIndexList = graph.CreateBuffer("world_grid_emissive_index_list"_sid, emissiveIndexBytes, false);
    worldGrid.cellPower = graph.CreateBuffer("world_grid_cell_power"_sid, static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * 2u * sizeof(float), false);
    worldGrid.probeGrid = graph.CreateBuffer("world_grid_probe_grid"_sid, static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * sizeof(uint32_t), false);

    const VkDeviceSize ddgiIndexBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * MAX_DDGI_VOLUMES_PER_WORLD_GRID_CELL * sizeof(uint32_t);
    worldGrid.ddgiGrid = graph.CreateBuffer("world_grid_ddgi_grid"_sid, gridBytes, false);
    worldGrid.ddgiIndexList = graph.CreateBuffer("world_grid_ddgi_index_list"_sid, ddgiIndexBytes, false);
    const RDGBuffer volumeWindows = graph.CreateBuffer("ddgi_volume_windows"_sid, sizeof(DDGIVolumeParams) * DDGI_MAX_RESIDENT_LOCAL_VOLUMES, false);

    // Cascades occupy volumes[0, count) and world volumes follow; the bin emits absolute slots, so no remap.
    const uint32_t volumeSlotBase = ddgiCascades.count;
    const uint32_t volumeCount = glm::min(ddgiCascades.localCount, DDGI_MAX_RESIDENT_LOCAL_VOLUMES);
    if (volumeCount > 0u) {
        DDGIVolumeParams* windows = arena.AllocArray<DDGIVolumeParams>(volumeCount);
        for (uint32_t i = 0; i < volumeCount; ++i) {
            windows[i] = ddgiCascades.volumes[volumeSlotBase + i];
        }
        const VkDeviceSize windowBytes = sizeof(DDGIVolumeParams) * volumeCount;
        RenderPass& upload = graph.AddPass("DDGI Volume Windows"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::WorldGridBinning);
        upload.AsyncCompute();
        upload.WriteTransferBuffer(volumeWindows);
        upload.Execute([windows, windowBytes, volumeWindows](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(volumeWindows), 0, windowBytes, windows);
        });
    }

    const uint32_t probeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());

    RenderPass& binning = graph.AddPass("World Grid Binning"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::WorldGridBinning);
    binning.AsyncCompute();
    binning.ReadBuffer(scene.sceneData);
    binning.ReadBuffer(scene.lightData);
    binning.ReadBuffer(scene.reflectionProbes);
    binning.WriteBuffer(worldGrid.lightGrid);
    binning.WriteBuffer(worldGrid.indexList);
    binning.WriteBuffer(worldGrid.emissiveGrid);
    binning.WriteBuffer(worldGrid.emissiveIndexList);
    binning.WriteBuffer(worldGrid.cellPower);
    binning.WriteBuffer(worldGrid.probeGrid);
    binning.WriteBuffer(worldGrid.ddgiGrid);
    binning.WriteBuffer(worldGrid.ddgiIndexList);
    if (volumeCount > 0u) { binning.ReadBuffer(volumeWindows); }
    binning.Execute([&scene, pipelineManager, sceneIndex, probeCount, volumeCount, volumeSlotBase, worldGrid, volumeWindows](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("world_grid_binning"_sid);
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        WorldGridBinningPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .worldGridBuffer = graph.GetBufferAddress(worldGrid.lightGrid),
            .worldGridIndexList = graph.GetBufferAddress(worldGrid.indexList),
            .worldGridEmissiveGrid = graph.GetBufferAddress(worldGrid.emissiveGrid),
            .worldGridEmissiveIndexList = graph.GetBufferAddress(worldGrid.emissiveIndexList),
            .worldGridCellPower = graph.GetBufferAddress(worldGrid.cellPower),
            .sceneDataIndex = sceneIndex,
            .reflectionProbes = probeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
            .worldGridProbeGrid = graph.GetBufferAddress(worldGrid.probeGrid),
            .reflectionProbeCount = probeCount,
            .ddgiVolumes = volumeCount > 0u ? graph.GetBufferAddress(volumeWindows) : 0,
            .worldGridDDGIGrid = graph.GetBufferAddress(worldGrid.ddgiGrid),
            .worldGridDDGIIndexList = graph.GetBufferAddress(worldGrid.ddgiIndexList),
            .ddgiVolumeCount = volumeCount,
            .ddgiVolumeSlotBase = volumeSlotBase,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (WORLD_GRID_CELL_COUNT + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });

    return worldGrid;
}

void SetupDebugWorldGridCursorCellPass(RenderGraph& graph,
                                       PipelineManager* pipelineManager,
                                       const SceneResources& scene,
                                       const WorldGridFrame& worldGrid,
                                       uint32_t sceneIndex,
                                       RDGTexture depthTexture,
                                       Core::Extent2D renderExtent,
                                       Core::Array<uint32_t, 2> cursorPixel)
{
    ZoneScoped;
    if (!scene.readback.IsValid() || !worldGrid.lightGrid.IsValid() || !scene.lightData.IsValid() || !depthTexture.IsValid()) { return; }

    RenderPass& pass = graph.AddPass("World Grid Cursor Cell"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Debug);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(worldGrid.lightGrid);
    pass.ReadBuffer(worldGrid.indexList);
    pass.ReadBuffer(worldGrid.emissiveGrid);
    pass.ReadBuffer(worldGrid.emissiveIndexList);
    pass.ReadBuffer(worldGrid.cellPower);
    pass.ReadSampledImage(depthTexture);
    pass.ReadWriteBuffer(scene.readback);
    pass.Execute([&scene, pipelineManager, sceneIndex, depthTexture, renderExtent, cursorPixel, lightGrid = worldGrid.lightGrid, indexList = worldGrid.indexList,
            emissiveGrid = worldGrid.emissiveGrid, emissiveIndexList = worldGrid.emissiveIndexList, cellPower = worldGrid.cellPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("debug_world_grid_cursor_cell"_sid);
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        DebugWorldGridCursorCellPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .worldGridBuffer = graph.GetBufferAddress(lightGrid),
            .worldGridIndexList = graph.GetBufferAddress(indexList),
            .worldGridEmissiveGrid = graph.GetBufferAddress(emissiveGrid),
            .worldGridEmissiveIndexList = graph.GetBufferAddress(emissiveIndexList),
            .worldGridCellPower = graph.GetBufferAddress(cellPower),
            .readback = graph.GetBufferAddress(scene.readback),
            .cursorPixel = {cursorPixel[0], cursorPixel[1]},
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .depthTextureIndex = graph.GetSampledImageViewDescriptorIndex(depthTexture),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });
}

void SetupDebugReGIRCursorCellPass(RenderGraph& graph,
                                   PipelineManager* pipelineManager,
                                   const SceneResources& scene,
                                   const ReSTIRFrame& restir,
                                   uint32_t sceneIndex,
                                   RDGTexture depthTexture,
                                   Core::Extent2D renderExtent,
                                   Core::Array<uint32_t, 2> cursorPixel)
{
    ZoneScoped;
    if (!scene.readback.IsValid() || !restir.regirHashEntries.IsValid() || !restir.regirEntries.IsValid() || !restir.regirCellData.IsValid()) { return; }
    if (!scene.lightData.IsValid() || !restir.lightsVS.IsValid() || !depthTexture.IsValid()) { return; }

    RenderPass& pass = graph.AddPass("ReGIR Cursor Cell"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Debug);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(restir.lightsVS);
    pass.ReadBuffer(restir.regirHashEntries);
    pass.ReadBuffer(restir.regirEntries);
    pass.ReadBuffer(restir.regirCellData);
    pass.ReadSampledImage(depthTexture);
    pass.ReadWriteBuffer(scene.readback);
    pass.Execute([&scene, pipelineManager, sceneIndex, depthTexture, renderExtent, cursorPixel, lightsVS = restir.lightsVS, regirHashEntries = restir.regirHashEntries,
            regirEntries = restir.regirEntries, regirCellData = restir.regirCellData](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("debug_regir_cursor_cell"_sid);
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        DebugReGIRCursorCellPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .lightVS = graph.GetBufferAddress(lightsVS),
            .regirHashEntries = graph.GetBufferAddress(regirHashEntries),
            .regirEntries = graph.GetBufferAddress(regirEntries),
            .regirCellData = graph.GetBufferAddress(regirCellData),
            .readback = graph.GetBufferAddress(scene.readback),
            .cursorPixel = {cursorPixel[0], cursorPixel[1]},
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .depthTextureIndex = graph.GetSampledImageViewDescriptorIndex(depthTexture),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });
}

void SetupDebugPickPixelPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const SceneResources& scene,
                             uint32_t sceneIndex,
                             RDGTexture visibilityTexture,
                             RDGTexture depthTexture,
                             Core::Extent2D renderExtent,
                             Core::Array<uint32_t, 2> pickPixel,
                             uint32_t requestId)
{
    ZoneScoped;
    if (!scene.readback.IsValid() || !scene.sceneData.IsValid() || !visibilityTexture.IsValid() || !depthTexture.IsValid()) { return; }

    RenderPass& pass = graph.AddPass("Pick Pixel"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Debug);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadSampledImage(visibilityTexture);
    pass.ReadSampledImage(depthTexture);
    pass.ReadWriteBuffer(scene.readback);
    pass.Execute([&scene, pipelineManager, sceneIndex, visibilityTexture, depthTexture, renderExtent, pickPixel, requestId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("debug_pick_pixel"_sid);
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        DebugPickPixelPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .readback = graph.GetBufferAddress(scene.readback),
            .pickPixel = {pickPixel[0], pickPixel[1]},
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .visibilityTextureIndex = graph.GetSampledImageViewDescriptorIndex(visibilityTexture),
            .depthTextureIndex = graph.GetSampledImageViewDescriptorIndex(depthTexture),
            .requestId = requestId,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });
}

void SetupVisibilityLightingResolvePass(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Extent2D renderExtent,
                                        const RenderTargets& targets,
                                        const SceneResources& scene,
                                        const GeometryFrame& geometry,
                                        const WorldGridFrame& worldGrid,
                                        const DDGIFrame& ddgi,
                                        const FinalGatherFrame& gather,
                                        const ReflectionFrame& reflection,
                                        const ReSTIRFrame& restir,
                                        uint32_t sceneIndex,
                                        uint64_t frameNumber,
                                        bool bDDGIApply,
                                        uint32_t giGatherMode,
                                        const Core::ReflectionConfiguration& reflectionConfig)
{
    ZoneScoped;
    if (!scene.lightingBucketingDispatches.IsValid()) { return; }

    const bool bWorldGrid = worldGrid.lightGrid.IsValid() && worldGrid.indexList.IsValid();

    const bool bDDGI = bDDGIApply && ddgi.cascades.IsValid();
    const bool bGIGather = giGatherMode != 0u && gather.resolved.IsValid();

    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    const RDGTexture reflectionTarget = reflection.specNoisy;
    const bool bReflection = reflectionRoughnessMax >= 0.0f && reflectionTarget.IsValid();


    RenderPass& lightingResolve = graph.AddPass("Visibility Lighting Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::LightingResolve);
    lightingResolve.ReadBuffer(scene.sceneData);
    lightingResolve.ReadBuffer(scene.instances);
    lightingResolve.ReadBuffer(scene.materials);
    lightingResolve.ReadBuffer(scene.lightData);
    lightingResolve.ReadBuffer(scene.reflectionProbes);
    if (bWorldGrid) {
        lightingResolve.ReadBuffer(worldGrid.lightGrid);
        lightingResolve.ReadBuffer(worldGrid.indexList);
    }
    if (worldGrid.probeGrid.IsValid()) {
        lightingResolve.ReadBuffer(worldGrid.probeGrid);
    }
    if (restir.reservoirFinal.IsValid()) {
        lightingResolve.ReadBuffer(restir.reservoirFinal);
    }
    lightingResolve.ReadIndirectBuffer(scene.lightingBucketingDispatches);
    if (geometry.lightingTileList.IsValid()) { lightingResolve.ReadBuffer(geometry.lightingTileList); }
    lightingResolve.ReadSampledImage(targets.visibility);
    lightingResolve.ReadSampledImage(targets.gbufferOne);
    lightingResolve.ReadSampledImage(targets.gbufferTwo);
    lightingResolve.ReadSampledImage(targets.depthCopy);
    if (targets.shadows.IsValid()) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    if (bDDGI) {
        lightingResolve.ReadBuffer(ddgi.cascades);
    }
    if (bGIGather) {
        lightingResolve.ReadSampledImage(gather.resolved);
        lightingResolve.ReadSampledImage(gather.data);
        lightingResolve.ReadSampledImage(gather.skyVisHistory);
    }
    if (bReflection) {
        lightingResolve.ReadSampledImage(reflectionTarget);
    }
    lightingResolve.WriteStorageImage(targets.colorOutput);
    lightingResolve.Execute([&viewFamily, &scene, pipelineManager, sceneIndex, frameNumber, renderExtent,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            output = targets.colorOutput, skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity,
            tileList = geometry.lightingTileList, reservoirFinal = restir.reservoirFinal, ddgiCascades = ddgi.cascades,
            worldGridLightGrid = worldGrid.lightGrid, worldGridIndexList = worldGrid.indexList, worldGridProbeGrid = worldGrid.probeGrid,
            giResolved = gather.resolved, giData = gather.data, giSkyVis = gather.skyVisHistory,
            bDDGI, bWorldGrid, bGIGather, giGatherMode, bReflection, reflectionTarget, reflectionRoughnessMax, lightSpecularFromReflectionsMax = bReflection ? ComputeLightSpecularFromReflectionsMax(reflectionConfig) : -1.0f
            ](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkDeviceAddress tileListAddress = graph.GetBufferAddress(tileList);

            for (const LightingPipelineInfo& entry : pipelineManager->GetLightingPipelines()) {
                if (!entry.id) { continue; }

                StringID shaderToUse = viewFamily.lightingShaderOverride ? viewFamily.lightingShaderOverride : entry.id;
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(shaderToUse);
                if (!pipelineEntry) { pipelineEntry = pipelineManager->GetPipelineEntry("default_unlit"_sid); }
                if (!pipelineEntry) { continue; }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                VisibilityLightingPushConstant pc{
                    .sceneData = graph.GetBufferAddress(scene.sceneData),
                    .lightData = graph.GetBufferAddress(scene.lightData),
                    .tileListBuffer = tileListAddress,
                    .instanceBuffer = graph.GetBufferAddress(scene.instances),
                    .materialBuffer = graph.GetBufferAddress(scene.materials),
                    .reservoirBuffer = graph.TryGetBufferAddress(reservoirFinal),
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .shadowsIndex = shadows.IsValid() ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                    .skyboxIndex = skyboxIndex,
                    .primaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(output),
                    .secondaryOutputImageIndex = ~0x0u,
                    .sceneDataIndex = sceneIndex,
                    .lightingIndex = entry.index,
                    .renderExtent = {renderExtent.width, renderExtent.height},
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .iblIntensity = iblIntensity,
                    .reflectionIndex = bReflection ? graph.GetSampledImageViewDescriptorIndex(reflectionTarget) : ~0x0u,
                    .ddgiCascades = bDDGI ? graph.GetBufferAddress(ddgiCascades) : 0,
                    .bDDGIApply = bDDGI ? 1u : 0u,
                    .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(worldGridLightGrid) : 0,
                    .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(worldGridIndexList) : 0,
                    .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giResolved) : ~0x0u,
                    .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giData) : ~0x0u,
                    .giGatherMode = bGIGather ? giGatherMode : 0u,
                    .reflectionRoughnessMax = reflectionRoughnessMax,
                    .lightSpecularFromReflectionsMax = lightSpecularFromReflectionsMax,
                    .reflectionProbes = viewFamily.reflectionProbes.Size() > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
                    .reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size()),
                    .diffuseRatioIndex = ~0x0u,
                    .worldGridProbeGrid = (!viewFamily.bReflectionProbeBruteForce && worldGridProbeGrid.IsValid()) ? graph.GetBufferAddress(worldGridProbeGrid) : 0,
                    .tileCapacity = BucketTileCapacity(renderExtent.width, renderExtent.height),
                    .indirectIntensity = viewFamily.indirectIntensity,
                    .skyVisIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giSkyVis) : ~0x0u,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(scene.lightingBucketingDispatches), entry.index * sizeof(BucketDispatchParameters) + offsetof(BucketDispatchParameters, xDispatch));
            }
        });
}

void SetupGroundTruthLightingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Extent2D renderExtent,
                                  const RenderTargets& targets,
                                  const SceneResources& scene,
                                  uint32_t sceneIndex,
                                  bool bReset,
                                  uint32_t& accumulationCount,
                                  uint64_t frameNumber)
{
    ZoneScoped;
    const uint32_t pixelCount = renderExtent.width * renderExtent.height;
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    const bool bHistory = graph.ResourceHasBufferVersion("gt_accum"_sid, bufferSize);
    if (!bHistory) { bReset = true; }
    if (bReset) { accumulationCount = 0; }
    const RDGBuffer accum = graph.CreateVersionedBuffer("gt_accum"_sid, bufferSize, 0, bHistory ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();

    if (bReset) {
        RenderPass& clearPass = graph.AddPass("GT Accum Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer(accum);
        clearPass.Execute([accum](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(accum), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass("Ground Truth Lighting"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::GroundTruth);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.materials);
    pass.ReadWriteBuffer(accum);
    pass.ReadSampledImage(targets.visibility);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (targets.shadows.IsValid()) {
        pass.ReadSampledImage(targets.shadows);
    }
    pass.WriteStorageImage(targets.colorOutput);
    pass.Execute([&scene, pipelineManager, sceneIndex, frameNumber, accumulationCount, accum,
            visibility = targets.visibility,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            output = targets.colorOutput, skyboxIndex = viewFamily.skyboxIndex, renderExtent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("lighting_ground_truth"_sid);
            if (!pipelineEntry) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            VisibilityLightingPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .tileListBuffer = 0,
                .instanceBuffer = graph.GetBufferAddress(scene.instances),
                .materialBuffer = graph.GetBufferAddress(scene.materials),
                .reservoirBuffer = 0,
                .accumulationBuffer = graph.GetBufferAddress(accum),
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .shadowsIndex = shadows.IsValid() ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0u,
                .skyboxIndex = skyboxIndex,
                .primaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .secondaryOutputImageIndex = ~0x0u,
                .sceneDataIndex = sceneIndex,
                .renderExtent = {renderExtent.width, renderExtent.height},
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .accumulationCount = accumulationCount,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent.width + 15) / 16;
            const uint32_t groupsY = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
}

void SetupDirectionalLightingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Extent2D renderExtent,
                                  Core::Extent2D shadowExtent,
                                  const RenderTargets& targets,
                                  const SceneResources& scene,
                                  const SunShadowFrame& sunShadow,
                                  uint32_t sceneIndex,
                                  uint32_t pixelScale)
{
    ZoneScoped;
    if (!sunShadow.shadow.IsValid()) { return; }

    const bool bHalfRes = pixelScale > 1u;

    RDGTexture shadowTex = sunShadow.shadow;
    if (sunShadow.sigmaShadow.IsValid()) { shadowTex = sunShadow.sigmaShadow; }
    if (sunShadow.sigmaStabilized.IsValid()) { shadowTex = sunShadow.sigmaStabilized; }

    RenderPass& pass = graph.AddPass("Directional Lighting"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(shadowTex);
    if (bHalfRes) {
        pass.ReadSampledImage(sunShadow.depth);
        pass.ReadSampledImage(sunShadow.gbuffer);
    }
    pass.ReadWriteImage(targets.colorOutput);
    pass.Execute([&scene, pipelineManager, sceneIndex, renderExtent, shadowExtent, pixelScale, bHalfRes, shadowTex,
            shadowDepth = sunShadow.depth, shadowGbuffer = sunShadow.gbuffer,
            depth = targets.depthCopy, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("directional_light"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            DirectionalLightPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(shadowTex),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .sceneDataIndex = sceneIndex,
                .renderExtent = {renderExtent.width, renderExtent.height},
                .shadowExtent = {shadowExtent.width, shadowExtent.height},
                .pixelScale = pixelScale,
                .shadowDepthIndex = bHalfRes ? graph.GetSampledImageViewDescriptorIndex(shadowDepth) : ~0x0u,
                .shadowNormalIndex = bHalfRes ? graph.GetSampledImageViewDescriptorIndex(shadowGbuffer) : ~0x0u,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent.width + 15) / 16;
            const uint32_t groupsY = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
}
} // Render
