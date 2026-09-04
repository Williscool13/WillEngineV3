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
    graph.CreateBuffer("cluster_light_grid"_sid, gridBytes, false);
    graph.CreateBuffer("cluster_light_index_list"_sid, indexBytes, false);

    RenderPass& cull = graph.AddPass("Frustum Binning"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::FrustumBinning);
    cull.ReadBuffer(SCENE_DATA_BUFFER);
    cull.ReadBuffer(LIGHT_DATA_BUFFER);
    cull.WriteBuffer("cluster_light_grid"_sid);
    cull.WriteBuffer("cluster_light_index_list"_sid);
    cull.Execute([pipelineManager, sceneIndex, clusterZNear, clusterZFar](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("frustum_binning"_sid);
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        FrustumBinningPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .lightGrid = graph.GetBufferAddress("cluster_light_grid"_sid),
            .lightIndexList = graph.GetBufferAddress("cluster_light_index_list"_sid),
            .zNear = clusterZNear,
            .zFar = clusterZFar,
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (CLUSTER_COUNT + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });
}

void SetupEmissiveTriLightPass(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, float emissiveTriRangeMultiplier)
{
    ZoneScoped;
    if (viewFamily.triLightCount == 0 || !graph.HasBuffer(LIGHT_DATA_BUFFER)) { return; }

    RenderPass& clearPass = graph.AddPass("Emissive Tri Lights Clear"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::ReSTIRDI);
    clearPass.AsyncCompute();
    clearPass.WriteBuffer(LIGHT_DATA_BUFFER);
    clearPass.Execute([pipelineManager, lightCount = viewFamily.triLightCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("emissive_tri_lights_clear"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        EmissiveTriLightClearPushConstant pc{
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .firstLight = static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS),
            .lightCount = lightCount,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (lightCount + 63u) / 64u, 1, 1);
    });

    const auto workCount = static_cast<uint32_t>(viewFamily.emissiveTriWork.Size());
    if (workCount == 0 || !graph.HasBuffer(EMISSIVE_TRI_WORK_BUFFER)) { return; }
    if (!graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_PRIMITIVE_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) { return; }

    RenderPass& pass = graph.AddPass("Emissive Tri Lights"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::ReSTIRDI);
    pass.AsyncCompute();
    pass.ReadBuffer(EMISSIVE_TRI_WORK_BUFFER);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_POSITION_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.WriteBuffer(LIGHT_DATA_BUFFER);
    pass.Execute([pipelineManager, workCount, emissiveTriRangeMultiplier](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("emissive_tri_lights"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        EmissiveTriLightPushConstant pc{
            .workBuffer = graph.GetBufferAddress(EMISSIVE_TRI_WORK_BUFFER),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .vertexPosBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .workCount = workCount,
            .rangeMultiplier = emissiveTriRangeMultiplier,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, workCount, 1, 1);
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
    graph.CreateBuffer("world_grid_light_grid"_sid, gridBytes, false);
    graph.CreateBuffer("world_grid_index_list"_sid, indexBytes, false);
    graph.CreateBuffer("world_grid_emissive_grid"_sid, gridBytes, false);
    graph.CreateBuffer("world_grid_emissive_index_list"_sid, emissiveIndexBytes, false);
    graph.CreateBuffer("world_grid_cell_power"_sid, static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * 2u * sizeof(float), false);
    graph.CreateBuffer("world_grid_probe_grid"_sid, static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * sizeof(uint32_t), false);

    const VkDeviceSize ddgiIndexBytes = static_cast<VkDeviceSize>(WORLD_GRID_CELL_COUNT) * MAX_DDGI_VOLUMES_PER_WORLD_GRID_CELL * sizeof(uint32_t);
    graph.CreateBuffer("world_grid_ddgi_grid"_sid, gridBytes, false);
    graph.CreateBuffer("world_grid_ddgi_index_list"_sid, ddgiIndexBytes, false);
    graph.CreateBuffer("ddgi_volume_windows"_sid, sizeof(DDGIVolumeParams) * DDGI_MAX_RESIDENT_LOCAL_VOLUMES, false);

    // Cascades occupy volumes[0, count); the resident world volumes follow, and the bin emits their absolute slot so the sampler indexes cascades[] with no remap.
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
        upload.WriteTransferBuffer("ddgi_volume_windows"_sid);
        upload.Execute([windows, windowBytes](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdUpdateBuffer(cmd, graph.GetBufferHandle("ddgi_volume_windows"_sid), 0, windowBytes, windows);
        });
    }

    const uint32_t probeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());

    RenderPass& binning = graph.AddPass("World Grid Binning"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::WorldGridBinning);
    binning.AsyncCompute();
    binning.ReadBuffer(SCENE_DATA_BUFFER);
    binning.ReadBuffer(LIGHT_DATA_BUFFER);
    binning.ReadBuffer(REFLECTION_PROBE_BUFFER);
    binning.WriteBuffer("world_grid_light_grid"_sid);
    binning.WriteBuffer("world_grid_index_list"_sid);
    binning.WriteBuffer("world_grid_emissive_grid"_sid);
    binning.WriteBuffer("world_grid_emissive_index_list"_sid);
    binning.WriteBuffer("world_grid_cell_power"_sid);
    binning.WriteBuffer("world_grid_probe_grid"_sid);
    binning.WriteBuffer("world_grid_ddgi_grid"_sid);
    binning.WriteBuffer("world_grid_ddgi_index_list"_sid);
    if (volumeCount > 0u) { binning.ReadBuffer("ddgi_volume_windows"_sid); }
    binning.Execute([pipelineManager, sceneIndex, probeCount, volumeCount, volumeSlotBase](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("world_grid_binning"_sid);
        if (!pipelineEntry) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        WorldGridBinningPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .worldGridBuffer = graph.GetBufferAddress("world_grid_light_grid"_sid),
            .worldGridIndexList = graph.GetBufferAddress("world_grid_index_list"_sid),
            .worldGridEmissiveGrid = graph.GetBufferAddress("world_grid_emissive_grid"_sid),
            .worldGridEmissiveIndexList = graph.GetBufferAddress("world_grid_emissive_index_list"_sid),
            .worldGridCellPower = graph.GetBufferAddress("world_grid_cell_power"_sid),
            .sceneDataIndex = sceneIndex,
            .reflectionProbes = probeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
            .worldGridProbeGrid = graph.GetBufferAddress("world_grid_probe_grid"_sid),
            .reflectionProbeCount = probeCount,
            .ddgiVolumes = volumeCount > 0u ? graph.GetBufferAddress("ddgi_volume_windows"_sid) : 0,
            .worldGridDDGIGrid = graph.GetBufferAddress("world_grid_ddgi_grid"_sid),
            .worldGridDDGIIndexList = graph.GetBufferAddress("world_grid_ddgi_index_list"_sid),
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
                                        uint64_t frameNumber,
                                        bool bDDGIApply,
                                        uint32_t giGatherMode,
                                        const Core::ReflectionConfiguration& reflectionConfig)
{
    ZoneScoped;
    if (!graph.HasBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER)) { return; }

    const bool bWorldGrid = graph.HasBuffer("world_grid_light_grid"_sid) && graph.HasBuffer("world_grid_index_list"_sid);

    const bool bDDGI = bDDGIApply && graph.HasBuffer(DDGI_CASCADES_BUFFER);
    const bool bGIGather = giGatherMode != 0u && graph.HasTexture(GI_GATHER_RESOLVED);

    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    const StringID reflectionTarget = REFLECTION_SPEC_NOISY_TARGET;
    const bool bReflection = reflectionRoughnessMax >= 0.0f && graph.HasTexture(reflectionTarget);


    RenderPass& lightingResolve = graph.AddPass("Visibility Lighting Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::LightingResolve);
    lightingResolve.ReadBuffer(SCENE_DATA_BUFFER);
    lightingResolve.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    lightingResolve.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    lightingResolve.ReadBuffer("light_data"_sid);
    lightingResolve.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (bWorldGrid) {
        lightingResolve.ReadBuffer("world_grid_light_grid"_sid);
        lightingResolve.ReadBuffer("world_grid_index_list"_sid);
    }
    if (graph.HasBuffer("world_grid_probe_grid"_sid)) {
        lightingResolve.ReadBuffer("world_grid_probe_grid"_sid);
    }
    if (graph.HasBuffer("restir_reservoir_final"_sid)) {
        lightingResolve.ReadBuffer("restir_reservoir_final"_sid);
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
                    .lightData = graph.GetBufferAddress("light_data"_sid),
                    .lightDispatchBuffer = lightDispatchAddress,
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                    .reservoirBuffer = graph.TryGetBufferAddress("restir_reservoir_final"_sid),
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
                    .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress("world_grid_light_grid"_sid) : 0,
                    .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress("world_grid_index_list"_sid) : 0,
                    .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_RESOLVED) : ~0x0u,
                    .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA) : ~0x0u,
                    .giGatherMode = bGIGather ? giGatherMode : 0u,
                    .reflectionRoughnessMax = reflectionRoughnessMax,
                    .lightSpecularFromReflectionsMax = lightSpecularFromReflectionsMax,
                    .reflectionProbes = viewFamily.reflectionProbes.Size() > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
                    .reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size()),
                    .worldGridProbeGrid = (!viewFamily.bReflectionProbeBruteForce && graph.HasBuffer("world_grid_probe_grid"_sid)) ? graph.GetBufferAddress("world_grid_probe_grid"_sid) : 0,
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

    if (!graph.ResourceHasVersion("gt_accum"_sid, 0)) { bReset = true; }
    graph.CreateVersionedBuffer("gt_accum"_sid, bufferSize, 0, graph.ResourceHasVersion("gt_accum"_sid, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);

    if (bReset) {
        RenderPass& clearPass = graph.AddPass("GT Accum Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer("gt_accum"_sid);
        clearPass.Execute([&](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("gt_accum"_sid), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass("Ground Truth Lighting"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::GroundTruth);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer("light_data"_sid);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadWriteBuffer("gt_accum"_sid);
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
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("lighting_ground_truth"_sid);
            if (!pipelineEntry) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            VisibilityLightingPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress("light_data"_sid),
                .lightDispatchBuffer = 0,
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                .reservoirBuffer = 0,
                .accumulationBuffer = graph.GetBufferAddress("gt_accum"_sid),
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
    if (!graph.HasTexture("rt_sun_shadow"_sid)) { return; }

    const bool bHalfRes = pixelScale > 1u;

    // Prefer the most-processed shadow available: temporally stabilized > spatially denoised > raw trace.
    StringID shadowTex = "rt_sun_shadow"_sid;
    if (graph.HasTexture("sigma_shadow"_sid)) { shadowTex = "sigma_shadow"_sid; }
    if (graph.HasTexture("sigma_stabilized"_sid)) { shadowTex = "sigma_stabilized"_sid; }

    RenderPass& pass = graph.AddPass("Directional Lighting"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(shadowTex);
    if (bHalfRes) {
        pass.ReadSampledImage("rt_sun_depth"_sid);
        pass.ReadSampledImage("rt_sun_gbuffer"_sid);
    }
    pass.ReadWriteImage(targets.colorOutput);
    pass.Execute([&, pipelineManager, sceneIndex, renderExtent, shadowExtent, pixelScale, bHalfRes, shadowTex,
            depth = targets.depthCopy, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("directional_light"_sid);
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
                .shadowDepthIndex = bHalfRes ? graph.GetSampledImageViewDescriptorIndex("rt_sun_depth"_sid) : ~0x0u,
                .shadowNormalIndex = bHalfRes ? graph.GetSampledImageViewDescriptorIndex("rt_sun_gbuffer"_sid) : ~0x0u,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
}
} // Render
