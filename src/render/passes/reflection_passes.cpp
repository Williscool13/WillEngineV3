//
// Created by William on 2026-07-09.
//

#include "render/passes/reflection_passes.h"

#include "ddgi_passes.h"
#include "render/render_config.h"
#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupReflectionTracePass(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex,
                              uint64_t frameNumber,
                              const Core::RTReflectionConfiguration& reflectionConfig)
{
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig, REFLECTION_NO_BRDF_CLAMP);
    if (reflectionRoughnessMax < 0.0f || !graph.HasBuffer(RT_TLAS_BUFFER)) {
        return;
    }

    graph.CreateBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER, sizeof(ReflectionHitDescriptor) * renderExtent[0] * renderExtent[1], true);

    RenderPass& pass = graph.AddPass(SID("[Reflection] Trace"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReflectionsShade);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.WriteBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER);

    pass.Execute([pipelineManager, sceneIndex, renderExtent, frameNumber, reflectionRoughnessMax,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReflectionTracePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .reflectionDescriptors = graph.GetBufferAddress(REFLECTION_HIT_DESCRIPTORS_BUFFER),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .sceneDataIndex = sceneIndex,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .roughnessMax = reflectionRoughnessMax,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("reflection_trace"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (renderExtent[0] + 15) / 16, (renderExtent[1] + 15) / 16, 1);
        });
}

void SetupReflectionShadePass(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex,
                              uint64_t frameNumber,
                              uint32_t activeCheckerboardField,
                              const Core::RTReflectionConfiguration& reflectionConfig,
                              bool bDDGIApply,
                              bool bCheckerboardPacked,
                              float brdfRoughnessMax,
                              bool bDisableScreenTier)
{
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig, brdfRoughnessMax);
    if (reflectionRoughnessMax < 0.0f || !graph.HasBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER)) {
        return;
    }

    const bool bHasTLAS = graph.HasBuffer(RT_TLAS_BUFFER);
    const bool bDDGI = bDDGIApply && graph.HasBuffer(DDGI_CASCADES_BUFFER);
    const bool bWorldGrid = graph.HasBuffer(SID("world_grid_light_grid")) && graph.HasBuffer(SID("world_grid_index_list"));
    const bool bScreenSpace = reflectionConfig.bScreenSpaceLighting && !bDisableScreenTier && graph.HasTexture(SID("lit_color_history")) && graph.HasTexture(SID("depth_history")) && graph.HasTexture(SID("gbuffer_one_history"));
    const int32_t skyboxIndex = viewFamily.skyboxIndex;

    graph.CreateTexture(REFLECTION_SPEC_NOISY_TARGET, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& pass = graph.AddPass(SID("[Reflection] Shade"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReflectionsShade);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER);
    pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (graph.HasBuffer(SID("world_grid_probe_grid"))) { pass.ReadBuffer(SID("world_grid_probe_grid")); }
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (bHasTLAS) { pass.ReadTLASBuffer(RT_TLAS_BUFFER); }
    if (bDDGI) { AddDDGISampleDependencies(graph, pass); }
    if (bWorldGrid) {
        pass.ReadBuffer(SID("world_grid_light_grid"));
        pass.ReadBuffer(SID("world_grid_index_list"));
    }
    if (bScreenSpace) {
        pass.ReadSampledImage(SID("lit_color_history"));
        pass.ReadSampledImage(SID("depth_history"));
        pass.ReadSampledImage(SID("gbuffer_one_history"));
    }
    pass.WriteStorageImage(REFLECTION_SPEC_NOISY_TARGET);

    const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
    pass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, bDDGI, bWorldGrid, bScreenSpace, skyboxIndex, reflectionRoughnessMax, intensity = reflectionConfig.intensity, bCheckerboardPacked,
            field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy, reflectionProbeCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;

            ReflectionShadePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .reflectionDescriptors = graph.GetBufferAddress(REFLECTION_HIT_DESCRIPTORS_BUFFER),
                .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
                .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
                .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_light_grid")) : 0,
                .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_index_list")) : 0,
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .sceneDataIndex = sceneIndex,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(REFLECTION_SPEC_NOISY_TARGET),
                .tlasIndex = tlasIndex,
                .skyboxIndex = skyboxIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .activeCheckerboardField = field,
                .roughnessMax = reflectionRoughnessMax,
                .intensity = intensity,
                .bDDGIApply = bDDGI ? 1u : 0u,
                .bCheckerboardPacked = bCheckerboardPacked ? 1u : 0u,
                .litHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(SID("lit_color_history")) : ~0u,
                .depthHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : ~0u,
                .gbufferOneHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : ~0u,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
                .worldGridProbeGrid = (!viewFamily.bReflectionProbeBruteForce && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("reflection_shade"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (renderExtent[0] + 15) / 16, (renderExtent[1] + 15) / 16, 1);
        });
}
} // Render
