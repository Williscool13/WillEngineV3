//
// Created by William on 2026-06-06.
//

#include "render/passes/restir_passes.h"

#include <tracy/Tracy.hpp>

#include "render/passes/ddgi_passes.h"
#include "render/passes/final_gather_passes.h"
#include "render/passes/reflection_passes.h"
#include "render/passes/shadow_passes.h"
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
ReSTIRFrame SetupReSTIRPasses(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Extent2D renderExtent,
                              const RenderTargets& targets,
                              const SceneResources& scene,
                              const WorldGridFrame& worldGrid,
                              uint32_t sceneIndex,
                              Core::Arena& arena,
                              uint64_t frameNumber,
                              const Core::ReSTIRParams& restirParams,
                              uint32_t activeCheckerboardField,
                              const Core::ReflectionConfiguration& reflectionConfig,
                              bool bResetHistory,
                              bool bSkipReflectionPiggyback,
                              float preExposure,
                              ReflectionFrame& reflection)
{
    ZoneScoped;
    ReSTIRFrame restir{};
    const uint32_t pixelCount = renderExtent.width * renderExtent.height;
    const uint32_t reservoirBufferSize = pixelCount * static_cast<uint32_t>(sizeof(Reservoir));
    const float reflectionRoughnessMax = bSkipReflectionPiggyback ? -1.0f : ComputeReflectionRoughnessMax(reflectionConfig);
    const uint32_t reflectionBufferSize = pixelCount * static_cast<uint32_t>(sizeof(ReflectionHitDescriptor));

    const bool bHasTLAS = scene.tlas.IsValid();
    const bool bTemporalReuse = restirParams.bEnableTemporal;
    const bool bConfidenceDenoiser = restirParams.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX || restirParams.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRD || restirParams.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRDReBLUR;
    const bool bConfidence = bTemporalReuse && RESTIR_ENABLE_CONFIDENCE && restirParams.bEnableConfidence && bConfidenceDenoiser;
    const bool bSunFlip = bTemporalReuse && RESTIR_ENABLE_CONFIDENCE && restirParams.bEnableConfidence && bConfidenceDenoiser;
    const bool bAntilag = bTemporalReuse && RESTIR_ENABLE_ANTILAG && restirParams.bEnableAntilag;
    const bool bShadowVis = bAntilag;

    const bool bReGIRProposal = restirParams.lightProposal == Core::ReSTIRParams::LightProposal::ReGIR;
    const bool bWorldGrid = worldGrid.lightGrid.IsValid() && worldGrid.indexList.IsValid();
    const RDGBuffer worldLightGrid = worldGrid.lightGrid;
    const RDGBuffer worldIndexList = worldGrid.indexList;
    const RDGBuffer worldEmissiveGrid = worldGrid.emissiveGrid;
    const RDGBuffer worldEmissiveIndexList = worldGrid.emissiveIndexList;
    const RDGBuffer worldCellPower = worldGrid.cellPower;

    const uint32_t GRAD_FACTOR = 3u;
    const Core::Extent2D gradientExtent = {(renderExtent.width + GRAD_FACTOR - 1u) / GRAD_FACTOR, (renderExtent.height + GRAD_FACTOR - 1u) / GRAD_FACTOR};

    const RDGBuffer lightsVS = graph.CreateBuffer("restir_lights_vs"_sid, MAX_LIGHTS * sizeof(LightVSData), false);
    restir.lightsVS = lightsVS;

    const uint32_t liveLightCount = viewFamily.analyticLightCount + viewFamily.triLightCount;

    RenderPass& transformPass = graph.AddPass("[ReSTIR DI] Transform Lights"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    transformPass.AsyncCompute();
    transformPass.ReadBuffer(scene.sceneData);
    transformPass.ReadBuffer(scene.lightData);
    transformPass.WriteBuffer(lightsVS);
    transformPass.Execute([&, pipelineManager, sceneIndex, liveLightCount, lightsVS](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_di_transform_lights"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRTransformLightsPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .lightVS = graph.GetBufferAddress(lightsVS),
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (liveLightCount + 63u) / 64u, 1, 1);
    });

    if (bReGIRProposal)
    {
        const uint32_t fullW = renderExtent.width;
        const uint32_t fullH = renderExtent.height;
        const uint32_t hashEntriesSize = REGIR_HASH_CAPACITY * static_cast<uint32_t>(sizeof(uint32_t));
        const uint32_t entriesSize = REGIR_HASH_CAPACITY * REGIR_ENTRIES_PER_CELL * static_cast<uint32_t>(sizeof(ReGIREntry));
        const uint32_t activeCellsSize = REGIR_HASH_CAPACITY * 4u * static_cast<uint32_t>(sizeof(int32_t));

        const RDGBuffer hashEntries = graph.CreateBuffer("regir_hash_entries"_sid, hashEntriesSize, false);
        const RDGBuffer entries = graph.CreateBuffer("regir_entries"_sid, entriesSize, false);
        const RDGBuffer cellData = graph.CreateBuffer("regir_cell_data"_sid, REGIR_HASH_CAPACITY * 2u * static_cast<uint32_t>(sizeof(uint32_t)), false);
        const RDGBuffer activeCells = graph.CreateBuffer("regir_active_cells"_sid, activeCellsSize, false);
        const RDGBuffer activeCount = graph.CreateBuffer("regir_active_count"_sid, sizeof(uint32_t), false);
        const RDGBuffer fillIndirect = graph.CreateBuffer("regir_fill_indirect"_sid, 3u * static_cast<uint32_t>(sizeof(uint32_t)), false);
        restir.regirHashEntries = hashEntries;
        restir.regirEntries = entries;
        restir.regirCellData = cellData;

        RenderPass& clearPass = graph.AddPass("[ReGIR] Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::ReGIR);
        clearPass.WriteTransferBuffer(hashEntries);
        clearPass.WriteTransferBuffer(activeCount);
        if (GPU_STATS_ENABLED) { clearPass.WriteTransferBuffer(scene.readback); }
        clearPass.Execute([&scene, hashEntries, activeCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(hashEntries), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(activeCount), 0, VK_WHOLE_SIZE, 0);
            if (GPU_STATS_ENABLED) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(scene.readback), offsetof(ReadbackStruct, regirInsertsFailed), sizeof(uint32_t), 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(scene.readback), offsetof(ReadbackStruct, regirGatherOverflow), sizeof(uint32_t), 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(scene.readback), offsetof(ReadbackStruct, regirConeRejected), sizeof(uint32_t), 0);
            }
        });

        RenderPass& touchPass = graph.AddPass("[ReGIR] Touch"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        touchPass.ReadBuffer(scene.sceneData);
        touchPass.ReadSampledImage(targets.depthCopy);
        touchPass.WriteBuffer(hashEntries);
        touchPass.WriteBuffer(activeCells);
        touchPass.WriteBuffer(activeCount);
        if (GPU_STATS_ENABLED) { touchPass.ReadWriteBuffer(scene.readback); }
        touchPass.Execute([&, pipelineManager, sceneIndex, fullW, fullH, hashEntries, activeCells, activeCount, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("regir_touch"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRTouchPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .hashEntries = graph.GetBufferAddress(hashEntries),
                .activeCells = graph.GetBufferAddress(activeCells),
                .activeCount = graph.GetBufferAddress(activeCount),
                .insertFailures = GPU_STATS_ENABLED ? graph.GetBufferAddress(scene.readback) + offsetof(ReadbackStruct, regirInsertsFailed) : 0,
                .renderExtent = {fullW, fullH},
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .sceneDataIndex = sceneIndex,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (fullW + 7) / 8, (fullH + 7) / 8, 1);
        });

        RenderPass& indirectPass = graph.AddPass("[ReGIR] Build Indirect"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        indirectPass.ReadBuffer(activeCount);
        indirectPass.WriteBuffer(fillIndirect);
        if (GPU_STATS_ENABLED) { indirectPass.ReadWriteBuffer(scene.readback); }
        indirectPass.Execute([pipelineManager, &scene, activeCount, fillIndirect](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("regir_build_indirect"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRBuildIndirectPushConstant pc{
                .activeCount = graph.GetBufferAddress(activeCount),
                .indirectArgs = graph.GetBufferAddress(fillIndirect),
                .activeCellStat = GPU_STATS_ENABLED ? graph.GetBufferAddress(scene.readback) + offsetof(ReadbackStruct, regirActiveCells) : 0,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });

        RenderPass& regirFillPass = graph.AddPass("[ReGIR] Fill"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReGIR);
        regirFillPass.ReadBuffer(scene.sceneData);
        regirFillPass.ReadBuffer(scene.lightData);
        regirFillPass.ReadBuffer(lightsVS);
        regirFillPass.ReadBuffer(activeCells);
        regirFillPass.ReadBuffer(activeCount);
        regirFillPass.ReadIndirectBuffer(fillIndirect);
        regirFillPass.WriteBuffer(entries);
        regirFillPass.WriteBuffer(cellData);
        if (GPU_STATS_ENABLED) { regirFillPass.ReadWriteBuffer(scene.readback); }
        regirFillPass.Execute([pipelineManager, sceneIndex, &scene, lightsVS, activeCells, activeCount, entries, cellData, fillIndirect](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("regir_fill"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReGIRFillPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .lightVS = graph.GetBufferAddress(lightsVS),
                .activeCells = graph.GetBufferAddress(activeCells),
                .activeCount = graph.GetBufferAddress(activeCount),
                .entries = graph.GetBufferAddress(entries),
                .cellData = graph.GetBufferAddress(cellData),
                .gatherOverflow = GPU_STATS_ENABLED ? graph.GetBufferAddress(scene.readback) + offsetof(ReadbackStruct, regirGatherOverflow) : 0,
                .coneRejected = GPU_STATS_ENABLED ? graph.GetBufferAddress(scene.readback) + offsetof(ReadbackStruct, regirConeRejected) : 0,
                .sceneDataIndex = sceneIndex,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(fillIndirect), 0);
        });

    }

    const RDGBuffer regirHashEntries = restir.regirHashEntries;
    const RDGBuffer regirEntries = restir.regirEntries;
    const RDGBuffer regirCellData = restir.regirCellData;

    RDGBufferRing reservoirHistoryRing{};
    RDGBuffer reservoirTemporal{};
    RDGBuffer reservoirBase{};
    RDGTexture shadowVis{};
    RDGTexture signal{};
    RDGTexture gradient{};
    {
        if (bTemporalReuse) {
            reservoirTemporal = graph.CreateBuffer("restir_reservoir_temporal"_sid, reservoirBufferSize, true);
            reservoirHistoryRing = graph.CreateVersionedBuffer("restir_reservoir_history"_sid, reservoirBufferSize, 1, VersionSource::Emplaced);
            restir.reservoirTemporal = reservoirTemporal;
            restir.reservoirHistory = reservoirHistoryRing.Version(1);
        }

        const bool bHasHistory = reservoirHistoryRing.Version(1).IsValid() && targets.gbufferOneHistory.IsValid() && targets.depthCopyHistory.IsValid() && !bResetHistory;
        const RDGBuffer reservoirHistory = bHasHistory ? reservoirHistoryRing.Version(1) : RDGBuffer{};
        const RDGTexture gbufferOneHistory = bHasHistory ? targets.gbufferOneHistory : RDGTexture{};
        const RDGTexture depthHistory = bHasHistory ? targets.depthCopyHistory : RDGTexture{};
        RDGTextureRing shadowVisRing{};
        if (bShadowVis) {
            shadowVisRing = graph.CreateVersionedTexture("restir_shadow_vis"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
            shadowVis = shadowVisRing.Current();
        }
        const bool bHasPrevVis = bShadowVis && shadowVisRing.Version(1).IsValid();
        const RDGTexture prevShadowVis = bHasPrevVis ? shadowVisRing.Version(1) : RDGTexture{};
        if (bConfidence) {
            signal = graph.CreateTexture("restir_signal"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
            gradient = graph.CreateTexture("restir_gradient"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, gradientExtent.width, gradientExtent.height, 1}, {std::nullopt}, true);
        }

        // Last frame's TLAS re-shades the winner against last frame's occluders; the TLAS ring is one frame deep (BLAS lifetime).
        const bool bHasPrevTlas = bConfidence && scene.tlasHistory.IsValid();
        const RDGBuffer prevTlas = bHasPrevTlas ? scene.tlasHistory : RDGBuffer{};

        reservoirBase = graph.CreateBuffer("restir_reservoir_base"_sid, reservoirBufferSize, true);
        restir.reservoirBase = reservoirBase;
        RDGBuffer hitDescriptors{};
        if (reflectionRoughnessMax >= 0.0f) {
            hitDescriptors = graph.CreateBuffer(REFLECTION_HIT_DESCRIPTORS_BUFFER, reflectionBufferSize, true);
            reflection.hitDescriptors = hitDescriptors;

            RenderPass& reflClearPass = graph.AddPass("[Reflection] Clear Descriptors"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::ReflectionsDenoise);
            reflClearPass.WriteTransferBuffer(hitDescriptors);
            reflClearPass.Execute([hitDescriptors](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(hitDescriptors), 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
            });
        }

        // Base candidate generation
        RenderPass& basePass = graph.AddPass("[ReSTIR DI] Base"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        basePass.ReadBuffer(scene.sceneData);
        basePass.ReadBuffer(scene.lightData);
        basePass.ReadBuffer(lightsVS);
        if (bReGIRProposal) {
            basePass.ReadBuffer(regirHashEntries);
            basePass.ReadBuffer(regirEntries);
            basePass.ReadBuffer(regirCellData);
        } else if (bWorldGrid) {
            basePass.ReadBuffer(worldLightGrid);
            basePass.ReadBuffer(worldIndexList);
            basePass.ReadBuffer(worldEmissiveGrid);
            basePass.ReadBuffer(worldEmissiveIndexList);
            basePass.ReadBuffer(worldCellPower);
        }
        basePass.ReadBuffer(scene.instances);
        basePass.ReadSampledImage(targets.gbufferOne);
        basePass.ReadSampledImage(targets.gbufferTwo);
        basePass.ReadSampledImage(targets.shadowOriginOffset);
        basePass.ReadSampledImage(targets.depthCopy);
        if (bHasTLAS) { basePass.ReadTLASBuffer(scene.tlas); }
        basePass.WriteBuffer(reservoirBase);
        if (reflectionRoughnessMax >= 0.0f) { basePass.WriteBuffer(hitDescriptors); }
        basePass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, reflectionRoughnessMax, bReGIRProposal, bWorldGrid, field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, shadowOriginOffset = targets.shadowOriginOffset, depth = targets.depthCopy,
                lightsVS, regirHashEntries, regirEntries, regirCellData, reservoirBase, hitDescriptors, worldLightGrid, worldIndexList, worldEmissiveGrid, worldEmissiveIndexList, worldCellPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(bReGIRProposal ? "restir_di_base_regir"_sid : "restir_di_base_bin"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(scene.tlas) : ~0u;
            const bool bBin = !bReGIRProposal && bWorldGrid;

            ReSTIRDICombinedTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .lightVS = graph.GetBufferAddress(lightsVS),
                .instanceBuffer = graph.GetBufferAddress(scene.instances),
                .hashEntries = bReGIRProposal ? graph.GetBufferAddress(regirHashEntries) : 0,
                .entries = bReGIRProposal ? graph.GetBufferAddress(regirEntries) : 0,
                .cellData = bReGIRProposal ? graph.GetBufferAddress(regirCellData) : 0,
                .historyBuffer = 0,
                .genBuffer = 0,
                .outputBuffer = graph.GetBufferAddress(reservoirBase),
                .reflectionDescriptors = reflectionRoughnessMax >= 0.0f ? graph.GetBufferAddress(hitDescriptors) : 0,
                .worldGridBuffer = bBin ? graph.GetBufferAddress(worldLightGrid) : 0,
                .worldGridIndexList = bBin ? graph.GetBufferAddress(worldIndexList) : 0,
                .worldGridEmissiveGrid = bBin ? graph.GetBufferAddress(worldEmissiveGrid) : 0,
                .worldGridEmissiveIndexList = bBin ? graph.GetBufferAddress(worldEmissiveIndexList) : 0,
                .worldGridCellPower = bBin ? graph.GetBufferAddress(worldCellPower) : 0,
                .shadowOriginOffsetIndex = graph.GetSampledImageViewDescriptorIndex(shadowOriginOffset),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .prevGbufferOneIndex = ~0u,
                .prevDepthIndex = ~0u,
                .renderExtent = {renderExtent.width, renderExtent.height},
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
                .lightSpecularFromReflectionsMax = ComputeLightSpecularFromReflectionsMax(reflectionConfig),
                .mirrorRoughnessMax = reflectionConfig.mirrorRoughnessMax,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t strideX = (field != 0u) ? ((renderExtent.width + 1u) >> 1u) : renderExtent.width;
            const uint32_t groupsX = (strideX + 15) / 16;
            const uint32_t groupsY = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

        if (bTemporalReuse) {
            RenderPass& fusedPass = graph.AddPass("[ReSTIR DI] SpatioTemporal"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
            fusedPass.ReadBuffer(scene.sceneData);
            fusedPass.ReadBuffer(scene.lightData);
            fusedPass.ReadBuffer(lightsVS);
            fusedPass.ReadBuffer(reservoirBase);
            if (bHasHistory) { fusedPass.ReadBuffer(reservoirHistory); }
            fusedPass.ReadSampledImage(targets.gbufferOne);
            fusedPass.ReadSampledImage(targets.gbufferTwo);
            fusedPass.ReadSampledImage(targets.shadowOriginOffset);
            fusedPass.ReadSampledImage(targets.depthCopy);
            if (bHasHistory) { fusedPass.ReadSampledImage(gbufferOneHistory); }
            if (bHasHistory) { fusedPass.ReadSampledImage(depthHistory); }
            if (bHasPrevVis) { fusedPass.ReadSampledImage(prevShadowVis); }
            if (bHasTLAS) { fusedPass.ReadTLASBuffer(scene.tlas); }
            if (bHasPrevTlas) { fusedPass.ReadTLASBuffer(prevTlas); }
            fusedPass.WriteBuffer(reservoirTemporal);
            if (bShadowVis) { fusedPass.WriteStorageImage(shadowVis); }
            if (bConfidence) { fusedPass.WriteStorageImage(signal); }
            fusedPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasTLAS, bHasPrevTlas, prevTlas, bHasHistory, bConfidence, bShadowVis, bHasPrevVis, prevShadowVis, reservoirHistory, gbufferOneHistory, depthHistory, field = activeCheckerboardField, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, shadowOriginOffset = targets.shadowOriginOffset, depth = targets.depthCopy,
                    lightsVS, reservoirBase, reservoirTemporal, shadowVis, signal](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_di_spatiotemporal"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(scene.tlas) : ~0u;

                ReSTIRDISpatioTemporalPushConstant pc{
                    .sceneData = graph.GetBufferAddress(scene.sceneData),
                    .lightData = graph.GetBufferAddress(scene.lightData),
                    .lightVS = graph.GetBufferAddress(lightsVS),
                    .historyBuffer = bHasHistory ? graph.GetBufferAddress(reservoirHistory) : 0,
                    .genBuffer = graph.GetBufferAddress(reservoirBase),
                    .outputBuffer = graph.GetBufferAddress(reservoirTemporal),
                    .shadowOriginOffsetIndex = graph.GetSampledImageViewDescriptorIndex(shadowOriginOffset),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .prevGbufferOneIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0u,
                    .prevDepthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0u,
                    .renderExtent = {renderExtent.width, renderExtent.height},
                    .sceneDataIndex = sceneIndex,
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .mCap = restirParams.temporalMCap,
                    .spatialMCap = restirParams.spatialMCap,
                    .tlasIndex = tlasIndex,
                    .prevTlasIndex = bHasPrevTlas ? graph.GetAccelerationStructureDescriptorIndex(prevTlas) : ~0u,
                    .prevShadowVisIndex = bHasPrevVis ? graph.GetSampledImageViewDescriptorIndex(prevShadowVis) : ~0u,
                    .shadowVisIndex = bShadowVis ? graph.GetStorageImageViewDescriptorIndex(shadowVis) : ~0u,
                    .signalIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex(signal) : ~0u,
                    .spatialRadius = restirParams.spatialRadius,
                    .spatialNeighbors = restirParams.spatialNeighbors,
                    .bPermutationSampling = restirParams.bPermutationSampling ? 1u : 0u,
                    .bTemporalSearch = restirParams.bTemporalSearch ? 1u : 0u,
                    .bInitialVisibility = (tlasIndex != ~0u && restirParams.bInitialVisibility) ? 1u : 0u,
                    .antilagStrength = restirParams.antilagStrength,
                    .activeCheckerboardField = field,
                    .wClamp = restirParams.restirWClamp,
                    .lightSpecularFromReflectionsMax = ComputeLightSpecularFromReflectionsMax(reflectionConfig),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t strideX = (field != 0u) ? ((renderExtent.width + 1u) >> 1u) : renderExtent.width;
                const uint32_t groupsX = (strideX + 15) / 16;
                const uint32_t groupsY = (renderExtent.height + 15) / 16;
                vkCmdDispatch(cmd, groupsX, groupsY, 1);
            });
        }
    }

    RDGTexture sunFlip{};
    if (restirParams.bSunLight && viewFamily.directionalLight.bEnabled && viewFamily.directionalLight.intensity > 0.0f && bHasTLAS) {
        const uint32_t sunField = restirParams.bCheckerboardFullRateResolve ? 0u : activeCheckerboardField;
        // Packed like the reservoir buffers were: one texel per dispatched lane, so the checkerboard leaves no unwritten texels in the aliased target.
        const uint32_t sunVisWidth = (sunField != 0u) ? ((renderExtent.width + 1u) >> 1u) : renderExtent.width;
        const RDGTexture sunVis = graph.CreateTexture("restir_sun_vis"_sid, TextureInfo{VK_FORMAT_R32_UINT, sunVisWidth, renderExtent.height, 1}, {std::nullopt}, true);
        restir.sunVis = sunVis;
        const bool bHasPrevTlas = bSunFlip && scene.tlasHistory.IsValid();
        const RDGBuffer prevTlas = bHasPrevTlas ? scene.tlasHistory : RDGBuffer{};
        if (bSunFlip) {
            sunFlip = graph.CreateTexture("restir_sun_flip"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
        }

        RenderPass& sunPass = graph.AddPass("[ReSTIR DI] Sun"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        sunPass.ReadBuffer(scene.sceneData);
        sunPass.ReadBuffer(scene.lightData);
        sunPass.ReadBuffer(scene.instances);
        sunPass.ReadBuffer(scene.primitives);
        sunPass.ReadBuffer(scene.materials);
        sunPass.ReadBuffer(scene.indices);
        sunPass.ReadBuffer(scene.vertexAttributes);
        sunPass.ReadSampledImage(targets.gbufferOne);
        sunPass.ReadSampledImage(targets.gbufferTwo);
        sunPass.ReadSampledImage(targets.shadowOriginOffset);
        sunPass.ReadSampledImage(targets.depthCopy);
        sunPass.ReadTLASBuffer(scene.tlas);
        if (bHasPrevTlas) { sunPass.ReadTLASBuffer(prevTlas); }
        sunPass.WriteStorageImage(sunVis);
        if (bSunFlip) { sunPass.WriteStorageImage(sunFlip); }
        sunPass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, bHasPrevTlas, prevTlas, bSunFlip, field = sunField, bAlphaTest = viewFamily.sigmaParams.bAlphaTest, alphaTestMaxDistance = restirParams.sunAlphaTestMaxDistance, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, shadowOriginOffset = targets.shadowOriginOffset, depth = targets.depthCopy,
                sunVis, sunFlip](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_di_sun"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRDISunPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .instanceBuffer = graph.GetBufferAddress(scene.instances),
                .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
                .materialBuffer = graph.GetBufferAddress(scene.materials),
                .indexBuffer = graph.GetBufferAddress(scene.indices),
                .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .visIndex = graph.GetStorageImageViewDescriptorIndex(sunVis),
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
                .prevTlasIndex = bHasPrevTlas ? graph.GetAccelerationStructureDescriptorIndex(prevTlas) : ~0u,
                .flipIndex = bSunFlip ? graph.GetStorageImageViewDescriptorIndex(sunFlip) : ~0u,
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .activeCheckerboardField = field,
                .bAlphaTest = bAlphaTest ? 1u : 0u,
                .shadowOriginOffsetIndex = graph.GetSampledImageViewDescriptorIndex(shadowOriginOffset),
                .alphaTestMaxDistance = alphaTestMaxDistance,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t strideX = (field != 0u) ? ((renderExtent.width + 1u) >> 1u) : renderExtent.width;
            const uint32_t groupsX = (strideX + 15) / 16;
            const uint32_t groupsY = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
    }

    const bool bSunFlipReady = bSunFlip && sunFlip.IsValid();
    if (bConfidence || bSunFlipReady) {
        const RDGTextureRing confidenceRing = graph.CreateVersionedTexture("restir_confidence"_sid, TextureInfo{VK_FORMAT_R8_UNORM, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
        const RDGTexture confidence = confidenceRing.Current();
        const bool bHasPrevConfidence = confidenceRing.Version(1).IsValid();
        const RDGTexture prevConfidence = bHasPrevConfidence ? confidenceRing.Version(1) : RDGTexture{};
        restir.confidence = confidence;
        restir.confidenceHistory = prevConfidence;

        if (bConfidence) {
            RenderPass& gradientPass = graph.AddPass("[ReSTIR DI] Confidence Gradient"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
            gradientPass.ReadSampledImage(signal);
            gradientPass.WriteStorageImage(gradient);
            gradientPass.Execute([&, pipelineManager, renderExtent, gradientExtent, signal, gradient](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_confidence_gradient"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                ReSTIRConfidenceGradientPushConstant pc{
                    .renderExtent = {renderExtent.width, renderExtent.height},
                    .gradientExtent = {gradientExtent.width, gradientExtent.height},
                    .signalIndex = graph.GetSampledImageViewDescriptorIndex(signal),
                    .gradientIndex = graph.GetStorageImageViewDescriptorIndex(gradient),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (gradientExtent.width + 7) / 8, (gradientExtent.height + 7) / 8, 1);
            });
        }

        RenderPass& resolvePass = graph.AddPass("[ReSTIR DI] Confidence Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        if (bConfidence) { resolvePass.ReadSampledImage(gradient); }
        if (bHasPrevConfidence) { resolvePass.ReadSampledImage(prevConfidence); }
        if (bSunFlipReady) { resolvePass.ReadSampledImage(sunFlip); }
        resolvePass.ReadSampledImage(targets.gbufferOne);
        resolvePass.WriteStorageImage(confidence);
        resolvePass.Execute([&, pipelineManager, renderExtent, gradientExtent, bConfidence, bHasPrevConfidence, prevConfidence, bSunFlipReady, gbufferOne = targets.gbufferOne,
                confStrength = restirParams.confidenceStrength, sensitivity = restirParams.confidenceSensitivity, darknessBias = restirParams.confidenceDarknessBias * preExposure,
                blendFactor = 1.0f / (restirParams.confidenceHistoryLength + 1.0f), blurRadius = restirParams.confidenceBlurRadius, gradient, sunFlip, confidence](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_confidence_resolve"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                ReSTIRConfidenceResolvePushConstant pc{
                    .renderExtent = {renderExtent.width, renderExtent.height},
                    .gradientExtent = {gradientExtent.width, gradientExtent.height},
                    .gradientIndex = bConfidence ? graph.GetSampledImageViewDescriptorIndex(gradient) : ~0u,
                    .prevConfidenceIndex = bHasPrevConfidence ? graph.GetSampledImageViewDescriptorIndex(prevConfidence) : ~0u,
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .sunFlipIndex = bSunFlipReady ? graph.GetSampledImageViewDescriptorIndex(sunFlip) : ~0u,
                    .confidenceIndex = graph.GetStorageImageViewDescriptorIndex(confidence),
                    .confidenceStrength = confStrength,
                    .sensitivity = sensitivity,
                    .darknessBias = darknessBias,
                    .blendFactor = blendFactor,
                    .blurRadius = blurRadius,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (renderExtent.width + 7) / 8, (renderExtent.height + 7) / 8, 1);
            });
    }

    RDGBuffer reuseBuffer = bTemporalReuse ? reservoirTemporal : reservoirBase;

    if (restirParams.boilingFilterStrength > 0.0f) {
        const RDGBuffer boiled = graph.CreateBuffer("restir_reservoir_boiled"_sid, reservoirBufferSize, true);

        RenderPass& boilingPass = graph.AddPass("[ReSTIR DI] Boiling Filter"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
        boilingPass.ReadBuffer(reuseBuffer);
        boilingPass.WriteBuffer(boiled);
        boilingPass.Execute([&, pipelineManager, renderExtent, inBuffer = reuseBuffer, boiled, strength = restirParams.boilingFilterStrength, field = activeCheckerboardField](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("restir_boiling_filter"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRBoilingFilterPushConstant pc{
                .inputBuffer = graph.GetBufferAddress(inBuffer),
                .outputBuffer = graph.GetBufferAddress(boiled),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .strength = strength,
                .activeCheckerboardField = field,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t strideX = (field != 0u) ? ((renderExtent.width + 1u) >> 1u) : renderExtent.width;
            const uint32_t groupsX = (strideX + 15) / 16;
            const uint32_t groupsY = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

        reuseBuffer = boiled;
    }

    if (bTemporalReuse) { graph.EmplaceVersion(reservoirHistoryRing, reuseBuffer); }

    restir.reservoirFinal = reuseBuffer;
    return restir;
}

void SetupReSTIRLightingResolvePass(RenderGraph& graph,
                                    PipelineManager* pipelineManager,
                                    const Core::ViewFamily& viewFamily,
                                    Core::Extent2D renderExtent,
                                    const RenderTargets& targets,
                                    const SceneResources& scene,
                                    const GeometryFrame& geometry,
                                    const ReSTIRFrame& restir,
                                    const ReflectionFrame& reflection,
                                    uint32_t sceneIndex,
                                    uint64_t frameNumber,
                                    uint32_t activeCheckerboardField,
                                    uint32_t bCheckerboardPacked,
                                    uint32_t bFullRateResolve,
                                    const Core::ReflectionConfiguration& reflectionConfig)
{
    ZoneScoped;
    if (!scene.lightingBucketingDispatches.IsValid()) { return; }

    const RDGTexture specNoisy = reflection.specNoisy;
    const RDGBuffer reservoirFinal = restir.reservoirFinal;
    const RDGTexture sunVis = restir.sunVis;
    const RDGBuffer lightsVS = restir.lightsVS;
    const RDGBuffer tileList = geometry.lightingTileList;
    const bool bMergedReflections = reflectionConfig.bMergedDenoise && ComputeReflectionRoughnessMax(reflectionConfig) >= 0.0f && specNoisy.IsValid();

    RenderPass& lightingResolve = graph.AddPass("[ReSTIR DI] Lighting Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    lightingResolve.ReadBuffer(scene.sceneData);
    lightingResolve.ReadBuffer(scene.lightData);
    if (bMergedReflections) {
        lightingResolve.ReadSampledImage(specNoisy);
    }
    if (reservoirFinal.IsValid()) {
        lightingResolve.ReadBuffer(reservoirFinal);
    }
    if (sunVis.IsValid()) {
        lightingResolve.ReadSampledImage(sunVis);
    }
    if (lightsVS.IsValid()) {
        lightingResolve.ReadBuffer(lightsVS);
    }
    lightingResolve.ReadIndirectBuffer(scene.lightingBucketingDispatches);
    if (tileList.IsValid()) { lightingResolve.ReadBuffer(tileList); }
    lightingResolve.ReadBuffer(scene.instances);
    lightingResolve.ReadBuffer(scene.materials);
    lightingResolve.ReadSampledImage(targets.visibility);
    lightingResolve.ReadSampledImage(targets.gbufferOne);
    lightingResolve.ReadSampledImage(targets.gbufferTwo);
    lightingResolve.ReadSampledImage(targets.depthCopy);
    if (targets.shadows.IsValid()) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    lightingResolve.WriteStorageImage(targets.intermediateOne);
    lightingResolve.WriteStorageImage(targets.intermediateTwo);
    const RDGTexture diffuseRatio = targets.restirDiffuseRatio;
    const bool bDiffuseRatio = diffuseRatio.IsValid();
    if (bDiffuseRatio) {
        lightingResolve.WriteStorageImage(diffuseRatio);
    }
    lightingResolve.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent, bDiffuseRatio,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            diffuseOut = targets.intermediateOne, specularOut = targets.intermediateTwo, skyboxIndex = viewFamily.skyboxIndex,
            field = activeCheckerboardField, packed = bCheckerboardPacked, fullRate = bFullRateResolve,
            bMergedReflections, specNoisy, reservoirFinal, sunVis, lightsVS, tileList, diffuseRatio](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkDeviceAddress tileListAddress = graph.GetBufferAddress(tileList);

            for (const LightingPipelineInfo& entry : pipelineManager->GetLightingPipelines()) {
                if (!entry.id) { continue; }

                StringID shaderToUse = viewFamily.lightingShaderOverride ? viewFamily.lightingShaderOverride : entry.id;
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(shaderToUse);
                if (!pipelineEntry) { pipelineEntry = pipelineManager->GetPipelineEntry("default_unlit"_sid); }
                if (!pipelineEntry) { continue; }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                VisibilityLightingPushConstant pc{
                    .sceneData = graph.GetBufferAddress(scene.sceneData),
                    .lightData = graph.GetBufferAddress(scene.lightData),
                    .lightVS = graph.TryGetBufferAddress(lightsVS),
                    .tileListBuffer = tileListAddress,
                    .instanceBuffer = graph.GetBufferAddress(scene.instances),
                    .materialBuffer = graph.GetBufferAddress(scene.materials),
                    .reservoirBuffer = graph.TryGetBufferAddress(reservoirFinal),
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .shadowsIndex = shadows.IsValid() ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                    .skyboxIndex = skyboxIndex,
                    .primaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(diffuseOut),
                    .secondaryOutputImageIndex = graph.GetStorageImageViewDescriptorIndex(specularOut),
                    .sceneDataIndex = sceneIndex,
                    .lightingIndex = entry.index,
                    .renderExtent = {renderExtent.width, renderExtent.height},
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .activeCheckerboardField = field,
                    .bCheckerboardPacked = packed,
                    .reflectionIndex = bMergedReflections ? graph.GetSampledImageViewDescriptorIndex(specNoisy) : ~0x0u,
                    .bFullRateResolve = fullRate,
                    .lightSpecularFromReflectionsMax = ComputeLightSpecularFromReflectionsMax(reflectionConfig),
                    .diffuseRatioIndex = bDiffuseRatio ? graph.GetStorageImageViewDescriptorIndex(diffuseRatio) : ~0x0u,
                    .sunVisIndex = sunVis.IsValid() ? graph.GetSampledImageViewDescriptorIndex(sunVis) : ~0x0u,
                    .tileCapacity = BucketTileCapacity(renderExtent.width, renderExtent.height),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(scene.lightingBucketingDispatches), entry.index * sizeof(BucketDispatchParameters) + offsetof(BucketDispatchParameters, xDispatch));
            }
        });
}

void SetupReSTIRRemodulatePass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               Core::Extent2D renderExtent,
                               const RenderTargets& targets,
                               const SceneResources& scene,
                               const WorldGridFrame& worldGrid,
                               const DDGIFrame& ddgi,
                               const FinalGatherFrame& gather,
                               const ReflectionFrame& reflection,
                               uint32_t sceneIndex,
                               uint32_t outputMode,
                               float iblIntensity,
                               uint64_t frameNumber,
                               bool bDDGIApply,
                               const Core::ReflectionConfiguration& reflectionConfig,
                               uint32_t giGatherMode)
{
    ZoneScoped;
    const uint32_t width = renderExtent.width;
    const uint32_t height = renderExtent.height;
    const bool bDDGI = bDDGIApply && ddgi.cascades.IsValid();
    const bool bGIGather = giGatherMode != 0u && gather.resolved.IsValid();
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
    const RDGTexture reflectionTarget = reflection.specNoisy;
    const bool bReflectionMerged = reflectionConfig.bMergedDenoise && reflectionRoughnessMax >= 0.0f && reflectionTarget.IsValid();
    const bool bReflection = !bReflectionMerged && reflectionRoughnessMax >= 0.0f && reflectionTarget.IsValid();
    const RDGBuffer probeGrid = worldGrid.probeGrid;
    const RDGBuffer ddgiCascades = ddgi.cascades;
    const RDGTexture giResolved = gather.resolved;
    const RDGTexture giData = gather.data;
    const RDGTexture giSkyVis = gather.skyVisHistory;

    RenderPass& pass = graph.AddPass("[ReSTIR DI] Remodulate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReSTIRDI);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(scene.reflectionProbes);
    if (probeGrid.IsValid()) { pass.ReadBuffer(probeGrid); }
    pass.ReadSampledImage(targets.intermediateOne);
    pass.ReadSampledImage(targets.intermediateTwo);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (targets.shadows.IsValid()) {
        pass.ReadSampledImage(targets.shadows);
    }
    if (bDDGI) {
        pass.ReadBuffer(ddgi.cascades);
    }
    if (bReflection) {
        pass.ReadSampledImage(reflectionTarget);
    }
    if (bGIGather) {
        pass.ReadSampledImage(giResolved);
        pass.ReadSampledImage(giData);
        pass.ReadSampledImage(giSkyVis);
    }
    pass.WriteStorageImage(targets.colorOutput);
    const RDGTexture diffuseRatio = targets.restirDiffuseRatio;
    const RDGTexture screenDiffuse = targets.giScreenDiffuse;
    const bool bScreenDiffuse = diffuseRatio.IsValid() && screenDiffuse.IsValid();
    if (bScreenDiffuse) {
        pass.ReadSampledImage(diffuseRatio);
        pass.WriteStorageImage(screenDiffuse);
    }
    const int32_t skyboxIndex = viewFamily.skyboxIndex;
    const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
    const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
    pass.Execute([pipelineManager, sceneIndex, outputMode, width, height, skyboxIndex, iblIntensity, indirectIntensity = viewFamily.indirectIntensity, bDDGI, bReflection, bReflectionMerged, reflectionRoughnessMax, reflectionTarget, bGIGather, giGatherMode, reflectionProbeCount, bProbeBrute, bScreenDiffuse,
            diffuse = targets.intermediateOne, specular = targets.intermediateTwo,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows, output = targets.colorOutput,
            &scene, probeGrid, ddgiCascades, giResolved, giData, giSkyVis, diffuseRatio, screenDiffuse](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
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
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(ddgiCascades) : 0,
                .bDDGIApply = bDDGI ? 1u : 0u,
                .shadowsIndex = shadows.IsValid() ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                .reflectionIndex = bReflection ? graph.GetSampledImageViewDescriptorIndex(reflectionTarget) : ~0x0u,
                .reflectionRoughnessMax = reflectionRoughnessMax,
                .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giResolved) : ~0x0u,
                .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giData) : ~0x0u,
                .giGatherMode = bGIGather ? giGatherMode : 0u,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
                .worldGridProbeGrid = (!bProbeBrute && probeGrid.IsValid()) ? graph.GetBufferAddress(probeGrid) : 0,
                .bReflectionMerged = bReflectionMerged ? 1u : 0u,
                .diffuseRatioIndex = bScreenDiffuse ? graph.GetSampledImageViewDescriptorIndex(diffuseRatio) : ~0x0u,
                .screenDiffuseOutIndex = bScreenDiffuse ? graph.GetStorageImageViewDescriptorIndex(screenDiffuse) : ~0x0u,
                .skyVisIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giSkyVis) : ~0x0u,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("restir_remodulate"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
}
} // Render
