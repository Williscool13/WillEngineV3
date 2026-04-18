//
// Created by William on 2026-04-16.
//

#include "renderer.h"

#include "pipelines/pipeline_data.h"
#include "pipelines/pipeline_manager.h"
#include "render-graph/render_pass.h"
#include "vulkan/vk_helpers.h"

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
    instancedMeshShading.WriteColorAttachment(targets.stableId);
    instancedMeshShading.WriteDepthAttachment(targets.depthStencil);
    instancedMeshShading.ReadBuffer(SCENE_DATA_BUFFER);
    instancedMeshShading.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    instancedMeshShading.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    instancedMeshShading.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    instancedMeshShading.ReadBuffer(visibleMeshlets);
    instancedMeshShading.ReadIndirectBuffer(compactedMeshletDispatchArgs);
    instancedMeshShading.Execute([&, pipelineManager, visibleMeshlets, compactedMeshletDispatchArgs, sceneIndex, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        constexpr VkClearValue uintClear = {.color = {.uint32 = {0u, 0u, 0u, 0u}}};
        constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};

        auto visibilityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.visibility), &uintClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        auto stableIdAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.stableId), &uintClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        auto depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), &depthClear, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        auto stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), &depthClear, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        const VkRenderingAttachmentInfo colorAttachments[] = {visibilityAttachment, stableIdAttachment};
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 2, &depthAttachment, &stencilAttachment);

        vkCmdBeginRendering(cmd, &renderInfo);

        VisibilityBufferAccumulatePushConstant pushConstants{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sceneIndex * sizeof(SceneData),
            .vertexBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_BUFFER),
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
    RenderPass& clearPass = graph.AddPass(SID("Clear Visibility Intermediates"), VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearPass.WriteClearImage(targets.barycentric);
    clearPass.WriteClearImage(targets.derivatives);
    clearPass.Execute([&](VkCommandBuffer cmd) {
        constexpr VkClearColorValue clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        VkImageSubresourceRange subresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
        vkCmdClearColorImage(cmd, graph.GetImageHandle(targets.barycentric), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &subresource);
        vkCmdClearColorImage(cmd, graph.GetImageHandle(targets.derivatives), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &subresource);
    });

    RenderPass& visBarDer = graph.AddPass(SID("Visibility Barycentric Derivative"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visBarDer.ReadSampledImage(targets.visibility);
    visBarDer.ReadBuffer(SCENE_DATA_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_VERTEX_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_MESHLET_VERTEX_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_MESHLET_TRIANGLE_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_MESHLET_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    visBarDer.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    visBarDer.WriteStorageImage(targets.barycentric);
    visBarDer.WriteStorageImage(targets.derivatives);
    visBarDer.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex](VkCommandBuffer cmd) {
        VisibilityBufferResolvePushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sceneIndex * sizeof(SceneData),
            .vertexBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_BUFFER),
            .meshletVerticesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
            .meshletTrianglesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
            .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .extents = {width, height},
            .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(targets.visibility),
            .barycentricTargetIndex = graph.GetStorageImageViewDescriptorIndex(targets.barycentric),
            .derivativeTargetIndex = graph.GetStorageImageViewDescriptorIndex(targets.derivatives),
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
    RenderPass& clearPass = graph.AddPass(SID("Clear Visibility Shading RTs"), VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearPass.WriteClearImage(targets.albedo);
    clearPass.WriteClearImage(targets.normal);
    clearPass.WriteClearImage(targets.pbr);
    clearPass.WriteClearImage(targets.emissive);
    clearPass.Execute([&](VkCommandBuffer cmd) {
        constexpr VkClearColorValue clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        VkImageSubresourceRange subresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
        vkCmdClearColorImage(cmd, graph.GetImageHandle(targets.albedo), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &subresource);
        vkCmdClearColorImage(cmd, graph.GetImageHandle(targets.normal), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &subresource);
        vkCmdClearColorImage(cmd, graph.GetImageHandle(targets.pbr), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &subresource);
        vkCmdClearColorImage(cmd, graph.GetImageHandle(targets.emissive), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &subresource);
    });

    RenderPass& visShading = graph.AddPass(SID("Visibility Shading"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visShading.ReadSampledImage(targets.visibility);
    visShading.ReadStorageImage(targets.barycentric);
    visShading.ReadStorageImage(targets.derivatives);
    visShading.ReadBuffer(SCENE_DATA_BUFFER);
    visShading.ReadBuffer(GEOMETRY_VERTEX_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MESHLET_VERTEX_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MESHLET_TRIANGLE_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MESHLET_BUFFER);
    visShading.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    visShading.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    visShading.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    visShading.WriteStorageImage(targets.albedo);
    visShading.WriteStorageImage(targets.normal);
    visShading.WriteStorageImage(targets.pbr);
    visShading.WriteStorageImage(targets.emissive);
    visShading.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1], sceneIndex](VkCommandBuffer cmd) {
        VisibilityShadingPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER) + sceneIndex * sizeof(SceneData),
            .vertexBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_BUFFER),
            .meshletVerticesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
            .meshletTrianglesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
            .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .extents = {width, height},
            .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(targets.visibility),
            .barycentricBufferIndex = graph.GetStorageImageViewDescriptorIndex(targets.barycentric),
            .derivativeBufferIndex = graph.GetStorageImageViewDescriptorIndex(targets.derivatives),
            .albedoTargetIndex = graph.GetStorageImageViewDescriptorIndex(targets.albedo),
            .normalTargetIndex = graph.GetStorageImageViewDescriptorIndex(targets.normal),
            .pbrTargetIndex = graph.GetStorageImageViewDescriptorIndex(targets.pbr),
            .emissiveTargetIndex = graph.GetStorageImageViewDescriptorIndex(targets.emissive),
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("visibility_shading"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
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
