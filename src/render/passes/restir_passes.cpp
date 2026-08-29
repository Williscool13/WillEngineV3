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

    const bool bReGIRProposal = Core::ReSTIRParams::REGIR_AVAILABLE && restirParams.lightProposal == Core::ReSTIRParams::LightProposal::ReGIR;
    const bool bWorldGrid = graph.HasBuffer(SID("world_grid_light_grid")) && graph.HasBuffer(SID("world_grid_index_list"));

    // Temporal-gradient confidence runs at 1/GRAD_FACTOR of the half-res ReSTIR grid (must match GRAD_FACTOR in the confidence shaders).
    const uint32_t GRAD_FACTOR = 3u;
    const Core::Array<uint32_t, 2> gradientExtent = {(renderExtent[0] + GRAD_FACTOR - 1u) / GRAD_FACTOR, (renderExtent[1] + GRAD_FACTOR - 1u) / GRAD_FACTOR};

    // Transform all lights (area + sphere) to view space once; every ReSTIR pass and the resolve read this instead of transforming per pixel.
    graph.CreateBuffer(SID("restir_lights_vs"), MAX_LIGHTS * sizeof(LightVSData), false);

    const uint32_t liveLightCount = viewFamily.analyticLightCount + viewFamily.triLightCount;

    RenderPass& transformPass = graph.AddPass(SID("[ReSTIR DI] Transform Lights"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    transformPass.ReadBuffer(SCENE_DATA_BUFFER);
    transformPass.ReadBuffer(SID("light_data"));
    transformPass.WriteBuffer(SID("restir_lights_vs"));
    transformPass.Execute([&, pipelineManager, sceneIndex, liveLightCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_transform_lights"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRTransformLightsPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
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

        graph.CreateBuffer(SID("regir_hash_entries"), entriesSize, false);
        graph.CreateBuffer(SID("regir_hash_reservoirs"), reservoirsSize, false);
        graph.CreateBuffer(SID("regir_cell_data"), REGIR_HASH_CAPACITY * 2u * static_cast<uint32_t>(sizeof(float)), false);
        graph.CreateBuffer(SID("regir_active_cells"), activeCellsSize, false);
        graph.CreateBuffer(SID("regir_active_count"), sizeof(uint32_t), false);
        graph.CreateBuffer(SID("regir_fill_indirect"), 3u * static_cast<uint32_t>(sizeof(uint32_t)), false);
        graph.CreateBuffer(SID("regir_tiles"), REGIR_TILE_BUFFER_SIZE, false);

        const bool bHasPrev = !restirParams.bResetReGIR && graph.HasBuffer(SID("regir_hash_entries_prev")) && graph.HasBuffer(SID("regir_hash_reservoirs_prev"));

        RenderPass& clearPass = graph.AddPass(SID("[ReGIR] Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::ReGIR);
        clearPass.WriteTransferBuffer(SID("regir_hash_entries"));
        clearPass.WriteTransferBuffer(SID("regir_active_count"));
        clearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("regir_hash_entries")), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("regir_active_count")), 0, VK_WHOLE_SIZE, 0);
        });

        RenderPass& touchPass = graph.AddPass(SID("[ReGIR] Touch"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        touchPass.ReadBuffer(SCENE_DATA_BUFFER);
        touchPass.ReadSampledImage(targets.depthCopy);
        touchPass.WriteBuffer(SID("regir_hash_entries"));
        touchPass.WriteBuffer(SID("regir_active_cells"));
        touchPass.WriteBuffer(SID("regir_active_count"));
        touchPass.Execute([&, pipelineManager, sceneIndex, fullW, fullH, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("regir_touch"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRTouchPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .hashEntries = graph.GetBufferAddress(SID("regir_hash_entries")),
                .activeCells = graph.GetBufferAddress(SID("regir_active_cells")),
                .activeCount = graph.GetBufferAddress(SID("regir_active_count")),
                .renderExtent = {fullW, fullH},
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .sceneDataIndex = sceneIndex,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (fullW + 7) / 8, (fullH + 7) / 8, 1);
        });

        RenderPass& indirectPass = graph.AddPass(SID("[ReGIR] Build Indirect"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        indirectPass.ReadBuffer(SID("regir_active_count"));
        indirectPass.WriteBuffer(SID("regir_fill_indirect"));
        indirectPass.Execute([pipelineManager](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("regir_build_indirect"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRBuildIndirectPushConstant pc{
                .activeCount = graph.GetBufferAddress(SID("regir_active_count")),
                .indirectArgs = graph.GetBufferAddress(SID("regir_fill_indirect")),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });

        const uint32_t analyticLightCount = viewFamily.analyticLightCount;
        // Matches the alias table, which is analytic-only until its build moves to the GPU
        const uint32_t fillLightCount = analyticLightCount;

        // Presample tiles
        {
            RenderPass& presamplePass = graph.AddPass(SID("[ReGIR] Presample Tiles"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
            presamplePass.ReadBuffer(LIGHT_ALIAS_BUFFER);
            presamplePass.WriteBuffer(SID("regir_tiles"));
            presamplePass.Execute([pipelineManager, frameNumber, fillLightCount, analyticLightCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("regir_presample_tiles"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                ReGIRPresampleTilesPushConstant pc{
                    .aliasEntries = graph.GetBufferAddress(LIGHT_ALIAS_BUFFER),
                    .tiles = graph.GetBufferAddress(SID("regir_tiles")),
                    .liveCount = fillLightCount,
                    .analyticCount = static_cast<int32_t>(analyticLightCount),
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t totalSlots = REGIR_TILE_COUNT * REGIR_TILE_SIZE;
                vkCmdDispatch(cmd, (totalSlots + 255u) / 256u, 1, 1);
            });
        }

        RenderPass& regirFillPass = graph.AddPass(SID("[ReGIR] Fill"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        regirFillPass.ReadBuffer(SCENE_DATA_BUFFER);
        regirFillPass.ReadBuffer(SID("light_data"));
        regirFillPass.ReadBuffer(SID("restir_lights_vs"));
        regirFillPass.ReadBuffer(SID("regir_active_cells"));
        regirFillPass.ReadBuffer(SID("regir_active_count"));
        regirFillPass.ReadBuffer(SID("regir_tiles"));
        if (bHasPrev) {
            regirFillPass.ReadBuffer(SID("regir_hash_entries_prev"));
            regirFillPass.ReadBuffer(SID("regir_hash_reservoirs_prev"));
        }
        regirFillPass.ReadIndirectBuffer(SID("regir_fill_indirect"));
        regirFillPass.WriteBuffer(SID("regir_hash_reservoirs"));
        regirFillPass.WriteBuffer(SID("regir_cell_data"));
        regirFillPass.Execute([&, pipelineManager, sceneIndex, frameNumber, bHasPrev](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("regir_fill"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRFillPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                .activeCells = graph.GetBufferAddress(SID("regir_active_cells")),
                .activeCount = graph.GetBufferAddress(SID("regir_active_count")),
                .reservoirs = graph.GetBufferAddress(SID("regir_hash_reservoirs")),
                .cellData = graph.GetBufferAddress(SID("regir_cell_data")),
                .hashEntriesPrev = bHasPrev ? graph.GetBufferAddress(SID("regir_hash_entries_prev")) : 0,
                .reservoirsPrev = bHasPrev ? graph.GetBufferAddress(SID("regir_hash_reservoirs_prev")) : 0,
                .tiles = graph.GetBufferAddress(SID("regir_tiles")),
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .bHasPrev = bHasPrev ? 1u : 0u,
                .wClamp = restirParams.regirWClamp,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(SID("regir_fill_indirect")), 0);
        });

        if (!restirParams.bResetReGIR) {
            graph.CarryBufferToNextFrame(SID("regir_hash_entries"), SID("regir_hash_entries_prev"), 0);
            graph.CarryBufferToNextFrame(SID("regir_hash_reservoirs"), SID("regir_hash_reservoirs_prev"), 0);
        }
    }

    {
        if (bTemporalReuse) {
            graph.CreateBuffer(SID("restir_reservoir_temporal"), reservoirBufferSize, true);
        }

        const bool bHasHistory = graph.HasBuffer(SID("restir_reservoir_history")) && !bResetHistory;
        const bool bHasPrevVis = bShadowVis && graph.HasTexture(SID("restir_shadow_vis_prev"));
        if (bShadowVis) {
            graph.CreateTexture(SID("restir_shadow_vis"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
        }
        if (bConfidence) {
            graph.CreateTexture(SID("restir_confidence"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
            graph.CreateTexture(SID("restir_signal"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
            graph.CreateTexture(SID("restir_gradient"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, gradientExtent[0], gradientExtent[1], 1}, {std::nullopt}, true);
            // Carry this frame's TLAS so the temporal pass can re-shade the winner against last frame's occluder positions. Single hop (BLAS lifetime).
            if (bHasTLAS) { graph.CarryTLASToNextFrame(RT_TLAS_BUFFER, SID("rt_tlas_history")); }
        }

        const bool bHasPrevTlas = bConfidence && graph.HasBuffer(SID("rt_tlas_history"));

        graph.CreateBuffer(SID("restir_reservoir_base"), reservoirBufferSize, true);
        if (reflectionRoughnessMax >= 0.0f) {
            graph.CreateBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER, reflectionBufferSize, true);

            RenderPass& reflClearPass = graph.AddPass(SID("[Reflection] Clear Descriptors"), VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::ReflectionsDenoise);
            reflClearPass.WriteTransferBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER);
            reflClearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(REFLECTION_HIT_DESCRIPTORS_BUFFER), 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
            });
        }

        // Base candidate generation
        RenderPass& basePass = graph.AddPass(SID("[ReSTIR DI] Base"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        basePass.ReadBuffer(SCENE_DATA_BUFFER);
        basePass.ReadBuffer(SID("light_data"));
        basePass.ReadBuffer(SID("restir_lights_vs"));
        if (bReGIRProposal) {
            basePass.ReadBuffer(SID("regir_hash_entries"));
            basePass.ReadBuffer(SID("regir_hash_reservoirs"));
            basePass.ReadBuffer(SID("regir_cell_data"));
        } else if (bWorldGrid) {
            basePass.ReadBuffer(SID("world_grid_light_grid"));
            basePass.ReadBuffer(SID("world_grid_index_list"));
            basePass.ReadBuffer(SID("world_grid_emissive_grid"));
            basePass.ReadBuffer(SID("world_grid_emissive_index_list"));
            basePass.ReadBuffer(SID("world_grid_cell_power"));
        }
        basePass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        basePass.ReadSampledImage(targets.gbufferOne);
        basePass.ReadSampledImage(targets.gbufferTwo);
        basePass.ReadSampledImage(targets.depthCopy);
        if (bHasTLAS) { basePass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        basePass.WriteBuffer(SID("restir_reservoir_base"));
        if (reflectionRoughnessMax >= 0.0f) { basePass.WriteBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER); }
        basePass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, reflectionRoughnessMax, bReGIRProposal, bWorldGrid, field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(bReGIRProposal ? SID("restir_di_base_regir") : SID("restir_di_base_bin"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;
            const bool bBin = !bReGIRProposal && bWorldGrid;

            ReSTIRDICombinedTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .hashEntries = bReGIRProposal ? graph.GetBufferAddress(SID("regir_hash_entries")) : 0,
                .reservoirs = bReGIRProposal ? graph.GetBufferAddress(SID("regir_hash_reservoirs")) : 0,
                .cellData = bReGIRProposal ? graph.GetBufferAddress(SID("regir_cell_data")) : 0,
                .historyBuffer = 0,
                .genBuffer = 0,
                .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_base")),
                .reflectionDescriptors = reflectionRoughnessMax >= 0.0f ? graph.GetBufferAddress(REFLECTION_HIT_DESCRIPTORS_BUFFER) : 0,
                .worldGridBuffer = bBin ? graph.GetBufferAddress(SID("world_grid_light_grid")) : 0,
                .worldGridIndexList = bBin ? graph.GetBufferAddress(SID("world_grid_index_list")) : 0,
                .worldGridEmissiveGrid = bBin ? graph.GetBufferAddress(SID("world_grid_emissive_grid")) : 0,
                .worldGridEmissiveIndexList = bBin ? graph.GetBufferAddress(SID("world_grid_emissive_index_list")) : 0,
                .worldGridCellPower = bBin ? graph.GetBufferAddress(SID("world_grid_cell_power")) : 0,
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
            RenderPass& temporalPass = graph.AddPass(SID("[ReSTIR DI] Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
            temporalPass.ReadBuffer(SCENE_DATA_BUFFER);
            temporalPass.ReadBuffer(SID("light_data"));
            temporalPass.ReadBuffer(SID("restir_lights_vs"));
            temporalPass.ReadBuffer(SID("restir_reservoir_base"));
            if (bHasHistory) { temporalPass.ReadBuffer(SID("restir_reservoir_history")); }
            temporalPass.ReadSampledImage(targets.gbufferOne);
            temporalPass.ReadSampledImage(targets.gbufferTwo);
            temporalPass.ReadSampledImage(targets.depthCopy);
            if (bHasHistory) { temporalPass.ReadSampledImage(SID("gbuffer_one_history")); }
            if (bHasHistory) { temporalPass.ReadSampledImage(SID("depth_history")); }
            if (bHasPrevVis) { temporalPass.ReadSampledImage(SID("restir_shadow_vis_prev")); }
            if (bHasTLAS) { temporalPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
            if (bHasPrevTlas) { temporalPass.ReadTLASBuffer(SID("rt_tlas_history")); }
            temporalPass.WriteBuffer(SID("restir_reservoir_temporal"));
            if (bShadowVis) { temporalPass.WriteStorageImage(SID("restir_shadow_vis")); }
            if (bConfidence) { temporalPass.WriteStorageImage(SID("restir_signal")); }
            temporalPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, bHasPrevTlas, bHasHistory, bConfidence, bShadowVis, bHasPrevVis, field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_temporal"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;
                const uint32_t prevTlasIndex = bHasPrevTlas ? graph.GetAccelerationStructureDescriptorIndex(SID("rt_tlas_history")) : ~0u;

                ReSTIRDICombinedTemporalPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                    .instanceBuffer = 0,
                    .hashEntries = 0,
                    .reservoirs = 0,
                    .cellData = 0,
                    .historyBuffer = bHasHistory ? graph.GetBufferAddress(SID("restir_reservoir_history")) : 0,
                    .genBuffer = graph.GetBufferAddress(SID("restir_reservoir_base")),
                    .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
                    .reflectionDescriptors = 0,
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .prevGbufferOneIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : ~0u,
                    .prevDepthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : ~0u,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .sceneDataIndex = sceneIndex,
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .mCap = restirParams.temporalMCap,
                    .tlasIndex = tlasIndex,
                    .prevTlasIndex = prevTlasIndex,
                    .prevShadowVisIndex = bHasPrevVis ? graph.GetSampledImageViewDescriptorIndex(SID("restir_shadow_vis_prev")) : ~0u,
                    .shadowVisIndex = bShadowVis ? graph.GetStorageImageViewDescriptorIndex(SID("restir_shadow_vis")) : ~0u,
                    .signalIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex(SID("restir_signal")) : ~0u,
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
        graph.CreateTexture(SID("restir_sun_vis"), TextureInfo{VK_FORMAT_R32_UINT, sunVisWidth, renderExtent[1], 1}, {std::nullopt}, true);
        const bool bHasPrevTlas = bConfidence && graph.HasBuffer(SID("rt_tlas_history"));
        const bool bHasPrevBlocker = bConfidence && graph.HasTexture(SID("restir_sun_blocker_prev"));
        const bool bSunReproject = bConfidence && !bResetHistory && graph.HasTexture(SID("gbuffer_one_history")) && graph.HasTexture(SID("depth_history"));
        if (bConfidence) {
            graph.CreateTexture(SID("restir_sun_blocker"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
        }

        RenderPass& sunPass = graph.AddPass(SID("[ReSTIR DI] Sun"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        sunPass.ReadBuffer(SCENE_DATA_BUFFER);
        sunPass.ReadBuffer(SID("light_data"));
        sunPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        sunPass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
        sunPass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
        sunPass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
        sunPass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
        sunPass.ReadSampledImage(targets.gbufferOne);
        sunPass.ReadSampledImage(targets.gbufferTwo);
        sunPass.ReadSampledImage(targets.depthCopy);
        if (bSunReproject) { sunPass.ReadSampledImage(SID("gbuffer_one_history")); }
        if (bSunReproject) { sunPass.ReadSampledImage(SID("depth_history")); }
        sunPass.ReadTLASBuffer(RT_TLAS_BUFFER);
        if (bHasPrevTlas) { sunPass.ReadTLASBuffer(SID("rt_tlas_history")); }
        sunPass.WriteStorageImage(SID("restir_sun_vis"));
        if (bConfidence) { sunPass.WriteStorageImage(SID("restir_signal")); }
        if (bConfidence) { sunPass.WriteStorageImage(SID("restir_sun_blocker")); }
        if (bHasPrevBlocker) { sunPass.ReadSampledImage(SID("restir_sun_blocker_prev")); }
        sunPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bSunReproject, bHasPrevTlas, bConfidence, bHasPrevBlocker, field = sunCheckerboardField, bAlphaTest = viewFamily.sigmaParams.bAlphaTest, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_sun"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRDISunPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
                .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .visIndex = graph.GetStorageImageViewDescriptorIndex(SID("restir_sun_vis")),
                .prevGbufferOneIndex = bSunReproject ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : ~0u,
                .prevDepthIndex = bSunReproject ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : ~0u,
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
                .prevTlasIndex = bHasPrevTlas ? graph.GetAccelerationStructureDescriptorIndex(SID("rt_tlas_history")) : ~0u,
                .signalIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex(SID("restir_signal")) : ~0u,
                .blockerIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex(SID("restir_sun_blocker")) : ~0u,
                .prevBlockerIndex = bHasPrevBlocker ? graph.GetSampledImageViewDescriptorIndex(SID("restir_sun_blocker_prev")) : ~0u,
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

        if (bConfidence) {
            graph.CarryTextureToNextFrame(SID("restir_sun_blocker"), SID("restir_sun_blocker_prev"), VK_IMAGE_USAGE_SAMPLED_BIT);
        }
    }

    // Temporal-gradient antilag confidence: gradient (stratum mean of the re-shade signal) -> resolve (blur + convert + asymmetric temporal) -> restir_confidence, consumed by RELAX.
    if (bConfidence) {
        const bool bHasPrevConfidence = graph.HasTexture(SID("restir_confidence_prev"));

        RenderPass& gradientPass = graph.AddPass(SID("[ReSTIR DI] Confidence Gradient"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        gradientPass.ReadSampledImage(SID("restir_signal"));
        gradientPass.WriteStorageImage(SID("restir_gradient"));
        gradientPass.Execute([&, pipelineManager, renderExtent, gradientExtent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_confidence_gradient"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRConfidenceGradientPushConstant pc{
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .gradientExtent = {gradientExtent[0], gradientExtent[1]},
                .signalIndex = graph.GetSampledImageViewDescriptorIndex(SID("restir_signal")),
                .gradientIndex = graph.GetStorageImageViewDescriptorIndex(SID("restir_gradient")),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (gradientExtent[0] + 7) / 8, (gradientExtent[1] + 7) / 8, 1);
        });

        RenderPass& resolvePass = graph.AddPass(SID("[ReSTIR DI] Confidence Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        resolvePass.ReadSampledImage(SID("restir_gradient"));
        if (bHasPrevConfidence) { resolvePass.ReadSampledImage(SID("restir_confidence_prev")); }
        resolvePass.ReadSampledImage(targets.gbufferOne);
        resolvePass.WriteStorageImage(SID("restir_confidence"));
        resolvePass.Execute([&, pipelineManager, renderExtent, gradientExtent, bHasPrevConfidence, gbufferOne = targets.gbufferOne,
                confStrength = restirParams.confidenceStrength, sensitivity = restirParams.confidenceSensitivity, darknessBias = restirParams.confidenceDarknessBias,
                blendFactor = 1.0f / (restirParams.confidenceHistoryLength + 1.0f), blurRadius = restirParams.confidenceBlurRadius](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_confidence_resolve"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                ReSTIRConfidenceResolvePushConstant pc{
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .gradientExtent = {gradientExtent[0], gradientExtent[1]},
                    .gradientIndex = graph.GetSampledImageViewDescriptorIndex(SID("restir_gradient")),
                    .prevConfidenceIndex = bHasPrevConfidence ? graph.GetSampledImageViewDescriptorIndex(SID("restir_confidence_prev")) : ~0u,
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .confidenceIndex = graph.GetStorageImageViewDescriptorIndex(SID("restir_confidence")),
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

    StringID reuseBuffer = bTemporalReuse ? SID("restir_reservoir_temporal") : SID("restir_reservoir_base");

    if (restirParams.boilingFilterStrength > 0.0f) {
        graph.CreateBuffer(SID("restir_reservoir_boiled"), reservoirBufferSize, true);

        RenderPass& boilingPass = graph.AddPass(SID("[ReSTIR DI] Boiling Filter"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        boilingPass.ReadBuffer(reuseBuffer);
        boilingPass.WriteBuffer(SID("restir_reservoir_boiled"));
        boilingPass.Execute([&, pipelineManager, renderExtent, inBuffer = reuseBuffer, strength = restirParams.boilingFilterStrength, field = activeCheckerboardField](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_boiling_filter"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRBoilingFilterPushConstant pc{
                .inputBuffer = graph.GetBufferAddress(inBuffer),
                .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_boiled")),
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

        reuseBuffer = SID("restir_reservoir_boiled");
    }

    if (bTemporalReuse) { graph.CarryBufferToNextFrame(reuseBuffer, SID("restir_reservoir_history"), 0); }
    if (bShadowVis) {
        graph.CarryTextureToNextFrame(SID("restir_shadow_vis"), SID("restir_shadow_vis_prev"), VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    if (bConfidence) {
        // restir_signal is no longer carried (the re-shade delta is computed in-pass against the carried prev TLAS). Only the confidence history is carried, for the resolve's asymmetric temporal blend.
        graph.CarryTextureToNextFrame(SID("restir_confidence"), SID("restir_confidence_prev"), VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    // Spatial reuse chain: N passes ping-ponging two scratch buffers, each reading the previous output. Pass 0 reads the temporal reuseBuffer.
    const uint32_t spatialPasses = restirParams.spatialPasses;
    if (spatialPasses == 0u) {
        graph.AliasBuffer(SID("restir_reservoir_final"), reuseBuffer);
        return;
    }
    const StringID spatialScratch[2] = {SID("restir_reservoir_spatial"), SID("restir_reservoir_spatial2")};
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
        spatialPass.ReadBuffer(SID("light_data"));
        spatialPass.ReadBuffer(SID("restir_lights_vs"));
        spatialPass.ReadBuffer(inputName);
        spatialPass.ReadSampledImage(targets.gbufferOne);
        spatialPass.ReadSampledImage(targets.gbufferTwo);
        spatialPass.ReadSampledImage(targets.depthCopy);
        if (bHasTLAS) { spatialPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        spatialPass.WriteBuffer(outputName);
        spatialPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, inputName, outputName, passIndex = i, bLastPass = (i == spatialPasses - 1u), field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_spatial"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;

            ReSTIRDISpatialPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
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

    graph.AliasBuffer(SID("restir_reservoir_final"), spatialScratch[spatialPasses - 1u & 1u]);
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

    RenderPass& lightingResolve = graph.AddPass(SID("[ReSTIR DI] Lighting Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    lightingResolve.ReadBuffer(SCENE_DATA_BUFFER);
    lightingResolve.ReadBuffer(SID("light_data"));
    if (bMergedReflections) {
        lightingResolve.ReadSampledImage(REFLECTION_SPEC_NOISY_TARGET);
    }
    if (graph.HasBuffer(SID("restir_reservoir_final"))) {
        lightingResolve.ReadBuffer(SID("restir_reservoir_final"));
    }
    if (graph.HasTexture(SID("restir_sun_vis"))) {
        lightingResolve.ReadSampledImage(SID("restir_sun_vis"));
    }
    if (graph.HasBuffer(SID("restir_lights_vs"))) {
        lightingResolve.ReadBuffer(SID("restir_lights_vs"));
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
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightVS = graph.TryGetBufferAddress(SID("restir_lights_vs")),
                    .lightDispatchBuffer = lightDispatchAddress,
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                    .reservoirBuffer = graph.TryGetBufferAddress(SID("restir_reservoir_final")),
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
                    .sunVisIndex = graph.HasTexture(SID("restir_sun_vis")) ? graph.GetSampledImageViewDescriptorIndex(SID("restir_sun_vis")) : ~0x0u,
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

    RenderPass& pass = graph.AddPass(SID("[ReSTIR DI] Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (graph.HasBuffer(SID("world_grid_probe_grid"))) { pass.ReadBuffer(SID("world_grid_probe_grid")); }
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
                .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
                .bReflectionMerged = bReflectionMerged ? 1u : 0u,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("restir_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
}
} // Render
