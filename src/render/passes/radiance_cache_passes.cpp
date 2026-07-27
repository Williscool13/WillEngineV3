//
// Created by William on 2026-07-10.
//

#include "render/passes/radiance_cache_passes.h"

#include "render/passes/ddgi_passes.h"
#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
RadianceCacheFrame SetupRadianceCacheBegin(RenderGraph& graph, PipelineManager* pipelineManager, uint64_t frameNumber, const glm::vec3& cameraPos, bool bFreeze)
{
    graph.CreateBuffer(RADIANCE_CACHE_ENTRIES, RADIANCE_CACHE_ENTRIES_BYTES, false);
    graph.CreateBuffer(RADIANCE_CACHE_KEYS, RADIANCE_CACHE_KEYS_BYTES, false);
    graph.CreateBuffer(RADIANCE_CACHE_CELLS, RADIANCE_CACHE_CELLS_BYTES, false);
    graph.CreateBuffer(RADIANCE_CACHE_ACTIVE, RADIANCE_CACHE_ENTRIES_BYTES, false);
    graph.CreateBuffer(RADIANCE_CACHE_ACTIVE_LIST, RADIANCE_CACHE_ACTIVE_LIST_BYTES, false);
    graph.CreateBuffer(RADIANCE_CACHE_ACTIVE_COUNT, sizeof(uint32_t), false);
    graph.CreateBuffer(RADIANCE_CACHE_SHADE_ARGS, RADIANCE_CACHE_SHADE_ARGS_BYTES, false);
    graph.CreateBuffer(RADIANCE_CACHE_DESCRIPTORS, RADIANCE_CACHE_DESCRIPTORS_BYTES, false);
    graph.CreateBuffer(RADIANCE_CACHE_STATS, sizeof(RadianceCacheStats), false);

    RenderPass& clearPass = graph.AddPass(SID("Radiance Cache Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::RadianceCache);
    clearPass.WriteTransferBuffer(RADIANCE_CACHE_ENTRIES);
    clearPass.WriteTransferBuffer(RADIANCE_CACHE_ACTIVE);
    clearPass.WriteTransferBuffer(RADIANCE_CACHE_ACTIVE_COUNT);
    clearPass.WriteTransferBuffer(RADIANCE_CACHE_SHADE_ARGS);
    clearPass.WriteTransferBuffer(RADIANCE_CACHE_CELLS);
    clearPass.WriteTransferBuffer(RADIANCE_CACHE_STATS);
    clearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_ENTRIES), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_ACTIVE), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_ACTIVE_COUNT), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_SHADE_ARGS), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_CELLS), 0, VK_WHOLE_SIZE, RADIANCE_CACHE_RADIANCE_UNSHADED);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_STATS), 0, VK_WHOLE_SIZE, 0);
    });

    const bool bHistoryValid = graph.HasBuffer(RADIANCE_CACHE_ENTRIES_HISTORY) && graph.HasBuffer(RADIANCE_CACHE_KEYS_HISTORY) && graph.HasBuffer(RADIANCE_CACHE_CELLS_HISTORY);
    if (bHistoryValid) {
        RenderPass& carryPass = graph.AddPass(SID("Radiance Cache Carry Forward"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::RadianceCache);
        carryPass.ReadBuffer(RADIANCE_CACHE_ENTRIES_HISTORY);
        carryPass.ReadBuffer(RADIANCE_CACHE_KEYS_HISTORY);
        carryPass.ReadBuffer(RADIANCE_CACHE_CELLS_HISTORY);
        carryPass.ReadWriteBuffer(RADIANCE_CACHE_ENTRIES);
        carryPass.ReadWriteBuffer(RADIANCE_CACHE_KEYS);
        carryPass.ReadWriteBuffer(RADIANCE_CACHE_CELLS);
        carryPass.ReadWriteBuffer(RADIANCE_CACHE_STATS);
        carryPass.Execute([pipelineManager, frameNumber, cameraPos, bFreeze](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("radiance_cache_carry_forward"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            RadianceCacheCarryForwardPushConstant pc{
                .prevEntries = graph.GetBufferAddress(RADIANCE_CACHE_ENTRIES_HISTORY),
                .prevKeys = graph.GetBufferAddress(RADIANCE_CACHE_KEYS_HISTORY),
                .prevCells = graph.GetBufferAddress(RADIANCE_CACHE_CELLS_HISTORY),
                .nextEntries = graph.GetBufferAddress(RADIANCE_CACHE_ENTRIES),
                .nextKeys = graph.GetBufferAddress(RADIANCE_CACHE_KEYS),
                .nextCells = graph.GetBufferAddress(RADIANCE_CACHE_CELLS),
                .cameraPos = glm::vec4(cameraPos, 0.0f),
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .bFreeze = bFreeze ? 1u : 0u,
                .stats = graph.GetBufferAddress(RADIANCE_CACHE_STATS),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            const uint32_t groups = (RADIANCE_CACHE_HASH_CAPACITY + 63u) / 64u;
            vkCmdDispatch(cmd, groups, 1, 1);
        });
    }

    graph.CreateBuffer(RADIANCE_CACHE_BUFFERS_CURRENT, sizeof(RadianceCacheBuffers), false);
    RenderPass& bundlePass = graph.AddPass(SID("Radiance Cache Buffers Upload"), VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::RadianceCache);
    bundlePass.WriteTransferBuffer(RADIANCE_CACHE_BUFFERS_CURRENT);
    bundlePass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const RadianceCacheBuffers buffers{
            .entries = graph.PeekBufferAddress(RADIANCE_CACHE_ENTRIES),
            .keys = graph.PeekBufferAddress(RADIANCE_CACHE_KEYS),
            .cells = graph.PeekBufferAddress(RADIANCE_CACHE_CELLS),
            .active = graph.PeekBufferAddress(RADIANCE_CACHE_ACTIVE),
            .descriptors = graph.PeekBufferAddress(RADIANCE_CACHE_DESCRIPTORS),
            .activeList = graph.PeekBufferAddress(RADIANCE_CACHE_ACTIVE_LIST),
            .activeCount = graph.PeekBufferAddress(RADIANCE_CACHE_ACTIVE_COUNT),
            .stats = graph.PeekBufferAddress(RADIANCE_CACHE_STATS),
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_BUFFERS_CURRENT), 0, sizeof(buffers), &buffers);
    });

    return RadianceCacheFrame{.bValid = true};
}

void SetupRadianceCacheShade(RenderGraph& graph, PipelineManager* pipelineManager, const RadianceCacheFrame& frame, uint32_t sceneIndex, bool bDDGIFeedbackValid, int32_t skyboxIndex, float iblIntensity, float maxRadiance, float bounceIntensity, uint32_t accumCap, uint32_t reflectionProbeCount, bool bReflectionProbeBruteForce)
{
    if (!frame.bValid) {
        return;
    }

    bounceIntensity = glm::clamp(bounceIntensity, 0.0f, 1.0f);
    accumCap = glm::clamp(accumCap, 1u, 255u);
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(SCENE_DATA_BUFFER) || !graph.HasBuffer(LIGHT_DATA_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER)
        || !graph.HasBuffer(GEOMETRY_PRIMITIVE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)
        || !graph.HasBuffer(GEOMETRY_INDEX_BUFFER) || !graph.HasBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER) || !graph.HasBuffer(GEOMETRY_VERTEX_POSITION_BUFFER)) {
        return;
    }
    const bool bFeedback = bDDGIFeedbackValid && graph.HasBuffer(DDGI_CASCADES_BUFFER);

    RenderPass& indirectPass = graph.AddPass(SID("Radiance Cache Build Indirect"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::RadianceCache);
    indirectPass.ReadWriteBuffer(RADIANCE_CACHE_ACTIVE_COUNT);
    indirectPass.WriteBuffer(RADIANCE_CACHE_SHADE_ARGS);
    indirectPass.Execute([pipelineManager](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("radiance_cache_build_indirect"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        RadianceCacheBuildIndirectPushConstant pc{
            .activeCount = graph.GetBufferAddress(RADIANCE_CACHE_ACTIVE_COUNT),
            .indirectArgs = graph.GetBufferAddress(RADIANCE_CACHE_SHADE_ARGS),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });

    RenderPass& pass = graph.AddPass(SID("Radiance Cache Shade"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::RadianceCache);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(RADIANCE_CACHE_ACTIVE_LIST);
    pass.ReadBuffer(RADIANCE_CACHE_ACTIVE_COUNT);
    pass.ReadIndirectBuffer(RADIANCE_CACHE_SHADE_ARGS);
    pass.ReadBuffer(RADIANCE_CACHE_DESCRIPTORS);
    pass.ReadWriteBuffer(RADIANCE_CACHE_CELLS);
    pass.ReadWriteBuffer(RADIANCE_CACHE_STATS);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_POSITION_BUFFER);
    pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (graph.HasBuffer(SID("world_grid_probe_grid"))) { pass.ReadBuffer(SID("world_grid_probe_grid")); }
    if (bFeedback) {
        AddDDGISampleDependencies(graph, pass);
    }
    pass.Execute([pipelineManager, sceneIndex, bFeedback, skyboxIndex, iblIntensity, maxRadiance, bounceIntensity, accumCap, reflectionProbeCount, bReflectionProbeBruteForce](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("radiance_cache_shade"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        RadianceCacheShadePushConstant pc{
            .activeList = graph.GetBufferAddress(RADIANCE_CACHE_ACTIVE_LIST),
            .activeCount = graph.GetBufferAddress(RADIANCE_CACHE_ACTIVE_COUNT),
            .descriptors = graph.GetBufferAddress(RADIANCE_CACHE_DESCRIPTORS),
            .cells = graph.GetBufferAddress(RADIANCE_CACHE_CELLS),
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
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
            .accumCap = accumCap,
            .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
            .worldGridProbeGrid = (!bReflectionProbeBruteForce && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
            .stats = graph.GetBufferAddress(RADIANCE_CACHE_STATS),
            .reflectionProbeCount = reflectionProbeCount,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(RADIANCE_CACHE_SHADE_ARGS), 0);
    });
}

void SetupRadianceCacheEnd(RenderGraph& graph, const RadianceCacheFrame& frame)
{
    if (!frame.bValid) {
        return;
    }
    graph.CarryBufferToNextFrame(RADIANCE_CACHE_ENTRIES, RADIANCE_CACHE_ENTRIES_HISTORY, 0);
    graph.CarryBufferToNextFrame(RADIANCE_CACHE_KEYS, RADIANCE_CACHE_KEYS_HISTORY, 0);
    graph.CarryBufferToNextFrame(RADIANCE_CACHE_CELLS, RADIANCE_CACHE_CELLS_HISTORY, 0);
}

void SetupRadianceCacheDebug(RenderGraph& graph, PipelineManager* pipelineManager, const RadianceCacheFrame& frame, float debugExposure, int32_t normalBucket)
{
#ifdef WDEBUG
    if (!frame.bValid || !graph.HasBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER)) {
        return;
    }

    RenderPass& pass = graph.AddPass(SID("Radiance Cache Debug"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadWriteBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER);
    pass.WriteBuffer(GPU_DEBUG_CUBE_INSTANCE_BUFFER);
    pass.ReadBuffer(RADIANCE_CACHE_ENTRIES);
    pass.ReadBuffer(RADIANCE_CACHE_KEYS);
    pass.ReadBuffer(RADIANCE_CACHE_CELLS);
    pass.Execute([pipelineManager, debugExposure, normalBucket](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gpu_debug_radiance_cache"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        RadianceCacheDebugPushConstant pc{
            .cubeArgs = graph.GetBufferAddress(GPU_DEBUG_CUBE_ARGS_BUFFER),
            .cubeBuffer = graph.GetBufferAddress(GPU_DEBUG_CUBE_INSTANCE_BUFFER),
            .entries = graph.GetBufferAddress(RADIANCE_CACHE_ENTRIES),
            .keys = graph.GetBufferAddress(RADIANCE_CACHE_KEYS),
            .cells = graph.GetBufferAddress(RADIANCE_CACHE_CELLS),
            .exposure = debugExposure,
            .bucketFilter = normalBucket,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (RADIANCE_CACHE_HASH_CAPACITY + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });
#endif
}
} // Render
