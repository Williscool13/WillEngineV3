//
// Created by William on 2026-07-09.
//

#include "render/passes/reflection_passes.h"

#include <tracy/Tracy.hpp>

#include "render/passes/ddgi_passes.h"
#include "render/render_config.h"
#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
ReflectionFrame SetupReflectionTracePass(RenderGraph& graph,
                                         PipelineManager* pipelineManager,
                                         Core::Extent2D renderExtent,
                                         const RenderTargets& targets,
                                         const SceneResources& scene,
                                         uint32_t sceneIndex,
                                         uint64_t frameNumber,
                                         const Core::ReflectionConfiguration& reflectionConfig)
{
    ZoneScoped;
    ReflectionFrame reflection{};
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    if (reflectionRoughnessMax < 0.0f || !scene.tlas.IsValid()) {
        return reflection;
    }

    const RDGBuffer hitDescriptors = graph.CreateBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER, sizeof(ReflectionHitDescriptor) * renderExtent.width * renderExtent.height, true);
    reflection.hitDescriptors = hitDescriptors;

    RenderPass& pass = graph.AddPass("[Reflection] Trace"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReflectionsShade);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadTLASBuffer(scene.tlas);
    pass.WriteBuffer(hitDescriptors);

    pass.Execute([pipelineManager, sceneIndex, renderExtent, frameNumber, reflectionRoughnessMax, mirrorRoughnessMax = reflectionConfig.mirrorRoughnessMax,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy, &scene, hitDescriptors](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReflectionTracePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .reflectionDescriptors = graph.GetBufferAddress(hitDescriptors),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .sceneDataIndex = sceneIndex,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .roughnessMax = reflectionRoughnessMax,
                .mirrorRoughnessMax = mirrorRoughnessMax,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("reflection_trace"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (renderExtent.width + 15) / 16, (renderExtent.height + 15) / 16, 1);
        });
    return reflection;
}

static constexpr float SSR_EDGE_FADE = 0.1f;

ReflectionFrame SetupSSRTracePass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  Core::Extent2D renderExtent,
                                  const RenderTargets& targets,
                                  const SceneResources& scene,
                                  uint32_t sceneIndex,
                                  uint64_t frameNumber,
                                  uint32_t activeCheckerboardField,
                                  const Core::ReflectionConfiguration& reflectionConfig)
{
    ZoneScoped;
    ReflectionFrame reflection{};
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    if (reflectionRoughnessMax < 0.0f) {
        return reflection;
    }

    const RDGBuffer hitDescriptors = graph.CreateBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER, sizeof(ReflectionHitDescriptor) * renderExtent.width * renderExtent.height, true);
    reflection.hitDescriptors = hitDescriptors;

    RenderPass& pass = graph.AddPass("[Reflection] SSR Trace"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReflectionsShade);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    pass.WriteBuffer(hitDescriptors);

    pass.Execute([pipelineManager, sceneIndex, renderExtent, frameNumber, reflectionRoughnessMax, ssrThickness = reflectionConfig.ssrThickness, ssrMaxSteps = reflectionConfig.ssrMaxSteps, activeCheckerboardField,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy, &scene, hitDescriptors](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            SSRTracePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .reflectionDescriptors = graph.GetBufferAddress(hitDescriptors),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .sceneDataIndex = sceneIndex,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .roughnessMax = reflectionRoughnessMax,
                .ssrThickness = ssrThickness,
                .ssrMaxSteps = static_cast<uint32_t>(ssrMaxSteps),
                .edgeFade = SSR_EDGE_FADE,
                .activeCheckerboardField = activeCheckerboardField,
                .pad0 = 0u,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("ssr_trace"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (renderExtent.width + 15) / 16, (renderExtent.height + 15) / 16, 1);
        });
    return reflection;
}

ReflectionFrame SetupReflectionShadePass(RenderGraph& graph,
                                         PipelineManager* pipelineManager,
                                         const Core::ViewFamily& viewFamily,
                                         Core::Extent2D renderExtent,
                                         const RenderTargets& targets,
                                         const SceneResources& scene,
                                         const ReflectionFrame& trace,
                                         const DDGIFrame& ddgi,
                                         const WorldGridFrame& worldGrid,
                                         uint32_t sceneIndex,
                                         uint64_t frameNumber,
                                         uint32_t activeCheckerboardField,
                                         const Core::ReflectionConfiguration& reflectionConfig,
                                         bool bDDGIApply,
                                         bool bCheckerboardPacked,
                                         bool bDisableScreenTier)
{
    ZoneScoped;
    ReflectionFrame reflection = trace;
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    if (reflectionRoughnessMax < 0.0f || !trace.hitDescriptors.IsValid()) {
        return reflection;
    }

    const RDGBuffer hitDescriptors = trace.hitDescriptors;
    const bool bHasTLAS = scene.tlas.IsValid();
    const bool bDDGI = bDDGIApply && ddgi.cascades.IsValid();
    const RDGBuffer ddgiCascades = ddgi.cascades;
    const bool bWorldGrid = worldGrid.lightGrid.IsValid() && worldGrid.indexList.IsValid();
    const RDGBuffer worldLightGrid = worldGrid.lightGrid;
    const RDGBuffer worldIndexList = worldGrid.indexList;
    const RDGBuffer probeGrid = worldGrid.probeGrid;
    const bool bSSRSource = reflectionConfig.bScreenSpaceTrace;
    const RDGTexture litHistory = targets.litSnapshotHistory;
    const RDGTexture depthHistory = targets.depthCopyHistory;
    const RDGTexture gbufferOneHistory = targets.gbufferOneHistory;
    const bool bScreenSpace = (reflectionConfig.bScreenSpaceLighting || bSSRSource) && !bDisableScreenTier && litHistory.IsValid() && depthHistory.IsValid() && gbufferOneHistory.IsValid();
    const int32_t skyboxIndex = viewFamily.skyboxIndex;

    const RDGTexture specNoisy = graph.CreateTexture(REFLECTION_SPEC_NOISY_TARGET, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, VkClearValue{.color = {{0.0f, 0.0f, 0.0f, 0.0f}}}, true);
    reflection.specNoisy = specNoisy;
    const bool bHitDelta = reflectionConfig.bMergedDenoise && bHasTLAS;
    RDGTexture hitDelta{};
    if (bHitDelta) {
        const RDGTextureRing hitDeltaRing = graph.CreateVersionedTexture(REFLECTION_HIT_DELTA_TARGET, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT, false,
                                                                            VkClearValue{.color = {{0.0f, 0.0f, 0.0f, 0.0f}}});
        hitDelta = hitDeltaRing.Current();
        reflection.hitDelta = hitDelta;
        reflection.hitDeltaHistory = hitDeltaRing.Version(1);
    }

    RenderPass& pass = graph.AddPass("[Reflection] Shade"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReflectionsShade);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.primitives);
    pass.ReadBuffer(scene.models);
    pass.ReadBuffer(scene.materials);
    pass.ReadBuffer(scene.indices);
    pass.ReadBuffer(scene.vertexAttributes);
    pass.ReadBuffer(scene.vertexPositions);
    pass.ReadBuffer(hitDescriptors);
    pass.ReadBuffer(scene.reflectionProbes);
    if (probeGrid.IsValid()) { pass.ReadBuffer(probeGrid); }
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (bHasTLAS) { pass.ReadTLASBuffer(scene.tlas); }
    if (bDDGI) { pass.ReadBuffer(ddgi.cascades); }
    if (bWorldGrid) {
        pass.ReadBuffer(worldLightGrid);
        pass.ReadBuffer(worldIndexList);
    }
    if (bScreenSpace) {
        pass.ReadSampledImage(litHistory);
        pass.ReadSampledImage(depthHistory);
        pass.ReadSampledImage(gbufferOneHistory);
    }
    pass.WriteStorageImage(specNoisy);
    if (bHitDelta) {
        pass.WriteStorageImage(hitDelta);
    }

    const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());

    const uint32_t sunMode = (!bHasTLAS && reflectionConfig.sunMode == Core::ReflectionConfiguration::SunMode::ShadowRay)
        ? static_cast<uint32_t>(Core::ReflectionConfiguration::SunMode::AlwaysLit)
        : static_cast<uint32_t>(reflectionConfig.sunMode);
    pass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, bDDGI, bWorldGrid, bScreenSpace, bHitDelta, skyboxIndex, reflectionRoughnessMax, intensity = reflectionConfig.intensity, iblIntensity = viewFamily.iblIntensity, bCheckerboardPacked, sunMode,
            field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy, reflectionProbeCount, litHistory, depthHistory, gbufferOneHistory,
            hitDescriptors, ddgiCascades, worldLightGrid, worldIndexList, probeGrid, specNoisy, hitDelta](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(scene.tlas) : ~0u;

            ReflectionShadePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .reflectionDescriptors = graph.GetBufferAddress(hitDescriptors),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .instanceBuffer = graph.GetBufferAddress(scene.instances),
                .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
                .modelBuffer = graph.GetBufferAddress(scene.models),
                .materialBuffer = graph.GetBufferAddress(scene.materials),
                .indexBuffer = graph.GetBufferAddress(scene.indices),
                .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
                .vertexPosBuffer = graph.GetBufferAddress(scene.vertexPositions),
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(ddgiCascades) : 0,
                .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(worldLightGrid) : 0,
                .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(worldIndexList) : 0,
                .renderExtent = {renderExtent.width, renderExtent.height},
                .sceneDataIndex = sceneIndex,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(specNoisy),
                .tlasIndex = tlasIndex,
                .skyboxIndex = skyboxIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .activeCheckerboardField = field,
                .roughnessMax = reflectionRoughnessMax,
                .intensity = intensity,
                .bDDGIApply = bDDGI ? 1u : 0u,
                .bCheckerboardPacked = bCheckerboardPacked ? 1u : 0u,
                .litHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(litHistory) : ~0u,
                .depthHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0u,
                .gbufferOneHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0u,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
                .worldGridProbeGrid = (!viewFamily.bReflectionProbeBruteForce && probeGrid.IsValid()) ? graph.GetBufferAddress(probeGrid) : 0,
                .sunMode = sunMode,
                .maxRayIntensity = reflectionConfig.maxRayIntensity,
                .iblIntensity = iblIntensity,
                .mirrorRoughnessMax = reflectionConfig.mirrorRoughnessMax,
                .bAlphaTest = reflectionConfig.bAlphaTest ? 1u : 0u,
                .hitLocalShadowRays = bHasTLAS ? static_cast<uint32_t>(reflectionConfig.hitLocalShadowRays) : 0u,
                .hitTextureLod = reflectionConfig.hitTextureLod,
                .hitDeltaIndex = bHitDelta ? graph.GetStorageImageViewDescriptorIndex(hitDelta) : ~0u,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("reflection_shade"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (renderExtent.width + 15) / 16, (renderExtent.height + 15) / 16, 1);
        });
    return reflection;
}
} // Render
