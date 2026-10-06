//
// Created by William on 2026-07-10.
//

#include "render/passes/radiance_cache_passes.h"

#include <tracy/Tracy.hpp>

#include "render/renderer_types.h"
#include "render/passes/ddgi_passes.h"
#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
RadianceCacheFrame SetupRadianceCacheBegin(RenderGraph& graph, PipelineManager* pipelineManager, uint64_t frameNumber, const glm::vec3& cameraPos, bool bFreeze, uint32_t shadeInterval)
{
    ZoneScoped;
    const RDGBufferRing entriesRing = graph.CreateVersionedBuffer(RADIANCE_CACHE_ENTRIES, RADIANCE_CACHE_ENTRIES_BYTES, 1, VersionSource::Fresh);
    const RDGBufferRing keysRing = graph.CreateVersionedBuffer(RADIANCE_CACHE_KEYS, RADIANCE_CACHE_KEYS_BYTES, 1, VersionSource::Fresh);
    const RDGBufferRing cellsRing = graph.CreateVersionedBuffer(RADIANCE_CACHE_CELLS, RADIANCE_CACHE_CELLS_BYTES, 1, VersionSource::Fresh);
    const RDGBuffer entries = entriesRing.Current();
    const RDGBuffer keys = keysRing.Current();
    const RDGBuffer cells = cellsRing.Current();
    const RDGBuffer active = graph.CreateBuffer(RADIANCE_CACHE_ACTIVE, RADIANCE_CACHE_ENTRIES_BYTES, false);
    const RDGBuffer activeList = graph.CreateBuffer(RADIANCE_CACHE_ACTIVE_LIST, RADIANCE_CACHE_ACTIVE_LIST_BYTES, false);
    const RDGBuffer activeCount = graph.CreateBuffer(RADIANCE_CACHE_ACTIVE_COUNT, sizeof(uint32_t), false);
    const RDGBuffer shadeArgs = graph.CreateBuffer(RADIANCE_CACHE_SHADE_ARGS, RADIANCE_CACHE_SHADE_ARGS_BYTES, false);
    const RDGBuffer descriptors = graph.CreateBuffer(RADIANCE_CACHE_DESCRIPTORS, RADIANCE_CACHE_DESCRIPTORS_BYTES, false);
    const RDGBuffer stats = graph.CreateBuffer(RADIANCE_CACHE_STATS, sizeof(RadianceCacheStats), false);

    RenderPass& clearPass = graph.AddPass("Radiance Cache Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::RadianceCache);
    clearPass.AsyncCompute();
    clearPass.WriteTransferBuffer(entries);
    clearPass.WriteTransferBuffer(active);
    clearPass.WriteTransferBuffer(activeCount);
    clearPass.WriteTransferBuffer(shadeArgs);
    clearPass.WriteTransferBuffer(cells);
    clearPass.WriteTransferBuffer(stats);
    clearPass.Execute([entries, active, activeCount, shadeArgs, cells, stats](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(entries), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(active), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(activeCount), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(shadeArgs), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(cells), 0, VK_WHOLE_SIZE, RADIANCE_CACHE_RADIANCE_UNSHADED);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(stats), 0, VK_WHOLE_SIZE, 0);
    });

    const RDGBufferRing touchRing = graph.CreateVersionedBuffer(RADIANCE_CACHE_TOUCH_ENTRIES, RADIANCE_CACHE_ENTRIES_BYTES, Core::FRAME_BUFFER_COUNT, VersionSource::Fresh, 0, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const RDGBuffer touchEntries = touchRing.Version(Core::FRAME_BUFFER_COUNT);
    const bool bTouchValid = touchEntries.IsValid();

    const bool bHistoryValid = entriesRing.Version(1).IsValid() && keysRing.Version(1).IsValid() && cellsRing.Version(1).IsValid();
    if (bHistoryValid) {
        const RDGBuffer prevEntries = entriesRing.Version(1);
        const RDGBuffer prevKeys = keysRing.Version(1);
        const RDGBuffer prevCells = cellsRing.Version(1);
        RenderPass& carryPass = graph.AddPass("Radiance Cache Carry Forward"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::RadianceCache);
        carryPass.AsyncCompute();
        carryPass.ReadBuffer(prevEntries);
        carryPass.ReadBuffer(prevKeys);
        carryPass.ReadBuffer(prevCells);
        if (bTouchValid) { carryPass.ReadBuffer(touchEntries); }
        carryPass.ReadWriteBuffer(entries);
        carryPass.ReadWriteBuffer(keys);
        carryPass.ReadWriteBuffer(cells);
        carryPass.ReadWriteBuffer(stats);
        carryPass.Execute([pipelineManager, frameNumber, cameraPos, bFreeze, shadeInterval, bTouchValid, touchEntries, prevEntries, prevKeys, prevCells, entries, keys, cells, stats](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("radiance_cache_carry_forward"_sid);
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            RadianceCacheCarryForwardPushConstant pc{
                .prevEntries = graph.GetBufferAddress(prevEntries),
                .prevKeys = graph.GetBufferAddress(prevKeys),
                .prevCells = graph.GetBufferAddress(prevCells),
                .nextEntries = graph.GetBufferAddress(entries),
                .nextKeys = graph.GetBufferAddress(keys),
                .nextCells = graph.GetBufferAddress(cells),
                .cameraPos = glm::vec4(cameraPos, 0.0f),
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .bFreeze = bFreeze ? 1u : 0u,
                .stats = GPU_STATS_ENABLED ? graph.GetBufferAddress(stats) : 0,
                .touchEntries = bTouchValid ? graph.GetBufferAddress(touchEntries) : 0,
                .touchFrame = static_cast<uint32_t>(frameNumber - Core::FRAME_BUFFER_COUNT),
                .staleShadeAge = 4u * glm::max(shadeInterval, 1u) + 3u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            const uint32_t groups = (RADIANCE_CACHE_HASH_CAPACITY + 63u) / 64u;
            vkCmdDispatch(cmd, groups, 1, 1);
        });
    }

    const RDGBuffer touchCurrent = touchRing.Current();
    RenderPass& touchClear = graph.AddPass("Radiance Cache Touch Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::RadianceCache);
    touchClear.AsyncCompute();
    touchClear.WriteTransferBuffer(touchCurrent);
    touchClear.Execute([touchCurrent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(touchCurrent), 0, VK_WHOLE_SIZE, 0);
    });

    const RDGBuffer buffersCurrent = graph.CreateBuffer(RADIANCE_CACHE_BUFFERS_CURRENT, sizeof(RadianceCacheBuffers), false);
    RenderPass& bundlePass = graph.AddPass("Radiance Cache Buffers Upload"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::RadianceCache);
    bundlePass.AsyncCompute();
    bundlePass.WriteTransferBuffer(buffersCurrent);
    bundlePass.Execute([entries, keys, cells, active, descriptors, activeList, activeCount, stats, buffersCurrent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const RadianceCacheBuffers buffers{
            .entries = graph.PeekBufferAddress(entries),
            .keys = graph.PeekBufferAddress(keys),
            .cells = graph.PeekBufferAddress(cells),
            .active = graph.PeekBufferAddress(active),
            .descriptors = graph.PeekBufferAddress(descriptors),
            .activeList = graph.PeekBufferAddress(activeList),
            .activeCount = graph.PeekBufferAddress(activeCount),
            .stats = GPU_STATS_ENABLED ? graph.PeekBufferAddress(stats) : 0,
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(buffersCurrent), 0, sizeof(buffers), &buffers);
    });

    return RadianceCacheFrame{
        .entries = entries,
        .keys = keys,
        .cells = cells,
        .touchEntries = touchCurrent,
        .active = active,
        .activeList = activeList,
        .activeCount = activeCount,
        .descriptors = descriptors,
        .buffersCurrent = buffersCurrent,
        .shadeArgs = shadeArgs,
        .stats = stats,
    };
}

void SetupRadianceCacheShade(RenderGraph& graph, PipelineManager* pipelineManager, const SceneResources& scene, const WorldGridFrame& worldGrid, const DDGIFrame& ddgi, const RadianceCacheFrame& frame, uint32_t sceneIndex, bool bDDGIFeedbackValid, int32_t skyboxIndex, float iblIntensity, float maxRadiance, float bounceIntensity, uint32_t accumCap, uint32_t reflectionProbeCount, bool bReflectionProbeBruteForce)
{
    ZoneScoped;
    if (!frame.IsValid()) {
        return;
    }

    bounceIntensity = glm::clamp(bounceIntensity, 0.0f, 1.0f);
    accumCap = glm::clamp(accumCap, 1u, 255u);
    if (!scene.tlas.IsValid() || !scene.sceneData.IsValid() || !scene.lightData.IsValid() || !scene.instances.IsValid()
        || !scene.primitives.IsValid() || !scene.models.IsValid() || !scene.materials.IsValid()
        || !scene.indices.IsValid() || !scene.vertexAttributes.IsValid() || !scene.vertexPositions.IsValid()) {
        return;
    }
    const bool bFeedback = bDDGIFeedbackValid && ddgi.IsValid();
    const bool bWorldGrid = worldGrid.lightGrid.IsValid() && worldGrid.indexList.IsValid();
    const RDGBuffer cascades = bFeedback ? ddgi.cascades : RDGBuffer{};
    const RDGBuffer probeGrid = worldGrid.probeGrid;
    const RDGBuffer lightGrid = worldGrid.lightGrid;
    const RDGBuffer indexList = worldGrid.indexList;
    const RDGBuffer activeList = frame.activeList;
    const RDGBuffer activeCount = frame.activeCount;
    const RDGBuffer descriptors = frame.descriptors;
    const RDGBuffer cells = frame.cells;
    const RDGBuffer stats = frame.stats;
    const RDGBuffer shadeArgs = frame.shadeArgs;

    RenderPass& indirectPass = graph.AddPass("Radiance Cache Build Indirect"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::RadianceCache);
    indirectPass.AsyncCompute();
    indirectPass.ReadWriteBuffer(activeCount);
    indirectPass.WriteBuffer(shadeArgs);
    indirectPass.Execute([pipelineManager, activeCount, shadeArgs](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("radiance_cache_build_indirect"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        RadianceCacheBuildIndirectPushConstant pc{
            .activeCount = graph.GetBufferAddress(activeCount),
            .indirectArgs = graph.GetBufferAddress(shadeArgs),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });

    RenderPass& pass = graph.AddPass("Radiance Cache Shade"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::RadianceCache);
    pass.AsyncCompute();
    pass.ReadTLASBuffer(scene.tlas);
    pass.ReadBuffer(activeList);
    pass.ReadBuffer(activeCount);
    pass.ReadIndirectBuffer(shadeArgs);
    pass.ReadBuffer(descriptors);
    pass.ReadWriteBuffer(cells);
    pass.ReadWriteBuffer(stats);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.primitives);
    pass.ReadBuffer(scene.models);
    pass.ReadBuffer(scene.materials);
    pass.ReadBuffer(scene.indices);
    pass.ReadBuffer(scene.vertexAttributes);
    pass.ReadBuffer(scene.vertexPositions);
    pass.ReadBuffer(scene.reflectionProbes);
    if (probeGrid.IsValid()) { pass.ReadBuffer(probeGrid); }
    if (bWorldGrid) {
        pass.ReadBuffer(lightGrid);
        pass.ReadBuffer(indexList);
    }
    if (bFeedback) {
        AddDDGISampleDependencies(graph, pass, ddgi);
    }
    pass.Execute([pipelineManager, &scene, sceneIndex, bFeedback, bWorldGrid, skyboxIndex, iblIntensity, maxRadiance, bounceIntensity, accumCap, reflectionProbeCount, bReflectionProbeBruteForce, activeList, activeCount, descriptors,
            cells, stats, shadeArgs, cascades, probeGrid, lightGrid, indexList](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("radiance_cache_shade"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        RadianceCacheShadePushConstant pc{
            .activeList = graph.GetBufferAddress(activeList),
            .activeCount = graph.GetBufferAddress(activeCount),
            .descriptors = graph.GetBufferAddress(descriptors),
            .cells = graph.GetBufferAddress(cells),
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .previousCascades = bFeedback ? graph.GetBufferAddress(cascades) : 0,
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .indexBuffer = graph.GetBufferAddress(scene.indices),
            .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
            .vertexPosBuffer = graph.GetBufferAddress(scene.vertexPositions),
            .sceneDataIndex = sceneIndex,
            .bFeedbackValid = bFeedback ? 1u : 0u,
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .maxRadiance = maxRadiance,
            .bounceIntensity = bounceIntensity,
            .accumCap = accumCap,
            .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
            .worldGridProbeGrid = (!bReflectionProbeBruteForce && probeGrid.IsValid()) ? graph.GetBufferAddress(probeGrid) : 0,
            .stats = GPU_STATS_ENABLED ? graph.GetBufferAddress(stats) : 0,
            .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(lightGrid) : 0,
            .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(indexList) : 0,
            .reflectionProbeCount = reflectionProbeCount,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(shadeArgs), 0);
    });
}

void SetupRadianceCacheDebug(RenderGraph& graph, PipelineManager* pipelineManager, const GPUDebugFrame& gpuDebug, const RadianceCacheFrame& frame, float debugExposure, int32_t normalBucket)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!frame.IsValid() || !gpuDebug.cubeArgs.IsValid()) {
        return;
    }

    const RDGBuffer cubeArgs = gpuDebug.cubeArgs;
    const RDGBuffer cubeInstances = gpuDebug.cubeInstances;
    const RDGBuffer entries = frame.entries;
    const RDGBuffer keys = frame.keys;
    const RDGBuffer cells = frame.cells;

    RenderPass& pass = graph.AddPass("Radiance Cache Debug"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadWriteBuffer(cubeArgs);
    pass.WriteBuffer(cubeInstances);
    pass.ReadBuffer(entries);
    pass.ReadBuffer(keys);
    pass.ReadBuffer(cells);
    pass.Execute([pipelineManager, debugExposure, normalBucket, cubeArgs, cubeInstances, entries, keys, cells](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gpu_debug_radiance_cache"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        RadianceCacheDebugPushConstant pc{
            .cubeArgs = graph.GetBufferAddress(cubeArgs),
            .cubeBuffer = graph.GetBufferAddress(cubeInstances),
            .entries = graph.GetBufferAddress(entries),
            .keys = graph.GetBufferAddress(keys),
            .cells = graph.GetBufferAddress(cells),
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
