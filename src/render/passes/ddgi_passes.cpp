//
// Created by William on 2026-07-06.
//

#include "render/passes/ddgi_passes.h"

#include "render/render_utils.h"
#include "render/interface/render_interface.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
static const StringID DDGI_IRRADIANCE[DDGI_MAX_CASCADES] = {SID("ddgi_irradiance_0"), SID("ddgi_irradiance_1"), SID("ddgi_irradiance_2"), SID("ddgi_irradiance_3"), SID("ddgi_irradiance_4"), SID("ddgi_irradiance_5"), SID("ddgi_irradiance_6"), SID("ddgi_irradiance_7"), SID("ddgi_irradiance_8"), SID("ddgi_irradiance_9"), SID("ddgi_irradiance_10"), SID("ddgi_irradiance_11"), SID("ddgi_irradiance_12"), SID("ddgi_irradiance_13"), SID("ddgi_irradiance_14"), SID("ddgi_irradiance_15")};
static const StringID DDGI_IRRADIANCE_HISTORY[DDGI_MAX_CASCADES] = {SID("ddgi_irradiance_history_0"), SID("ddgi_irradiance_history_1"), SID("ddgi_irradiance_history_2"), SID("ddgi_irradiance_history_3"), SID("ddgi_irradiance_history_4"), SID("ddgi_irradiance_history_5"), SID("ddgi_irradiance_history_6"), SID("ddgi_irradiance_history_7"), SID("ddgi_irradiance_history_8"), SID("ddgi_irradiance_history_9"), SID("ddgi_irradiance_history_10"), SID("ddgi_irradiance_history_11"), SID("ddgi_irradiance_history_12"), SID("ddgi_irradiance_history_13"), SID("ddgi_irradiance_history_14"), SID("ddgi_irradiance_history_15")};
static const StringID DDGI_VISIBILITY[DDGI_MAX_CASCADES] = {SID("ddgi_visibility_0"), SID("ddgi_visibility_1"), SID("ddgi_visibility_2"), SID("ddgi_visibility_3"), SID("ddgi_visibility_4"), SID("ddgi_visibility_5"), SID("ddgi_visibility_6"), SID("ddgi_visibility_7"), SID("ddgi_visibility_8"), SID("ddgi_visibility_9"), SID("ddgi_visibility_10"), SID("ddgi_visibility_11"), SID("ddgi_visibility_12"), SID("ddgi_visibility_13"), SID("ddgi_visibility_14"), SID("ddgi_visibility_15")};
static const StringID DDGI_VISIBILITY_HISTORY[DDGI_MAX_CASCADES] = {SID("ddgi_visibility_history_0"), SID("ddgi_visibility_history_1"), SID("ddgi_visibility_history_2"), SID("ddgi_visibility_history_3"), SID("ddgi_visibility_history_4"), SID("ddgi_visibility_history_5"), SID("ddgi_visibility_history_6"), SID("ddgi_visibility_history_7"), SID("ddgi_visibility_history_8"), SID("ddgi_visibility_history_9"), SID("ddgi_visibility_history_10"), SID("ddgi_visibility_history_11"), SID("ddgi_visibility_history_12"), SID("ddgi_visibility_history_13"), SID("ddgi_visibility_history_14"), SID("ddgi_visibility_history_15")};
static const StringID DDGI_OFFSETS[DDGI_MAX_CASCADES] = {SID("ddgi_probe_offsets_0"), SID("ddgi_probe_offsets_1"), SID("ddgi_probe_offsets_2"), SID("ddgi_probe_offsets_3"), SID("ddgi_probe_offsets_4"), SID("ddgi_probe_offsets_5"), SID("ddgi_probe_offsets_6"), SID("ddgi_probe_offsets_7"), SID("ddgi_probe_offsets_8"), SID("ddgi_probe_offsets_9"), SID("ddgi_probe_offsets_10"), SID("ddgi_probe_offsets_11"), SID("ddgi_probe_offsets_12"), SID("ddgi_probe_offsets_13"), SID("ddgi_probe_offsets_14"), SID("ddgi_probe_offsets_15")};
static const StringID DDGI_OFFSETS_HISTORY[DDGI_MAX_CASCADES] = {SID("ddgi_probe_offsets_history_0"), SID("ddgi_probe_offsets_history_1"), SID("ddgi_probe_offsets_history_2"), SID("ddgi_probe_offsets_history_3"), SID("ddgi_probe_offsets_history_4"), SID("ddgi_probe_offsets_history_5"), SID("ddgi_probe_offsets_history_6"), SID("ddgi_probe_offsets_history_7"), SID("ddgi_probe_offsets_history_8"), SID("ddgi_probe_offsets_history_9"), SID("ddgi_probe_offsets_history_10"), SID("ddgi_probe_offsets_history_11"), SID("ddgi_probe_offsets_history_12"), SID("ddgi_probe_offsets_history_13"), SID("ddgi_probe_offsets_history_14"), SID("ddgi_probe_offsets_history_15")};
static const StringID DDGI_RESTART[DDGI_MAX_CASCADES] = {SID("ddgi_probe_restart_0"), SID("ddgi_probe_restart_1"), SID("ddgi_probe_restart_2"), SID("ddgi_probe_restart_3"), SID("ddgi_probe_restart_4"), SID("ddgi_probe_restart_5"), SID("ddgi_probe_restart_6"), SID("ddgi_probe_restart_7"), SID("ddgi_probe_restart_8"), SID("ddgi_probe_restart_9"), SID("ddgi_probe_restart_10"), SID("ddgi_probe_restart_11"), SID("ddgi_probe_restart_12"), SID("ddgi_probe_restart_13"), SID("ddgi_probe_restart_14"), SID("ddgi_probe_restart_15")};
static const StringID DDGI_RESTART_HISTORY[DDGI_MAX_CASCADES] = {SID("ddgi_probe_restart_history_0"), SID("ddgi_probe_restart_history_1"), SID("ddgi_probe_restart_history_2"), SID("ddgi_probe_restart_history_3"), SID("ddgi_probe_restart_history_4"), SID("ddgi_probe_restart_history_5"), SID("ddgi_probe_restart_history_6"), SID("ddgi_probe_restart_history_7"), SID("ddgi_probe_restart_history_8"), SID("ddgi_probe_restart_history_9"), SID("ddgi_probe_restart_history_10"), SID("ddgi_probe_restart_history_11"), SID("ddgi_probe_restart_history_12"), SID("ddgi_probe_restart_history_13"), SID("ddgi_probe_restart_history_14"), SID("ddgi_probe_restart_history_15")};
static const StringID DDGI_ACTIVE[DDGI_MAX_CASCADES] = {SID("ddgi_probe_active_0"), SID("ddgi_probe_active_1"), SID("ddgi_probe_active_2"), SID("ddgi_probe_active_3"), SID("ddgi_probe_active_4"), SID("ddgi_probe_active_5"), SID("ddgi_probe_active_6"), SID("ddgi_probe_active_7"), SID("ddgi_probe_active_8"), SID("ddgi_probe_active_9"), SID("ddgi_probe_active_10"), SID("ddgi_probe_active_11"), SID("ddgi_probe_active_12"), SID("ddgi_probe_active_13"), SID("ddgi_probe_active_14"), SID("ddgi_probe_active_15")};
static const StringID DDGI_ACTIVE_HISTORY[DDGI_MAX_CASCADES] = {SID("ddgi_probe_active_history_0"), SID("ddgi_probe_active_history_1"), SID("ddgi_probe_active_history_2"), SID("ddgi_probe_active_history_3"), SID("ddgi_probe_active_history_4"), SID("ddgi_probe_active_history_5"), SID("ddgi_probe_active_history_6"), SID("ddgi_probe_active_history_7"), SID("ddgi_probe_active_history_8"), SID("ddgi_probe_active_history_9"), SID("ddgi_probe_active_history_10"), SID("ddgi_probe_active_history_11"), SID("ddgi_probe_active_history_12"), SID("ddgi_probe_active_history_13"), SID("ddgi_probe_active_history_14"), SID("ddgi_probe_active_history_15")};
static const StringID DDGI_RAY_DATA[DDGI_MAX_CASCADES] = {SID("ddgi_ray_data_0"), SID("ddgi_ray_data_1"), SID("ddgi_ray_data_2"), SID("ddgi_ray_data_3"), SID("ddgi_ray_data_4"), SID("ddgi_ray_data_5"), SID("ddgi_ray_data_6"), SID("ddgi_ray_data_7"), SID("ddgi_ray_data_8"), SID("ddgi_ray_data_9"), SID("ddgi_ray_data_10"), SID("ddgi_ray_data_11"), SID("ddgi_ray_data_12"), SID("ddgi_ray_data_13"), SID("ddgi_ray_data_14"), SID("ddgi_ray_data_15")};
static const StringID DDGI_TRACE_PASS[DDGI_MAX_CASCADES] = {SID("DDGI Probe Trace 0"), SID("DDGI Probe Trace 1"), SID("DDGI Probe Trace 2"), SID("DDGI Probe Trace 3"), SID("DDGI Probe Trace 4"), SID("DDGI Probe Trace 5"), SID("DDGI Probe Trace 6"), SID("DDGI Probe Trace 7"), SID("DDGI Probe Trace 8"), SID("DDGI Probe Trace 9"), SID("DDGI Probe Trace 10"), SID("DDGI Probe Trace 11"), SID("DDGI Probe Trace 12"), SID("DDGI Probe Trace 13"), SID("DDGI Probe Trace 14"), SID("DDGI Probe Trace 15")};
static const StringID DDGI_BLEND_IRRADIANCE_PASS[DDGI_MAX_CASCADES] = {SID("DDGI Blend Irradiance 0"), SID("DDGI Blend Irradiance 1"), SID("DDGI Blend Irradiance 2"), SID("DDGI Blend Irradiance 3"), SID("DDGI Blend Irradiance 4"), SID("DDGI Blend Irradiance 5"), SID("DDGI Blend Irradiance 6"), SID("DDGI Blend Irradiance 7"), SID("DDGI Blend Irradiance 8"), SID("DDGI Blend Irradiance 9"), SID("DDGI Blend Irradiance 10"), SID("DDGI Blend Irradiance 11"), SID("DDGI Blend Irradiance 12"), SID("DDGI Blend Irradiance 13"), SID("DDGI Blend Irradiance 14"), SID("DDGI Blend Irradiance 15")};
static const StringID DDGI_BLEND_VISIBILITY_PASS[DDGI_MAX_CASCADES] = {SID("DDGI Blend Visibility 0"), SID("DDGI Blend Visibility 1"), SID("DDGI Blend Visibility 2"), SID("DDGI Blend Visibility 3"), SID("DDGI Blend Visibility 4"), SID("DDGI Blend Visibility 5"), SID("DDGI Blend Visibility 6"), SID("DDGI Blend Visibility 7"), SID("DDGI Blend Visibility 8"), SID("DDGI Blend Visibility 9"), SID("DDGI Blend Visibility 10"), SID("DDGI Blend Visibility 11"), SID("DDGI Blend Visibility 12"), SID("DDGI Blend Visibility 13"), SID("DDGI Blend Visibility 14"), SID("DDGI Blend Visibility 15")};
static const StringID DDGI_RELOCATE_PASS[DDGI_MAX_CASCADES] = {SID("DDGI Probe Relocate 0"), SID("DDGI Probe Relocate 1"), SID("DDGI Probe Relocate 2"), SID("DDGI Probe Relocate 3"), SID("DDGI Probe Relocate 4"), SID("DDGI Probe Relocate 5"), SID("DDGI Probe Relocate 6"), SID("DDGI Probe Relocate 7"), SID("DDGI Probe Relocate 8"), SID("DDGI Probe Relocate 9"), SID("DDGI Probe Relocate 10"), SID("DDGI Probe Relocate 11"), SID("DDGI Probe Relocate 12"), SID("DDGI Probe Relocate 13"), SID("DDGI Probe Relocate 14"), SID("DDGI Probe Relocate 15")};
static const StringID DDGI_DEBUG_PASS[DDGI_MAX_CASCADES] = {SID("DDGI Probe Debug 0"), SID("DDGI Probe Debug 1"), SID("DDGI Probe Debug 2"), SID("DDGI Probe Debug 3"), SID("DDGI Probe Debug 4"), SID("DDGI Probe Debug 5"), SID("DDGI Probe Debug 6"), SID("DDGI Probe Debug 7"), SID("DDGI Probe Debug 8"), SID("DDGI Probe Debug 9"), SID("DDGI Probe Debug 10"), SID("DDGI Probe Debug 11"), SID("DDGI Probe Debug 12"), SID("DDGI Probe Debug 13"), SID("DDGI Probe Debug 14"), SID("DDGI Probe Debug 15")};

DDGICascades ComputeDDGICascades(const Core::DDGIParams& params, const glm::vec3& cameraPosition, const DDGICascades& previous, uint64_t frameNumber)
{
    const glm::ivec3 counts = glm::clamp(glm::ivec3(params.probeCountX, params.probeCountY, params.probeCountZ), glm::ivec3(2), glm::ivec3(32));
    const float baseSpacing = glm::max(params.probeSpacing, 0.1f);

    DDGICascades cascades{};
    cascades.count = glm::clamp(params.cascadeCount, 1u, DDGI_MAX_CASCADES);

    const uint32_t updatedCascade = cascades.count == 1 || frameNumber % 2 == 0 ? 0u : 1u + static_cast<uint32_t>((frameNumber / 2) % (cascades.count - 1));
    for (uint32_t k = 0; k < cascades.count; ++k) {
        cascades.bUpdated[k] = k == updatedCascade;

        const float cascadeScale = static_cast<float>(1u << k);
        const float biasScale = params.bScaleBiasPerCascade ? cascadeScale : 1.0f;
        const float spacing = baseSpacing * cascadeScale;
        DDGIVolumeParams volume{};
        volume.probeCount = glm::uvec3(counts);
        volume.probeSpacing = glm::vec3(spacing);
        volume.normalBias = glm::max(params.normalBias, 0.0f) * biasScale;
        volume.viewBias = glm::max(params.viewBias, 0.0f) * biasScale;
        volume.irradianceGamma = glm::max(params.irradianceGamma, 1.0f);
        volume.edgeFadeCells = glm::clamp(params.edgeBlendCells, 1.0f, 8.0f);
        volume.baseCell = glm::ivec3(glm::floor(cameraPosition / spacing + 0.5f)) - counts / 2;

        if (!cascades.bUpdated[k] && k < previous.count && previous.volumes[k].probeCount == volume.probeCount && previous.volumes[k].probeSpacing == volume.probeSpacing) {
            volume.baseCell = previous.volumes[k].baseCell;
        }
        cascades.volumes[k] = volume;
    }
    return cascades;
}

/** Uniform random rotation (Shoemake) hashed from the frame number, so the ray set decorrelates across frames. */
static glm::vec4 DDGIRayRotation(uint64_t frameNumber)
{
    uint32_t state = static_cast<uint32_t>(frameNumber) * 747796405u + 2891336453u;
    float u[3];
    for (int i = 0; i < 3; ++i) {
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        word = (word >> 22u) ^ word;
        u[i] = static_cast<float>(word & 0x00FFFFFFu) / 16777216.0f;
    }
    const float s1 = std::sqrt(1.0f - u[0]);
    const float s2 = std::sqrt(u[0]);
    const float a = 6.28318530718f * u[1];
    const float b = 6.28318530718f * u[2];
    return {s1 * std::sin(a), s1 * std::cos(a), s2 * std::sin(b), s2 * std::cos(b)};
}

struct DDGICascadeDescSource
{
    DDGIVolumeParams volume{};
    StringID irradiance{};
    StringID visibility{};
    StringID offsets{};
    bool bValid{false};
};

struct DDGICascadeDescSources
{
    DDGICascadeDescSource entries[DDGI_MAX_CASCADES]{};
    uint32_t count{0};
};

/** Small vkCmdUpdateBuffer pass resolving the sources' descriptor indices/addresses at execute time into a DDGICascadeSetGPU. Sources must outlive execution (arena-allocated). */
static void AddDDGICascadeDescriptorUpload(RenderGraph& graph, StringID passName, StringID bufferId, const DDGICascadeDescSources* sources)
{
    graph.CreateBuffer(bufferId, sizeof(DDGICascadeSetGPU), false);
    RenderPass& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::DDGI);
    pass.WriteTransferBuffer(bufferId);
    pass.Execute([sources, bufferId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DDGICascadeSetGPU set{};
        set.cascadeCount = sources->count;
        for (uint32_t k = 0; k < sources->count; ++k) {
            const DDGICascadeDescSource& source = sources->entries[k];
            if (!source.bValid) {
                continue;
            }
            DDGICascadeDescriptor& desc = set.cascades[k];
            desc.volume = source.volume;
            desc.irradianceIndex = graph.GetSampledImageViewDescriptorIndex(source.irradiance);
            desc.visibilityIndex = graph.GetSampledImageViewDescriptorIndex(source.visibility);
            desc.probeOffsets = source.offsets != StringID{} ? graph.GetBufferAddress(source.offsets) : 0;
            desc.bOffsetsValid = source.offsets != StringID{} ? 1u : 0u;
            desc.bValid = 1u;
        }
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(bufferId), 0, sizeof(set), &set);
    });
}

void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, Core::Arena& arena, const Core::DDGIParams& params, const DDGICascades& cascades, const DDGICascades& previous, int32_t skyboxIndex, float iblIntensity, uint64_t frameNumber, bool bBounceOnly, const WorldCacheFrame& worldCache)
{
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) {
        return;
    }
    if (cascades.count == 0) {
        return;
    }

    const uint32_t probeCountTotal = cascades.volumes[0].probeCount.x * cascades.volumes[0].probeCount.y * cascades.volumes[0].probeCount.z;

    const bool bClassify = params.bClassification && params.bRelocation;

    bool bHistoryValid[DDGI_MAX_CASCADES]{};
    bool bOffsetsHistoryValid[DDGI_MAX_CASCADES]{};
    bool bRestartHistoryValid[DDGI_MAX_CASCADES]{};
    bool bActiveHistoryValid[DDGI_MAX_CASCADES]{};
    bool bAnyHistory = false;
    for (uint32_t k = 0; k < cascades.count; ++k) {
        const bool bSameWindow = k < previous.count && previous.volumes[k].probeCount == cascades.volumes[k].probeCount && previous.volumes[k].probeSpacing == cascades.volumes[k].probeSpacing
            && previous.volumes[k].irradianceGamma == cascades.volumes[k].irradianceGamma;
        bHistoryValid[k] = bSameWindow && graph.HasTexture(DDGI_IRRADIANCE_HISTORY[k]) && graph.HasTexture(DDGI_VISIBILITY_HISTORY[k]);
        bOffsetsHistoryValid[k] = bSameWindow && params.bRelocation && graph.HasBuffer(DDGI_OFFSETS_HISTORY[k]);
        bRestartHistoryValid[k] = bSameWindow && params.bRelocation && graph.HasBuffer(DDGI_RESTART_HISTORY[k]);
        bActiveHistoryValid[k] = bSameWindow && bClassify && graph.HasBuffer(DDGI_ACTIVE_HISTORY[k]);
        bAnyHistory |= bHistoryValid[k];
    }
    const bool bFeedback = bAnyHistory && params.bInfiniteBounce && !bBounceOnly;

    if (bFeedback) {
        DDGICascadeDescSources* prevSources = arena.AllocArray<DDGICascadeDescSources>(1);
        *prevSources = DDGICascadeDescSources{};
        prevSources->count = cascades.count;
        for (uint32_t k = 0; k < cascades.count; ++k) {
            if (!bHistoryValid[k]) {
                continue;
            }
            prevSources->entries[k] = DDGICascadeDescSource{
                .volume = previous.volumes[k],
                .irradiance = DDGI_IRRADIANCE_HISTORY[k],
                .visibility = DDGI_VISIBILITY_HISTORY[k],
                .offsets = bOffsetsHistoryValid[k] ? DDGI_OFFSETS_HISTORY[k] : StringID{},
                .bValid = true,
            };
        }
        AddDDGICascadeDescriptorUpload(graph, SID("DDGI Prev Cascade Descriptors"), DDGI_CASCADES_PREV_BUFFER, prevSources);
    }

    for (uint32_t k = 0; k < cascades.count; ++k) {
        const DDGIVolumeParams& volume = cascades.volumes[k];

        if (!cascades.bUpdated[k]) {
            // Re-carry so the frozen cascade's resources survive the skip; mismatched history is dropped and the cascade restarts on its next turn.
            if (bHistoryValid[k]) {
                graph.CarryTextureToNextFrame(DDGI_IRRADIANCE_HISTORY[k], DDGI_IRRADIANCE_HISTORY[k], VK_IMAGE_USAGE_SAMPLED_BIT);
                graph.CarryTextureToNextFrame(DDGI_VISIBILITY_HISTORY[k], DDGI_VISIBILITY_HISTORY[k], VK_IMAGE_USAGE_SAMPLED_BIT);
            }
            if (bOffsetsHistoryValid[k]) {
                graph.CarryBufferToNextFrame(DDGI_OFFSETS_HISTORY[k], DDGI_OFFSETS_HISTORY[k], 0);
            }
            if (bRestartHistoryValid[k]) {
                graph.CarryBufferToNextFrame(DDGI_RESTART_HISTORY[k], DDGI_RESTART_HISTORY[k], 0);
            }
            if (bActiveHistoryValid[k]) {
                graph.CarryBufferToNextFrame(DDGI_ACTIVE_HISTORY[k], DDGI_ACTIVE_HISTORY[k], 0);
            }
            continue;
        }

        const glm::vec4 rayRotation = DDGIRayRotation(frameNumber * DDGI_MAX_CASCADES + k);
        const glm::ivec3 previousBaseCell = k < previous.count ? previous.volumes[k].baseCell : volume.baseCell;
        const uint32_t raysPerProbe = glm::clamp(k == 0 ? params.raysPerProbe : params.outerRaysPerProbe, 16u, DDGI_MAX_RAYS_PER_PROBE);
        const bool bLocalNEE = k == 0 || params.bOuterLocalNEE;
        const float maxRayRadiance = glm::max(params.maxRayRadiance, 0.0f);
        const float bounceIntensity = glm::clamp(params.bounceIntensity, 0.0f, 1.0f);
        const bool bWorldGrid = graph.HasBuffer(SID("world_grid_light_grid")) && graph.HasBuffer(SID("world_grid_index_list"));

        graph.CreateBuffer(DDGI_RAY_DATA[k], static_cast<VkDeviceSize>(probeCountTotal) * raysPerProbe * sizeof(glm::vec4), false);

        RenderPass& tracePass = graph.AddPass(DDGI_TRACE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        tracePass.ReadTLASBuffer(RT_TLAS_BUFFER);
        tracePass.ReadBuffer(LIGHT_DATA_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
        tracePass.WriteBuffer(DDGI_RAY_DATA[k]);
        tracePass.ReadBuffer(SCENE_DATA_BUFFER);
        if (bWorldGrid) {
            tracePass.ReadBuffer(SID("world_grid_light_grid"));
            tracePass.ReadBuffer(SID("world_grid_index_list"));
        }
        if (worldCache.bValid) {
            tracePass.ReadBuffer(WORLD_CACHE_BUFFERS_CURRENT);
            tracePass.ReadWriteBuffer(WORLD_CACHE_ENTRIES);
            tracePass.ReadWriteBuffer(WORLD_CACHE_KEYS);
            tracePass.ReadWriteBuffer(WORLD_CACHE_CELLS);
            tracePass.ReadWriteBuffer(WORLD_CACHE_ACTIVE);
            tracePass.ReadWriteBuffer(WORLD_CACHE_ACTIVE_LIST);
            tracePass.ReadWriteBuffer(WORLD_CACHE_ACTIVE_COUNT);
            tracePass.ReadWriteBuffer(WORLD_CACHE_DESCRIPTORS);
            tracePass.ReadWriteBuffer(WORLD_CACHE_STATS);
        }
        if (bFeedback) {
            tracePass.ReadBuffer(DDGI_CASCADES_PREV_BUFFER);
            for (uint32_t j = 0; j < cascades.count; ++j) {
                if (bHistoryValid[j]) {
                    tracePass.ReadSampledImage(DDGI_IRRADIANCE_HISTORY[j]);
                    tracePass.ReadSampledImage(DDGI_VISIBILITY_HISTORY[j]);
                }
                if (bOffsetsHistoryValid[j]) {
                    tracePass.ReadBuffer(DDGI_OFFSETS_HISTORY[j]);
                }
            }
        } else if (bOffsetsHistoryValid[k]) {
            tracePass.ReadBuffer(DDGI_OFFSETS_HISTORY[k]);
        }
        if (bActiveHistoryValid[k]) {
            tracePass.ReadBuffer(DDGI_ACTIVE_HISTORY[k]);
        }
        tracePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, skyboxIndex, iblIntensity, raysPerProbe, probeCountTotal, bBounceOnly, bFeedback, bLocalNEE, maxRayRadiance, bounceIntensity, bWorldGrid, bOffsetsHistory = bOffsetsHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], offsetsHistoryId = DDGI_OFFSETS_HISTORY[k], activeHistoryId = DDGI_ACTIVE_HISTORY[k], rayDataId = DDGI_RAY_DATA[k], frameNumber, bWorldCache = worldCache.bValid](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_trace"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeTracePushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCell = previousBaseCell,
                .bFeedbackValid = bFeedback ? 1u : 0u,
                .rayData = graph.GetBufferAddress(rayDataId),
                .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
                .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
                .probeOffsets = bOffsetsHistory ? graph.GetBufferAddress(offsetsHistoryId) : 0,
                .previousCascades = bFeedback ? graph.GetBufferAddress(DDGI_CASCADES_PREV_BUFFER) : 0,
                .worldCache = bWorldCache ? graph.GetBufferAddress(WORLD_CACHE_BUFFERS_CURRENT) : 0,
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_light_grid")) : 0,
                .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_index_list")) : 0,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(activeHistoryId) : 0,
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
                .skyboxIndex = skyboxIndex,
                .raysPerProbe = raysPerProbe,
                .bBounceOnly = bBounceOnly ? 1u : 0u,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .bLocalNEE = bLocalNEE ? 1u : 0u,
                .maxRayRadiance = maxRayRadiance,
                .bounceIntensity = bounceIntensity,
                .iblIntensity = iblIntensity,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (raysPerProbe + 63) / 64, probeCountTotal, 1);
        });

        const uint32_t atlasWidth = volume.probeCount.x * volume.probeCount.y * DDGI_IRRADIANCE_TILE;
        const uint32_t atlasHeight = volume.probeCount.z * DDGI_IRRADIANCE_TILE;
        graph.CreateTexture(DDGI_IRRADIANCE[k], TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, atlasWidth, atlasHeight, 1}, {std::nullopt}, false);
        graph.CreateTexture(DDGI_VISIBILITY[k], TextureInfo{VK_FORMAT_R16G16_SFLOAT, volume.probeCount.x * volume.probeCount.y * DDGI_VISIBILITY_TILE, volume.probeCount.z * DDGI_VISIBILITY_TILE, 1}, {std::nullopt}, false);

        RenderPass& blendPass = graph.AddPass(DDGI_BLEND_IRRADIANCE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        blendPass.ReadBuffer(DDGI_RAY_DATA[k]);
        blendPass.WriteStorageImage(DDGI_IRRADIANCE[k]);
        if (bHistoryValid[k]) {
            blendPass.ReadSampledImage(DDGI_IRRADIANCE_HISTORY[k]);
        }
        if (bRestartHistoryValid[k]) {
            blendPass.ReadBuffer(DDGI_RESTART_HISTORY[k]);
        }
        if (bActiveHistoryValid[k]) {
            blendPass.ReadBuffer(DDGI_ACTIVE_HISTORY[k]);
        }
        blendPass.Execute([pipelineManager, params, volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], raysPerProbe, probeCountTotal, rayDataId = DDGI_RAY_DATA[k], restartHistoryId = DDGI_RESTART_HISTORY[k], activeHistoryId = DDGI_ACTIVE_HISTORY[k], atlasId = DDGI_IRRADIANCE[k], atlasHistoryId = DDGI_IRRADIANCE_HISTORY[k]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_blend_irradiance"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeBlendPushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCell = previousBaseCell,
                .bHistoryValid = bHistory ? 1u : 0u,
                .rayData = graph.GetBufferAddress(rayDataId),
                .probeRestart = bRestartHistory ? graph.GetBufferAddress(restartHistoryId) : 0,
                .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(atlasId),
                .atlasHistoryIndex = bHistory ? graph.GetSampledImageViewDescriptorIndex(atlasHistoryId) : 0u,
                .raysPerProbe = raysPerProbe,
                .hysteresis = glm::clamp(params.hysteresis, 0.0f, 0.995f),
                .irradianceThreshold = params.irradianceThreshold,
                .brightnessThreshold = params.brightnessThreshold,
                .bRestartValid = bRestartHistory ? 1u : 0u,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(activeHistoryId) : 0,
                .bActiveValid = bActiveHistory ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        RenderPass& visibilityPass = graph.AddPass(DDGI_BLEND_VISIBILITY_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        visibilityPass.ReadBuffer(DDGI_RAY_DATA[k]);
        visibilityPass.WriteStorageImage(DDGI_VISIBILITY[k]);
        if (bHistoryValid[k]) {
            visibilityPass.ReadSampledImage(DDGI_VISIBILITY_HISTORY[k]);
        }
        if (bRestartHistoryValid[k]) {
            visibilityPass.ReadBuffer(DDGI_RESTART_HISTORY[k]);
        }
        if (bActiveHistoryValid[k]) {
            visibilityPass.ReadBuffer(DDGI_ACTIVE_HISTORY[k]);
        }
        visibilityPass.Execute([pipelineManager, params, volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], raysPerProbe, probeCountTotal, rayDataId = DDGI_RAY_DATA[k], restartHistoryId = DDGI_RESTART_HISTORY[k], activeHistoryId = DDGI_ACTIVE_HISTORY[k], atlasId = DDGI_VISIBILITY[k], atlasHistoryId = DDGI_VISIBILITY_HISTORY[k]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_blend_visibility"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeBlendPushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCell = previousBaseCell,
                .bHistoryValid = bHistory ? 1u : 0u,
                .rayData = graph.GetBufferAddress(rayDataId),
                .probeRestart = bRestartHistory ? graph.GetBufferAddress(restartHistoryId) : 0,
                .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(atlasId),
                .atlasHistoryIndex = bHistory ? graph.GetSampledImageViewDescriptorIndex(atlasHistoryId) : 0u,
                .raysPerProbe = raysPerProbe,
                .hysteresis = glm::clamp(params.visibilityHysteresis, 0.0f, 0.995f),
                .distanceExponent = glm::max(params.distanceExponent, 1.0f),
                .bRestartValid = bRestartHistory ? 1u : 0u,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(activeHistoryId) : 0,
                .bActiveValid = bActiveHistory ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        if (params.bRelocation) {
            // World-space standoff scales with the cascade like the biases, so coarse probes keep proportionate clearance.
            const float minFrontfaceDistance = glm::max(params.minFrontfaceDistance, 0.0f) * static_cast<float>(1u << k);
            graph.CreateBuffer(DDGI_OFFSETS[k], static_cast<VkDeviceSize>(probeCountTotal) * sizeof(glm::vec4), false);
            graph.CreateBuffer(DDGI_RESTART[k], static_cast<VkDeviceSize>(probeCountTotal) * sizeof(uint32_t), false);

            RenderPass& relocatePass = graph.AddPass(DDGI_RELOCATE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
            relocatePass.ReadBuffer(DDGI_RAY_DATA[k]);
            relocatePass.WriteBuffer(DDGI_OFFSETS[k]);
            relocatePass.WriteBuffer(DDGI_RESTART[k]);
            if (bOffsetsHistoryValid[k]) {
                relocatePass.ReadBuffer(DDGI_OFFSETS_HISTORY[k]);
            }
            if (bClassify) {
                graph.CreateBuffer(DDGI_ACTIVE[k], static_cast<VkDeviceSize>(probeCountTotal) * sizeof(uint32_t), false);
                relocatePass.WriteBuffer(DDGI_ACTIVE[k]);
            }
            if (bActiveHistoryValid[k]) {
                relocatePass.ReadBuffer(DDGI_ACTIVE_HISTORY[k]);
            }
            relocatePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, bOffsetsHistory = bOffsetsHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], bClassify, raysPerProbe, probeCountTotal, minFrontfaceDistance, rayDataId = DDGI_RAY_DATA[k], offsetsHistoryId = DDGI_OFFSETS_HISTORY[k], offsetsId = DDGI_OFFSETS[k], restartId = DDGI_RESTART[k], activeHistoryId = DDGI_ACTIVE_HISTORY[k], activeId = DDGI_ACTIVE[k]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_relocate"));
                if (!pipelineEntry) {
                    return;
                }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                DDGIProbeRelocatePushConstant pc{
                    .volume = volume,
                    .rayRotation = rayRotation,
                    .previousBaseCell = previousBaseCell,
                    .bOffsetsValid = bOffsetsHistory ? 1u : 0u,
                    .rayData = graph.GetBufferAddress(rayDataId),
                    .offsetsIn = bOffsetsHistory ? graph.GetBufferAddress(offsetsHistoryId) : 0,
                    .offsetsOut = graph.GetBufferAddress(offsetsId),
                    .restartOut = graph.GetBufferAddress(restartId),
                    .raysPerProbe = raysPerProbe,
                    .minFrontfaceDistance = minFrontfaceDistance,
                    .activeIn = bActiveHistory ? graph.GetBufferAddress(activeHistoryId) : 0,
                    .activeOut = bClassify ? graph.GetBufferAddress(activeId) : 0,
                    .bActiveValid = bActiveHistory ? 1u : 0u,
                    .bClassify = bClassify ? 1u : 0u,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (probeCountTotal + 63) / 64, 1, 1);
            });

            graph.CarryBufferToNextFrame(DDGI_OFFSETS[k], DDGI_OFFSETS_HISTORY[k], 0);
            graph.CarryBufferToNextFrame(DDGI_RESTART[k], DDGI_RESTART_HISTORY[k], 0);
            if (bClassify) {
                graph.CarryBufferToNextFrame(DDGI_ACTIVE[k], DDGI_ACTIVE_HISTORY[k], 0);
            }
        }

        graph.CarryTextureToNextFrame(DDGI_IRRADIANCE[k], DDGI_IRRADIANCE_HISTORY[k], VK_IMAGE_USAGE_SAMPLED_BIT);
        graph.CarryTextureToNextFrame(DDGI_VISIBILITY[k], DDGI_VISIBILITY_HISTORY[k], VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    // Current chain for lighting/remodulate: updated cascades read this frame's outputs, frozen cascades read their carried history.
    DDGICascadeDescSources* sources = arena.AllocArray<DDGICascadeDescSources>(1);
    *sources = DDGICascadeDescSources{};
    sources->count = cascades.count;
    for (uint32_t k = 0; k < cascades.count; ++k) {
        if (cascades.bUpdated[k]) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = DDGI_IRRADIANCE[k],
                .visibility = DDGI_VISIBILITY[k],
                .offsets = params.bRelocation ? DDGI_OFFSETS[k] : StringID{},
                .bValid = true,
            };
        } else if (bHistoryValid[k]) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = DDGI_IRRADIANCE_HISTORY[k],
                .visibility = DDGI_VISIBILITY_HISTORY[k],
                .offsets = bOffsetsHistoryValid[k] ? DDGI_OFFSETS_HISTORY[k] : StringID{},
                .bValid = true,
            };
        }
    }
    AddDDGICascadeDescriptorUpload(graph, SID("DDGI Cascade Descriptors"), DDGI_CASCADES_BUFFER, sources);
}

bool AddDDGISampleDependencies(RenderGraph& graph, RenderPass& pass)
{
    if (!graph.HasBuffer(DDGI_CASCADES_BUFFER)) {
        return false;
    }
    pass.ReadBuffer(DDGI_CASCADES_BUFFER);

    for (uint32_t k = 0; k < DDGI_MAX_CASCADES; ++k) {
        if (graph.HasTexture(DDGI_IRRADIANCE[k]) && graph.HasTexture(DDGI_VISIBILITY[k])) {
            pass.ReadSampledImage(DDGI_IRRADIANCE[k]);
            pass.ReadSampledImage(DDGI_VISIBILITY[k]);
        }
        if (graph.HasTexture(DDGI_IRRADIANCE_HISTORY[k]) && graph.HasTexture(DDGI_VISIBILITY_HISTORY[k])) {
            pass.ReadSampledImage(DDGI_IRRADIANCE_HISTORY[k]);
            pass.ReadSampledImage(DDGI_VISIBILITY_HISTORY[k]);
        }
        if (graph.HasBuffer(DDGI_OFFSETS[k])) {
            pass.ReadBuffer(DDGI_OFFSETS[k]);
        }
        if (graph.HasBuffer(DDGI_OFFSETS_HISTORY[k])) {
            pass.ReadBuffer(DDGI_OFFSETS_HISTORY[k]);
        }
    }
    return true;
}

// Cascade identification tints for the all-cascades debug view (unorm RGBA, low byte = red): white, warm red, green, blue.
static const uint32_t DDGI_CASCADE_TINT[DDGI_MAX_CASCADES] = {
    0xFFFFFFFFu, 0xFF8C8CFFu, 0xFF8CFF8Cu, 0xFFFFB399u,
    0xFF99FFFFu, 0xFFFF99FFu, 0xFFFFFF99u, 0xFFB399FFu,
    0xFF99B3FFu, 0xFFB3FF99u, 0xFF99FFB3u, 0xFFFF99B3u,
    0xFFCCCCCCu, 0xFFCC99FFu, 0xFF99CCFFu, 0xFFFFCC99u,
};

void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGICascades& cascades, float probeDebugExposure, int32_t debugCascade, bool bHideInactive)
{
#ifdef WDEBUG
    if (!graph.HasBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER)) {
        return;
    }

    for (uint32_t k = 0; k < cascades.count; ++k) {
        if (debugCascade >= 0 && k != static_cast<uint32_t>(debugCascade)) {
            continue;
        }
        const StringID atlasId = graph.HasTexture(DDGI_IRRADIANCE[k]) ? DDGI_IRRADIANCE[k] : DDGI_IRRADIANCE_HISTORY[k];
        if (!graph.HasTexture(atlasId)) {
            continue;
        }
        const StringID offsetsId = graph.HasBuffer(DDGI_OFFSETS[k]) ? DDGI_OFFSETS[k] : DDGI_OFFSETS_HISTORY[k];
        const bool bOffsets = graph.HasBuffer(offsetsId);
        const StringID activeId = graph.HasBuffer(DDGI_ACTIVE[k]) ? DDGI_ACTIVE[k] : DDGI_ACTIVE_HISTORY[k];
        const bool bActive = graph.HasBuffer(activeId);
        const DDGIVolumeParams& volume = cascades.volumes[k];

        RenderPass& pass = graph.AddPass(DDGI_DEBUG_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
        pass.ReadWriteBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
        pass.WriteBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER);
        pass.ReadSampledImage(atlasId);
        if (bOffsets) {
            pass.ReadBuffer(offsetsId);
        }
        if (bActive) {
            pass.ReadBuffer(activeId);
        }
        const uint32_t packedTint = debugCascade < 0 ? DDGI_CASCADE_TINT[k] : 0xFFFFFFFFu;
        pass.Execute([pipelineManager, volume, bOffsets, bActive, bHideInactive, probeDebugExposure, packedTint, atlasId, offsetsId, activeId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_debug"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeDebugPushConstant pc{
                .volume = volume,
                .sphereArgs = graph.GetBufferAddress(GPU_DEBUG_SPHERE_ARGS_BUFFER),
                .sphereBuffer = graph.GetBufferAddress(GPU_DEBUG_SPHERE_INSTANCE_BUFFER),
                .probeOffsets = bOffsets ? graph.GetBufferAddress(offsetsId) : 0,
                .irradianceAtlasIndex = graph.GetSampledImageViewDescriptorIndex(atlasId),
                .bOffsetsValid = bOffsets ? 1u : 0u,
                .probeDebugExposure = probeDebugExposure,
                .packedTint = packedTint,
                .probeActive = bActive ? graph.GetBufferAddress(activeId) : 0,
                .bActiveValid = bActive ? 1u : 0u,
                .bHideInactive = bHideInactive ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (volume.probeCount.x + 3) / 4, (volume.probeCount.y + 3) / 4, (volume.probeCount.z + 3) / 4);
        });
    }
#endif
}
} // Render
