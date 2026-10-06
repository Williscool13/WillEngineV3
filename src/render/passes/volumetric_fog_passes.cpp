//
// Created by William on 2026-10-03.
//

#include "render/passes/volumetric_fog_passes.h"
#include "render/passes/ddgi_passes.h"

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
                        Core::Extent2D renderExtent,
                        const RenderTargets& targets,
                        const SceneResources& scene,
                        const WorldGridFrame& worldGrid,
                        const DDGIFrame& ddgi,
                        uint32_t sceneIndex,
                        uint64_t frameIndex,
                        bool bPreOverlayCopy,
                        bool bDDGIApply,
                        int32_t debugMode,
                        bool bResetHistory)
{
    ZoneScoped;
    const Core::VolumetricFog& fog = viewFamily.volumetricFog;
    if (!fog.bEnabled) { return; }

    const Core::Array<uint32_t, 2> gridSize{(renderExtent.width + VOLUMETRIC_FOG_TILE_SIZE - 1) / VOLUMETRIC_FOG_TILE_SIZE, (renderExtent.height + VOLUMETRIC_FOG_TILE_SIZE - 1) / VOLUMETRIC_FOG_TILE_SIZE};
    const float maxDistance = glm::max(fog.maxDistance, VOLUMETRIC_FOG_NEAR * 2.0f);
    const TextureInfo gridInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gridSize[0], gridSize[1], 1, VOLUMETRIC_FOG_SLICES};
    const RDGTextureRing scatterRing = graph.CreateVersionedTexture(VOLUMETRIC_FOG_SCATTER, gridInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTexture scatter = scatterRing.Current();
    const RDGTexture scatterHistory = scatterRing.Version(1);
    const bool bHistory = scatterHistory.IsValid() && !bResetHistory;
    const RDGTexture tileDepth = graph.CreateTexture(VOLUMETRIC_FOG_TILE_DEPTH, TextureInfo{VK_FORMAT_R32_SFLOAT, gridSize[0], gridSize[1], 1}, {std::nullopt}, true);
    const RDGTexture filtered = graph.CreateTexture(VOLUMETRIC_FOG_FILTERED, gridInfo, {std::nullopt}, true);
    const RDGTexture integrated = graph.CreateTexture(VOLUMETRIC_FOG_INTEGRATED, gridInfo, {std::nullopt}, true);
    const bool bDebug = debugMode > 0;
    RDGTexture debugTarget{};
    if (bDebug) {
        debugTarget = graph.CreateTexture(VOLUMETRIC_FOG_DEBUG_TARGET, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    }

    const bool bTLAS = scene.tlas.IsValid();
    const bool bWorldGrid = worldGrid.lightGrid.IsValid() && worldGrid.indexList.IsValid();
    const RDGBuffer worldGridLights = worldGrid.lightGrid;
    const RDGBuffer worldGridIndices = worldGrid.indexList;

    RenderPass& tileDepthPass = graph.AddPass("[Fog] Tile Depth"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::VolumetricFog);
    tileDepthPass.ReadSampledImage(targets.depthCopy);
    tileDepthPass.WriteStorageImage(tileDepth);
    tileDepthPass.Execute([pipelineManager, renderExtent, gridSize, depth = targets.depthCopy, tileDepth](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("volumetric_fog_tile_depth"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            VolumetricFogTileDepthPushConstant pc{
                .renderExtent = {renderExtent.width, renderExtent.height},
                .gridSize = {gridSize[0], gridSize[1]},
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .tileDepthOutIndex = graph.GetStorageImageViewDescriptorIndex(tileDepth),
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (gridSize[0] + 7) / 8, (gridSize[1] + 7) / 8, 1);
        });

    RenderPass& scatterPass = graph.AddPass("[Fog] Scatter"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::VolumetricFog);
    scatterPass.ReadBuffer(scene.sceneData);
    scatterPass.ReadBuffer(scene.lightData);
    if (bWorldGrid) {
        scatterPass.ReadBuffer(worldGridLights);
        scatterPass.ReadBuffer(worldGridIndices);
    }
    if (bTLAS) {
        scatterPass.ReadTLASBuffer(scene.tlas);
    }
    if (bHistory) {
        scatterPass.ReadSampledImage(scatterHistory);
    }
    scatterPass.ReadSampledImage(tileDepth);
    const bool bDDGI = bDDGIApply && ddgi.IsValid();
    if (bDDGI) { scatterPass.ReadBuffer(ddgi.cascades); }
    scatterPass.WriteStorageImage(scatter);
    scatterPass.Execute([&scene, pipelineManager, sceneIndex, renderExtent, gridSize, maxDistance, fog, bTLAS, bWorldGrid, bHistory, scatterHistory, bDDGI, debugMode, frameIndex, skyboxIndex = viewFamily.skyboxIndex,
            iblIntensity = viewFamily.iblIntensity, worldGridLights, worldGridIndices, ddgiCascades = ddgi.cascades, scatter, tileDepth](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("volumetric_fog_scatter"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            VolumetricFogScatterPushConstant pc{
                .albedoDensity = {fog.albedo, glm::max(fog.density, 0.0f)},
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(worldGridLights) : 0,
                .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(worldGridIndices) : 0,
                .sceneDataIndex = sceneIndex,
                .scatterOutIndex = graph.GetStorageImageViewDescriptorIndex(scatter),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .gridSize = {gridSize[0], gridSize[1]},
                .heightFalloff = glm::max(fog.heightFalloff, 0.0f),
                .baseHeight = fog.baseHeight,
                .maxDistance = maxDistance,
                .ambientScale = glm::max(fog.ambientScale, 0.0f),
                .iblIntensity = iblIntensity,
                .skyboxIndex = skyboxIndex,
                .anisotropy = glm::clamp(fog.anisotropy, -0.95f, 0.95f),
                .tlasIndex = bTLAS ? graph.GetAccelerationStructureDescriptorIndex(scene.tlas) : ~0u,
                .frameIndex = static_cast<uint32_t>(frameIndex),
                .historyIndex = bHistory ? graph.GetSampledImageViewDescriptorIndex(scatterHistory) : ~0u,
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(ddgiCascades) : 0,
                .debugMode = static_cast<uint32_t>(glm::max(debugMode, 0)),
                .tileDepthIndex = graph.GetSampledImageViewDescriptorIndex(tileDepth),
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (gridSize[0] + 7) / 8, (gridSize[1] + 7) / 8, VOLUMETRIC_FOG_SLICES);
        });

    RenderPass& filterPass = graph.AddPass("[Fog] Filter"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::VolumetricFog);
    filterPass.ReadSampledImage(scatter);
    filterPass.WriteStorageImage(filtered);
    filterPass.Execute([pipelineManager, gridSize, scatter, filtered](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("volumetric_fog_filter"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            VolumetricFogFilterPushConstant pc{
                .gridSize = {gridSize[0], gridSize[1]},
                .scatterIndex = graph.GetSampledImageViewDescriptorIndex(scatter),
                .filteredOutIndex = graph.GetStorageImageViewDescriptorIndex(filtered),
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (gridSize[0] + 7) / 8, (gridSize[1] + 7) / 8, VOLUMETRIC_FOG_SLICES);
        });

    RenderPass& integratePass = graph.AddPass("[Fog] Integrate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::VolumetricFog);
    integratePass.ReadBuffer(scene.sceneData);
    integratePass.ReadSampledImage(filtered);
    integratePass.WriteStorageImage(integrated);
    integratePass.Execute([&scene, pipelineManager, sceneIndex, renderExtent, gridSize, maxDistance, filtered, integrated](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("volumetric_fog_integrate"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            VolumetricFogIntegratePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .gridSize = {gridSize[0], gridSize[1]},
                .sceneDataIndex = sceneIndex,
                .scatterIndex = graph.GetSampledImageViewDescriptorIndex(filtered),
                .integratedOutIndex = graph.GetStorageImageViewDescriptorIndex(integrated),
                .maxDistance = maxDistance,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (gridSize[0] + 7) / 8, (gridSize[1] + 7) / 8, 1);
        });

    RenderPass& applyPass = graph.AddPass("[Fog] Apply"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::VolumetricFog);
    applyPass.ReadBuffer(scene.sceneData);
    applyPass.ReadBuffer(scene.lightData);
    applyPass.ReadSampledImage(targets.depthCopy);
    applyPass.ReadSampledImage(integrated);
    applyPass.ReadWriteImage(targets.colorOutput);
    if (bPreOverlayCopy) {
        applyPass.WriteStorageImage(targets.preOverlayColor);
    }
    if (bDebug) {
        applyPass.WriteStorageImage(debugTarget);
    }
    applyPass.Execute([&scene, pipelineManager, sceneIndex, renderExtent, gridSize, maxDistance, bPreOverlayCopy, bDebug, debugMode, frameIndex, fog, depth = targets.depthCopy, color = targets.colorOutput,
            preOverlay = targets.preOverlayColor, integrated, debugTarget, skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("volumetric_fog_apply"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            VolumetricFogApplyPushConstant pc{
                .albedoDensity = {fog.albedo, glm::max(fog.density, 0.0f)},
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .gridSize = {gridSize[0], gridSize[1]},
                .sceneDataIndex = sceneIndex,
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .integratedIndex = graph.GetSampledImageViewDescriptorIndex(integrated),
                .colorIndex = graph.GetStorageImageViewDescriptorIndex(color),
                .foggedCopyIndex = bPreOverlayCopy ? graph.GetStorageImageViewDescriptorIndex(preOverlay) : ~0u,
                .maxDistance = maxDistance,
                .frameIndex = static_cast<uint32_t>(frameIndex),
                .heightFalloff = glm::max(fog.heightFalloff, 0.0f),
                .baseHeight = fog.baseHeight,
                .ambientScale = glm::max(fog.ambientScale, 0.0f),
                .iblIntensity = iblIntensity,
                .skyboxIndex = skyboxIndex,
                .anisotropy = glm::clamp(fog.anisotropy, -0.95f, 0.95f),
                .debugMode = static_cast<uint32_t>(glm::max(debugMode, 0)),
                .debugOutIndex = bDebug ? graph.GetStorageImageViewDescriptorIndex(debugTarget) : ~0u,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (renderExtent.width + 15) / 16, (renderExtent.height + 15) / 16, 1);
        });
}
} // Render
