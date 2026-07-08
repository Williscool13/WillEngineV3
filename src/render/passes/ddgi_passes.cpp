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
static const StringID kDDGIIrradiance[DDGI_MAX_CASCADES] = {SID("ddgi_irradiance_0"), SID("ddgi_irradiance_1"), SID("ddgi_irradiance_2"), SID("ddgi_irradiance_3")};
static const StringID kDDGIIrradianceHistory[DDGI_MAX_CASCADES] = {SID("ddgi_irradiance_history_0"), SID("ddgi_irradiance_history_1"), SID("ddgi_irradiance_history_2"), SID("ddgi_irradiance_history_3")};
static const StringID kDDGIVisibility[DDGI_MAX_CASCADES] = {SID("ddgi_visibility_0"), SID("ddgi_visibility_1"), SID("ddgi_visibility_2"), SID("ddgi_visibility_3")};
static const StringID kDDGIVisibilityHistory[DDGI_MAX_CASCADES] = {SID("ddgi_visibility_history_0"), SID("ddgi_visibility_history_1"), SID("ddgi_visibility_history_2"), SID("ddgi_visibility_history_3")};
static const StringID kDDGIOffsets[DDGI_MAX_CASCADES] = {SID("ddgi_probe_offsets_0"), SID("ddgi_probe_offsets_1"), SID("ddgi_probe_offsets_2"), SID("ddgi_probe_offsets_3")};
static const StringID kDDGIOffsetsHistory[DDGI_MAX_CASCADES] = {SID("ddgi_probe_offsets_history_0"), SID("ddgi_probe_offsets_history_1"), SID("ddgi_probe_offsets_history_2"), SID("ddgi_probe_offsets_history_3")};
static const StringID kDDGIRestart[DDGI_MAX_CASCADES] = {SID("ddgi_probe_restart_0"), SID("ddgi_probe_restart_1"), SID("ddgi_probe_restart_2"), SID("ddgi_probe_restart_3")};
static const StringID kDDGIRestartHistory[DDGI_MAX_CASCADES] = {SID("ddgi_probe_restart_history_0"), SID("ddgi_probe_restart_history_1"), SID("ddgi_probe_restart_history_2"), SID("ddgi_probe_restart_history_3")};
static const StringID kDDGIRayData[DDGI_MAX_CASCADES] = {SID("ddgi_ray_data_0"), SID("ddgi_ray_data_1"), SID("ddgi_ray_data_2"), SID("ddgi_ray_data_3")};
static const StringID kDDGITracePass[DDGI_MAX_CASCADES] = {SID("DDGI Probe Trace 0"), SID("DDGI Probe Trace 1"), SID("DDGI Probe Trace 2"), SID("DDGI Probe Trace 3")};
static const StringID kDDGIBlendIrradiancePass[DDGI_MAX_CASCADES] = {SID("DDGI Blend Irradiance 0"), SID("DDGI Blend Irradiance 1"), SID("DDGI Blend Irradiance 2"), SID("DDGI Blend Irradiance 3")};
static const StringID kDDGIBlendVisibilityPass[DDGI_MAX_CASCADES] = {SID("DDGI Blend Visibility 0"), SID("DDGI Blend Visibility 1"), SID("DDGI Blend Visibility 2"), SID("DDGI Blend Visibility 3")};
static const StringID kDDGIRelocatePass[DDGI_MAX_CASCADES] = {SID("DDGI Probe Relocate 0"), SID("DDGI Probe Relocate 1"), SID("DDGI Probe Relocate 2"), SID("DDGI Probe Relocate 3")};
static const StringID kDDGIDebugPass[DDGI_MAX_CASCADES] = {SID("DDGI Probe Debug 0"), SID("DDGI Probe Debug 1"), SID("DDGI Probe Debug 2"), SID("DDGI Probe Debug 3")};

DDGICascades ComputeDDGICascades(const Core::DDGIParams& params, const glm::vec3& cameraPosition, const DDGICascades& previous, uint64_t frameNumber)
{
    const glm::ivec3 counts = glm::clamp(glm::ivec3(params.probeCountX, params.probeCountY, params.probeCountZ), glm::ivec3(2), glm::ivec3(32));
    const float baseSpacing = glm::max(params.probeSpacing, 0.1f);

    DDGICascades cascades{};
    cascades.count = glm::clamp(params.cascadeCount, 1u, DDGI_MAX_CASCADES);
    for (uint32_t k = 0; k < cascades.count; ++k) {
        cascades.bUpdated[k] = k == 0 || (cascades.count > 1 && k == 1 + static_cast<uint32_t>(frameNumber % (cascades.count - 1)));

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
    RenderPass& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_CLEAR_BIT, ResourceCategory::Lighting);
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

void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, Core::Arena& arena, const Core::DDGIParams& params, const DDGICascades& cascades, const DDGICascades& previous, int32_t skyboxIndex, uint64_t frameNumber, bool bBounceOnly)
{
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) {
        return;
    }
    if (cascades.count == 0) {
        return;
    }

    const uint32_t probeCountTotal = cascades.volumes[0].probeCount.x * cascades.volumes[0].probeCount.y * cascades.volumes[0].probeCount.z;

    bool bHistoryValid[DDGI_MAX_CASCADES]{};
    bool bOffsetsHistoryValid[DDGI_MAX_CASCADES]{};
    bool bRestartHistoryValid[DDGI_MAX_CASCADES]{};
    bool bAnyHistory = false;
    for (uint32_t k = 0; k < cascades.count; ++k) {
        const bool bSameWindow = k < previous.count && previous.volumes[k].probeCount == cascades.volumes[k].probeCount && previous.volumes[k].probeSpacing == cascades.volumes[k].probeSpacing;
        bHistoryValid[k] = bSameWindow && graph.HasTexture(kDDGIIrradianceHistory[k]) && graph.HasTexture(kDDGIVisibilityHistory[k]);
        bOffsetsHistoryValid[k] = bSameWindow && params.bRelocation && graph.HasBuffer(kDDGIOffsetsHistory[k]);
        bRestartHistoryValid[k] = bSameWindow && params.bRelocation && graph.HasBuffer(kDDGIRestartHistory[k]);
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
                .irradiance = kDDGIIrradianceHistory[k],
                .visibility = kDDGIVisibilityHistory[k],
                .offsets = bOffsetsHistoryValid[k] ? kDDGIOffsetsHistory[k] : StringID{},
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
                graph.CarryTextureToNextFrame(kDDGIIrradianceHistory[k], kDDGIIrradianceHistory[k], VK_IMAGE_USAGE_SAMPLED_BIT);
                graph.CarryTextureToNextFrame(kDDGIVisibilityHistory[k], kDDGIVisibilityHistory[k], VK_IMAGE_USAGE_SAMPLED_BIT);
            }
            if (bOffsetsHistoryValid[k]) {
                graph.CarryBufferToNextFrame(kDDGIOffsetsHistory[k], kDDGIOffsetsHistory[k], 0);
            }
            if (bRestartHistoryValid[k]) {
                graph.CarryBufferToNextFrame(kDDGIRestartHistory[k], kDDGIRestartHistory[k], 0);
            }
            continue;
        }

        const glm::vec4 rayRotation = DDGIRayRotation(frameNumber * DDGI_MAX_CASCADES + k);
        const glm::ivec3 previousBaseCell = k < previous.count ? previous.volumes[k].baseCell : volume.baseCell;
        const uint32_t raysPerProbe = glm::clamp(k == 0 ? params.raysPerProbe : params.outerRaysPerProbe, 16u, DDGI_MAX_RAYS_PER_PROBE);
        const bool bLocalNEE = k == 0 || params.bOuterLocalNEE;
        const float maxRayRadiance = glm::max(params.maxRayRadiance, 0.0f);

        graph.CreateBuffer(kDDGIRayData[k], static_cast<VkDeviceSize>(probeCountTotal) * raysPerProbe * sizeof(glm::vec4), false);

        RenderPass& tracePass = graph.AddPass(kDDGITracePass[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
        tracePass.ReadTLASBuffer(RT_TLAS_BUFFER);
        tracePass.ReadBuffer(LIGHT_DATA_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
        tracePass.WriteBuffer(kDDGIRayData[k]);
        if (bFeedback) {
            tracePass.ReadBuffer(DDGI_CASCADES_PREV_BUFFER);
            for (uint32_t j = 0; j < cascades.count; ++j) {
                if (bHistoryValid[j]) {
                    tracePass.ReadSampledImage(kDDGIIrradianceHistory[j]);
                    tracePass.ReadSampledImage(kDDGIVisibilityHistory[j]);
                }
                if (bOffsetsHistoryValid[j]) {
                    tracePass.ReadBuffer(kDDGIOffsetsHistory[j]);
                }
            }
        } else if (bOffsetsHistoryValid[k]) {
            tracePass.ReadBuffer(kDDGIOffsetsHistory[k]);
        }
        tracePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, skyboxIndex, raysPerProbe, probeCountTotal, bBounceOnly, bFeedback, bLocalNEE, maxRayRadiance, bOffsetsHistory = bOffsetsHistoryValid[k], offsetsHistoryId = kDDGIOffsetsHistory[k], rayDataId = kDDGIRayData[k], frameNumber](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
                .skyboxIndex = skyboxIndex,
                .raysPerProbe = raysPerProbe,
                .bBounceOnly = bBounceOnly ? 1u : 0u,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .bOffsetsValid = bOffsetsHistory ? 1u : 0u,
                .bLocalNEE = bLocalNEE ? 1u : 0u,
                .maxRayRadiance = maxRayRadiance,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (raysPerProbe + 63) / 64, probeCountTotal, 1);
        });

        const uint32_t atlasWidth = volume.probeCount.x * volume.probeCount.y * DDGI_IRRADIANCE_TILE;
        const uint32_t atlasHeight = volume.probeCount.z * DDGI_IRRADIANCE_TILE;
        graph.CreateTexture(kDDGIIrradiance[k], TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, atlasWidth, atlasHeight, 1}, {std::nullopt}, false);
        graph.CreateTexture(kDDGIVisibility[k], TextureInfo{VK_FORMAT_R16G16_SFLOAT, volume.probeCount.x * volume.probeCount.y * DDGI_VISIBILITY_TILE, volume.probeCount.z * DDGI_VISIBILITY_TILE, 1}, {std::nullopt}, false);

        RenderPass& blendPass = graph.AddPass(kDDGIBlendIrradiancePass[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
        blendPass.ReadBuffer(kDDGIRayData[k]);
        blendPass.WriteStorageImage(kDDGIIrradiance[k]);
        if (bHistoryValid[k]) {
            blendPass.ReadSampledImage(kDDGIIrradianceHistory[k]);
        }
        if (bRestartHistoryValid[k]) {
            blendPass.ReadBuffer(kDDGIRestartHistory[k]);
        }
        blendPass.Execute([pipelineManager, params, volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], raysPerProbe, probeCountTotal, rayDataId = kDDGIRayData[k], restartHistoryId = kDDGIRestartHistory[k], atlasId = kDDGIIrradiance[k], atlasHistoryId = kDDGIIrradianceHistory[k]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        RenderPass& visibilityPass = graph.AddPass(kDDGIBlendVisibilityPass[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
        visibilityPass.ReadBuffer(kDDGIRayData[k]);
        visibilityPass.WriteStorageImage(kDDGIVisibility[k]);
        if (bHistoryValid[k]) {
            visibilityPass.ReadSampledImage(kDDGIVisibilityHistory[k]);
        }
        if (bRestartHistoryValid[k]) {
            visibilityPass.ReadBuffer(kDDGIRestartHistory[k]);
        }
        visibilityPass.Execute([pipelineManager, params, volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], raysPerProbe, probeCountTotal, rayDataId = kDDGIRayData[k], restartHistoryId = kDDGIRestartHistory[k], atlasId = kDDGIVisibility[k], atlasHistoryId = kDDGIVisibilityHistory[k]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .hysteresis = glm::clamp(params.hysteresis, 0.0f, 0.995f),
                .distanceExponent = glm::max(params.distanceExponent, 1.0f),
                .bRestartValid = bRestartHistory ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        if (params.bRelocation) {
            // World-space standoff scales with the cascade like the biases, so coarse probes keep proportionate clearance.
            const float minFrontfaceDistance = glm::max(params.minFrontfaceDistance, 0.0f) * static_cast<float>(1u << k);
            graph.CreateBuffer(kDDGIOffsets[k], static_cast<VkDeviceSize>(probeCountTotal) * sizeof(glm::vec4), false);
            graph.CreateBuffer(kDDGIRestart[k], static_cast<VkDeviceSize>(probeCountTotal) * sizeof(uint32_t), false);

            RenderPass& relocatePass = graph.AddPass(kDDGIRelocatePass[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
            relocatePass.ReadBuffer(kDDGIRayData[k]);
            relocatePass.WriteBuffer(kDDGIOffsets[k]);
            relocatePass.WriteBuffer(kDDGIRestart[k]);
            if (bOffsetsHistoryValid[k]) {
                relocatePass.ReadBuffer(kDDGIOffsetsHistory[k]);
            }
            relocatePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, bOffsetsHistory = bOffsetsHistoryValid[k], raysPerProbe, probeCountTotal, minFrontfaceDistance, rayDataId = kDDGIRayData[k], offsetsHistoryId = kDDGIOffsetsHistory[k], offsetsId = kDDGIOffsets[k], restartId = kDDGIRestart[k]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (probeCountTotal + 63) / 64, 1, 1);
            });

            graph.CarryBufferToNextFrame(kDDGIOffsets[k], kDDGIOffsetsHistory[k], 0);
            graph.CarryBufferToNextFrame(kDDGIRestart[k], kDDGIRestartHistory[k], 0);
        }

        graph.CarryTextureToNextFrame(kDDGIIrradiance[k], kDDGIIrradianceHistory[k], VK_IMAGE_USAGE_SAMPLED_BIT);
        graph.CarryTextureToNextFrame(kDDGIVisibility[k], kDDGIVisibilityHistory[k], VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    // Current chain for lighting/remodulate: updated cascades read this frame's outputs, frozen cascades read their carried history.
    DDGICascadeDescSources* sources = arena.AllocArray<DDGICascadeDescSources>(1);
    *sources = DDGICascadeDescSources{};
    sources->count = cascades.count;
    for (uint32_t k = 0; k < cascades.count; ++k) {
        if (cascades.bUpdated[k]) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = kDDGIIrradiance[k],
                .visibility = kDDGIVisibility[k],
                .offsets = params.bRelocation ? kDDGIOffsets[k] : StringID{},
                .bValid = true,
            };
        } else if (bHistoryValid[k]) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = kDDGIIrradianceHistory[k],
                .visibility = kDDGIVisibilityHistory[k],
                .offsets = bOffsetsHistoryValid[k] ? kDDGIOffsetsHistory[k] : StringID{},
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
        if (graph.HasTexture(kDDGIIrradiance[k]) && graph.HasTexture(kDDGIVisibility[k])) {
            pass.ReadSampledImage(kDDGIIrradiance[k]);
            pass.ReadSampledImage(kDDGIVisibility[k]);
        }
        if (graph.HasTexture(kDDGIIrradianceHistory[k]) && graph.HasTexture(kDDGIVisibilityHistory[k])) {
            pass.ReadSampledImage(kDDGIIrradianceHistory[k]);
            pass.ReadSampledImage(kDDGIVisibilityHistory[k]);
        }
        if (graph.HasBuffer(kDDGIOffsets[k])) {
            pass.ReadBuffer(kDDGIOffsets[k]);
        }
        if (graph.HasBuffer(kDDGIOffsetsHistory[k])) {
            pass.ReadBuffer(kDDGIOffsetsHistory[k]);
        }
    }
    return true;
}

void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGICascades& cascades, float probeDebugExposure)
{
#ifdef WDEBUG
    if (!graph.HasBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER)) {
        return;
    }

    for (uint32_t k = 0; k < cascades.count; ++k) {
        const StringID atlasId = graph.HasTexture(kDDGIIrradiance[k]) ? kDDGIIrradiance[k] : kDDGIIrradianceHistory[k];
        if (!graph.HasTexture(atlasId)) {
            continue;
        }
        const StringID offsetsId = graph.HasBuffer(kDDGIOffsets[k]) ? kDDGIOffsets[k] : kDDGIOffsetsHistory[k];
        const bool bOffsets = graph.HasBuffer(offsetsId);
        const DDGIVolumeParams& volume = cascades.volumes[k];

        RenderPass& pass = graph.AddPass(kDDGIDebugPass[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Debug);
        pass.ReadWriteBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
        pass.WriteBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER);
        pass.ReadSampledImage(atlasId);
        if (bOffsets) {
            pass.ReadBuffer(offsetsId);
        }
        pass.Execute([pipelineManager, volume, bOffsets, probeDebugExposure, atlasId, offsetsId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (volume.probeCount.x + 3) / 4, (volume.probeCount.y + 3) / 4, (volume.probeCount.z + 3) / 4);
        });
    }
#endif
}
} // Render
