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
void SetupReflectionShadePass(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex,
                              uint64_t frameNumber,
                              uint32_t activeCheckerboardField,
                              const Core::RTReflectionConfiguration& reflectionConfig,
                              bool bDDGIApply)
{
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    if (reflectionRoughnessMax < 0.0f || !graph.HasBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER)) {
        return;
    }

    const bool bHasTLAS = graph.HasBuffer(RT_TLAS_BUFFER);
    const bool bDDGI = bDDGIApply && graph.HasBuffer(DDGI_CASCADES_BUFFER);
    const int32_t skyboxIndex = viewFamily.skyboxIndex;

    graph.CreateTexture(REFLECTION_SPEC_NOISY_TARGET, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& pass = graph.AddPass(SID("[Reflection] Shade"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (bHasTLAS) { pass.ReadTLASBuffer(RT_TLAS_BUFFER); }
    if (bDDGI) { AddDDGISampleDependencies(graph, pass); }
    pass.WriteStorageImage(REFLECTION_SPEC_NOISY_TARGET);

    pass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, bDDGI, skyboxIndex, reflectionRoughnessMax, intensity = reflectionConfig.intensity,
            field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .bLocalNEE = 1u,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("reflection_shade"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (renderExtent[0] + 15) / 16, (renderExtent[1] + 15) / 16, 1);
        });
}
} // Render
