//
// Created by William on 2026-06-06.
//

#include "render/passes/restir_passes.h"

#include <tracy/Tracy.hpp>

#include "ddgi_passes.h"
#include "final_gather_passes.h"
#include "reflection_passes.h"
#include "shadow_passes.h"
#include "render/render_config.h"
#include "render/render_utils.h"
#include "core/math/math_helpers.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"
#include "render/shaders/restir_features_macros.h"

namespace Render
{
void SetupReSTIRPasses(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex,
                       Core::Arena& arena,
                       uint64_t frameNumber,
                       const Core::ReSTIRParams& restirParams,
                       uint32_t activeCheckerboardField,
                       const Core::ReflectionConfiguration& reflectionConfig,
                       bool bResetHistory,
                       bool bSkipReflectionPiggyback)
{
    ZoneScoped;
    const uint32_t pixelCount = renderExtent[0] * renderExtent[1];
    const uint32_t reservoirBufferSize = pixelCount * static_cast<uint32_t>(sizeof(Reservoir));
    const float reflectionRoughnessMax = bSkipReflectionPiggyback ? -1.0f : ComputeReflectionRoughnessMax(reflectionConfig);
    const uint32_t reflectionBufferSize = pixelCount * static_cast<uint32_t>(sizeof(ReflectionHitDescriptor));

    const bool bHasTLAS = graph.HasBuffer(RT_TLAS_BUFFER);
    // tlasIndex (and the carried prev-TLAS index) resolve inside the Execute lambdas: the AS resource's physical + RT descriptor are assigned during Compile, after pass setup.
    // Temporal reuse pass; off => spatial/shading read the base reservoir directly.
    const bool bTemporalReuse = restirParams.bEnableTemporal;
    // RELAX moving-shadow confidence
    const bool bConfidence = bTemporalReuse && RESTIR_ENABLE_CONFIDENCE && restirParams.bEnableConfidence && restirParams.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX;
    // restir_shadow_vis is antilag-only
    const bool bAntilag = bTemporalReuse && RESTIR_ENABLE_ANTILAG && restirParams.bEnableAntilag;
    const bool bShadowVis = bAntilag;

    const bool bReGIRProposal = restirParams.lightProposal == Core::ReSTIRParams::LightProposal::ReGIR;
    const bool bWorldGrid = graph.HasBuffer("world_grid_light_grid"_sid) && graph.HasBuffer("world_grid_index_list"_sid);

    // Temporal-gradient confidence runs at 1/GRAD_FACTOR of the half-res ReSTIR grid (must match GRAD_FACTOR in the confidence shaders).
    const uint32_t GRAD_FACTOR = 3u;
    const Core::Array<uint32_t, 2> gradientExtent = {(renderExtent[0] + GRAD_FACTOR - 1u) / GRAD_FACTOR, (renderExtent[1] + GRAD_FACTOR - 1u) / GRAD_FACTOR};

    // Transform all lights (area + sphere) to view space once; every ReSTIR pass and the resolve read this instead of transforming per pixel.
    graph.CreateBuffer("restir_lights_vs"_sid, MAX_LIGHTS * sizeof(LightVSData), false);

    const uint32_t liveLightCount = viewFamily.analyticLightCount + viewFamily.triLightCount;

    RenderPass& transformPass = graph.AddPass("[ReSTIR DI] Transform Lights"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    transformPass.ReadBuffer(SCENE_DATA_BUFFER);
    transformPass.ReadBuffer("light_data"_sid);
    transformPass.WriteBuffer("restir_lights_vs"_sid);
    transformPass.Execute([&, pipelineManager, sceneIndex, liveLightCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_di_transform_lights"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRTransformLightsPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress("light_data"_sid),
            .lightVS = graph.GetBufferAddress("restir_lights_vs"_sid),
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (liveLightCount + 63u) / 64u, 1, 1);
    });

    if (bReGIRProposal)
    {
        const uint32_t fullW = renderExtent[0];
        const uint32_t fullH = renderExtent[1];
        const uint32_t entriesSize = REGIR_HASH_CAPACITY * static_cast<uint32_t>(sizeof(uint32_t));
        const uint32_t reservoirsSize = REGIR_HASH_CAPACITY * REGIR_RESERVOIRS_PER_CELL * static_cast<uint32_t>(sizeof(ReGIRReservoir));
        const uint32_t activeCellsSize = REGIR_HASH_CAPACITY * 4u * static_cast<uint32_t>(sizeof(int32_t));

        graph.CreateVersionedBuffer("regir_hash_entries"_sid, entriesSize, 1, VersionSource::Fresh);
        graph.CreateVersionedBuffer("regir_hash_reservoirs"_sid, reservoirsSize, 1, VersionSource::Fresh);
        graph.CreateBuffer("regir_cell_data"_sid, REGIR_HASH_CAPACITY * 2u * static_cast<uint32_t>(sizeof(float)), false);
        graph.CreateBuffer("regir_active_cells"_sid, activeCellsSize, false);
        graph.CreateBuffer("regir_active_count"_sid, sizeof(uint32_t), false);
        graph.CreateBuffer("regir_fill_indirect"_sid, 3u * static_cast<uint32_t>(sizeof(uint32_t)), false);
        graph.CreateBuffer("regir_tiles"_sid, REGIR_TILE_BUFFER_SIZE, false);

        const bool bHasPrev = !restirParams.bResetReGIR && graph.ResourceHasVersion("regir_hash_entries"_sid, 1) && graph.ResourceHasVersion("regir_hash_reservoirs"_sid, 1);
        const StringID prevEntries = graph.ResourceVersionID("regir_hash_entries"_sid, 1);
        const StringID prevReservoirs = graph.ResourceVersionID("regir_hash_reservoirs"_sid, 1);

        RenderPass& clearPass = graph.AddPass("[ReGIR] Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::ReGIR);
        clearPass.WriteTransferBuffer("regir_hash_entries"_sid);
        clearPass.WriteTransferBuffer("regir_active_count"_sid);
        clearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("regir_hash_entries"_sid), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("regir_active_count"_sid), 0, VK_WHOLE_SIZE, 0);
        });

        RenderPass& touchPass = graph.AddPass("[ReGIR] Touch"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        touchPass.ReadBuffer(SCENE_DATA_BUFFER);
        touchPass.ReadSampledImage(targets.depthCopy);
        touchPass.WriteBuffer("regir_hash_entries"_sid);
        touchPass.WriteBuffer("regir_active_cells"_sid);
        touchPass.WriteBuffer("regir_active_count"_sid);
        touchPass.Execute([&, pipelineManager, sceneIndex, fullW, fullH, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("regir_touch"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRTouchPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .hashEntries = graph.GetBufferAddress("regir_hash_entries"_sid),
                .activeCells = graph.GetBufferAddress("regir_active_cells"_sid),
                .activeCount = graph.GetBufferAddress("regir_active_count"_sid),
                .renderExtent = {fullW, fullH},
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .sceneDataIndex = sceneIndex,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (fullW + 7) / 8, (fullH + 7) / 8, 1);
        });

        RenderPass& indirectPass = graph.AddPass("[ReGIR] Build Indirect"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        indirectPass.ReadBuffer("regir_active_count"_sid);
        indirectPass.WriteBuffer("regir_fill_indirect"_sid);
        indirectPass.Execute([pipelineManager](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("regir_build_indirect"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRBuildIndirectPushConstant pc{
                .activeCount = graph.GetBufferAddress("regir_active_count"_sid),
                .indirectArgs = graph.GetBufferAddress("regir_fill_indirect"_sid),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });

        const uint32_t analyticLightCount = viewFamily.analyticLightCount;
        const uint32_t fillLightCount = liveLightCount;

        graph.CreateBuffer("light_power_cdf"_sid, MAX_LIGHTS * sizeof(float), false);

        RenderPass& cdfPass = graph.AddPass("[ReGIR] Light Power CDF"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        cdfPass.ReadBuffer("light_data"_sid);
        cdfPass.WriteBuffer("light_power_cdf"_sid);
        cdfPass.Execute([pipelineManager, fillLightCount, analyticLightCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("light_power_cdf"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            LightPowerCDFPushConstant pc{
                .lightData = graph.GetBufferAddress("light_data"_sid),
                .cdf = graph.GetBufferAddress("light_power_cdf"_sid),
                .liveCount = fillLightCount,
                .analyticCount = static_cast<int32_t>(analyticLightCount),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });

        // Presample tiles
        {
            RenderPass& presamplePass = graph.AddPass("[ReGIR] Presample Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
            presamplePass.ReadBuffer("light_power_cdf"_sid);
            presamplePass.WriteBuffer("regir_tiles"_sid);
            presamplePass.Execute([pipelineManager, frameNumber, fillLightCount, analyticLightCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("regir_presample_tiles"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                ReGIRPresampleTilesPushConstant pc{
                    .cdf = graph.GetBufferAddress("light_power_cdf"_sid),
                    .tiles = graph.GetBufferAddress("regir_tiles"_sid),
                    .liveCount = fillLightCount,
                    .analyticCount = static_cast<int32_t>(analyticLightCount),
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t totalSlots = REGIR_TILE_COUNT * REGIR_TILE_SIZE;
                vkCmdDispatch(cmd, (totalSlots + 255u) / 256u, 1, 1);
            });
        }

        RenderPass& regirFillPass = graph.AddPass("[ReGIR] Fill"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        regirFillPass.ReadBuffer(SCENE_DATA_BUFFER);
        regirFillPass.ReadBuffer("light_data"_sid);
        regirFillPass.ReadBuffer("restir_lights_vs"_sid);
        regirFillPass.ReadBuffer("regir_active_cells"_sid);
        regirFillPass.ReadBuffer("regir_active_count"_sid);
        regirFillPass.ReadBuffer("regir_tiles"_sid);
        if (bHasPrev) {
            regirFillPass.ReadBuffer(prevEntries);
            regirFillPass.ReadBuffer(prevReservoirs);
        }
        regirFillPass.ReadIndirectBuffer("regir_fill_indirect"_sid);
        regirFillPass.WriteBuffer("regir_hash_reservoirs"_sid);
        regirFillPass.WriteBuffer("regir_cell_data"_sid);
        regirFillPass.Execute([&, pipelineManager, sceneIndex, frameNumber, bHasPrev](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("regir_fill"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRFillPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress("light_data"_sid),
                .lightVS = graph.GetBufferAddress("restir_lights_vs"_sid),
                .activeCells = graph.GetBufferAddress("regir_active_cells"_sid),
                .activeCount = graph.GetBufferAddress("regir_active_count"_sid),
                .reservoirs = graph.GetBufferAddress("regir_hash_reservoirs"_sid),
                .cellData = graph.GetBufferAddress("regir_cell_data"_sid),
                .hashEntriesPrev = bHasPrev ? graph.GetBufferAddress(prevEntries) : 0,
                .reservoirsPrev = bHasPrev ? graph.GetBufferAddress(prevReservoirs) : 0,
                .tiles = graph.GetBufferAddress("regir_tiles"_sid),
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .bHasPrev = bHasPrev ? 1u : 0u,
                .wClamp = restirParams.regirWClamp,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatchIndirect(cmd, graph.GetBufferHandle("regir_fill_indirect"_sid), 0);
        });

    }

    {
        if (bTemporalReuse) {
            graph.CreateBuffer("restir_reservoir_temporal"_sid, reservoirBufferSize, true);
            graph.CreateVersionedBuffer("restir_reservoir_history"_sid, reservoirBufferSize, 1, VersionSource::Emplaced);
        }

        const bool bHasHistory = graph.ResourceHasVersion("restir_reservoir_history"_sid, 1) && !bResetHistory;
        const StringID reservoirHistory = bHasHistory ? graph.ResourceVersionID("restir_reservoir_history"_sid, 1) : StringID{};
        const StringID gbufferOneHistory = bHasHistory ? graph.ResourceVersionID(targets.gbufferOne, 1) : StringID{};
        const StringID depthHistory = bHasHistory ? graph.ResourceVersionID(targets.depthCopy, 1) : StringID{};
        if (bShadowVis) {
            graph.CreateVersionedTexture("restir_shadow_vis"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
        }
        const bool bHasPrevVis = bShadowVis && graph.ResourceHasVersion("restir_shadow_vis"_sid, 1);
        const StringID prevShadowVis = bHasPrevVis ? graph.ResourceVersionID("restir_shadow_vis"_sid, 1) : StringID{};
        if (bConfidence) {
            graph.CreateVersionedTexture("restir_confidence"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
            graph.CreateTexture("restir_signal"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
            graph.CreateTexture("restir_gradient"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, gradientExtent[0], gradientExtent[1], 1}, {std::nullopt}, true);
        }

        // Last frame's TLAS lets the temporal pass re-shade the winner against last frame's occluder positions; the TLAS ring is one frame deep by construction (BLAS lifetime).
        const bool bHasPrevTlas = bConfidence && graph.ResourceHasVersion(RT_TLAS_BUFFER, 1);
        const StringID prevTlas = bHasPrevTlas ? graph.ResourceVersionID(RT_TLAS_BUFFER, 1) : StringID{};

        graph.CreateBuffer("restir_reservoir_base"_sid, reservoirBufferSize, true);
        if (reflectionRoughnessMax >= 0.0f) {
            graph.CreateBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER, reflectionBufferSize, true);

            RenderPass& reflClearPass = graph.AddPass("[Reflection] Clear Descriptors"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::ReflectionsDenoise);
            reflClearPass.WriteTransferBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER);
            reflClearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(REFLECTION_HIT_DESCRIPTORS_BUFFER), 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
            });
        }

        // Base candidate generation
        RenderPass& basePass = graph.AddPass("[ReSTIR DI] Base"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        basePass.ReadBuffer(SCENE_DATA_BUFFER);
        basePass.ReadBuffer("light_data"_sid);
        basePass.ReadBuffer("restir_lights_vs"_sid);
        if (bReGIRProposal) {
            basePass.ReadBuffer("regir_hash_entries"_sid);
            basePass.ReadBuffer("regir_hash_reservoirs"_sid);
            basePass.ReadBuffer("regir_cell_data"_sid);
        } else if (bWorldGrid) {
            basePass.ReadBuffer("world_grid_light_grid"_sid);
            basePass.ReadBuffer("world_grid_index_list"_sid);
            basePass.ReadBuffer("world_grid_emissive_grid"_sid);
            basePass.ReadBuffer("world_grid_emissive_index_list"_sid);
            basePass.ReadBuffer("world_grid_cell_power"_sid);
        }
        basePass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        basePass.ReadSampledImage(targets.gbufferOne);
        basePass.ReadSampledImage(targets.gbufferTwo);
        basePass.ReadSampledImage(targets.depthCopy);
        if (bHasTLAS) { basePass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        basePass.WriteBuffer("restir_reservoir_base"_sid);
        if (reflectionRoughnessMax >= 0.0f) { basePass.WriteBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER); }
        basePass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, reflectionRoughnessMax, bReGIRProposal, bWorldGrid, field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(bReGIRProposal ? "restir_di_base_regir"_sid : "restir_di_base_bin"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;
            const bool bBin = !bReGIRProposal && bWorldGrid;

            ReSTIRDICombinedTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress("light_data"_sid),
                .lightVS = graph.GetBufferAddress("restir_lights_vs"_sid),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .hashEntries = bReGIRProposal ? graph.GetBufferAddress("regir_hash_entries"_sid) : 0,
                .reservoirs = bReGIRProposal ? graph.GetBufferAddress("regir_hash_reservoirs"_sid) : 0,
                .cellData = bReGIRProposal ? graph.GetBufferAddress("regir_cell_data"_sid) : 0,
                .historyBuffer = 0,
                .genBuffer = 0,
                .outputBuffer = graph.GetBufferAddress("restir_reservoir_base"_sid),
                .reflectionDescriptors = reflectionRoughnessMax >= 0.0f ? graph.GetBufferAddress(REFLECTION_HIT_DESCRIPTORS_BUFFER) : 0,
                .worldGridBuffer = bBin ? graph.GetBufferAddress("world_grid_light_grid"_sid) : 0,
                .worldGridIndexList = bBin ? graph.GetBufferAddress("world_grid_index_list"_sid) : 0,
                .worldGridEmissiveGrid = bBin ? graph.GetBufferAddress("world_grid_emissive_grid"_sid) : 0,
                .worldGridEmissiveIndexList = bBin ? graph.GetBufferAddress("world_grid_emissive_index_list"_sid) : 0,
                .worldGridCellPower = bBin ? graph.GetBufferAddress("world_grid_cell_power"_sid) : 0,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .prevGbufferOneIndex = ~0u,
                .prevDepthIndex = ~0u,
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .mCap = 0u,
                .tlasIndex = tlasIndex,
                .prevTlasIndex = ~0u,
                .prevShadowVisIndex = ~0u,
                .shadowVisIndex = ~0u,
                .signalIndex = ~0u,
                .bPermutationSampling = 0u,
                .antilagStrength = 0.0f,
                .bInitialVisibility = (tlasIndex != ~0u && restirParams.bInitialVisibility) ? 1u : 0u,
                .activeCheckerboardField = field,
                .reflectionRoughnessMax = reflectionRoughnessMax,
                .brdfRoughnessMax = reflectionConfig.tracedRoughnessMax,
                .lightSpecularFromReflectionsMax = reflectionConfig.lightSpecularFromReflectionsMax,
                .mirrorRoughnessMax = reflectionConfig.mirrorRoughnessMax,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t strideX = (field != 0u) ? ((renderExtent[0] + 1u) >> 1u) : renderExtent[0];
            const uint32_t groupsX = (strideX + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

        if (bTemporalReuse) {
            // Temporal reuse
            RenderPass& temporalPass = graph.AddPass("[ReSTIR DI] Temporal"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
            temporalPass.ReadBuffer(SCENE_DATA_BUFFER);
            temporalPass.ReadBuffer("light_data"_sid);
            temporalPass.ReadBuffer("restir_lights_vs"_sid);
            temporalPass.ReadBuffer("restir_reservoir_base"_sid);
            if (bHasHistory) { temporalPass.ReadBuffer(reservoirHistory); }
            temporalPass.ReadSampledImage(targets.gbufferOne);
            temporalPass.ReadSampledImage(targets.gbufferTwo);
            temporalPass.ReadSampledImage(targets.depthCopy);
            if (bHasHistory) { temporalPass.ReadSampledImage(gbufferOneHistory); }
            if (bHasHistory) { temporalPass.ReadSampledImage(depthHistory); }
            if (bHasPrevVis) { temporalPass.ReadSampledImage(prevShadowVis); }
            if (bHasTLAS) { temporalPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
            if (bHasPrevTlas) { temporalPass.ReadTLASBuffer(prevTlas); }
            temporalPass.WriteBuffer("restir_reservoir_temporal"_sid);
            if (bShadowVis) { temporalPass.WriteStorageImage("restir_shadow_vis"_sid); }
            if (bConfidence) { temporalPass.WriteStorageImage("restir_signal"_sid); }
            temporalPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, bHasPrevTlas, bHasHistory, bConfidence, bShadowVis, bHasPrevVis, prevShadowVis, reservoirHistory, gbufferOneHistory, depthHistory, field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_di_temporal"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;
                const uint32_t prevTlasIndex = bHasPrevTlas ? graph.GetAccelerationStructureDescriptorIndex(prevTlas) : ~0u;

                ReSTIRDICombinedTemporalPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress("light_data"_sid),
                    .lightVS = graph.GetBufferAddress("restir_lights_vs"_sid),
                    .instanceBuffer = 0,
                    .hashEntries = 0,
                    .reservoirs = 0,
                    .cellData = 0,
                    .historyBuffer = bHasHistory ? graph.GetBufferAddress(reservoirHistory) : 0,
                    .genBuffer = graph.GetBufferAddress("restir_reservoir_base"_sid),
                    .outputBuffer = graph.GetBufferAddress("restir_reservoir_temporal"_sid),
                    .reflectionDescriptors = 0,
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .prevGbufferOneIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0u,
                    .prevDepthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0u,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .sceneDataIndex = sceneIndex,
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .mCap = restirParams.temporalMCap,
                    .tlasIndex = tlasIndex,
                    .prevTlasIndex = prevTlasIndex,
                    .prevShadowVisIndex = bHasPrevVis ? graph.GetSampledImageViewDescriptorIndex(prevShadowVis) : ~0u,
                    .shadowVisIndex = bShadowVis ? graph.GetStorageImageViewDescriptorIndex("restir_shadow_vis"_sid) : ~0u,
                    .signalIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex("restir_signal"_sid) : ~0u,
                    .bPermutationSampling = restirParams.bPermutationSampling ? 1u : 0u,
                    .antilagStrength = restirParams.antilagStrength,
                    .bInitialVisibility = (tlasIndex != ~0u && restirParams.bInitialVisibility) ? 1u : 0u,
                    .bTemporalSearch = restirParams.bTemporalSearch ? 1u : 0u,
                    .activeCheckerboardField = field,
                    .finalWClamp = (restirParams.spatialPasses == 0u) ? restirParams.restirWClamp : 0.0f,
                    .lightSpecularFromReflectionsMax = reflectionConfig.lightSpecularFromReflectionsMax,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t strideX = (field != 0u) ? ((renderExtent[0] + 1u) >> 1u) : renderExtent[0];
                const uint32_t groupsX = (strideX + 15) / 16;
                const uint32_t groupsY = (renderExtent[1] + 15) / 16;
                vkCmdDispatch(cmd, groupsX, groupsY, 1);
            });
        }
    }

    if (restirParams.bSunLight && viewFamily.directionalLight.bEnabled && bHasTLAS) {
        const uint32_t sunCheckerboardField = (activeCheckerboardField != 0u && restirParams.bCheckerboardFullRateResolve) ? 0u : activeCheckerboardField;
        // Packed like the reservoir buffers were: one texel per dispatched lane, so the checkerboard leaves no unwritten texels in the aliased target.
        const uint32_t sunVisWidth = (sunCheckerboardField != 0u) ? ((renderExtent[0] + 1u) >> 1u) : renderExtent[0];
        graph.CreateTexture("restir_sun_vis"_sid, TextureInfo{VK_FORMAT_R32_UINT, sunVisWidth, renderExtent[1], 1}, {std::nullopt}, true);
        const bool bHasPrevTlas = bConfidence && graph.ResourceHasVersion(RT_TLAS_BUFFER, 1);
        const StringID prevTlas = bHasPrevTlas ? graph.ResourceVersionID(RT_TLAS_BUFFER, 1) : StringID{};
        if (bConfidence) {
            graph.CreateVersionedTexture("restir_sun_blocker"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
        }
        const bool bHasPrevBlocker = bConfidence && graph.ResourceHasVersion("restir_sun_blocker"_sid, 1);
        const StringID prevSunBlocker = bHasPrevBlocker ? graph.ResourceVersionID("restir_sun_blocker"_sid, 1) : StringID{};
        const bool bSunReproject = bConfidence && !bResetHistory && graph.ResourceHasVersion(targets.gbufferOne, 1) && graph.ResourceHasVersion(targets.depthCopy, 1);
        const StringID gbufferOneHistory = bSunReproject ? graph.ResourceVersionID(targets.gbufferOne, 1) : StringID{};
        const StringID depthHistory = bSunReproject ? graph.ResourceVersionID(targets.depthCopy, 1) : StringID{};

        RenderPass& sunPass = graph.AddPass("[ReSTIR DI] Sun"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        sunPass.ReadBuffer(SCENE_DATA_BUFFER);
        sunPass.ReadBuffer("light_data"_sid);
        sunPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        sunPass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
        sunPass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
        sunPass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
        sunPass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
        sunPass.ReadSampledImage(targets.gbufferOne);
        sunPass.ReadSampledImage(targets.gbufferTwo);
        sunPass.ReadSampledImage(targets.depthCopy);
        if (bSunReproject) { sunPass.ReadSampledImage(gbufferOneHistory); }
        if (bSunReproject) { sunPass.ReadSampledImage(depthHistory); }
        sunPass.ReadTLASBuffer(RT_TLAS_BUFFER);
        if (bHasPrevTlas) { sunPass.ReadTLASBuffer(prevTlas); }
        sunPass.WriteStorageImage("restir_sun_vis"_sid);
        if (bConfidence) { sunPass.WriteStorageImage("restir_signal"_sid); }
        if (bConfidence) { sunPass.WriteStorageImage("restir_sun_blocker"_sid); }
        if (bHasPrevBlocker) { sunPass.ReadSampledImage(prevSunBlocker); }
        sunPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bSunReproject, bHasPrevTlas, bConfidence, bHasPrevBlocker, prevSunBlocker, gbufferOneHistory, depthHistory, field = sunCheckerboardField, bAlphaTest = viewFamily.sigmaParams.bAlphaTest, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_di_sun"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRDISunPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress("light_data"_sid),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
                .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .visIndex = graph.GetStorageImageViewDescriptorIndex("restir_sun_vis"_sid),
                .prevGbufferOneIndex = bSunReproject ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0u,
                .prevDepthIndex = bSunReproject ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0u,
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
                .prevTlasIndex = bHasPrevTlas ? graph.GetAccelerationStructureDescriptorIndex(prevTlas) : ~0u,
                .signalIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex("restir_signal"_sid) : ~0u,
                .blockerIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex("restir_sun_blocker"_sid) : ~0u,
                .prevBlockerIndex = bHasPrevBlocker ? graph.GetSampledImageViewDescriptorIndex(prevSunBlocker) : ~0u,
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .activeCheckerboardField = field,
                .bAlphaTest = bAlphaTest ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t strideX = (field != 0u) ? ((renderExtent[0] + 1u) >> 1u) : renderExtent[0];
            const uint32_t groupsX = (strideX + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
    }

    // Temporal-gradient antilag confidence: gradient (stratum mean of the re-shade signal) -> resolve (blur + convert + asymmetric temporal) -> restir_confidence, consumed by RELAX.
    if (bConfidence) {
        const bool bHasPrevConfidence = graph.ResourceHasVersion("restir_confidence"_sid, 1);
        const StringID prevConfidence = bHasPrevConfidence ? graph.ResourceVersionID("restir_confidence"_sid, 1) : StringID{};

        RenderPass& gradientPass = graph.AddPass("[ReSTIR DI] Confidence Gradient"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        gradientPass.ReadSampledImage("restir_signal"_sid);
        gradientPass.WriteStorageImage("restir_gradient"_sid);
        gradientPass.Execute([&, pipelineManager, renderExtent, gradientExtent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_confidence_gradient"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRConfidenceGradientPushConstant pc{
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .gradientExtent = {gradientExtent[0], gradientExtent[1]},
                .signalIndex = graph.GetSampledImageViewDescriptorIndex("restir_signal"_sid),
                .gradientIndex = graph.GetStorageImageViewDescriptorIndex("restir_gradient"_sid),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (gradientExtent[0] + 7) / 8, (gradientExtent[1] + 7) / 8, 1);
        });

        RenderPass& resolvePass = graph.AddPass("[ReSTIR DI] Confidence Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        resolvePass.ReadSampledImage("restir_gradient"_sid);
        if (bHasPrevConfidence) { resolvePass.ReadSampledImage(prevConfidence); }
        resolvePass.ReadSampledImage(targets.gbufferOne);
        resolvePass.WriteStorageImage("restir_confidence"_sid);
        resolvePass.Execute([&, pipelineManager, renderExtent, gradientExtent, bHasPrevConfidence, prevConfidence, gbufferOne = targets.gbufferOne,
                confStrength = restirParams.confidenceStrength, sensitivity = restirParams.confidenceSensitivity, darknessBias = restirParams.confidenceDarknessBias,
                blendFactor = 1.0f / (restirParams.confidenceHistoryLength + 1.0f), blurRadius = restirParams.confidenceBlurRadius](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_confidence_resolve"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                ReSTIRConfidenceResolvePushConstant pc{
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .gradientExtent = {gradientExtent[0], gradientExtent[1]},
                    .gradientIndex = graph.GetSampledImageViewDescriptorIndex("restir_gradient"_sid),
                    .prevConfidenceIndex = bHasPrevConfidence ? graph.GetSampledImageViewDescriptorIndex(prevConfidence) : ~0u,
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .confidenceIndex = graph.GetStorageImageViewDescriptorIndex("restir_confidence"_sid),
                    .confidenceStrength = confStrength,
                    .sensitivity = sensitivity,
                    .darknessBias = darknessBias,
                    .blendFactor = blendFactor,
                    .blurRadius = blurRadius,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (renderExtent[0] + 7) / 8, (renderExtent[1] + 7) / 8, 1);
            });
    }

    StringID reuseBuffer = bTemporalReuse ? "restir_reservoir_temporal"_sid : "restir_reservoir_base"_sid;

    if (restirParams.boilingFilterStrength > 0.0f) {
        graph.CreateBuffer("restir_reservoir_boiled"_sid, reservoirBufferSize, true);

        RenderPass& boilingPass = graph.AddPass("[ReSTIR DI] Boiling Filter"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        boilingPass.ReadBuffer(reuseBuffer);
        boilingPass.WriteBuffer("restir_reservoir_boiled"_sid);
        boilingPass.Execute([&, pipelineManager, renderExtent, inBuffer = reuseBuffer, strength = restirParams.boilingFilterStrength, field = activeCheckerboardField](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_boiling_filter"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRBoilingFilterPushConstant pc{
                .inputBuffer = graph.GetBufferAddress(inBuffer),
                .outputBuffer = graph.GetBufferAddress("restir_reservoir_boiled"_sid),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .strength = strength,
                .activeCheckerboardField = field,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t strideX = (field != 0u) ? ((renderExtent[0] + 1u) >> 1u) : renderExtent[0];
            const uint32_t groupsX = (strideX + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

        reuseBuffer = "restir_reservoir_boiled"_sid;
    }

    if (bTemporalReuse) { graph.EmplaceVersion("restir_reservoir_history"_sid, reuseBuffer); }

    // Spatial reuse chain: N passes ping-ponging two scratch buffers, each reading the previous output. Pass 0 reads the temporal reuseBuffer.
    const uint32_t spatialPasses = restirParams.spatialPasses;
    if (spatialPasses == 0u) {
        graph.AliasBuffer("restir_reservoir_final"_sid, reuseBuffer);
        return;
    }
    const StringID spatialScratch[2] = {"restir_reservoir_spatial"_sid, "restir_reservoir_spatial2"_sid};
    graph.CreateBuffer(spatialScratch[0], reservoirBufferSize, true);
    if (spatialPasses > 1u) {
        graph.CreateBuffer(spatialScratch[1], reservoirBufferSize, true);
    }

    for (uint32_t i = 0; i < spatialPasses; i++) {
        const StringID inputName = i == 0u ? reuseBuffer : spatialScratch[i - 1u & 1u];
        const StringID outputName = spatialScratch[i & 1u];

        const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReSTIR DI] Spatial %u", i);

        RenderPass& spatialPass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        spatialPass.ReadBuffer(SCENE_DATA_BUFFER);
        spatialPass.ReadBuffer("light_data"_sid);
        spatialPass.ReadBuffer("restir_lights_vs"_sid);
        spatialPass.ReadBuffer(inputName);
        spatialPass.ReadSampledImage(targets.gbufferOne);
        spatialPass.ReadSampledImage(targets.gbufferTwo);
        spatialPass.ReadSampledImage(targets.depthCopy);
        if (bHasTLAS) { spatialPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        spatialPass.WriteBuffer(outputName);
        spatialPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, inputName, outputName, passIndex = i, bLastPass = (i == spatialPasses - 1u), field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_di_spatial"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;

            ReSTIRDISpatialPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress("light_data"_sid),
                .lightVS = graph.GetBufferAddress("restir_lights_vs"_sid),
                .inputBuffer = graph.GetBufferAddress(inputName),
                .outputBuffer = graph.GetBufferAddress(outputName),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .spatialRadius = restirParams.spatialRadius,
                .spatialNeighbors = restirParams.spatialNeighbors,
                .mCap = restirParams.spatialMCap,
                .tlasIndex = tlasIndex,
                .passIndex = passIndex,
                .bAdaptiveSpatial = restirParams.bAdaptiveSpatial ? 1u : 0u,
                .adaptiveSpatialBoost = restirParams.adaptiveSpatialBoost,
                .adaptiveMReference = restirParams.temporalMCap,
                .bValidateVisibility = bLastPass ? 1u : 0u,
                .wClamp = restirParams.restirWClamp,
                .activeCheckerboardField = field,
                .lightSpecularFromReflectionsMax = reflectionConfig.lightSpecularFromReflectionsMax,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t strideX = (field != 0u) ? ((renderExtent[0] + 1u) >> 1u) : renderExtent[0];
            const uint32_t groupsX = (strideX + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
    }

    graph.AliasBuffer("restir_reservoir_final"_sid, spatialScratch[spatialPasses - 1u & 1u]);
}

void SetupReSTIRLightingResolvePass(RenderGraph& graph,
                                    PipelineManager* pipelineManager,
                                    const Core::ViewFamily& viewFamily,
                                    Core::Array<uint32_t, 2> renderExtent,
                                    const RenderTargets& targets,
                                    uint32_t sceneIndex,
                                    uint64_t frameNumber,
                                    uint32_t activeCheckerboardField,
                                    uint32_t bCheckerboardPacked,
                                    uint32_t bFullRateResolve,
                                    const Core::ReflectionConfiguration& reflectionConfig)
{
    ZoneScoped;
    if (!graph.HasBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER)) { return; }

    const bool bMergedReflections = reflectionConfig.bMergedDenoise && ComputeReflectionRoughnessMax(reflectionConfig) >= 0.0f && graph.HasTexture(REFLECTION_SPEC_NOISY_TARGET);

    RenderPass& lightingResolve = graph.AddPass("[ReSTIR DI] Lighting Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    lightingResolve.ReadBuffer(SCENE_DATA_BUFFER);
    lightingResolve.ReadBuffer("light_data"_sid);
    if (bMergedReflections) {
        lightingResolve.ReadSampledImage(REFLECTION_SPEC_NOISY_TARGET);
    }
    if (graph.HasBuffer("restir_reservoir_final"_sid)) {
        lightingResolve.ReadBuffer("restir_reservoir_final"_sid);
    }
    if (graph.HasTexture("restir_sun_vis"_sid)) {
        lightingResolve.ReadSampledImage("restir_sun_vis"_sid);
    }
    if (graph.HasBuffer("restir_lights_vs"_sid)) {
        lightingResolve.ReadBuffer("restir_lights_vs"_sid);
    }
    lightingResolve.ReadIndirectBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    lightingResolve.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    lightingResolve.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    lightingResolve.ReadSampledImage(targets.visibility);
    lightingResolve.ReadSampledImage(targets.gbufferOne);
    lightingResolve.ReadSampledImage(targets.gbufferTwo);
    lightingResolve.ReadSampledImage(targets.depthCopy);
    if (targets.shadows != StringID{}) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    lightingResolve.WriteStorageImage(targets.intermediateOne);
    lightingResolve.WriteStorageImage(targets.intermediateTwo);
    lightingResolve.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            diffuseOut = targets.intermediateOne, specularOut = targets.intermediateTwo, skyboxIndex = viewFamily.skyboxIndex,
            field = activeCheckerboardField, packed = bCheckerboardPacked, fullRate = bFullRateResolve,
            bMergedReflections](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkDeviceAddress lightDispatchAddress = graph.GetBufferAddress(LIGHTING_DISPATCH_BUCKETING_BUFFER);

            for (const LightingPipelineInfo& entry : pipelineManager->GetLightingPipelines()) {
                if (!entry.id) { continue; }

                StringID shaderToUse = viewFamily.lightingShaderOverride ? viewFamily.lightingShaderOverride : entry.id;
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(shaderToUse);
                if (!pipelineEntry) { continue; }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                VisibilityLightingPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress("light_data"_sid),
                    .lightVS = graph.TryGetBufferAddress("restir_lights_vs"_sid),
                    .lightDispatchBuffer = lightDispatchAddress,
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                    .reservoirBuffer = graph.TryGetBufferAddress("restir_reservoir_final"_sid),
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                    .skyboxIndex = skyboxIndex,
                    .primaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(diffuseOut),
                    .secondaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(specularOut),
                    .sceneDataIndex = sceneIndex,
                    .lightingIndex = entry.index,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .activeCheckerboardField = field,
                    .bCheckerboardPacked = packed,
                    .reflectionIndex = bMergedReflections ? graph.GetSampledImageViewDescriptorIndex(REFLECTION_SPEC_NOISY_TARGET) : ~0x0u,
                    .bFullRateResolve = fullRate,
                    .lightSpecularFromReflectionsMax = reflectionConfig.lightSpecularFromReflectionsMax,
                    .sunVisIndex = graph.HasTexture("restir_sun_vis"_sid) ? graph.GetSampledImageViewDescriptorIndex("restir_sun_vis"_sid) : ~0x0u,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                                      entry.index * sizeof(LightingDispatchParameters) + offsetof(LightingDispatchParameters, xDispatch));
            }
        });
}

void SetupReSTIRRemodulatePass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               Core::Array<uint32_t, 2> renderExtent,
                               const RenderTargets& targets,
                               uint32_t sceneIndex,
                               uint32_t outputMode,
                               float iblIntensity,
                               uint64_t frameNumber,
                               bool bDDGIApply,
                               const Core::ReflectionConfiguration& reflectionConfig,
                               uint32_t giGatherMode)
{
    ZoneScoped;
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    const bool bDDGI = bDDGIApply && graph.HasBuffer(DDGI_CASCADES_BUFFER);
    const bool bGIGather = giGatherMode != 0u && graph.HasTexture(GI_GATHER_RESOLVED);
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    const StringID reflectionTarget = REFLECTION_SPEC_NOISY_TARGET;
    const bool bReflectionMerged = reflectionConfig.bMergedDenoise && reflectionRoughnessMax >= 0.0f && graph.HasTexture(REFLECTION_SPEC_NOISY_TARGET);
    const bool bReflection = !bReflectionMerged && reflectionRoughnessMax >= 0.0f && graph.HasTexture(reflectionTarget);

    RenderPass& pass = graph.AddPass("[ReSTIR DI] Remodulate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (graph.HasBuffer("world_grid_probe_grid"_sid)) { pass.ReadBuffer("world_grid_probe_grid"_sid); }
    pass.ReadSampledImage(targets.intermediateOne);
    pass.ReadSampledImage(targets.intermediateTwo);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (targets.shadows != StringID{}) {
        pass.ReadSampledImage(targets.shadows);
    }
    if (bDDGI) {
        AddDDGISampleDependencies(graph, pass);
    }
    if (bReflection) {
        pass.ReadSampledImage(reflectionTarget);
    }
    if (bGIGather) {
        pass.ReadSampledImage(GI_GATHER_RESOLVED);
        pass.ReadSampledImage(GI_GATHER_DATA);
    }
    pass.WriteStorageImage(targets.colorOutput);
    const int32_t skyboxIndex = viewFamily.skyboxIndex;
    const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
    const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
    pass.Execute([pipelineManager, sceneIndex, outputMode, width, height, skyboxIndex, iblIntensity, indirectIntensity = viewFamily.indirectIntensity, bDDGI, bReflection, bReflectionMerged, reflectionRoughnessMax, reflectionTarget, bGIGather, giGatherMode, reflectionProbeCount, bProbeBrute,
            diffuse = targets.intermediateOne, specular = targets.intermediateTwo,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
                .sceneDataIndex = sceneIndex,
                .diffuseIndex = graph.GetSampledImageViewDescriptorIndex(diffuse),
                .specularIndex = graph.GetSampledImageViewDescriptorIndex(specular),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .width = width,
                .height = height,
                .outputMode = outputMode,
                .skyboxIndex = skyboxIndex,
                .iblIntensity = iblIntensity,
                .indirectIntensity = indirectIntensity,
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
                .bDDGIApply = bDDGI ? 1u : 0u,
                .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                .reflectionIndex = bReflection ? graph.GetSampledImageViewDescriptorIndex(reflectionTarget) : ~0x0u,
                .reflectionRoughnessMax = reflectionRoughnessMax,
                .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_RESOLVED) : ~0x0u,
                .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA) : ~0x0u,
                .giGatherMode = bGIGather ? giGatherMode : 0u,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
                .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer("world_grid_probe_grid"_sid)) ? graph.GetBufferAddress("world_grid_probe_grid"_sid) : 0,
                .bReflectionMerged = bReflectionMerged ? 1u : 0u,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("restir_remodulate"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
}
} // Render
