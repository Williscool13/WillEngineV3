//
// Created by William on 2026-10-03.
//

#include "render/passes/volumetric_fog_passes.h"

#include <tracy/Tracy.hpp>

#include "render/render_config.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/shaders/push_constant_interop.h"
#include "render/shaders/volumetric_fog_interop.h"

namespace Render
{
void SetupVolumetricFog(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Array<uint32_t, 2> renderExtent,
                        const RenderTargets& targets,
                        uint32_t sceneIndex,
                        uint64_t frameIndex,
                        bool bFoggedCopy)
{
    ZoneScoped;
    const Core::VolumetricFog& fog = viewFamily.volumetricFog;
    if (!fog.bEnabled) { return; }

    const Core::Array<uint32_t, 2> gridSize{(renderExtent[0] + VOLUMETRIC_FOG_TILE_SIZE - 1) / VOLUMETRIC_FOG_TILE_SIZE, (renderExtent[1] + VOLUMETRIC_FOG_TILE_SIZE - 1) / VOLUMETRIC_FOG_TILE_SIZE};
    const float maxDistance = glm::max(fog.maxDistance, VOLUMETRIC_FOG_NEAR * 2.0f);
    const TextureInfo gridInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gridSize[0], gridSize[1], 1, VOLUMETRIC_FOG_SLICES};
    graph.CreateVersionedTexture(VOLUMETRIC_FOG_SCATTER, gridInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const bool bHistory = graph.ResourceHasVersion(VOLUMETRIC_FOG_SCATTER, 1);
    const StringID scatterHistory = graph.ResourceVersionID(VOLUMETRIC_FOG_SCATTER, 1);
    graph.CreateTexture(VOLUMETRIC_FOG_INTEGRATED, gridInfo);

    const bool bTLAS = graph.HasBuffer(RT_TLAS_BUFFER);
    const bool bWorldGrid = graph.HasBuffer("world_grid_light_grid"_sid) && graph.HasBuffer("world_grid_index_list"_sid);

    RenderPass& scatterPass = graph.AddPass("[Fog] Scatter"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::VolumetricFog);
    scatterPass.ReadBuffer(SCENE_DATA_BUFFER);
    scatterPass.ReadBuffer(LIGHT_DATA_BUFFER);
    if (bWorldGrid) {
        scatterPass.ReadBuffer("world_grid_light_grid"_sid);
        scatterPass.ReadBuffer("world_grid_index_list"_sid);
    }
    if (bTLAS) {
        scatterPass.ReadTLASBuffer(RT_TLAS_BUFFER);
    }
    if (bHistory) {
        scatterPass.ReadSampledImage(scatterHistory);
    }
    scatterPass.WriteStorageImage(VOLUMETRIC_FOG_SCATTER);
    scatterPass.Execute([pipelineManager, sceneIndex, renderExtent, gridSize, maxDistance, fog, bTLAS, bWorldGrid, bHistory, scatterHistory, frameIndex, skyboxIndex = viewFamily.skyboxIndex,
            iblIntensity = viewFamily.iblIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("volumetric_fog_scatter"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            VolumetricFogScatterPushConstant pc{
                .albedoDensity = {fog.albedo, glm::max(fog.density, 0.0f)},
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
                .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress("world_grid_light_grid"_sid) : 0,
                .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress("world_grid_index_list"_sid) : 0,
                .sceneDataIndex = sceneIndex,
                .scatterOutIndex = graph.GetStorageImageViewDescriptorIndex(VOLUMETRIC_FOG_SCATTER),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .gridSize = {gridSize[0], gridSize[1]},
                .heightFalloff = glm::max(fog.heightFalloff, 0.0f),
                .baseHeight = fog.baseHeight,
                .maxDistance = maxDistance,
                .ambientScale = glm::max(fog.ambientScale, 0.0f),
                .iblIntensity = iblIntensity,
                .skyboxIndex = skyboxIndex,
                .anisotropy = glm::clamp(fog.anisotropy, -0.95f, 0.95f),
                .tlasIndex = bTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u,
                .frameIndex = static_cast<uint32_t>(frameIndex),
                .historyIndex = bHistory ? graph.GetSampledImageViewDescriptorIndex(scatterHistory) : ~0u,            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (gridSize[0] + 7) / 8, (gridSize[1] + 7) / 8, VOLUMETRIC_FOG_SLICES);
        });

    RenderPass& integratePass = graph.AddPass("[Fog] Integrate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::VolumetricFog);
    integratePass.ReadBuffer(SCENE_DATA_BUFFER);
    integratePass.ReadSampledImage(VOLUMETRIC_FOG_SCATTER);
    integratePass.WriteStorageImage(VOLUMETRIC_FOG_INTEGRATED);
    integratePass.Execute([pipelineManager, sceneIndex, renderExtent, gridSize, maxDistance](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("volumetric_fog_integrate"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            VolumetricFogIntegratePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .gridSize = {gridSize[0], gridSize[1]},
                .sceneDataIndex = sceneIndex,
                .scatterIndex = graph.GetSampledImageViewDescriptorIndex(VOLUMETRIC_FOG_SCATTER),
                .integratedOutIndex = graph.GetStorageImageViewDescriptorIndex(VOLUMETRIC_FOG_INTEGRATED),
                .maxDistance = maxDistance,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (gridSize[0] + 7) / 8, (gridSize[1] + 7) / 8, 1);
        });

    RenderPass& applyPass = graph.AddPass("[Fog] Apply"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::VolumetricFog);
    applyPass.ReadBuffer(SCENE_DATA_BUFFER);
    applyPass.ReadSampledImage(targets.depthCopy);
    applyPass.ReadSampledImage(VOLUMETRIC_FOG_INTEGRATED);
    applyPass.ReadWriteImage(targets.colorOutput);
    if (bFoggedCopy) {
        applyPass.WriteStorageImage(LIT_COLOR_FOGGED);
    }
    applyPass.Execute([pipelineManager, sceneIndex, renderExtent, gridSize, maxDistance, bFoggedCopy, depth = targets.depthCopy, color = targets.colorOutput](
            VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("volumetric_fog_apply"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            VolumetricFogApplyPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .gridSize = {gridSize[0], gridSize[1]},
                .sceneDataIndex = sceneIndex,
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .integratedIndex = graph.GetSampledImageViewDescriptorIndex(VOLUMETRIC_FOG_INTEGRATED),
                .colorIndex = graph.GetStorageImageViewDescriptorIndex(color),
                .foggedCopyIndex = bFoggedCopy ? graph.GetStorageImageViewDescriptorIndex(LIT_COLOR_FOGGED) : ~0u,
                .maxDistance = maxDistance,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (renderExtent[0] + 15) / 16, (renderExtent[1] + 15) / 16, 1);
        });
}
} // Render
