//
// Created by William on 2026-04-16.
//

#include "renderer.h"

#include "render_utils.h"
#include "pipelines/pipeline_data.h"
#include "pipelines/pipeline_manager.h"
#include "render-graph/render_pass.h"
#include "vulkan/vk_helpers.h"
#include "vulkan/vk_config.h"
#include "post-processing/post_processing.h"

namespace Render
{
void SetupGeometryPass(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       const RenderFamilyProperties& renderFamilyProperties,
                       Core::Array<uint32_t, 2> renderExtent,
                       const VisibilityBufferTargets& targets,
                       uint32_t sceneIndex)
{
    if (viewFamily.mainPassInstances.IsEmpty()) {
        return;
    }

    const StringID instanceMeshletOffsets = SID("instance_meshlet_offsets");
    const StringID level1Sums = SID("level1_sums");
    const StringID level1BlockSums = SID("level1_block_sums");
    const StringID level2Sums = SID("level2_sums");
    const StringID level2BlockSums = SID("level2_block_sums");
    const StringID scannedLevel2BlockSums = SID("scanned_level2_block_sums");
    const StringID intermediateMeshlets = SID("intermediate_meshlets");
    const StringID meshletLevel1Sums = SID("meshlet_level1_sums");
    const StringID meshletLevel1BlockSums = SID("meshlet_level1_block_sums");
    const StringID meshletLevel2Sums = SID("meshlet_level2_sums");
    const StringID meshletLevel2BlockSums = SID("meshlet_level2_block_sums");
    const StringID meshletScannedLevel2BlockSums = SID("meshlet_scanned_level2_block_sums");
    const StringID visibleMeshlets = SID("visible_meshlets");
    const StringID meshletCountDispatchArgs = SID("meshlet_count_dispatch_args");
    const StringID compactedMeshletDispatchArgs = SID("compacted_meshlet_dispatch_args");
    auto instanceCount = static_cast<uint32_t>(viewFamily.mainPassInstances.Size());
    auto lodBias = static_cast<int32_t>(LOD_BIAS);
    auto highestMeshletCount = renderFamilyProperties.visibleMeshletUpperBound;


    // Instancing
    {
        // Create and Clear
        {
            graph.CreateBuffer(instanceMeshletOffsets, renderFamilyProperties.instanceMeshletOffsetsBufferSize, false);
            graph.CreateBuffer(level1Sums, renderFamilyProperties.level1SumsBufferSize, false);
            graph.CreateBuffer(level1BlockSums, renderFamilyProperties.level1BlockSumsBufferSize, false);
            graph.CreateBuffer(level2Sums, renderFamilyProperties.level2SumsBufferSize, false);
            graph.CreateBuffer(level2BlockSums, renderFamilyProperties.level2BlockSumsBufferSize, false);
            graph.CreateBuffer(scannedLevel2BlockSums, renderFamilyProperties.scannedLevel2BlockSumsBufferSize, false);
            graph.CreateBuffer(intermediateMeshlets, renderFamilyProperties.intermediateMeshletBufferSize, false);
            graph.CreateBuffer(meshletLevel1Sums, renderFamilyProperties.meshletLevel1SumsBufferSize, false);
            graph.CreateBuffer(meshletLevel1BlockSums, renderFamilyProperties.meshletLevel1BlockSumsBufferSize, false);
            graph.CreateBuffer(meshletLevel2Sums, renderFamilyProperties.meshletLevel2SumsBufferSize, false);
            graph.CreateBuffer(meshletLevel2BlockSums, renderFamilyProperties.meshletLevel2BlockSumsBufferSize, false);
            graph.CreateBuffer(meshletScannedLevel2BlockSums, renderFamilyProperties.meshletScannedLevel2BlockSumsBufferSize, false);
            graph.CreateBuffer(visibleMeshlets, renderFamilyProperties.visibleMeshletsBufferSize, false);
            graph.CreateBuffer(meshletCountDispatchArgs, sizeof(InstancingMeshletDispatchIndirect), false);
            graph.CreateBuffer(compactedMeshletDispatchArgs, sizeof(InstancingCompactedMeshletDispatchIndirect), false);

            RenderPass& clearPass = graph.AddPass(SID("Clear Temp Instancing Buffers"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
            clearPass.WriteTransferBuffer(instanceMeshletOffsets);
            clearPass.WriteTransferBuffer(level1Sums);
            clearPass.WriteTransferBuffer(level1BlockSums);
            clearPass.WriteTransferBuffer(level2Sums);
            clearPass.WriteTransferBuffer(level2BlockSums);
            clearPass.WriteTransferBuffer(scannedLevel2BlockSums);
            clearPass.WriteTransferBuffer(intermediateMeshlets);
            clearPass.WriteTransferBuffer(meshletLevel1Sums);
            clearPass.WriteTransferBuffer(meshletLevel1BlockSums);
            clearPass.WriteTransferBuffer(meshletLevel2Sums);
            clearPass.WriteTransferBuffer(meshletLevel2BlockSums);
            clearPass.WriteTransferBuffer(meshletScannedLevel2BlockSums);
            clearPass.WriteTransferBuffer(visibleMeshlets);
            clearPass.WriteTransferBuffer(meshletCountDispatchArgs);
            clearPass.WriteTransferBuffer(compactedMeshletDispatchArgs);
            clearPass.Execute([instanceMeshletOffsets, level1Sums, level1BlockSums, level2Sums, level2BlockSums,
                    scannedLevel2BlockSums, intermediateMeshlets, meshletLevel1Sums, meshletLevel1BlockSums,
                    meshletLevel2Sums, meshletLevel2BlockSums, meshletScannedLevel2BlockSums,
                    visibleMeshlets, meshletCountDispatchArgs, compactedMeshletDispatchArgs, &graph](VkCommandBuffer cmd) {
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(instanceMeshletOffsets), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(level1Sums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(level1BlockSums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(level2Sums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(level2BlockSums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(scannedLevel2BlockSums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(intermediateMeshlets), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshletLevel1Sums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshletLevel1BlockSums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshletLevel2Sums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshletLevel2BlockSums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshletScannedLevel2BlockSums), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(visibleMeshlets), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshletCountDispatchArgs), 0, VK_WHOLE_SIZE, 0);
                    vkCmdFillBuffer(cmd, graph.GetBufferHandle(compactedMeshletDispatchArgs), 0, VK_WHOLE_SIZE, 0);
                });
        }

        // Instance Visibility/LOD
        {
            RenderPass& instanceLODPass = graph.AddPass(SID("Instance Visibility/LOD Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            instanceLODPass.ReadBuffer(SCENE_DATA_BUFFER);
            instanceLODPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
            instanceLODPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            instanceLODPass.WriteBuffer(instanceMeshletOffsets);
            instanceLODPass.Execute(
                [instanceMeshletOffsets,
                    instanceCount,
                    lodBias,
                    &graph,
                    pipelineManager,
                    sceneIndex](VkCommandBuffer cmd) {
                    InstanceLODPushConstant pc{
                        .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                        .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                        .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                        .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                        .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                        .instanceCount = instanceCount,
                        .sceneDataIndex = sceneIndex,
                        .lodBias = lodBias,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_instance_lod"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                    uint32_t xDispatch = (instanceCount + INSTANCING_VISIBILITY_DISPATCH_X - 1) / INSTANCING_VISIBILITY_DISPATCH_X;
                    vkCmdDispatch(cmd, xDispatch, 1, 1);
                });
        }

        // todo if count < 255, then just do this in 1 group, 1 step.
        // Prefix Sum for Expansion
        {
            uint32_t level1BlockCount = (instanceCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

            RenderPass& upsweep1Pass = graph.AddPass(SID("Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            upsweep1Pass.ReadBuffer(instanceMeshletOffsets);
            upsweep1Pass.WriteBuffer(level1Sums);
            upsweep1Pass.WriteBuffer(level1BlockSums);
            upsweep1Pass.Execute([instanceMeshletOffsets, level1Sums, level1BlockSums, &graph, pipelineManager, instanceCount, level1BlockCount](VkCommandBuffer cmd) {
                PrefixSumUpsweep1PushConstant pc{
                    .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                    .level1Sums = graph.GetBufferAddress(level1Sums),
                    .level1BlockSums = graph.GetBufferAddress(level1BlockSums),
                    .elementCount = instanceCount,
                    .blockCount = level1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_up_1"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level1BlockCount, 1, 1);
            });

            uint32_t level2BlockCount = (level1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

            if (level2BlockCount > 1) {
                RenderPass& upsweep2Pass = graph.AddPass(SID("Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                upsweep2Pass.ReadBuffer(level1BlockSums);
                upsweep2Pass.WriteBuffer(level2Sums);
                upsweep2Pass.WriteBuffer(level2BlockSums);
                upsweep2Pass.Execute([level1BlockSums, level2Sums, level2BlockSums, &graph, pipelineManager, level1BlockCount, level2BlockCount](VkCommandBuffer cmd) {
                    PrefixSumUpsweep2PushConstant pc{
                        .level1BlockSums = graph.GetBufferAddress(level1BlockSums),
                        .level2Sums = graph.GetBufferAddress(level2Sums),
                        .level2BlockSums = graph.GetBufferAddress(level2BlockSums),
                        .elementCount = level1BlockCount,
                        .blockCount = level2BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_up_2"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, level2BlockCount, 1, 1);
                });

                RenderPass& scanBlocksPass = graph.AddPass(SID("Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                scanBlocksPass.ReadBuffer(level2BlockSums);
                scanBlocksPass.WriteBuffer(scannedLevel2BlockSums);
                scanBlocksPass.Execute([level2BlockSums, scannedLevel2BlockSums, &graph, pipelineManager, level2BlockCount](VkCommandBuffer cmd) {
                    PrefixSumScanBlocksPushConstant pc{
                        .level2BlockSums = graph.GetBufferAddress(level2BlockSums),
                        .scannedLevel2BlockSums = graph.GetBufferAddress(scannedLevel2BlockSums),
                        .blockCount = level2BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_scan_blocks"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, 1, 1, 1);
                });

                RenderPass& downsweep1Pass = graph.AddPass(SID("Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                downsweep1Pass.ReadBuffer(scannedLevel2BlockSums);
                downsweep1Pass.ReadWriteBuffer(level2Sums);
                downsweep1Pass.Execute([scannedLevel2BlockSums, level2Sums, &graph, pipelineManager, level1BlockCount, level2BlockCount](VkCommandBuffer cmd) {
                    PrefixSumDownsweep1PushConstant pc{
                        .scannedLevel2BlockSums = graph.GetBufferAddress(scannedLevel2BlockSums),
                        .level2Sums = graph.GetBufferAddress(level2Sums),
                        .elementCount = level1BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_down_1"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, level2BlockCount, 1, 1);
                });
            }
            else {
                RenderPass& scanBlocksPass = graph.AddPass(SID("Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                scanBlocksPass.ReadBuffer(level1BlockSums);
                scanBlocksPass.WriteBuffer(scannedLevel2BlockSums);
                scanBlocksPass.Execute([level1BlockSums, scannedLevel2BlockSums, &graph, pipelineManager, level1BlockCount](VkCommandBuffer cmd) {
                    PrefixSumScanBlocksPushConstant pc{
                        .level2BlockSums = graph.GetBufferAddress(level1BlockSums),
                        .scannedLevel2BlockSums = graph.GetBufferAddress(scannedLevel2BlockSums),
                        .blockCount = level1BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_scan_blocks"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, 1, 1, 1);
                });
            }

            RenderPass& downsweep2Pass = graph.AddPass(SID("Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            downsweep2Pass.ReadBuffer(level1Sums);
            if (level2BlockCount > 1) {
                downsweep2Pass.ReadBuffer(level2Sums);
            }
            else {
                downsweep2Pass.ReadBuffer(scannedLevel2BlockSums);
            }
            downsweep2Pass.WriteBuffer(instanceMeshletOffsets);
            downsweep2Pass.Execute(
                [level1Sums, level2Sums, scannedLevel2BlockSums, instanceMeshletOffsets, &graph, pipelineManager, level2BlockCount, instanceCount, level1BlockCount](VkCommandBuffer cmd) {
                    PrefixSumDownsweep2PushConstant pc{
                        .level1Sums = graph.GetBufferAddress(level1Sums),
                        .level2Sums = level2BlockCount > 1 ? graph.GetBufferAddress(level2Sums) : graph.GetBufferAddress(scannedLevel2BlockSums),
                        .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                        .elementCount = instanceCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_down_2"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, level1BlockCount, 1, 1);
                });

            RenderPass& totalMeshletCalculator = graph.AddPass(
                SID("Total Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            totalMeshletCalculator.ReadBuffer(instanceMeshletOffsets);
            totalMeshletCalculator.WriteBuffer(meshletCountDispatchArgs);
            totalMeshletCalculator.Execute([instanceMeshletOffsets, meshletCountDispatchArgs, &graph, pipelineManager, instanceCount](VkCommandBuffer cmd) {
                TotalMeshletCountPushConstant pc{
                    .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                    .instanceCount = instanceCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_total_meshlet_count"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        // Expand Instance to Meshlet
        {
            RenderPass& expandInstancesToMeshlets = graph.AddPass(
                SID("Expand Instance To Meshlet"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            expandInstancesToMeshlets.ReadBuffer(SCENE_DATA_BUFFER);
            expandInstancesToMeshlets.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            expandInstancesToMeshlets.ReadBuffer(instanceMeshletOffsets);
            expandInstancesToMeshlets.ReadIndirectBuffer(meshletCountDispatchArgs);
            expandInstancesToMeshlets.WriteBuffer(intermediateMeshlets);
            expandInstancesToMeshlets.Execute([instanceMeshletOffsets, meshletCountDispatchArgs, intermediateMeshlets,
                    &graph, pipelineManager, instanceCount, highestMeshletCount, sceneIndex](VkCommandBuffer cmd) {
                    ExpandMeshletsPushConstant pc{
                        .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
                        .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                        .intermediateMeshlets = graph.GetBufferAddress(intermediateMeshlets),
                        .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                        .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                        .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                        .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                        .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                        .sceneDataIndex = sceneIndex,
                        .instanceCount = instanceCount,
                        .currentFrameBufferMeshletLimit = highestMeshletCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_expand_instance_to_meshlet"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshletCountDispatchArgs), offsetof(InstancingMeshletDispatchIndirect, x));
                });
        }

        // Prefix Sum for Compaction
        {
            uint32_t meshletLevel1BlockCount = (highestMeshletCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
            uint32_t meshletLevel2BlockCount = (meshletLevel1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

            RenderPass& meshletUpsweep1Pass = graph.AddPass(
                SID("Meshlet Visibility Prefix Sum Upsweep 1"),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletUpsweep1Pass.ReadBuffer(intermediateMeshlets);
            meshletUpsweep1Pass.WriteBuffer(meshletLevel1Sums);
            meshletUpsweep1Pass.WriteBuffer(meshletLevel1BlockSums);
            meshletUpsweep1Pass.ReadIndirectBuffer(meshletCountDispatchArgs);
            meshletUpsweep1Pass.Execute(
                [intermediateMeshlets, meshletCountDispatchArgs, meshletLevel1Sums, meshletLevel1BlockSums, &graph, pipelineManager, meshletLevel1BlockCount, highestMeshletCount
                ](VkCommandBuffer cmd) {
                    MeshletVisibilityPrefixSumUpsweep1PushConstant pc{
                        .intermediateMeshlets = graph.GetBufferAddress(intermediateMeshlets),
                        .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
                        .meshletLevel1Sums = graph.GetBufferAddress(meshletLevel1Sums),
                        .meshletLevel1BlockSums = graph.GetBufferAddress(meshletLevel1BlockSums),
                        .blockCount = meshletLevel1BlockCount,
                        .currentFrameBufferMeshletLimit = highestMeshletCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_meshlet_visibility_prefix_sum_up_1"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshletCountDispatchArgs), offsetof(InstancingMeshletDispatchIndirect, x));
                });

            if (meshletLevel2BlockCount > 1) {
                RenderPass& meshletUpsweep2Pass = graph.AddPass(
                    SID("Meshlet Visibility Prefix Sum Upsweep 2"),
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                meshletUpsweep2Pass.ReadBuffer(meshletLevel1BlockSums);
                meshletUpsweep2Pass.WriteBuffer(meshletLevel2Sums);
                meshletUpsweep2Pass.WriteBuffer(meshletLevel2BlockSums);
                meshletUpsweep2Pass.Execute(
                    [meshletLevel1BlockSums, meshletLevel2Sums, meshletLevel2BlockSums, &graph, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                        PrefixSumUpsweep2PushConstant pc{
                            .level1BlockSums = graph.GetBufferAddress(meshletLevel1BlockSums),
                            .level2Sums = graph.GetBufferAddress(meshletLevel2Sums),
                            .level2BlockSums = graph.GetBufferAddress(meshletLevel2BlockSums),
                            .elementCount = meshletLevel1BlockCount,
                            .blockCount = meshletLevel2BlockCount,
                        };

                        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_up_2"));
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                        vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
                    });

                RenderPass& meshletScanBlocksPass = graph.AddPass(
                    SID("Meshlet Visibility Prefix Sum Scan Blocks"),
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                meshletScanBlocksPass.ReadBuffer(meshletLevel2BlockSums);
                meshletScanBlocksPass.WriteBuffer(meshletScannedLevel2BlockSums);
                meshletScanBlocksPass.Execute([meshletLevel2BlockSums, meshletScannedLevel2BlockSums, &graph, pipelineManager, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                    PrefixSumScanBlocksPushConstant pc{
                        .level2BlockSums = graph.GetBufferAddress(meshletLevel2BlockSums),
                        .scannedLevel2BlockSums = graph.GetBufferAddress(meshletScannedLevel2BlockSums),
                        .blockCount = meshletLevel2BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_scan_blocks"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, 1, 1, 1);
                });

                RenderPass& meshletDownsweep1Pass = graph.AddPass(
                    SID("Meshlet Visibility Prefix Sum Downsweep 1"),
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                meshletDownsweep1Pass.ReadBuffer(meshletScannedLevel2BlockSums);
                meshletDownsweep1Pass.ReadWriteBuffer(meshletLevel2Sums);
                meshletDownsweep1Pass.Execute([meshletScannedLevel2BlockSums, meshletLevel2Sums, &graph, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                    PrefixSumDownsweep1PushConstant pc{
                        .scannedLevel2BlockSums = graph.GetBufferAddress(meshletScannedLevel2BlockSums),
                        .level2Sums = graph.GetBufferAddress(meshletLevel2Sums),
                        .elementCount = meshletLevel1BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_down_1"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
                });
            }
            else {
                RenderPass& meshletScanBlocksPass = graph.AddPass(
                    SID("Meshlet Visibility Prefix Sum Scan Blocks"),
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                meshletScanBlocksPass.ReadBuffer(meshletLevel1BlockSums);
                meshletScanBlocksPass.WriteBuffer(meshletScannedLevel2BlockSums);
                meshletScanBlocksPass.Execute([meshletLevel1BlockSums, meshletScannedLevel2BlockSums, &graph, pipelineManager, meshletLevel1BlockCount](VkCommandBuffer cmd) {
                    PrefixSumScanBlocksPushConstant pc{
                        .level2BlockSums = graph.GetBufferAddress(meshletLevel1BlockSums),
                        .scannedLevel2BlockSums = graph.GetBufferAddress(meshletScannedLevel2BlockSums),
                        .blockCount = meshletLevel1BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_scan_blocks"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, 1, 1, 1);
                });
            }

            RenderPass& meshletDownsweep2Pass = graph.AddPass(
                SID("Meshlet Visibility Prefix Sum Downsweep 2"),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletDownsweep2Pass.ReadBuffer(meshletLevel1Sums);
            meshletDownsweep2Pass.ReadBuffer(intermediateMeshlets);
            if (meshletLevel2BlockCount > 1) {
                meshletDownsweep2Pass.ReadBuffer(meshletLevel2Sums);
            }
            else {
                meshletDownsweep2Pass.ReadBuffer(meshletScannedLevel2BlockSums);
            }
            meshletDownsweep2Pass.WriteBuffer(visibleMeshlets);
            meshletDownsweep2Pass.WriteBuffer(compactedMeshletDispatchArgs);
            meshletDownsweep2Pass.ReadIndirectBuffer(meshletCountDispatchArgs);
            meshletDownsweep2Pass.Execute([meshletLevel1Sums, meshletLevel2Sums, meshletScannedLevel2BlockSums, intermediateMeshlets,
                    meshletCountDispatchArgs, visibleMeshlets, compactedMeshletDispatchArgs,
                    &graph, pipelineManager, meshletLevel2BlockCount, highestMeshletCount](VkCommandBuffer cmd) {
                    MeshletVisibilityPrefixSumDownsweep2PushConstant pc{
                        .meshletLevel1Sums = graph.GetBufferAddress(meshletLevel1Sums),
                        .meshletLevel2Sums = meshletLevel2BlockCount > 1 ? graph.GetBufferAddress(meshletLevel2Sums) : graph.GetBufferAddress(meshletScannedLevel2BlockSums),
                        .intermediateMeshlets = graph.GetBufferAddress(intermediateMeshlets),
                        .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
                        .visibleMeshlets = graph.GetBufferAddress(visibleMeshlets),
                        .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
                        .currentFrameBufferMeshletLimit = highestMeshletCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_meshlet_visibility_prefix_sum_down_2"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshletCountDispatchArgs), offsetof(InstancingMeshletDispatchIndirect, x));
                });

            RenderPass& compactedDispatchCalc = graph.AddPass(
                SID("Compacted Meshlet Dispatch Calculation"),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            compactedDispatchCalc.ReadWriteBuffer(compactedMeshletDispatchArgs);
            compactedDispatchCalc.Execute([compactedMeshletDispatchArgs, &graph, pipelineManager, highestMeshletCount](VkCommandBuffer cmd) {
                CompactedMeshletDispatchPushConstant pc{
                    .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
                    .currentFrameBufferMeshletLimit = highestMeshletCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_compacted_meshlet_dispatch"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        RenderPass& maxMeshletCount = graph.AddPass(SID("Max Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        maxMeshletCount.ReadBuffer(meshletCountDispatchArgs);
        maxMeshletCount.ReadWriteBuffer(SID("readback_buffer"));
        maxMeshletCount.Execute([&, pipelineManager, bufferSrc = meshletCountDispatchArgs](VkCommandBuffer cmd) {
            MaxMeshletCountPushConstant pc{
                .indirectDispatchBuffer = graph.GetBufferAddress(bufferSrc),
                .currentHighest = graph.GetBufferAddress(SID("readback_buffer")) + offsetof(ReadbackStruct, meshletCount),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_max_meshlet_count"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    RenderPass& instancedMeshShading = graph.AddPass(SID("Instanced Mesh Shading"), VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    instancedMeshShading.WriteColorAttachment(targets.visibility);
    instancedMeshShading.WriteColorAttachment(targets.gbufferOne);
    instancedMeshShading.WriteColorAttachment(targets.stableId);
    instancedMeshShading.WriteDepthAttachment(targets.depthStencil);
    instancedMeshShading.ReadBuffer(SCENE_DATA_BUFFER);
    instancedMeshShading.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    instancedMeshShading.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    instancedMeshShading.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    instancedMeshShading.ReadBuffer(visibleMeshlets);
    instancedMeshShading.ReadIndirectBuffer(compactedMeshletDispatchArgs);
    instancedMeshShading.Execute([&, pipelineManager, visibleMeshlets, compactedMeshletDispatchArgs, sceneIndex, width = renderExtent[0], height = renderExtent[1],
            bWireframe = renderFamilyProperties.bWireframe,
            visibility = targets.visibility, stableId = targets.stableId, depthStencil = targets.depthStencil](VkCommandBuffer cmd) {
            VkViewport viewport = VkHelpers::GenerateViewport(width, height);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdSetPolygonModeEXT(cmd, bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL);

            constexpr VkClearValue uintClear = {.color = {.uint32 = {0u, 0u, 0u, 0u}}};
            constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};

            auto visibilityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(visibility), &uintClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            auto stableIdAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(stableId), &uintClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            auto depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthStencil), &depthClear, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            auto stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthStencil), &depthClear, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            const VkRenderingAttachmentInfo colorAttachments[] = {visibilityAttachment, stableIdAttachment};
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 2, &depthAttachment, &stencilAttachment);

            vkCmdBeginRendering(cmd, &renderInfo);

            VisibilityBufferAccumulatePushConstant pushConstants{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sceneIndex * sizeof(SceneData),
                .vertexPosBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
                .meshletVerticesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
                .meshletTrianglesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
                .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                .visibleMeshlets = graph.GetBufferAddress(visibleMeshlets),
                .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("visibility_buffer_accumulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(VisibilityBufferAccumulatePushConstant), &pushConstants);

            vkCmdDrawMeshTasksIndirectEXT(
                cmd,
                graph.GetBufferHandle(compactedMeshletDispatchArgs),
                offsetof(InstancingCompactedMeshletDispatchIndirect, x),
                1,
                sizeof(InstancingCompactedMeshletDispatchIndirect));

            vkCmdEndRendering(cmd);
        });
}

void SetupVisibilityBarycentricDerivativePass(RenderGraph& graph,
                                              PipelineManager* pipelineManager,
                                              const Core::ViewFamily& viewFamily,
                                              Core::Array<uint32_t, 2> renderExtent,
                                              const VisibilityBufferBarycentricDerivativeTargets& targets,
                                              uint32_t sceneIndex)
{
    RenderPass& visBarDer = graph.AddPass(SID("Visibility Barycentric Derivative"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visBarDer.ReadSampledImage(targets.visibility);
    visBarDer.ReadBuffer(SCENE_DATA_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_VERTEX_POSITION_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_MESHLET_VERTEX_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_MESHLET_TRIANGLE_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_MESHLET_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    visBarDer.WriteStorageImage(targets.barycentric);
    visBarDer.WriteStorageImage(targets.derivatives);
    visBarDer.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            visibility = targets.visibility, barycentric = targets.barycentric, derivatives = targets.derivatives](VkCommandBuffer cmd) {
            VisibilityBufferResolvePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sceneIndex * sizeof(SceneData),
                .vertexPosBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
                .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
                .meshletVerticesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
                .meshletTrianglesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
                .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                .extents = {width, height},
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                .barycentricTargetIndex = graph.GetStorageImageViewDescriptorIndex(barycentric),
                .derivativeTargetIndex = graph.GetStorageImageViewDescriptorIndex(derivatives),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("visibility_buffer_barycentric_derivative"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}

void SetupVisibilityShadingPass(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Array<uint32_t, 2> renderExtent,
                                const VisibilityShadingTargets& targets,
                                uint32_t sceneIndex)
{
    RenderPass& visShading = graph.AddPass(SID("Visibility Shading"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visShading.ReadSampledImage(targets.visibility);
    visShading.ReadStorageImage(targets.barycentric);
    visShading.ReadStorageImage(targets.derivatives);
    visShading.ReadBuffer(SCENE_DATA_BUFFER);
    visShading.ReadBuffer(GEOMETRY_VERTEX_POSITION_BUFFER);
    visShading.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MESHLET_VERTEX_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MESHLET_TRIANGLE_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MESHLET_BUFFER);
    visShading.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    visShading.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    visShading.WriteStorageImage(targets.gbufferOne);
    visShading.WriteStorageImage(targets.gbufferTwo);
    visShading.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            visibility = targets.visibility, barycentric = targets.barycentric, derivatives = targets.derivatives,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo](VkCommandBuffer cmd) {
            VisibilityShadingPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sceneIndex * sizeof(SceneData),
                .vertexPosBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
                .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
                .meshletVerticesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
                .meshletTrianglesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
                .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                .extents = {width, height},
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                .barycentricBufferIndex = graph.GetStorageImageViewDescriptorIndex(barycentric),
                .derivativeBufferIndex = graph.GetStorageImageViewDescriptorIndex(derivatives),
                .gbufferOneIndex = graph.GetStorageImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetStorageImageViewDescriptorIndex(gbufferTwo),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("visibility_shading"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}

void SetupDeferredResolvePass(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const DeferredResolveTargets& targets,
                              uint32_t sceneIndex)
{
    RenderPass& deferredResolvePass = graph.AddPass(SID("Deferred Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    deferredResolvePass.ReadBuffer(SCENE_DATA_BUFFER);
    deferredResolvePass.ReadBuffer(SHADOW_DATA_BUFFER);
    deferredResolvePass.ReadBuffer(SID("light_data"));
    deferredResolvePass.ReadSampledImage(targets.gbufferOne);
    deferredResolvePass.ReadSampledImage(targets.gbufferTwo);
    deferredResolvePass.ReadSampledImage(targets.depthStencil);
    if (targets.shadows != StringID{}) {
        deferredResolvePass.ReadSampledImage(targets.shadows);
    }
    deferredResolvePass.WriteStorageImage(targets.output);
    deferredResolvePass.Execute([&, pipelineManager,
            width = renderExtent[0], height = renderExtent[1], sceneIndex,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            depth = targets.depthStencil, shadows = targets.shadows,
            output = targets.output, skyboxIndex = viewFamily.skyboxIndex](VkCommandBuffer cmd) {
            DeferredResolvePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .shadowData = graph.GetBufferAddress(SHADOW_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                .skyboxIndex = skyboxIndex,
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .sceneDataIndex = sceneIndex,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("deferred_resolve"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}

void SetupGroundTruthAmbientOcclusion(RenderGraph& graph,
                                      PipelineManager* pipelineManager,
                                      const Core::ViewFamily& viewFamily,
                                      Core::Array<uint32_t, 2> renderExtent,
                                      const MainRenderTargets& targets,
                                      uint64_t frameNumber,
                                      uint32_t sceneIndex)
{
    const Core::GTAOConfiguration& gtaoConfig = viewFamily.gtaoConfig;

    graph.CreateTexture(SID("gtao_depth"), TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 5}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_ao"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_edges"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    // Denoise pass(es) - typically run 2-3 times for better quality
    graph.CreateTexture(SID("gtao_temp"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("gtao_filtered"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& depthPrepass = graph.AddPass(SID("GTAO Depth Prepass"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    depthPrepass.ReadBuffer(SID("scene_data"));
    depthPrepass.ReadSampledImage(targets.depthStencil);
    depthPrepass.WriteStorageImage(SID("gtao_depth"));
    depthPrepass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            depthStencil = targets.depthStencil,
            effectRadius = gtaoConfig.effectRadius,
            effectFalloffRange = gtaoConfig.effectFalloffRange,
            radiusMultiplier = gtaoConfig.radiusMultiplier](VkCommandBuffer cmd) {
            GTAODepthPrepassPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
                .inputDepth = graph.GetDepthOnlySampledImageViewDescriptorIndex(depthStencil),
                .outputDepth0 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 0),
                .outputDepth1 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 1),
                .outputDepth2 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 2),
                .outputDepth3 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 3),
                .outputDepth4 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 4),
                .effectRadius = effectRadius,
                .effectFalloffRange = effectFalloffRange,
                .radiusMultiplier = radiusMultiplier,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_depth_prepass"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);


            uint32_t xDispatch = (width / 2 + GTAO_DEPTH_PREPASS_DISPATCH_X - 1) / GTAO_DEPTH_PREPASS_DISPATCH_X;
            uint32_t yDispatch = (height / 2 + GTAO_DEPTH_PREPASS_DISPATCH_Y - 1) / GTAO_DEPTH_PREPASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    RenderPass& gtaoMainPass = graph.AddPass(SID("GTAO Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    gtaoMainPass.ReadSampledImage(SID("gtao_depth"));
    gtaoMainPass.ReadSampledImage(targets.gbufferOne);
    gtaoMainPass.WriteStorageImage(SID("gtao_ao"));
    gtaoMainPass.WriteStorageImage(SID("gtao_edges"));
    gtaoMainPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex, frameNumber,
            normal = targets.gbufferOne,
            effectRadius = gtaoConfig.effectRadius,
            radiusMultiplier = gtaoConfig.radiusMultiplier,
            effectFalloffRange = gtaoConfig.effectFalloffRange,
            sampleDistributionPower = gtaoConfig.sampleDistributionPower,
            thinOccluderCompensation = gtaoConfig.thinOccluderCompensation,
            finalValuePower = gtaoConfig.finalValuePower,
            depthMipSamplingOffset = gtaoConfig.depthMipSamplingOffset,
            sliceCount = gtaoConfig.sliceCount,
            stepsPerSlice = gtaoConfig.stepsPerSlice](VkCommandBuffer cmd) {
            GTAOMainPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sizeof(SceneData) * sceneIndex,
                .prefilteredDepthIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_depth")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(normal),
                .aoOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_ao")),
                .edgeDataIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_edges")),

                .effectRadius = effectRadius,
                .radiusMultiplier = radiusMultiplier,
                .effectFalloffRange = effectFalloffRange,
                .sampleDistributionPower = sampleDistributionPower,
                .thinOccluderCompensation = thinOccluderCompensation,
                .finalValuePower = finalValuePower,
                .depthMipSamplingOffset = depthMipSamplingOffset,
                .sliceCount = sliceCount,
                .stepsPerSlice = stepsPerSlice,
                .noiseIndex = static_cast<uint32_t>(frameNumber % 64),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_main"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
            uint32_t yDispatch = (height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    RenderPass& denoise1 = graph.AddPass(SID("GTAO Denoise 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    denoise1.ReadSampledImage(SID("gtao_ao"));
    denoise1.ReadSampledImage(SID("gtao_edges"));
    denoise1.WriteStorageImage(SID("gtao_temp"));
    denoise1.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex](VkCommandBuffer cmd) {
        GTAODenoisePushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
            .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_ao")),
            .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_edges")),
            .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_temp")),
            .denoiseBlurBeta = 1e4f,
            .isFinalDenoisePass = 0,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_denoise"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& denoise2 = graph.AddPass(SID("GTAO Denoise 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    denoise2.ReadSampledImage(SID("gtao_temp"));
    denoise2.ReadSampledImage(SID("gtao_edges"));
    denoise2.WriteStorageImage(SID("gtao_filtered"));
    denoise2.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            denoiseBlurBeta = gtaoConfig.denoiseBlurBeta](VkCommandBuffer cmd) {
            GTAODenoisePushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
                .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_temp")),
                .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_edges")),
                .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_filtered")),
                .denoiseBlurBeta = denoiseBlurBeta,
                .isFinalDenoisePass = 1,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_denoise"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
            uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}

void SetupShadowsResolve(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Array<uint32_t, 2> renderExtent,
                         const MainRenderTargets& targets,
                         uint32_t sceneIndex)
{
    graph.CreateTexture(SID("shadows_resolve_target"), TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    RenderPass& shadowsResolvePass = graph.AddPass(SID("Shadows Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    shadowsResolvePass.ReadSampledImage(targets.gbufferOne);
    shadowsResolvePass.ReadSampledImage(targets.depthStencil);

    bool bHasGTAO = graph.HasTexture(SID("gtao_filtered"));
    if (bHasGTAO) {
        shadowsResolvePass.ReadSampledImage(SID("gtao_filtered"));
    }

    bool bHasShadows = graph.HasTexture(SID("shadow_cascade_0"));
    if (bHasShadows) {
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_0"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_1"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_2"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_3"));
    }

    shadowsResolvePass.ReadBuffer(SID("scene_data"));
    shadowsResolvePass.ReadBuffer(SID("shadow_data"));
    shadowsResolvePass.ReadBuffer(SID("light_data"));
    shadowsResolvePass.WriteStorageImage(SID("shadows_resolve_target"));
    shadowsResolvePass.Execute([&, pipelineManager, bHasShadows, bHasGTAO,
            width = renderExtent[0], height = renderExtent[1], sceneIndex,
            depthStencil = targets.depthStencil, gbufferOne = targets.gbufferOne](VkCommandBuffer cmd) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("shadows_resolve"));

            glm::ivec4 csmIndices{-1, -1, -1, -1};
            if (bHasShadows) {
                csmIndices.x = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_0")));
                csmIndices.y = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_1")));
                csmIndices.z = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_2")));
                csmIndices.w = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_3")));
            }

            int32_t gtaoIndex = bHasGTAO ? static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("gtao_filtered"))) : -1;

            ShadowsResolvePushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")) + sizeof(SceneData) * sceneIndex,
                .shadowData = graph.GetBufferAddress(SID("shadow_data")),
                .lightData = graph.GetBufferAddress(SID("light_data")),
                .gtaoFilteredIndex = gtaoIndex,
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("shadows_resolve_target")),
                .csmIndices = csmIndices,
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depthStencil),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}

void SetupSkyboxRendering(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          const MainRenderTargets& targets,
                          uint32_t sceneIndex)
{
    RenderPass& skyboxPass = graph.AddPass(SID("Skybox"), VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    skyboxPass.WriteColorAttachment(targets.outputColor);
    skyboxPass.ReadWriteDepthAttachment(targets.depthStencil);
    skyboxPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex,
            outputColor = targets.outputColor, depthStencil = targets.depthStencil, skyboxIndex = viewFamily.skyboxIndex, skyboxLOD = viewFamily.skyboxLOD](VkCommandBuffer cmd) {
            VkViewport viewport = VkHelpers::GenerateViewport(width, height);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            auto colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(outputColor), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            auto depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            Core::Array<VkRenderingAttachmentInfo, 1> colorAttachments{colorAttachment};
            VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments.Data(), 1, &depthAttachment, nullptr);
            vkCmdBeginRendering(cmd, &renderInfo);

            EnvironmentSkyboxPushConstant pc{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .sceneDataIndex = sceneIndex,
                .cubemapIndex = skyboxIndex,
                .skyboxLOD = skyboxLOD,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("environment_skybox"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

            vkCmdDraw(cmd, 3, 1, 0, 0);

            vkCmdEndRendering(cmd);
        });
}

StringID SetupSubpixelMorphologicalAntiAliasing(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent,
                                                const MainRenderTargets& ppTargets)
{
    graph.CreateTexture(SID("smaa_edges"), TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("smaa_blend"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("smaa_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    const Core::SMAAConfiguration& smaaConfig = viewFamily.smaaConfig;

    // Pass 1: Edge Detection
    RenderPass& edgePass = graph.AddPass(SID("SMAA Edge Detection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    edgePass.ReadBuffer(SID("scene_data"));
    edgePass.ReadSampledImage(ppTargets.outputColor);
    edgePass.ReadSampledImage(ppTargets.depthStencil);
    edgePass.WriteStorageImage(SID("smaa_edges"));
    edgePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = ppTargets.outputColor, depthStencil = ppTargets.depthStencil,
            smaaConfig](VkCommandBuffer cmd) {
            SmaaEdgeDetectionPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputEdgeIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_edges")),
                .threshold = smaaConfig.threshold,
                .localContrastAdaptation = smaaConfig.localContrastAdaptation,
            };

            StringID pipelineID;
            switch (smaaConfig.edgeDetectionMode) {
                case Core::SMAAEdgeDetectionMode::Color: pipelineID = SID("smaa_color_edge_detection"); break;
                case Core::SMAAEdgeDetectionMode::Depth: pipelineID = SID("smaa_depth_edge_detection"); break;
                default:                                 pipelineID = SID("smaa_luma_edge_detection");  break;
            }

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(pipelineID);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaEdgeDetectionPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    // Pass 2: Blend Weight Calculation
    RenderPass& blendPass = graph.AddPass(SID("SMAA Blend Weight"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    blendPass.ReadBuffer(SID("scene_data"));
    blendPass.ReadSampledImage(SID("smaa_edges"));
    blendPass.WriteStorageImage(SID("smaa_blend"));
    blendPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], smaaConfig](VkCommandBuffer cmd) {
        SmaaBlendWeightPushConstant pushData{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .edgeIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_edges")),
            .outputBlendIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_blend")),
            .maxSearchSteps = smaaConfig.maxSearchSteps,
            .maxSearchStepsDiag = smaaConfig.maxSearchStepsDiag,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_blend_weight"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaBlendWeightPushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });


    // Pass 3: Neighborhood Blending
    RenderPass& neighborhoodPass = graph.AddPass(SID("SMAA Neighborhood Blend"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    neighborhoodPass.ReadBuffer(SID("scene_data"));
    neighborhoodPass.ReadSampledImage(ppTargets.outputColor);
    neighborhoodPass.ReadSampledImage(SID("smaa_blend"));
    neighborhoodPass.WriteStorageImage(SID("smaa_output"));
    neighborhoodPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = ppTargets.outputColor](VkCommandBuffer cmd) {
            SmaaNeighborhoodBlendPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .blendWeightIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_blend")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_output")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_neighborhood_blend"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaNeighborhoodBlendPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return SID("smaa_output");
}

StringID SetupSMAA_T2X(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const MainRenderTargets& ppTargets)
{
    graph.CreateTexture(SID("smaa_edges"), TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("smaa_blend"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("smaa_t2x_current"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CarryTextureToNextFrame(SID("smaa_t2x_current"), SID("smaa_t2x_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    const Core::SMAAConfiguration& smaaConfig = viewFamily.smaaConfig;

    // Pass 1: Edge Detection
    RenderPass& edgePass = graph.AddPass(SID("SMAA T2X Edge Detection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    edgePass.ReadBuffer(SID("scene_data"));
    edgePass.ReadSampledImage(ppTargets.outputColor);
    edgePass.ReadSampledImage(ppTargets.depthStencil);
    edgePass.WriteStorageImage(SID("smaa_edges"));
    edgePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = ppTargets.outputColor, depthStencil = ppTargets.depthStencil,
            smaaConfig](VkCommandBuffer cmd) {
            SmaaEdgeDetectionPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
                .outputEdgeIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_edges")),
                .threshold = smaaConfig.threshold,
                .localContrastAdaptation = smaaConfig.localContrastAdaptation,
            };

            StringID pipelineID;
            switch (smaaConfig.edgeDetectionMode) {
                case Core::SMAAEdgeDetectionMode::Color: pipelineID = SID("smaa_color_edge_detection"); break;
                case Core::SMAAEdgeDetectionMode::Depth: pipelineID = SID("smaa_depth_edge_detection"); break;
                default:                                 pipelineID = SID("smaa_luma_edge_detection");  break;
            }

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(pipelineID);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaEdgeDetectionPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    // Pass 2: Blend Weight Calculation
    RenderPass& blendPass = graph.AddPass(SID("SMAA T2X Blend Weight"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    blendPass.ReadBuffer(SID("scene_data"));
    blendPass.ReadSampledImage(SID("smaa_edges"));
    blendPass.WriteStorageImage(SID("smaa_blend"));
    blendPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], smaaConfig](VkCommandBuffer cmd) {
        SmaaBlendWeightPushConstant pushData{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .edgeIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_edges")),
            .outputBlendIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_blend")),
            .maxSearchSteps = smaaConfig.maxSearchSteps,
            .maxSearchStepsDiag = smaaConfig.maxSearchStepsDiag,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_blend_weight"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaBlendWeightPushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Pass 3: Neighborhood Blending
    RenderPass& neighborhoodPass = graph.AddPass(SID("SMAA T2X Neighborhood Blend"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    neighborhoodPass.ReadBuffer(SID("scene_data"));
    neighborhoodPass.ReadSampledImage(ppTargets.outputColor);
    neighborhoodPass.ReadSampledImage(SID("smaa_blend"));
    neighborhoodPass.WriteStorageImage(SID("smaa_t2x_current"));
    neighborhoodPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = ppTargets.outputColor](VkCommandBuffer cmd) {
            SmaaNeighborhoodBlendPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .blendWeightIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_blend")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_t2x_current")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_neighborhood_blend"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaNeighborhoodBlendPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    if (!graph.HasTexture(SID("smaa_t2x_history"))) {
        return SID("smaa_t2x_current");
    }

    // Pass 4: Temporal Resolve
    graph.CreateTexture(SID("smaa_t2x_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& resolvePass = graph.AddPass(SID("SMAA T2X Temporal Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    resolvePass.ReadBuffer(SID("scene_data"));
    resolvePass.ReadSampledImage(SID("smaa_t2x_current"));
    resolvePass.ReadSampledImage(SID("smaa_t2x_history"));
    resolvePass.ReadSampledImage(ppTargets.gbufferOne);
    resolvePass.WriteStorageImage(SID("smaa_t2x_output"));
    resolvePass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            gbufferOne = ppTargets.gbufferOne](VkCommandBuffer cmd) {
            SmaaTemporalResolvePushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .currentColorIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_t2x_current")),
                .previousColorIndex = graph.GetSampledImageViewDescriptorIndex(SID("smaa_t2x_history")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("smaa_t2x_output")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("smaa_temporal_resolve"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SmaaTemporalResolvePushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    return SID("smaa_t2x_output");
}

StringID SetupTemporalAntiAliasing(RenderGraph& graph,
                                   PipelineManager* pipelineManager,
                                   const Core::ViewFamily& viewFamily,
                                   Core::Array<uint32_t, 2> renderExtent,
                                   const MainRenderTargets& ppTargets)
{
    graph.CreateTexture(SID("taa_current"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    graph.CarryTextureToNextFrame(SID("taa_current"), SID("taa_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    if (graph.HasTexture(ppTargets.gbufferOne)) {
        graph.CarryTextureToNextFrame(ppTargets.gbufferOne, SID("gbuffer_one_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    if (!graph.HasTexture(SID("taa_history")) || !graph.HasTexture(SID("gbuffer_one_history"))) {
        RenderPass& taaPass = graph.AddPass(SID("TAA Copy Deferred"), VK_PIPELINE_STAGE_2_COPY_BIT);
        taaPass.ReadCopyImage(ppTargets.outputColor);
        taaPass.WriteCopyImage(SID("taa_current"));
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], outputColor = ppTargets.outputColor](VkCommandBuffer cmd) {
            VkImage drawImage = graph.GetImageHandle(outputColor);
            VkImage taaImage = graph.GetImageHandle(SID("taa_current"));

            VkImageCopy2 copyRegion{};
            copyRegion.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent = {width, height, 1};

            VkCopyImageInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
            copyInfo.srcImage = drawImage;
            copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyInfo.dstImage = taaImage;
            copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &copyRegion;

            vkCmdCopyImage2(cmd, &copyInfo);
        });
        return ppTargets.outputColor;
    }

    RenderPass& taaPass = graph.AddPass(SID("TAA Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    taaPass.ReadBuffer(SID("scene_data"));
    taaPass.ReadSampledImage(ppTargets.outputColor);
    taaPass.ReadSampledImage(ppTargets.depthStencil);
    taaPass.ReadSampledImage(SID("taa_history"));
    taaPass.ReadSampledImage(ppTargets.gbufferOne);
    taaPass.ReadSampledImage(SID("gbuffer_one_history"));
    taaPass.WriteStorageImage(SID("taa_current"));
    taaPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            outputColor = ppTargets.outputColor, depthStencil = ppTargets.depthStencil,
            gbufferOne = ppTargets.gbufferOne](VkCommandBuffer cmd) {
            TemporalAntialiasingPushConstant pushData{
                .sceneData = graph.GetBufferAddress(SID("scene_data")),
                .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex(outputColor),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depthStencil),
                .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex(SID("taa_history")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferOneHistoryIndex = graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")),
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("taa_current")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("temporal_antialiasing"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TemporalAntialiasingPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

    graph.CreateTexture(SID("taa_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& finalCopyPass = graph.AddPass(SID("TAA Final Copy"), VK_PIPELINE_STAGE_2_BLIT_BIT);
    finalCopyPass.ReadBlitImage(SID("taa_current"));
    finalCopyPass.WriteBlitImage(SID("taa_output"));
    finalCopyPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        VkImage src = graph.GetImageHandle(SID("taa_current"));
        VkImage dst = graph.GetImageHandle(SID("taa_output"));

        VkOffset3D renderOffset = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};

        VkImageBlit2 blitRegion{};
        blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = renderOffset;
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = renderOffset;

        VkBlitImageInfo2 blitInfo{};
        blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blitInfo.srcImage = src;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = dst;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_LINEAR;

        vkCmdBlitImage2(cmd, &blitInfo);
    });

    return SID("taa_output");
}

StringID SetupPostProcessing(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Array<uint32_t, 2> renderExtent,
                             const MainRenderTargets& targets,
                             float deltaTime,
                             uint64_t frameNumber)
{
    PostProcessContext ctx{
        .graph = graph,
        .config = viewFamily.postProcessConfig,
        .targets = targets,
        .view = viewFamily,
        .extent = renderExtent,
        .deltaTime = deltaTime,
        .frameNumber = frameNumber,
        .pipelines = pipelineManager,
    };

    StringID current = ctx.targets.outputColor;
    current = PPExposure(ctx, current);
    current = PPBloom(ctx, current);
    current = PPSharpening(ctx, current);
    current = PPTonemap(ctx, current);
    // current = PPMotionBlur(ctx, current); // disabled: motion vector format change
    current = PPColorGrading(ctx, current);
    current = PPVignetteAberration(ctx, current);
    current = PPPanini(ctx, current);
    current = PPFilmGrain(ctx, current);
    return current;
}
} // Render

/*InstancedGeometryPassOutputs SetupInstancedGeometryShadowPass(RenderGraph& graph, const InstancedGeometryPassConfig& config, PipelineManager* pipelineManager, uint32_t sceneDataIndex,
                                                              uint32_t cascadeIndex, bool bAllowBufferAliasing)
{
    // Pre-build all buffer names
    const StringID instance_meshlet_offsets = SID("_instance_meshlet_offsets");
    const StringID level1_sums = SID("_level1_sums");
    const StringID level1_block_sums = SID("_level1_block_sums");
    const StringID level2_sums = SID("_level2_sums");
    const StringID level2_block_sums = SID("_level2_block_sums");
    const StringID scanned_level2_block_sums = SID("_scanned_level2_block_sums");
    const StringID intermediate_meshlets = SID("_intermediate_meshlets");
    const StringID meshlet_level1_sums = SID("_meshlet_level1_sums");
    const StringID meshlet_level1_block_sums = SID("_meshlet_level1_block_sums");
    const StringID meshlet_level2_sums = SID("_meshlet_level2_sums");
    const StringID meshlet_level2_block_sums = SID("_meshlet_level2_block_sums");
    const StringID meshlet_scanned_level2_block_sums = SID("_meshlet_scanned_level2_block_sums");
    const StringID visible_meshlets = SID("_visible_meshlets");
    const StringID meshlet_count_dispatch_args = SID("_meshlet_count_dispatch_args");
    const StringID compacted_meshlet_dispatch_args = SID("_compacted_meshlet_dispatch_args");

    // Create and Clear
    {
        graph.CreateBuffer(instance_meshlet_offsets, config.instanceMeshletOffsetsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(level1_sums, config.level1SumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(level1_block_sums, config.level1BlockSumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(level2_sums, config.level2SumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(level2_block_sums, config.level2BlockSumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(scanned_level2_block_sums, config.scannedLevel2BlockSumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(intermediate_meshlets, config.intermediateMeshletBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_level1_sums, config.meshletLevel1SumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_level1_block_sums, config.meshletLevel1BlockSumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_level2_sums, config.meshletLevel2SumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_level2_block_sums, config.meshletLevel2BlockSumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_scanned_level2_block_sums, config.meshletScannedLevel2BlockSumsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(visible_meshlets, config.visibleMeshletsBufferSize, false, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_count_dispatch_args, sizeof(InstancingMeshletDispatchIndirect), false, bAllowBufferAliasing);
        graph.CreateBuffer(compacted_meshlet_dispatch_args, sizeof(InstancingCompactedMeshletDispatchIndirect), false, bAllowBufferAliasing);

        RenderPass& clearPass = graph.AddPass(SID("Clear Temp Instancing Buffers"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        clearPass.WriteTransferBuffer(instance_meshlet_offsets);
        clearPass.WriteTransferBuffer(level1_sums);
        clearPass.WriteTransferBuffer(level1_block_sums);
        clearPass.WriteTransferBuffer(level2_sums);
        clearPass.WriteTransferBuffer(level2_block_sums);
        clearPass.WriteTransferBuffer(scanned_level2_block_sums);
        clearPass.WriteTransferBuffer(intermediate_meshlets);
        clearPass.WriteTransferBuffer(meshlet_level1_sums);
        clearPass.WriteTransferBuffer(meshlet_level1_block_sums);
        clearPass.WriteTransferBuffer(meshlet_level2_sums);
        clearPass.WriteTransferBuffer(meshlet_level2_block_sums);
        clearPass.WriteTransferBuffer(meshlet_scanned_level2_block_sums);
        clearPass.WriteTransferBuffer(visible_meshlets);
        clearPass.WriteTransferBuffer(meshlet_count_dispatch_args);
        clearPass.WriteTransferBuffer(compacted_meshlet_dispatch_args);
        clearPass.Execute([instance_meshlet_offsets, level1_sums, level1_block_sums, level2_sums, level2_block_sums,
                scanned_level2_block_sums, intermediate_meshlets, meshlet_level1_sums, meshlet_level1_block_sums,
                meshlet_level2_sums, meshlet_level2_block_sums, meshlet_scanned_level2_block_sums,
                visible_meshlets, meshlet_count_dispatch_args, compacted_meshlet_dispatch_args, &graph](VkCommandBuffer cmd) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(instance_meshlet_offsets), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(level1_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(level1_block_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(level2_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(level2_block_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(scanned_level2_block_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(intermediate_meshlets), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshlet_level1_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshlet_level1_block_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshlet_level2_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshlet_level2_block_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshlet_scanned_level2_block_sums), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(visible_meshlets), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshlet_count_dispatch_args), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(compacted_meshlet_dispatch_args), 0, VK_WHOLE_SIZE, 0);
            });
    }

    // Instance Visibility/LOD
    {
        RenderPass& instanceLODPass = graph.AddPass(SID("Instance Visibility/LOD Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        instanceLODPass.ReadBuffer(SCENE_DATA_BUFFER);
        instanceLODPass.ReadBuffer(SHADOW_DATA_BUFFER);
        instanceLODPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
        instanceLODPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        instanceLODPass.WriteBuffer(instance_meshlet_offsets);
        instanceLODPass.Execute([instance_meshlet_offsets,
                instanceCount = config.instanceCount,
                lodBias = config.lodBias,
                instanceBufferOffset = config.instanceBufferOffset,
                &graph,
                pipelineManager,
                sceneDataIndex, cascadeIndex](VkCommandBuffer cmd) {
                InstanceLODShadowsPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .shadowData = graph.GetBufferAddress(SHADOW_DATA_BUFFER),
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER) + instanceBufferOffset,
                    .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                    .instanceCount = instanceCount,
                    .sceneDataIndex = sceneDataIndex,
                    .cascadeIndex = cascadeIndex,
                    .lodBias = lodBias,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_instance_lod_shadows"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                uint32_t xDispatch = (instanceCount + INSTANCING_VISIBILITY_DISPATCH_X - 1) / INSTANCING_VISIBILITY_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });
    }

    uint32_t instanceCount = config.instanceCount;

    // Prefix Sum for Expansion
    {
        uint32_t level1BlockCount = (instanceCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

        RenderPass& upsweep1Pass = graph.AddPass(SID("Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        upsweep1Pass.ReadBuffer(instance_meshlet_offsets);
        upsweep1Pass.WriteBuffer(level1_sums);
        upsweep1Pass.WriteBuffer(level1_block_sums);
        upsweep1Pass.Execute([instance_meshlet_offsets, level1_sums, level1_block_sums, &graph, pipelineManager, instanceCount, level1BlockCount](VkCommandBuffer cmd) {
            PrefixSumUpsweep1PushConstant pc{
                .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                .level1Sums = graph.GetBufferAddress(level1_sums),
                .level1BlockSums = graph.GetBufferAddress(level1_block_sums),
                .elementCount = instanceCount,
                .blockCount = level1BlockCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_up_1"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, level1BlockCount, 1, 1);
        });

        uint32_t level2BlockCount = (level1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

        if (level2BlockCount > 1) {
            RenderPass& upsweep2Pass = graph.AddPass(SID("Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            upsweep2Pass.ReadBuffer(level1_block_sums);
            upsweep2Pass.WriteBuffer(level2_sums);
            upsweep2Pass.WriteBuffer(level2_block_sums);
            upsweep2Pass.Execute([level1_block_sums, level2_sums, level2_block_sums, &graph, pipelineManager, level1BlockCount, level2BlockCount](VkCommandBuffer cmd) {
                PrefixSumUpsweep2PushConstant pc{
                    .level1BlockSums = graph.GetBufferAddress(level1_block_sums),
                    .level2Sums = graph.GetBufferAddress(level2_sums),
                    .level2BlockSums = graph.GetBufferAddress(level2_block_sums),
                    .elementCount = level1BlockCount,
                    .blockCount = level2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_up_2"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level2BlockCount, 1, 1);
            });

            RenderPass& scanBlocksPass = graph.AddPass(SID("Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            scanBlocksPass.ReadBuffer(level2_block_sums);
            scanBlocksPass.WriteBuffer(scanned_level2_block_sums);
            scanBlocksPass.Execute([level2_block_sums, scanned_level2_block_sums, &graph, pipelineManager, level2BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress(level2_block_sums),
                    .scannedLevel2BlockSums = graph.GetBufferAddress(scanned_level2_block_sums),
                    .blockCount = level2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_scan_blocks"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& downsweep1Pass = graph.AddPass(SID("Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            downsweep1Pass.ReadBuffer(scanned_level2_block_sums);
            downsweep1Pass.ReadWriteBuffer(level2_sums);
            downsweep1Pass.Execute([scanned_level2_block_sums, level2_sums, &graph, pipelineManager, level1BlockCount, level2BlockCount](VkCommandBuffer cmd) {
                PrefixSumDownsweep1PushConstant pc{
                    .scannedLevel2BlockSums = graph.GetBufferAddress(scanned_level2_block_sums),
                    .level2Sums = graph.GetBufferAddress(level2_sums),
                    .elementCount = level1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_down_1"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level2BlockCount, 1, 1);
            });
        }
        else {
            RenderPass& scanBlocksPass = graph.AddPass(SID("Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            scanBlocksPass.ReadBuffer(level1_block_sums);
            scanBlocksPass.WriteBuffer(scanned_level2_block_sums);
            scanBlocksPass.Execute([level1_block_sums, scanned_level2_block_sums, &graph, pipelineManager, level1BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress(level1_block_sums),
                    .scannedLevel2BlockSums = graph.GetBufferAddress(scanned_level2_block_sums),
                    .blockCount = level1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_scan_blocks"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        RenderPass& downsweep2Pass = graph.AddPass(SID("Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        downsweep2Pass.ReadBuffer(level1_sums);
        if (level2BlockCount > 1) {
            downsweep2Pass.ReadBuffer(level2_sums);
        }
        else {
            downsweep2Pass.ReadBuffer(scanned_level2_block_sums);
        }
        downsweep2Pass.WriteBuffer(instance_meshlet_offsets);
        downsweep2Pass.Execute(
            [level1_sums, level2_sums, scanned_level2_block_sums, instance_meshlet_offsets, &graph, pipelineManager, level2BlockCount, instanceCount, level1BlockCount](VkCommandBuffer cmd) {
                PrefixSumDownsweep2PushConstant pc{
                    .level1Sums = graph.GetBufferAddress(level1_sums),
                    .level2Sums = level2BlockCount > 1 ? graph.GetBufferAddress(level2_sums) : graph.GetBufferAddress(scanned_level2_block_sums),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                    .elementCount = instanceCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_down_2"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level1BlockCount, 1, 1);
            });

        RenderPass& totalMeshletCalculator = graph.AddPass(SID("Total Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        totalMeshletCalculator.ReadBuffer(instance_meshlet_offsets);
        totalMeshletCalculator.WriteBuffer(meshlet_count_dispatch_args);
        totalMeshletCalculator.Execute([instance_meshlet_offsets, meshlet_count_dispatch_args, &graph, pipelineManager, instanceCount](VkCommandBuffer cmd) {
            TotalMeshletCountPushConstant pc{
                .indirectDispatchBuffer = graph.GetBufferAddress(meshlet_count_dispatch_args),
                .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                .instanceCount = instanceCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_total_meshlet_count"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    uint32_t highestMeshletCount = config.visibleMeshletUpperBound;

    // Expand Instance to Meshlet
    {
        RenderPass& expandInstancesToMeshlets = graph.AddPass(SID("Expand Instance To Meshlet"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        expandInstancesToMeshlets.ReadBuffer(SCENE_DATA_BUFFER);
        expandInstancesToMeshlets.ReadBuffer(SHADOW_DATA_BUFFER);
        expandInstancesToMeshlets.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        expandInstancesToMeshlets.ReadBuffer(instance_meshlet_offsets);
        expandInstancesToMeshlets.ReadIndirectBuffer(meshlet_count_dispatch_args);
        expandInstancesToMeshlets.WriteBuffer(intermediate_meshlets);
        expandInstancesToMeshlets.Execute([instance_meshlet_offsets, meshlet_count_dispatch_args, intermediate_meshlets, instanceBufferOffset = config.instanceBufferOffset,
                &graph, pipelineManager, instanceCount, highestMeshletCount, sceneDataIndex, cascadeIndex](VkCommandBuffer cmd) {
                ExpandMeshletsShadowsPushConstant pc{
                    .indirectDispatchBuffer = graph.GetBufferAddress(meshlet_count_dispatch_args),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                    .intermediateMeshlets = graph.GetBufferAddress(intermediate_meshlets),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER) + instanceBufferOffset,
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                    .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .shadowData = graph.GetBufferAddress(SHADOW_DATA_BUFFER),
                    .sceneDataIndex = sceneDataIndex,
                    .cascadeIndex = cascadeIndex,
                    .instanceCount = instanceCount,
                    .currentFrameBufferMeshletLimit = highestMeshletCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_expand_instance_to_meshlet_shadows"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshlet_count_dispatch_args), offsetof(InstancingMeshletDispatchIndirect, x));
            });
    }

    // Prefix Sum for Compaction
    {
        uint32_t meshletLevel1BlockCount = (highestMeshletCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
        uint32_t meshletLevel2BlockCount = (meshletLevel1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

        RenderPass& meshletUpsweep1Pass = graph.AddPass(SID("Meshlet Visibility Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        meshletUpsweep1Pass.ReadBuffer(intermediate_meshlets);
        meshletUpsweep1Pass.WriteBuffer(meshlet_level1_sums);
        meshletUpsweep1Pass.WriteBuffer(meshlet_level1_block_sums);
        meshletUpsweep1Pass.ReadIndirectBuffer(meshlet_count_dispatch_args);
        meshletUpsweep1Pass.Execute(
            [intermediate_meshlets, meshlet_count_dispatch_args, meshlet_level1_sums, meshlet_level1_block_sums, &graph, pipelineManager, meshletLevel1BlockCount, highestMeshletCount
            ](VkCommandBuffer cmd) {
                MeshletVisibilityPrefixSumUpsweep1PushConstant pc{
                    .intermediateMeshlets = graph.GetBufferAddress(intermediate_meshlets),
                    .indirectDispatchBuffer = graph.GetBufferAddress(meshlet_count_dispatch_args),
                    .meshletLevel1Sums = graph.GetBufferAddress(meshlet_level1_sums),
                    .meshletLevel1BlockSums = graph.GetBufferAddress(meshlet_level1_block_sums),
                    .blockCount = meshletLevel1BlockCount,
                    .currentFrameBufferMeshletLimit = highestMeshletCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_meshlet_visibility_prefix_sum_up_1"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshlet_count_dispatch_args), offsetof(InstancingMeshletDispatchIndirect, x));
            });

        if (meshletLevel2BlockCount > 1) {
            RenderPass& meshletUpsweep2Pass = graph.AddPass(SID("Meshlet Visibility Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletUpsweep2Pass.ReadBuffer(meshlet_level1_block_sums);
            meshletUpsweep2Pass.WriteBuffer(meshlet_level2_sums);
            meshletUpsweep2Pass.WriteBuffer(meshlet_level2_block_sums);
            meshletUpsweep2Pass.Execute(
                [meshlet_level1_block_sums, meshlet_level2_sums, meshlet_level2_block_sums, &graph, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                    PrefixSumUpsweep2PushConstant pc{
                        .level1BlockSums = graph.GetBufferAddress(meshlet_level1_block_sums),
                        .level2Sums = graph.GetBufferAddress(meshlet_level2_sums),
                        .level2BlockSums = graph.GetBufferAddress(meshlet_level2_block_sums),
                        .elementCount = meshletLevel1BlockCount,
                        .blockCount = meshletLevel2BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_up_2"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
                });

            RenderPass& meshletScanBlocksPass = graph.AddPass(SID("Meshlet Visibility Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletScanBlocksPass.ReadBuffer(meshlet_level2_block_sums);
            meshletScanBlocksPass.WriteBuffer(meshlet_scanned_level2_block_sums);
            meshletScanBlocksPass.Execute([meshlet_level2_block_sums, meshlet_scanned_level2_block_sums, &graph, pipelineManager, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress(meshlet_level2_block_sums),
                    .scannedLevel2BlockSums = graph.GetBufferAddress(meshlet_scanned_level2_block_sums),
                    .blockCount = meshletLevel2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_scan_blocks"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& meshletDownsweep1Pass = graph.AddPass(SID("Meshlet Visibility Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletDownsweep1Pass.ReadBuffer(meshlet_scanned_level2_block_sums);
            meshletDownsweep1Pass.ReadWriteBuffer(meshlet_level2_sums);
            meshletDownsweep1Pass.Execute([meshlet_scanned_level2_block_sums, meshlet_level2_sums, &graph, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                PrefixSumDownsweep1PushConstant pc{
                    .scannedLevel2BlockSums = graph.GetBufferAddress(meshlet_scanned_level2_block_sums),
                    .level2Sums = graph.GetBufferAddress(meshlet_level2_sums),
                    .elementCount = meshletLevel1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_prefix_sum_down_1"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
            });
        }
        else {
            RenderPass& meshletScanBlocksPass = graph.AddPass(SID("Meshlet Visibility Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletScanBlocksPass.ReadBuffer(meshlet_level1_block_sums);
            meshletScanBlocksPass.WriteBuffer(meshlet_scanned_level2_block_sums);
            meshletScanBlocksPass.Execute([meshlet_level1_block_sums, meshlet_scanned_level2_block_sums, &graph, pipelineManager, meshletLevel1BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress(meshlet_level1_block_sums),
                    .scannedLevel2BlockSums = graph.GetBufferAddress(meshlet_scanned_level2_block_sums),
                    .blockCount = meshletLevel1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_scan_blocks"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        RenderPass& meshletDownsweep2Pass = graph.AddPass(SID("Meshlet Visibility Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        meshletDownsweep2Pass.ReadBuffer(meshlet_level1_sums);
        meshletDownsweep2Pass.ReadBuffer(intermediate_meshlets);
        if (meshletLevel2BlockCount > 1) {
            meshletDownsweep2Pass.ReadBuffer(meshlet_level2_sums);
        }
        else {
            meshletDownsweep2Pass.ReadBuffer(meshlet_scanned_level2_block_sums);
        }
        meshletDownsweep2Pass.WriteBuffer(visible_meshlets);
        meshletDownsweep2Pass.WriteBuffer(compacted_meshlet_dispatch_args);
        meshletDownsweep2Pass.ReadIndirectBuffer(meshlet_count_dispatch_args);
        meshletDownsweep2Pass.Execute([meshlet_level1_sums, meshlet_level2_sums, meshlet_scanned_level2_block_sums, intermediate_meshlets,
                meshlet_count_dispatch_args, visible_meshlets, compacted_meshlet_dispatch_args,
                &graph, pipelineManager, meshletLevel2BlockCount, highestMeshletCount](VkCommandBuffer cmd) {
                MeshletVisibilityPrefixSumDownsweep2PushConstant pc{
                    .meshletLevel1Sums = graph.GetBufferAddress(meshlet_level1_sums),
                    .meshletLevel2Sums = meshletLevel2BlockCount > 1 ? graph.GetBufferAddress(meshlet_level2_sums) : graph.GetBufferAddress(meshlet_scanned_level2_block_sums),
                    .intermediateMeshlets = graph.GetBufferAddress(intermediate_meshlets),
                    .indirectDispatchBuffer = graph.GetBufferAddress(meshlet_count_dispatch_args),
                    .visibleMeshlets = graph.GetBufferAddress(visible_meshlets),
                    .compactedDispatchBuffer = graph.GetBufferAddress(compacted_meshlet_dispatch_args),
                    .currentFrameBufferMeshletLimit = highestMeshletCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_meshlet_visibility_prefix_sum_down_2"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshlet_count_dispatch_args), offsetof(InstancingMeshletDispatchIndirect, x));
            });

        RenderPass& compactedDispatchCalc = graph.AddPass(SID("Compacted Meshlet Dispatch Calculation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        compactedDispatchCalc.ReadWriteBuffer(compacted_meshlet_dispatch_args);
        compactedDispatchCalc.Execute([compacted_meshlet_dispatch_args, &graph, pipelineManager, highestMeshletCount](VkCommandBuffer cmd) {
            CompactedMeshletDispatchPushConstant pc{
                .compactedDispatchBuffer = graph.GetBufferAddress(compacted_meshlet_dispatch_args),
                .currentFrameBufferMeshletLimit = highestMeshletCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_compacted_meshlet_dispatch"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    RenderPass& maxMeshletCount = graph.AddPass(SID("Max Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    maxMeshletCount.ReadBuffer(meshlet_count_dispatch_args);
    maxMeshletCount.ReadWriteBuffer(SID("readback_buffer"));
    maxMeshletCount.Execute([&, pipelineManager, bufferSrc = meshlet_count_dispatch_args](VkCommandBuffer cmd) {
        MaxMeshletCountPushConstant pc{
            .indirectDispatchBuffer = graph.GetBufferAddress(bufferSrc),
            .currentHighest = graph.GetBufferAddress(SID("readback_buffer")) + offsetof(ReadbackStruct, meshletCount),
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_max_meshlet_count"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });

    return InstancedGeometryPassOutputs{
        .visibleMeshlets = visible_meshlets,
        .compactedDispatchArgs = compacted_meshlet_dispatch_args,
        .meshletCountDispatchArgs = meshlet_count_dispatch_args,
    };
}*/
