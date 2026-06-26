//
// Created by William on 2026-06-06.
//

#include "render/passes/restir_passes.h"

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
void SetupQuadSelectionPass(RenderGraph& graph,
                            PipelineManager* pipelineManager,
                            Core::Array<uint32_t, 2> renderExtent,
                            const RenderTargets& targets,
                            uint32_t sceneIndex,
                            uint64_t frameNumber)
{
    graph.CreateTexture(SID("quad_selection"), TextureInfo{VK_FORMAT_R32_UINT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass(SID("[ReSTIR DI] Quad Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.WriteStorageImage(SID("quad_selection"));
    pass.Execute([&, pipelineManager, sceneIndex, renderExtent, frameNumber, depth = targets.depthCopy, gbufferOne = targets.gbufferOne](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_quad_selection"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        QuadSelectionPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .normalIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .outIndex = graph.GetStorageImageViewDescriptorIndex(SID("quad_selection")),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .pixelScale = 2u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 7) / 8, (renderExtent[1] + 7) / 8, 1);
    });
}

void SetupReSTIRPasses(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex,
                       Core::Arena& arena,
                       uint64_t frameNumber,
                       const Core::ReSTIRParams& restirParams,
                       bool bUseReGIR)
{
    const uint32_t pixelCount = renderExtent[0] * renderExtent[1];
    const uint32_t pixelScale = restirParams.bHalfRes ? 2u : 1u;
    const uint32_t reservoirBufferSize = pixelCount * static_cast<uint32_t>(sizeof(Reservoir));

    const bool bHasTLAS = graph.HasBuffer(RT_TLAS_BUFFER);
    // tlasIndex (and the carried prev-TLAS index) resolve inside the Execute lambdas: the AS resource's physical + RT descriptor are assigned during Compile, after pass setup.
    // RELAX moving-shadow confidence
    const bool bConfidence = RESTIR_ENABLE_CONFIDENCE && restirParams.bEnableConfidence && restirParams.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX;
    // restir_shadow_vis is antilag-only
    const bool bAntilag = RESTIR_ENABLE_ANTILAG && restirParams.bEnableAntilag;
    const bool bShadowVis = bAntilag;

    // Temporal-gradient confidence runs at 1/GRAD_FACTOR of the half-res ReSTIR grid (must match GRAD_FACTOR in the confidence shaders).
    const uint32_t GRAD_FACTOR = 3u;
    const Core::Array<uint32_t, 2> gradientExtent = {(renderExtent[0] + GRAD_FACTOR - 1u) / GRAD_FACTOR, (renderExtent[1] + GRAD_FACTOR - 1u) / GRAD_FACTOR};

    // Transform all lights (area + sphere) to view space once; every ReSTIR pass and the resolve read this instead of transforming per pixel.
    graph.CreateBuffer(SID("restir_lights_vs"), MAX_LIGHTS * sizeof(LightVSData), true);

    RenderPass& transformPass = graph.AddPass(SID("[ReSTIR DI] Transform Lights"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    transformPass.ReadBuffer(SCENE_DATA_BUFFER);
    transformPass.ReadBuffer(SID("light_data"));
    transformPass.WriteBuffer(SID("restir_lights_vs"));
    transformPass.Execute([&, pipelineManager, sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_transform_lights"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ReSTIRTransformLightsPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (MAX_LIGHTS + 63) / 64, 1, 1);
    });

    // ReGIR hash grid is built only for the ReGIR-fed main pass; the plain ReSTIR pass never touches the grid.
    if (bUseReGIR) {
        const uint32_t fullW = renderExtent[0] * pixelScale;
        const uint32_t fullH = renderExtent[1] * pixelScale;
        const uint32_t entriesSize = REGIR_HASH_CAPACITY * static_cast<uint32_t>(sizeof(uint32_t));
        const uint32_t reservoirsSize = REGIR_HASH_CAPACITY * REGIR_RESERVOIRS_PER_CELL * static_cast<uint32_t>(sizeof(ReGIRReservoir));
        const uint32_t activeCellsSize = REGIR_HASH_CAPACITY * 4u * static_cast<uint32_t>(sizeof(int32_t));

        graph.CreateBuffer(SID("regir_hash_entries"), entriesSize, true);
        graph.CreateBuffer(SID("regir_hash_reservoirs"), reservoirsSize, true);
        graph.CreateBuffer(SID("regir_cell_data"), REGIR_HASH_CAPACITY * 2u * static_cast<uint32_t>(sizeof(float)), true);
        graph.CreateBuffer(SID("regir_active_cells"), activeCellsSize, false);
        graph.CreateBuffer(SID("regir_active_count"), static_cast<uint32_t>(sizeof(uint32_t)), false);
        graph.CreateBuffer(SID("regir_fill_indirect"), 3u * static_cast<uint32_t>(sizeof(uint32_t)), false);

        const bool bHasPrev = !restirParams.bResetReGIR && graph.HasBuffer(SID("regir_hash_entries_prev")) && graph.HasBuffer(SID("regir_hash_reservoirs_prev"));

        RenderPass& clearPass = graph.AddPass(SID("[ReGIR] Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, ResourceCategory::ReSTIR);
        clearPass.WriteTransferBuffer(SID("regir_hash_entries"));
        clearPass.WriteTransferBuffer(SID("regir_active_count"));
        clearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("regir_hash_entries")), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("regir_active_count")), 0, VK_WHOLE_SIZE, 0);
        });

        RenderPass& touchPass = graph.AddPass(SID("[ReGIR] Touch"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
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

        RenderPass& indirectPass = graph.AddPass(SID("[ReGIR] Build Indirect"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
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

        const uint32_t fillBvhLightCount = static_cast<uint32_t>(viewFamily.lights.Size());
        const uint32_t fillBvhNumLeaves = fillBvhLightCount > 0u ? static_cast<uint32_t>(NextPowerOfTwo(fillBvhLightCount)) : 0u;

        RenderPass& regirFillPass = graph.AddPass(SID("[ReGIR] Fill"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        regirFillPass.ReadBuffer(SCENE_DATA_BUFFER);
        regirFillPass.ReadBuffer(SID("light_data"));
        regirFillPass.ReadBuffer(SID("restir_lights_vs"));
        regirFillPass.ReadBuffer(SID("light_bvh"));
        regirFillPass.ReadBuffer(SID("regir_active_cells"));
        regirFillPass.ReadBuffer(SID("regir_active_count"));
        if (bHasPrev) {
            regirFillPass.ReadBuffer(SID("regir_hash_entries_prev"));
            regirFillPass.ReadBuffer(SID("regir_hash_reservoirs_prev"));
        }
        regirFillPass.ReadIndirectBuffer(SID("regir_fill_indirect"));
        regirFillPass.WriteBuffer(SID("regir_hash_reservoirs"));
        regirFillPass.WriteBuffer(SID("regir_cell_data"));
        regirFillPass.Execute([&, pipelineManager, sceneIndex, frameNumber, bHasPrev, fillBvhNumLeaves](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .bvhNodes = graph.GetBufferAddress(SID("light_bvh")),
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .bHasPrev = bHasPrev ? 1u : 0u,
                .bvhNumLeaves = fillBvhNumLeaves,
                .wClamp = restirParams.regirWClamp,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(SID("regir_fill_indirect")), 0);
        });

        if (!restirParams.bResetReGIR) {
            graph.CarryBufferToNextFrame(SID("regir_hash_entries"), SID("regir_hash_entries_prev"), 0);
            graph.CarryBufferToNextFrame(SID("regir_hash_reservoirs"), SID("regir_hash_reservoirs_prev"), 0);
        }
    } // if (bUseReGIR)

    {
        static constexpr bool SPLIT_CANDIDATE_GENERATION = true;

        graph.CreateBuffer(SID("restir_reservoir_temporal"), reservoirBufferSize, true);

        const bool bHasHistory = graph.HasBuffer(SID("restir_reservoir_history"));
        const bool bHasQuadHistory = restirParams.bHalfRes && graph.HasTexture(SID("quad_selection_history"));
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

        const uint32_t bvhLightCount = static_cast<uint32_t>(viewFamily.lights.Size());
        const uint32_t bvhNumLeaves = bvhLightCount > 0u ? static_cast<uint32_t>(NextPowerOfTwo(bvhLightCount)) : 0u;
        const bool bHasPrevTlas = bConfidence && graph.HasBuffer(SID("rt_tlas_history"));

        if (SPLIT_CANDIDATE_GENERATION) {
            graph.CreateBuffer(SID("restir_reservoir_base"), reservoirBufferSize, true);

            // Base candidate generation
            RenderPass& basePass = graph.AddPass(SID("[ReSTIR DI] Base"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
            basePass.ReadBuffer(SCENE_DATA_BUFFER);
            basePass.ReadBuffer(SID("light_data"));
            basePass.ReadBuffer(SID("restir_lights_vs"));
            basePass.ReadBuffer(SID("light_bvh"));
            if (bUseReGIR) {
                basePass.ReadBuffer(SID("regir_hash_entries"));
                basePass.ReadBuffer(SID("regir_hash_reservoirs"));
                basePass.ReadBuffer(SID("regir_cell_data"));
            }
            basePass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            basePass.ReadSampledImage(targets.gbufferOne);
            basePass.ReadSampledImage(targets.gbufferTwo);
            basePass.ReadSampledImage(targets.depthCopy);
            if (restirParams.bHalfRes) { basePass.ReadSampledImage(SID("quad_selection")); }
            if (bHasTLAS) { basePass.ReadTLASBuffer(RT_TLAS_BUFFER); }
            basePass.WriteBuffer(SID("restir_reservoir_base"));
            basePass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, bHasTLAS, bUseReGIR, bvhNumLeaves, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(bUseReGIR ? SID("restir_di_base_regir") : SID("restir_di_base"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;

                ReSTIRDICombinedTemporalPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .hashEntries = bUseReGIR ? graph.GetBufferAddress(SID("regir_hash_entries")) : 0,
                    .reservoirs = bUseReGIR ? graph.GetBufferAddress(SID("regir_hash_reservoirs")) : 0,
                    .cellData = bUseReGIR ? graph.GetBufferAddress(SID("regir_cell_data")) : 0,
                    .historyBuffer = 0,
                    .genBuffer = 0,
                    .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_base")),
                    .bvhNodes = graph.GetBufferAddress(SID("light_bvh")),
                    .lightLeaf = graph.GetBufferAddress(SID("light_bvh")) + static_cast<VkDeviceAddress>(bvhNumLeaves > 0u ? 2u * bvhNumLeaves - 1u : 0u) * sizeof(LightBVHNode),
                    .visibilityBufferIndex = ~0u,
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .prevGbufferOneIndex = ~0u,
                    .prevDepthIndex = ~0u,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .sceneDataIndex = sceneIndex,
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .mCap = 0u,
                    .pixelScale = pixelScale,
                    .tlasIndex = tlasIndex,
                    .prevTlasIndex = ~0u,
                    .prevShadowVisIndex = ~0u,
                    .shadowVisIndex = ~0u,
                    .confidenceIndex = ~0u,
                    .signalIndex = ~0u,
                    .confidenceStrength = 0.0f,
                    .bPermutationSampling = 0u,
                    .antilagStrength = 0.0f,
                    .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
                    .quadSelectionHistoryIndex = ~0u,
                    .bInitialVisibility = (tlasIndex != ~0u && restirParams.bInitialVisibility) ? 1u : 0u,
                    .bvhNumLeaves = bvhNumLeaves,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t groupsX = (renderExtent[0] + 15) / 16;
                const uint32_t groupsY = (renderExtent[1] + 15) / 16;
                vkCmdDispatch(cmd, groupsX, groupsY, 1);
            });

            // Temporal reuse
            RenderPass& temporalPass = graph.AddPass(SID("[ReSTIR DI] Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
            temporalPass.ReadBuffer(SCENE_DATA_BUFFER);
            temporalPass.ReadBuffer(SID("light_data"));
            temporalPass.ReadBuffer(SID("restir_lights_vs"));
            temporalPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            temporalPass.ReadBuffer(SID("restir_reservoir_base"));
            if (bHasHistory) { temporalPass.ReadBuffer(SID("restir_reservoir_history")); }
            temporalPass.ReadSampledImage(targets.gbufferOne);
            temporalPass.ReadSampledImage(targets.gbufferTwo);
            temporalPass.ReadSampledImage(targets.depthCopy);
            if (bHasHistory) { temporalPass.ReadSampledImage(SID("gbuffer_one_history")); }
            if (bHasHistory) { temporalPass.ReadSampledImage(SID("depth_history")); }
            if (bHasPrevVis) { temporalPass.ReadSampledImage(SID("restir_shadow_vis_prev")); }
            if (restirParams.bHalfRes) { temporalPass.ReadSampledImage(SID("quad_selection")); }
            if (bHasQuadHistory) { temporalPass.ReadSampledImage(SID("quad_selection_history")); }
            if (bHasTLAS) { temporalPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
            if (bHasPrevTlas) { temporalPass.ReadTLASBuffer(SID("rt_tlas_history")); }
            temporalPass.WriteBuffer(SID("restir_reservoir_temporal"));
            if (bShadowVis) { temporalPass.WriteStorageImage(SID("restir_shadow_vis")); }
            if (bConfidence) { temporalPass.WriteStorageImage(SID("restir_signal")); }
            temporalPass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, bHasTLAS, bHasPrevTlas, bHasHistory, bHasQuadHistory, bConfidence, bShadowVis, bHasPrevVis, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_temporal"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;
                const uint32_t prevTlasIndex = bHasPrevTlas ? graph.GetAccelerationStructureDescriptorIndex(SID("rt_tlas_history")) : ~0u;

                ReSTIRDICombinedTemporalPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .hashEntries = 0,
                    .reservoirs = 0,
                    .cellData = 0,
                    .historyBuffer = bHasHistory ? graph.GetBufferAddress(SID("restir_reservoir_history")) : 0,
                    .genBuffer = graph.GetBufferAddress(SID("restir_reservoir_base")),
                    .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
                    .bvhNodes = 0,
                    .lightLeaf = 0,
                    .visibilityBufferIndex = ~0u,
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .prevGbufferOneIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : ~0u,
                    .prevDepthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : ~0u,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .sceneDataIndex = sceneIndex,
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .mCap = restirParams.temporalMCap,
                    .pixelScale = pixelScale,
                    .tlasIndex = tlasIndex,
                    .prevTlasIndex = prevTlasIndex,
                    .prevShadowVisIndex = bHasPrevVis ? graph.GetSampledImageViewDescriptorIndex(SID("restir_shadow_vis_prev")) : ~0u,
                    .shadowVisIndex = bShadowVis ? graph.GetStorageImageViewDescriptorIndex(SID("restir_shadow_vis")) : ~0u,
                    .confidenceIndex = ~0u,
                    .signalIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex(SID("restir_signal")) : ~0u,
                    .confidenceStrength = restirParams.confidenceStrength,
                    .bPermutationSampling = restirParams.bPermutationSampling ? 1u : 0u,
                    .antilagStrength = restirParams.antilagStrength,
                    .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
                    .quadSelectionHistoryIndex = (pixelScale == 2u) ? (bHasQuadHistory ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection_history")) : graph.GetSampledImageViewDescriptorIndex(SID("quad_selection"))) : ~0u,
                    .bInitialVisibility = (tlasIndex != ~0u && restirParams.bInitialVisibility) ? 1u : 0u,
                    .bvhNumLeaves = 0u,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t groupsX = (renderExtent[0] + 15) / 16;
                const uint32_t groupsY = (renderExtent[1] + 15) / 16;
                vkCmdDispatch(cmd, groupsX, groupsY, 1);
            });
        }
        else {
            RenderPass& combinedPass = graph.AddPass(SID("[ReSTIR DI] Combined Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
            combinedPass.ReadBuffer(SCENE_DATA_BUFFER);
            combinedPass.ReadBuffer(SID("light_data"));
            combinedPass.ReadBuffer(SID("restir_lights_vs"));
            combinedPass.ReadBuffer(SID("light_bvh"));
            if (bUseReGIR) {
                combinedPass.ReadBuffer(SID("regir_hash_entries"));
                combinedPass.ReadBuffer(SID("regir_hash_reservoirs"));
                combinedPass.ReadBuffer(SID("regir_cell_data"));
            }
            combinedPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            if (bHasHistory) { combinedPass.ReadBuffer(SID("restir_reservoir_history")); }
            combinedPass.ReadSampledImage(targets.gbufferOne);
            combinedPass.ReadSampledImage(targets.gbufferTwo);
            combinedPass.ReadSampledImage(targets.depthCopy);
            if (bHasHistory) { combinedPass.ReadSampledImage(SID("gbuffer_one_history")); }
            if (bHasHistory) { combinedPass.ReadSampledImage(SID("depth_history")); }
            if (bHasPrevVis) { combinedPass.ReadSampledImage(SID("restir_shadow_vis_prev")); }
            if (restirParams.bHalfRes) { combinedPass.ReadSampledImage(SID("quad_selection")); }
            if (bHasQuadHistory) { combinedPass.ReadSampledImage(SID("quad_selection_history")); }
            if (bHasTLAS) { combinedPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
            if (bHasPrevTlas) { combinedPass.ReadTLASBuffer(SID("rt_tlas_history")); }
            combinedPass.WriteBuffer(SID("restir_reservoir_temporal"));
            if (bShadowVis) { combinedPass.WriteStorageImage(SID("restir_shadow_vis")); }
            if (bConfidence) { combinedPass.WriteStorageImage(SID("restir_signal")); }
            combinedPass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, bHasTLAS, bHasPrevTlas, bUseReGIR, bHasHistory, bHasQuadHistory, bConfidence, bShadowVis, bHasPrevVis, bvhNumLeaves, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(bUseReGIR ? SID("restir_di_combined_temporal_regir") : SID("restir_di_combined_temporal"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;
                const uint32_t prevTlasIndex = bHasPrevTlas ? graph.GetAccelerationStructureDescriptorIndex(SID("rt_tlas_history")) : ~0u;

                ReSTIRDICombinedTemporalPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .hashEntries = bUseReGIR ? graph.GetBufferAddress(SID("regir_hash_entries")) : 0,
                    .reservoirs = bUseReGIR ? graph.GetBufferAddress(SID("regir_hash_reservoirs")) : 0,
                    .cellData = bUseReGIR ? graph.GetBufferAddress(SID("regir_cell_data")) : 0,
                    .historyBuffer = bHasHistory ? graph.GetBufferAddress(SID("restir_reservoir_history")) : 0,
                    .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
                    .bvhNodes = graph.GetBufferAddress(SID("light_bvh")),
                    .lightLeaf = graph.GetBufferAddress(SID("light_bvh")) + static_cast<VkDeviceAddress>(bvhNumLeaves > 0u ? 2u * bvhNumLeaves - 1u : 0u) * sizeof(LightBVHNode),
                    .visibilityBufferIndex = ~0u,
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .prevGbufferOneIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : ~0u,
                    .prevDepthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : ~0u,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .sceneDataIndex = sceneIndex,
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .mCap = restirParams.temporalMCap,
                    .pixelScale = pixelScale,
                    .tlasIndex = tlasIndex,
                    .prevTlasIndex = prevTlasIndex,
                    .prevShadowVisIndex = bHasPrevVis ? graph.GetSampledImageViewDescriptorIndex(SID("restir_shadow_vis_prev")) : ~0u,
                    .shadowVisIndex = bShadowVis ? graph.GetStorageImageViewDescriptorIndex(SID("restir_shadow_vis")) : ~0u,
                    .confidenceIndex = ~0u,
                    .signalIndex = bConfidence ? graph.GetStorageImageViewDescriptorIndex(SID("restir_signal")) : ~0u,
                    .confidenceStrength = restirParams.confidenceStrength,
                    .bPermutationSampling = restirParams.bPermutationSampling ? 1u : 0u,
                    .antilagStrength = restirParams.antilagStrength,
                    .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
                    .quadSelectionHistoryIndex = (pixelScale == 2u) ? (bHasQuadHistory ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection_history")) : graph.GetSampledImageViewDescriptorIndex(SID("quad_selection"))) : ~0u,
                    .bInitialVisibility = (tlasIndex != ~0u && restirParams.bInitialVisibility) ? 1u : 0u,
                    .bvhNumLeaves = bvhNumLeaves,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t groupsX = (renderExtent[0] + 15) / 16;
                const uint32_t groupsY = (renderExtent[1] + 15) / 16;
                vkCmdDispatch(cmd, groupsX, groupsY, 1);
            });
        }
    }

    // Temporal-gradient antilag confidence: gradient (stratum mean of the re-shade signal) -> resolve (blur + convert + asymmetric temporal) -> restir_confidence, consumed by RELAX.
    if (bConfidence) {
        const bool bHasPrevConfidence = graph.HasTexture(SID("restir_confidence_prev"));

        RenderPass& gradientPass = graph.AddPass(SID("[ReSTIR DI] Confidence Gradient"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
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

        RenderPass& resolvePass = graph.AddPass(SID("[ReSTIR DI] Confidence Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        resolvePass.ReadSampledImage(SID("restir_gradient"));
        if (bHasPrevConfidence) { resolvePass.ReadSampledImage(SID("restir_confidence_prev")); }
        resolvePass.ReadSampledImage(targets.gbufferOne);
        if (restirParams.bHalfRes) { resolvePass.ReadSampledImage(SID("quad_selection")); }
        resolvePass.WriteStorageImage(SID("restir_confidence"));
        resolvePass.Execute([&, pipelineManager, renderExtent, gradientExtent, pixelScale, bHasPrevConfidence, gbufferOne = targets.gbufferOne,
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
                    .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
                    .confidenceIndex = graph.GetStorageImageViewDescriptorIndex(SID("restir_confidence")),
                    .pixelScale = pixelScale,
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

    StringID reuseBuffer = SID("restir_reservoir_temporal");

    if (restirParams.boilingFilterStrength > 0.0f) {
        graph.CreateBuffer(SID("restir_reservoir_boiled"), reservoirBufferSize, true);

        RenderPass& boilingPass = graph.AddPass(SID("[ReSTIR DI] Boiling Filter"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        boilingPass.ReadBuffer(SID("restir_reservoir_temporal"));
        boilingPass.WriteBuffer(SID("restir_reservoir_boiled"));
        boilingPass.Execute([&, pipelineManager, renderExtent, strength = restirParams.boilingFilterStrength](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_boiling_filter"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            ReSTIRBoilingFilterPushConstant pc{
                .inputBuffer = graph.GetBufferAddress(SID("restir_reservoir_temporal")),
                .outputBuffer = graph.GetBufferAddress(SID("restir_reservoir_boiled")),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .strength = strength,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
            const uint32_t groupsY = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

        reuseBuffer = SID("restir_reservoir_boiled");
    }

    graph.CarryBufferToNextFrame(reuseBuffer, SID("restir_reservoir_history"), 0);
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

        RenderPass& spatialPass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        spatialPass.ReadBuffer(SCENE_DATA_BUFFER);
        spatialPass.ReadBuffer(SID("light_data"));
        spatialPass.ReadBuffer(SID("restir_lights_vs"));
        spatialPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        spatialPass.ReadBuffer(inputName);
        spatialPass.ReadSampledImage(targets.gbufferOne);
        spatialPass.ReadSampledImage(targets.gbufferTwo);
        spatialPass.ReadSampledImage(targets.depthCopy);
        if (restirParams.bHalfRes) { spatialPass.ReadSampledImage(SID("quad_selection")); }
        if (bHasTLAS) { spatialPass.ReadTLASBuffer(RT_TLAS_BUFFER); }
        spatialPass.WriteBuffer(outputName);
        spatialPass.Execute([&, pipelineManager, sceneIndex, renderExtent, pixelScale, frameNumber, bHasTLAS, inputName, outputName, passIndex = i, bLastPass = (i == spatialPasses - 1u), gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("restir_di_spatial"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            const uint32_t tlasIndex = bHasTLAS ? graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER) : ~0u;

            ReSTIRDISpatialPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .lightVS = graph.GetBufferAddress(SID("restir_lights_vs")),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .inputBuffer = graph.GetBufferAddress(inputName),
                .outputBuffer = graph.GetBufferAddress(outputName),
                .visibilityBufferIndex = ~0u,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .renderExtent = {renderExtent[0], renderExtent[1]},
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .spatialRadius = restirParams.spatialRadius,
                .spatialNeighbors = restirParams.spatialNeighbors,
                .mCap = restirParams.spatialMCap,
                .pixelScale = pixelScale,
                .tlasIndex = tlasIndex,
                .passIndex = passIndex,
                .bAdaptiveSpatial = restirParams.bAdaptiveSpatial ? 1u : 0u,
                .adaptiveSpatialBoost = restirParams.adaptiveSpatialBoost,
                .adaptiveMReference = restirParams.temporalMCap,
                .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
                .bValidateVisibility = bLastPass ? 1u : 0u,
                .wClamp = restirParams.restirWClamp,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent[0] + 15) / 16;
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
                                    Core::Arena& arena,
                                    uint64_t frameNumber,
                                    uint32_t pixelScale)
{
    if (!graph.HasBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER)) { return; }

    struct LightingEntry
    {
        uint32_t bucketIndex{};
        StringID lightingShader;
    };

    const auto lightingCount = static_cast<uint32_t>(viewFamily.lightingBuckets.Size());
    auto buckets = arena.AllocArray<LightingEntry>(lightingCount);
    uint32_t idx = 0;
    for (const auto& [shader, bucketIndex] : viewFamily.lightingBuckets) {
        buckets[idx++] = {bucketIndex, shader};
    }

    RenderPass& lightingResolve = graph.AddPass(SID("[ReSTIR DI] Lighting Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    lightingResolve.ReadBuffer(SCENE_DATA_BUFFER);
    lightingResolve.ReadBuffer(SID("light_data"));
    if (graph.HasBuffer(SID("restir_reservoir_final"))) {
        lightingResolve.ReadBuffer(SID("restir_reservoir_final"));
    }
    if (graph.HasBuffer(SID("restir_lights_vs"))) {
        lightingResolve.ReadBuffer(SID("restir_lights_vs"));
    }
    lightingResolve.ReadIndirectBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    lightingResolve.ReadSampledImage(targets.gbufferOne);
    lightingResolve.ReadSampledImage(targets.gbufferTwo);
    lightingResolve.ReadSampledImage(targets.depthCopy);
    if (pixelScale == 2u) { lightingResolve.ReadSampledImage(SID("quad_selection")); }
    if (targets.shadows != StringID{}) {
        lightingResolve.ReadSampledImage(targets.shadows);
    }
    lightingResolve.WriteStorageImage(targets.intermediateOne);
    lightingResolve.WriteStorageImage(targets.intermediateTwo);
    lightingResolve.Execute([&, pipelineManager, sceneIndex, frameNumber, renderExtent, pixelScale,
            visibility = targets.visibility, gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, shadows = targets.shadows,
            diffuseOut = targets.intermediateOne, specularOut = targets.intermediateTwo, skyboxIndex = viewFamily.skyboxIndex,
            buckets, lightingCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkDeviceAddress lightDispatchAddress = graph.GetBufferAddress(LIGHTING_DISPATCH_BUCKETING_BUFFER);

            for (uint32_t i = 0; i < lightingCount; ++i) {
                const LightingEntry& entry = buckets[i];
                if (!entry.lightingShader) { continue; }

                StringID shaderToUse = viewFamily.lightingShaderOverride ? viewFamily.lightingShaderOverride : entry.lightingShader;
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(shaderToUse);
                if (!pipelineEntry) { continue; }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                VisibilityLightingPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .lightData = graph.GetBufferAddress(SID("light_data")),
                    .lightVS = graph.TryGetBufferAddress(SID("restir_lights_vs")),
                    .lightDispatchBuffer = lightDispatchAddress,
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
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
                    .lightingIndex = entry.bucketIndex,
                    .renderExtent = {renderExtent[0], renderExtent[1]},
                    .frameIndex = static_cast<uint32_t>(frameNumber),
                    .pixelScale = pixelScale,
                    .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                                      entry.bucketIndex * sizeof(LightingDispatchParameters) + offsetof(LightingDispatchParameters, xDispatch));
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
                               uint32_t pixelScale,
                               float iblIntensity,
                               uint64_t frameNumber)
{
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];

    RenderPass& pass = graph.AddPass(SID("[ReSTIR DI] Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.intermediateOne);
    pass.ReadSampledImage(targets.intermediateTwo);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.ReadSampledImage(targets.depthCopy);
    if (pixelScale == 2u) { pass.ReadSampledImage(SID("quad_selection")); }
    pass.WriteStorageImage(targets.colorOutput);
    const int32_t skyboxIndex = viewFamily.skyboxIndex;
    pass.Execute([pipelineManager, sceneIndex, outputMode, pixelScale, width, height, skyboxIndex, iblIntensity, frameNumber,
            diffuse = targets.intermediateOne, specular = targets.intermediateTwo,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthCopy, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
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
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .pixelScale = pixelScale,
                .skyboxIndex = skyboxIndex,
                .iblIntensity = iblIntensity,
                .quadSelectionIndex = (pixelScale == 2u) ? graph.GetSampledImageViewDescriptorIndex(SID("quad_selection")) : ~0u,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("restir_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
}
} // Render
