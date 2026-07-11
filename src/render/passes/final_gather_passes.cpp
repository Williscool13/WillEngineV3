//
// Created by William on 2026-07-12.
//

#include "render/passes/final_gather_passes.h"

#include "render/passes/ddgi_passes.h"
#include "render/render_utils.h"
#include "render/interface/render_interface.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, uint64_t frameNumber)
{
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(SCENE_DATA_BUFFER) || !graph.HasBuffer(WORLD_CACHE_ENTRIES) || !graph.HasBuffer(WORLD_CACHE_CELLS)
        || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_PRIMITIVE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER)
        || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER) || !graph.HasBuffer(GEOMETRY_INDEX_BUFFER) || !graph.HasBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER)) {
        return {};
    }

    const Core::Array<uint32_t, 2> gatherExtent = {(renderExtent[0] + 1u) / 2u, (renderExtent[1] + 1u) / 2u};
    graph.CreateTexture(GI_GATHER_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_DATA, TextureInfo{VK_FORMAT_R16G16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass(SID("GI Diffuse Gather"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(WORLD_CACHE_ENTRIES);
    pass.ReadBuffer(WORLD_CACHE_CELLS);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    const bool bCascades = AddDDGISampleDependencies(graph, pass);
    pass.WriteStorageImage(GI_GATHER_SH_R);
    pass.WriteStorageImage(GI_GATHER_SH_G);
    pass.WriteStorageImage(GI_GATHER_SH_B);
    pass.WriteStorageImage(GI_GATHER_DATA);

    pass.Execute([pipelineManager, sceneIndex, frameNumber, gatherExtent, renderExtent, bCascades,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy,
            skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gi_gather"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIGatherPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .ddgiCascades = bCascades ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
            .cacheEntries = graph.GetBufferAddress(WORLD_CACHE_ENTRIES),
            .cacheCells = graph.GetBufferAddress(WORLD_CACHE_CELLS),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .gatherExtent = {gatherExtent[0], gatherExtent[1]},
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .shRIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_SH_R),
            .shGIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_SH_G),
            .shBIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_SH_B),
            .dataIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_DATA),
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .bCascadesValid = bCascades ? 1u : 0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (gatherExtent[0] + 7u) / 8u, (gatherExtent[1] + 7u) / 8u, 1);
    });

    return FinalGatherFrame{.bValid = true};
}
} // Render
