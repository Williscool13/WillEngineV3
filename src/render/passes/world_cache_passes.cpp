//
// Created by William on 2026-07-10.
//

#include "render/passes/world_cache_passes.h"

#include "render/passes/ddgi_passes.h"
#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
WorldCacheFrame SetupWorldCacheBegin(RenderGraph& graph, PipelineManager* pipelineManager, uint64_t frameNumber, const glm::vec3& cameraPos)
{
    graph.CreateBuffer(WORLD_CACHE_ENTRIES, WORLD_CACHE_ENTRIES_BYTES, false);
    graph.CreateBuffer(WORLD_CACHE_KEYS, WORLD_CACHE_KEYS_BYTES, false);
    graph.CreateBuffer(WORLD_CACHE_CELLS, WORLD_CACHE_CELLS_BYTES, false);
    graph.CreateBuffer(WORLD_CACHE_ACTIVE, WORLD_CACHE_ENTRIES_BYTES, false);
    graph.CreateBuffer(WORLD_CACHE_DESCRIPTORS, WORLD_CACHE_DESCRIPTORS_BYTES, false);

    RenderPass& clearPass = graph.AddPass(SID("World Cache Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, ResourceCategory::Lighting);
    clearPass.WriteTransferBuffer(WORLD_CACHE_ENTRIES);
    clearPass.WriteTransferBuffer(WORLD_CACHE_ACTIVE);
    clearPass.WriteTransferBuffer(WORLD_CACHE_CELLS);
    clearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(WORLD_CACHE_ENTRIES), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(WORLD_CACHE_ACTIVE), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(WORLD_CACHE_CELLS), 0, VK_WHOLE_SIZE, WORLD_CACHE_RADIANCE_UNSHADED);
    });

    const bool bHistoryValid = graph.HasBuffer(WORLD_CACHE_ENTRIES_HISTORY) && graph.HasBuffer(WORLD_CACHE_KEYS_HISTORY) && graph.HasBuffer(WORLD_CACHE_CELLS_HISTORY);
    if (bHistoryValid) {
        RenderPass& carryPass = graph.AddPass(SID("World Cache Carry Forward"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
        carryPass.ReadBuffer(WORLD_CACHE_ENTRIES_HISTORY);
        carryPass.ReadBuffer(WORLD_CACHE_KEYS_HISTORY);
        carryPass.ReadBuffer(WORLD_CACHE_CELLS_HISTORY);
        carryPass.ReadWriteBuffer(WORLD_CACHE_ENTRIES);
        carryPass.ReadWriteBuffer(WORLD_CACHE_KEYS);
        carryPass.ReadWriteBuffer(WORLD_CACHE_CELLS);
        carryPass.Execute([pipelineManager, frameNumber, cameraPos](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("world_cache_carry_forward"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            WorldCacheCarryForwardPushConstant pc{
                .prevEntries = graph.GetBufferAddress(WORLD_CACHE_ENTRIES_HISTORY),
                .prevKeys = graph.GetBufferAddress(WORLD_CACHE_KEYS_HISTORY),
                .prevCells = graph.GetBufferAddress(WORLD_CACHE_CELLS_HISTORY),
                .nextEntries = graph.GetBufferAddress(WORLD_CACHE_ENTRIES),
                .nextKeys = graph.GetBufferAddress(WORLD_CACHE_KEYS),
                .nextCells = graph.GetBufferAddress(WORLD_CACHE_CELLS),
                .cameraPos = glm::vec4(cameraPos, 0.0f),
                .frameIndex = static_cast<uint32_t>(frameNumber),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            const uint32_t groups = (WORLD_CACHE_HASH_CAPACITY + 63u) / 64u;
            vkCmdDispatch(cmd, groups, 1, 1);
        });
    }

    graph.CreateBuffer(WORLD_CACHE_BUFFERS_CURRENT, sizeof(WorldCacheBuffers), false);
    RenderPass& bundlePass = graph.AddPass(SID("World Cache Buffers Upload"), VK_PIPELINE_STAGE_2_CLEAR_BIT, ResourceCategory::Lighting);
    bundlePass.WriteTransferBuffer(WORLD_CACHE_BUFFERS_CURRENT);
    bundlePass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const WorldCacheBuffers buffers{
            .entries = graph.GetBufferAddress(WORLD_CACHE_ENTRIES),
            .keys = graph.GetBufferAddress(WORLD_CACHE_KEYS),
            .cells = graph.GetBufferAddress(WORLD_CACHE_CELLS),
            .active = graph.GetBufferAddress(WORLD_CACHE_ACTIVE),
            .descriptors = graph.GetBufferAddress(WORLD_CACHE_DESCRIPTORS),
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(WORLD_CACHE_BUFFERS_CURRENT), 0, sizeof(buffers), &buffers);
    });

    return WorldCacheFrame{.bValid = true};
}

void SetupWorldCacheShade(RenderGraph& graph, PipelineManager* pipelineManager, const WorldCacheFrame& frame, uint32_t sceneIndex, bool bDDGIFeedbackValid, int32_t skyboxIndex, float iblIntensity, float maxRadiance, float bounceIntensity)
{
    if (!frame.bValid) {
        return;
    }
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(SCENE_DATA_BUFFER) || !graph.HasBuffer(LIGHT_DATA_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER)
        || !graph.HasBuffer(GEOMETRY_PRIMITIVE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)
        || !graph.HasBuffer(GEOMETRY_INDEX_BUFFER) || !graph.HasBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER) || !graph.HasBuffer(GEOMETRY_VERTEX_POSITION_BUFFER)) {
        return;
    }
    const bool bWorldGrid = graph.HasBuffer(SID("world_grid_light_grid")) && graph.HasBuffer(SID("world_grid_index_list"));
    const bool bFeedback = bDDGIFeedbackValid && graph.HasBuffer(DDGI_CASCADES_BUFFER);

    RenderPass& pass = graph.AddPass(SID("World Cache Shade"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(WORLD_CACHE_ACTIVE);
    pass.ReadBuffer(WORLD_CACHE_DESCRIPTORS);
    pass.ReadWriteBuffer(WORLD_CACHE_CELLS);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_POSITION_BUFFER);
    if (bWorldGrid) {
        pass.ReadBuffer(SID("world_grid_light_grid"));
        pass.ReadBuffer(SID("world_grid_index_list"));
    }
    if (bFeedback) {
        AddDDGISampleDependencies(graph, pass);
    }
    pass.Execute([pipelineManager, sceneIndex, bWorldGrid, bFeedback, skyboxIndex, iblIntensity, maxRadiance, bounceIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("world_cache_shade"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        WorldCacheShadePushConstant pc{
            .active = graph.GetBufferAddress(WORLD_CACHE_ACTIVE),
            .descriptors = graph.GetBufferAddress(WORLD_CACHE_DESCRIPTORS),
            .cells = graph.GetBufferAddress(WORLD_CACHE_CELLS),
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_light_grid")) : 0,
            .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_index_list")) : 0,
            .previousCascades = bFeedback ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .vertexPosBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
            .sceneDataIndex = sceneIndex,
            .bFeedbackValid = bFeedback ? 1u : 0u,
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .maxRadiance = maxRadiance,
            .bounceIntensity = bounceIntensity,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (WORLD_CACHE_HASH_CAPACITY + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });
}

void SetupWorldCacheEnd(RenderGraph& graph, const WorldCacheFrame& frame)
{
    if (!frame.bValid) {
        return;
    }
    graph.CarryBufferToNextFrame(WORLD_CACHE_ENTRIES, WORLD_CACHE_ENTRIES_HISTORY, 0);
    graph.CarryBufferToNextFrame(WORLD_CACHE_KEYS, WORLD_CACHE_KEYS_HISTORY, 0);
    graph.CarryBufferToNextFrame(WORLD_CACHE_CELLS, WORLD_CACHE_CELLS_HISTORY, 0);
}

void SetupWorldCacheDebug(RenderGraph& graph, PipelineManager* pipelineManager, const WorldCacheFrame& frame, float debugExposure, int32_t normalBucket)
{
#ifdef WDEBUG
    if (!frame.bValid || !graph.HasBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER)) {
        return;
    }

    RenderPass& pass = graph.AddPass(SID("World Cache Debug"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Debug);
    pass.ReadWriteBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER);
    pass.WriteBuffer(GPU_DEBUG_CUBE_INSTANCE_BUFFER);
    pass.ReadBuffer(WORLD_CACHE_ENTRIES);
    pass.ReadBuffer(WORLD_CACHE_KEYS);
    pass.ReadBuffer(WORLD_CACHE_CELLS);
    pass.Execute([pipelineManager, debugExposure, normalBucket](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gpu_debug_world_cache"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        WorldCacheDebugPushConstant pc{
            .cubeArgs = graph.GetBufferAddress(GPU_DEBUG_CUBE_ARGS_BUFFER),
            .cubeBuffer = graph.GetBufferAddress(GPU_DEBUG_CUBE_INSTANCE_BUFFER),
            .entries = graph.GetBufferAddress(WORLD_CACHE_ENTRIES),
            .keys = graph.GetBufferAddress(WORLD_CACHE_KEYS),
            .cells = graph.GetBufferAddress(WORLD_CACHE_CELLS),
            .exposure = debugExposure,
            .bucketFilter = normalBucket,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (WORLD_CACHE_HASH_CAPACITY + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });
#endif
}
} // Render
