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
DDGIVolumeParams ComputeDDGIVolumeParams(const Core::DDGIParams& params, const glm::vec3& cameraPosition)
{
    const glm::ivec3 counts = glm::clamp(glm::ivec3(params.probeCountX, params.probeCountY, params.probeCountZ), glm::ivec3(2), glm::ivec3(32));
    const float spacing = glm::max(params.probeSpacing, 0.1f);
    const glm::ivec3 centerCell = glm::ivec3(glm::floor(cameraPosition / spacing + 0.5f));

    DDGIVolumeParams volume{};
    volume.baseCell = centerCell - counts / 2;
    volume.probeCount = glm::uvec3(counts);
    volume.probeSpacing = glm::vec3(spacing);
    volume.normalBias = glm::max(params.normalBias, 0.0f);
    volume.viewBias = glm::max(params.viewBias, 0.0f);
    volume.irradianceGamma = glm::max(params.irradianceGamma, 1.0f);
    return volume;
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

void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, const Core::DDGIParams& params, const DDGIVolumeParams& volume, const DDGIVolumeParams& previousVolume, int32_t skyboxIndex, uint64_t frameNumber, bool bBounceOnly)
{
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) {
        return;
    }

    const uint32_t probeCountTotal = volume.probeCount.x * volume.probeCount.y * volume.probeCount.z;
    const uint32_t raysPerProbe = glm::clamp(params.raysPerProbe, 16u, DDGI_MAX_RAYS_PER_PROBE);
    const glm::vec4 rayRotation = DDGIRayRotation(frameNumber);

    const bool bHistoryValid = graph.HasTexture(SID("ddgi_irradiance_history"))
        && graph.HasTexture(SID("ddgi_visibility_history"))
        && previousVolume.probeCount == volume.probeCount
        && previousVolume.probeSpacing == volume.probeSpacing;
    const bool bFeedback = bHistoryValid && params.bInfiniteBounce && !bBounceOnly;
    const bool bOffsetsHistoryValid = params.bRelocation
        && graph.HasBuffer(SID("ddgi_probe_offsets_history"))
        && previousVolume.probeCount == volume.probeCount
        && previousVolume.probeSpacing == volume.probeSpacing;

    graph.CreateBuffer(SID("ddgi_ray_data"), static_cast<VkDeviceSize>(probeCountTotal) * raysPerProbe * sizeof(glm::vec4), false);

    RenderPass& tracePass = graph.AddPass(SID("DDGI Probe Trace"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
    tracePass.ReadTLASBuffer(RT_TLAS_BUFFER);
    tracePass.ReadBuffer(LIGHT_DATA_BUFFER);
    tracePass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    tracePass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    tracePass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    tracePass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    tracePass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    tracePass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    tracePass.WriteBuffer(SID("ddgi_ray_data"));
    if (bFeedback) {
        tracePass.ReadSampledImage(SID("ddgi_irradiance_history"));
        tracePass.ReadSampledImage(SID("ddgi_visibility_history"));
    }
    if (bOffsetsHistoryValid) {
        tracePass.ReadBuffer(SID("ddgi_probe_offsets_history"));
    }
    tracePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell = previousVolume.baseCell, skyboxIndex, raysPerProbe, probeCountTotal, bBounceOnly, bFeedback, bOffsetsHistoryValid, frameNumber](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
            .rayData = graph.GetBufferAddress(SID("ddgi_ray_data")),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .probeOffsets = bOffsetsHistoryValid ? graph.GetBufferAddress(SID("ddgi_probe_offsets_history")) : 0,
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .skyboxIndex = skyboxIndex,
            .raysPerProbe = raysPerProbe,
            .bBounceOnly = bBounceOnly ? 1u : 0u,
            .irradianceHistoryIndex = bFeedback ? graph.GetSampledImageViewDescriptorIndex(SID("ddgi_irradiance_history")) : ~0x0u,
            .visibilityHistoryIndex = bFeedback ? graph.GetSampledImageViewDescriptorIndex(SID("ddgi_visibility_history")) : ~0x0u,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .bOffsetsValid = bOffsetsHistoryValid ? 1u : 0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (raysPerProbe + 63) / 64, probeCountTotal, 1);
    });

    const uint32_t atlasWidth = volume.probeCount.x * volume.probeCount.y * DDGI_IRRADIANCE_TILE;
    const uint32_t atlasHeight = volume.probeCount.z * DDGI_IRRADIANCE_TILE;
    graph.CreateTexture(SID("ddgi_irradiance"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, atlasWidth, atlasHeight, 1}, {std::nullopt}, false);
    graph.CreateTexture(SID("ddgi_visibility"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, volume.probeCount.x * volume.probeCount.y * DDGI_VISIBILITY_TILE, volume.probeCount.z * DDGI_VISIBILITY_TILE, 1}, {std::nullopt}, false);

    RenderPass& blendPass = graph.AddPass(SID("DDGI Blend Irradiance"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
    blendPass.ReadBuffer(SID("ddgi_ray_data"));
    blendPass.WriteStorageImage(SID("ddgi_irradiance"));
    if (bHistoryValid) {
        blendPass.ReadSampledImage(SID("ddgi_irradiance_history"));
    }
    blendPass.Execute([pipelineManager, params, volume, rayRotation, previousBaseCell = previousVolume.baseCell, bHistoryValid, raysPerProbe, probeCountTotal](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_blend_irradiance"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        DDGIProbeBlendPushConstant pc{
            .volume = volume,
            .rayRotation = rayRotation,
            .previousBaseCell = previousBaseCell,
            .bHistoryValid = bHistoryValid ? 1u : 0u,
            .rayData = graph.GetBufferAddress(SID("ddgi_ray_data")),
            .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("ddgi_irradiance")),
            .atlasHistoryIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(SID("ddgi_irradiance_history")) : 0u,
            .raysPerProbe = raysPerProbe,
            .hysteresis = glm::clamp(params.hysteresis, 0.0f, 0.995f),
            .irradianceThreshold = params.irradianceThreshold,
            .brightnessThreshold = params.brightnessThreshold,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, probeCountTotal, 1, 1);
    });

    RenderPass& visibilityPass = graph.AddPass(SID("DDGI Blend Visibility"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
    visibilityPass.ReadBuffer(SID("ddgi_ray_data"));
    visibilityPass.WriteStorageImage(SID("ddgi_visibility"));
    if (bHistoryValid) {
        visibilityPass.ReadSampledImage(SID("ddgi_visibility_history"));
    }
    visibilityPass.Execute([pipelineManager, params, volume, rayRotation, previousBaseCell = previousVolume.baseCell, bHistoryValid, raysPerProbe, probeCountTotal](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_blend_visibility"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        DDGIProbeBlendPushConstant pc{
            .volume = volume,
            .rayRotation = rayRotation,
            .previousBaseCell = previousBaseCell,
            .bHistoryValid = bHistoryValid ? 1u : 0u,
            .rayData = graph.GetBufferAddress(SID("ddgi_ray_data")),
            .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("ddgi_visibility")),
            .atlasHistoryIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(SID("ddgi_visibility_history")) : 0u,
            .raysPerProbe = raysPerProbe,
            .hysteresis = glm::clamp(params.hysteresis, 0.0f, 0.995f),
            .distanceExponent = glm::max(params.distanceExponent, 1.0f),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, probeCountTotal, 1, 1);
    });

    if (params.bRelocation) {
        const uint32_t relocationIterations = glm::clamp(static_cast<uint32_t>(params.relocationIterations), 1u, 8u);
        const VkDeviceSize offsetsBufferSize = static_cast<VkDeviceSize>(probeCountTotal) * sizeof(glm::vec4);
        graph.CreateBuffer(SID("ddgi_probe_offsets"), offsetsBufferSize, false);

        StringID scratch[2] = {};
        if (relocationIterations > 1u) {
            scratch[0] = SID("ddgi_probe_offsets_scratch0");
            graph.CreateBuffer(scratch[0], offsetsBufferSize, false);
            if (relocationIterations > 2u) {
                scratch[1] = SID("ddgi_probe_offsets_scratch1");
                graph.CreateBuffer(scratch[1], offsetsBufferSize, false);
            }
        }

        StringID previousOutput{};
        for (uint32_t iter = 0; iter < relocationIterations; ++iter) {
            const bool bFirst = iter == 0u;
            const bool bLast = iter == relocationIterations - 1u;
            const StringID outputName = bLast ? SID("ddgi_probe_offsets") : scratch[iter & 1u];
            const bool bInputValid = bFirst ? bOffsetsHistoryValid : true;
            const StringID inputName = bFirst ? SID("ddgi_probe_offsets_history") : previousOutput;
            const glm::ivec3 iterPreviousBaseCell = bFirst ? previousVolume.baseCell : volume.baseCell;

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("DDGI Probe Relocate %u", iter);
            const StringID passId = relocationIterations > 1u ? StringID(passName.c_str(), passName.Size()) : SID("DDGI Probe Relocate");

            RenderPass& relocatePass = graph.AddPass(passId, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Lighting);
            relocatePass.ReadBuffer(SID("ddgi_ray_data"));
            relocatePass.WriteBuffer(outputName);
            if (bInputValid) {
                relocatePass.ReadBuffer(inputName);
            }
            relocatePass.Execute([pipelineManager, params, volume, rayRotation, iterPreviousBaseCell, bInputValid, inputName, outputName, raysPerProbe, probeCountTotal](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_relocate"));
                if (!pipelineEntry) {
                    return;
                }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                DDGIProbeRelocatePushConstant pc{
                    .volume = volume,
                    .rayRotation = rayRotation,
                    .previousBaseCell = iterPreviousBaseCell,
                    .bOffsetsValid = bInputValid ? 1u : 0u,
                    .rayData = graph.GetBufferAddress(SID("ddgi_ray_data")),
                    .offsetsIn = bInputValid ? graph.GetBufferAddress(inputName) : 0,
                    .offsetsOut = graph.GetBufferAddress(outputName),
                    .raysPerProbe = raysPerProbe,
                    .minFrontfaceDistance = glm::max(params.minFrontfaceDistance, 0.0f),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (probeCountTotal + 63) / 64, 1, 1);
            });

            previousOutput = outputName;
        }

        graph.CarryBufferToNextFrame(SID("ddgi_probe_offsets"), SID("ddgi_probe_offsets_history"), 0);
    }

    graph.CarryTextureToNextFrame(SID("ddgi_irradiance"), SID("ddgi_irradiance_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("ddgi_visibility"), SID("ddgi_visibility_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
}

void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGIVolumeParams& volume, float probeDebugExposure)
{
#ifdef WDEBUG
    if (!graph.HasBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER) || !graph.HasTexture(SID("ddgi_irradiance"))) {
        return;
    }

    const bool bOffsets = graph.HasBuffer(SID("ddgi_probe_offsets"));

    RenderPass& pass = graph.AddPass(SID("DDGI Probe Debug"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Debug);
    pass.ReadWriteBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
    pass.WriteBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER);
    pass.ReadSampledImage(SID("ddgi_irradiance"));
    if (bOffsets) {
        pass.ReadBuffer(SID("ddgi_probe_offsets"));
    }
    pass.Execute([pipelineManager, volume, bOffsets, probeDebugExposure](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_debug"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        DDGIProbeDebugPushConstant pc{
            .volume = volume,
            .sphereArgs = graph.GetBufferAddress(GPU_DEBUG_SPHERE_ARGS_BUFFER),
            .sphereBuffer = graph.GetBufferAddress(GPU_DEBUG_SPHERE_INSTANCE_BUFFER),
            .probeOffsets = bOffsets ? graph.GetBufferAddress(SID("ddgi_probe_offsets")) : 0,
            .irradianceAtlasIndex = graph.GetSampledImageViewDescriptorIndex(SID("ddgi_irradiance")),
            .bOffsetsValid = bOffsets ? 1u : 0u,
            .probeDebugExposure = probeDebugExposure,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (volume.probeCount.x + 3) / 4, (volume.probeCount.y + 3) / 4, (volume.probeCount.z + 3) / 4);
    });
#endif
}
} // Render
