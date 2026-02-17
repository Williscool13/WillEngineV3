//
// Created by William on 2026-02-14.
//

#include "instanced_geometry_pass.h"

#include <vulkan/vulkan_core.h>

#include "../../pipelines/pipeline_data.h"
#include "../../pipelines/pipeline_manager.h"
#include "../render_graph.h"
#include "../render_pass.h"

namespace Render
{
InstancedGeometryPassOutputs SetupInstancedGeometryPass(RenderGraph& graph, const InstancedGeometryPassConfig& config, PipelineManager* pipelineManager, uint32_t sceneDataIndex)
{
    // Pre-build all buffer names
    const std::string instance_meshlet_offsets = config.prefix + "instance_meshlet_offsets";
    const std::string level1_sums = config.prefix + "level1_sums";
    const std::string level1_block_sums = config.prefix + "level1_block_sums";
    const std::string level2_sums = config.prefix + "level2_sums";
    const std::string level2_block_sums = config.prefix + "level2_block_sums";
    const std::string scanned_level2_block_sums = config.prefix + "scanned_level2_block_sums";
    const std::string intermediate_meshlets = config.prefix + "intermediate_meshlets";
    const std::string meshlet_level1_sums = config.prefix + "meshlet_level1_sums";
    const std::string meshlet_level1_block_sums = config.prefix + "meshlet_level1_block_sums";
    const std::string meshlet_level2_sums = config.prefix + "meshlet_level2_sums";
    const std::string meshlet_level2_block_sums = config.prefix + "meshlet_level2_block_sums";
    const std::string meshlet_scanned_level2_block_sums = config.prefix + "meshlet_scanned_level2_block_sums";
    const std::string visible_meshlets = config.prefix + "visible_meshlets";
    const std::string meshlet_count_dispatch_args = config.prefix + "meshlet_count_dispatch_args";
    const std::string compacted_meshlet_dispatch_args = config.prefix + "compacted_meshlet_dispatch_args";

    // Create and Clear
    {
        graph.CreateBuffer(instance_meshlet_offsets, config.instanceMeshletOffsetsBufferSize);
        graph.CreateBuffer(level1_sums, config.level1SumsBufferSize);
        graph.CreateBuffer(level1_block_sums, config.level1BlockSumsBufferSize);
        graph.CreateBuffer(level2_sums, config.level2SumsBufferSize);
        graph.CreateBuffer(level2_block_sums, config.level2BlockSumsBufferSize);
        graph.CreateBuffer(scanned_level2_block_sums, config.scannedLevel2BlockSumsBufferSize);
        graph.CreateBuffer(intermediate_meshlets, config.intermediateMeshletBufferSize);
        graph.CreateBuffer(meshlet_level1_sums, config.meshletLevel1SumsBufferSize);
        graph.CreateBuffer(meshlet_level1_block_sums, config.meshletLevel1BlockSumsBufferSize);
        graph.CreateBuffer(meshlet_level2_sums, config.meshletLevel2SumsBufferSize);
        graph.CreateBuffer(meshlet_level2_block_sums, config.meshletLevel2BlockSumsBufferSize);
        graph.CreateBuffer(meshlet_scanned_level2_block_sums, config.meshletScannedLevel2BlockSumsBufferSize);
        graph.CreateBuffer(visible_meshlets, config.visibleMeshletsBufferSize);
        graph.CreateBuffer(meshlet_count_dispatch_args, sizeof(InstancingMeshletDispatchIndirect));
        graph.CreateBuffer(compacted_meshlet_dispatch_args, sizeof(InstancingCompactedMeshletDispatchIndirect));

        RenderPass& clearPass = graph.AddPass(config.prefix + "Clear Temp Instancing Buffers", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
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
        RenderPass& instanceLODPass = graph.AddPass(config.prefix + "Instance Visibility/LOD Selection", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        instanceLODPass.ReadBuffer(GEOMETRY_BUFFER_SCENE_DATA);
        instanceLODPass.ReadBuffer(GEOMETRY_BUFFER_MODEL);
        instanceLODPass.ReadBuffer(config.instanceBufferName);
        instanceLODPass.WriteBuffer(instance_meshlet_offsets);
        instanceLODPass.Execute([instance_meshlet_offsets,
                instanceCount = config.instanceCount,
                lodBias = config.lodBias,
                instanceBufferName = config.instanceBufferName,
                &graph,
                pipelineManager,
                sceneDataIndex](VkCommandBuffer cmd) {
                InstanceLODPushConstant pc{
                    .sceneData = graph.GetBufferAddress(GEOMETRY_BUFFER_SCENE_DATA),
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_BUFFER_PRIMITIVE),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_BUFFER_MODEL),
                    .instanceBuffer = graph.GetBufferAddress(instanceBufferName),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                    .instanceCount = instanceCount,
                    .sceneDataIndex = sceneDataIndex,
                    .lodBias = lodBias,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_instance_lod");
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

        RenderPass& upsweep1Pass = graph.AddPass(config.prefix + "Prefix Sum Upsweep 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_up_1");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, level1BlockCount, 1, 1);
        });

        uint32_t level2BlockCount = (level1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

        if (level2BlockCount > 1) {
            RenderPass& upsweep2Pass = graph.AddPass(config.prefix + "Prefix Sum Upsweep 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_up_2");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level2BlockCount, 1, 1);
            });

            RenderPass& scanBlocksPass = graph.AddPass(config.prefix + "Prefix Sum Scan Blocks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            scanBlocksPass.ReadBuffer(level2_block_sums);
            scanBlocksPass.WriteBuffer(scanned_level2_block_sums);
            scanBlocksPass.Execute([level2_block_sums, scanned_level2_block_sums, &graph, pipelineManager, level2BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress(level2_block_sums),
                    .scannedLevel2BlockSums = graph.GetBufferAddress(scanned_level2_block_sums),
                    .blockCount = level2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& downsweep1Pass = graph.AddPass(config.prefix + "Prefix Sum Downsweep 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            downsweep1Pass.ReadBuffer(scanned_level2_block_sums);
            downsweep1Pass.ReadWriteBuffer(level2_sums);
            downsweep1Pass.Execute([scanned_level2_block_sums, level2_sums, &graph, pipelineManager, level1BlockCount, level2BlockCount](VkCommandBuffer cmd) {
                PrefixSumDownsweep1PushConstant pc{
                    .scannedLevel2BlockSums = graph.GetBufferAddress(scanned_level2_block_sums),
                    .level2Sums = graph.GetBufferAddress(level2_sums),
                    .elementCount = level1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_down_1");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level2BlockCount, 1, 1);
            });
        }
        else {
            RenderPass& scanBlocksPass = graph.AddPass(config.prefix + "Prefix Sum Scan Blocks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            scanBlocksPass.ReadBuffer(level1_block_sums);
            scanBlocksPass.WriteBuffer(scanned_level2_block_sums);
            scanBlocksPass.Execute([level1_block_sums, scanned_level2_block_sums, &graph, pipelineManager, level1BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress(level1_block_sums),
                    .scannedLevel2BlockSums = graph.GetBufferAddress(scanned_level2_block_sums),
                    .blockCount = level1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        RenderPass& downsweep2Pass = graph.AddPass(config.prefix + "Prefix Sum Downsweep 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_down_2");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level1BlockCount, 1, 1);
            });

        RenderPass& totalMeshletCalculator = graph.AddPass(config.prefix + "Total Meshlet Count", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        totalMeshletCalculator.ReadBuffer(instance_meshlet_offsets);
        totalMeshletCalculator.WriteBuffer(meshlet_count_dispatch_args);
        totalMeshletCalculator.Execute([instance_meshlet_offsets, meshlet_count_dispatch_args, &graph, pipelineManager, instanceCount](VkCommandBuffer cmd) {
            TotalMeshletCountPushConstant pc{
                .indirectDispatchBuffer = graph.GetBufferAddress(meshlet_count_dispatch_args),
                .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                .instanceCount = instanceCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_total_meshlet_count");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    uint32_t highestMeshletCount = config.visibleMeshletUpperBound;

    // Expand Instance to Meshlet
    {
        RenderPass& expandInstancesToMeshlets = graph.AddPass(config.prefix + "Expand Instance To Meshlet", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        expandInstancesToMeshlets.ReadBuffer(GEOMETRY_BUFFER_SCENE_DATA);
        expandInstancesToMeshlets.ReadBuffer(instance_meshlet_offsets);
        expandInstancesToMeshlets.ReadIndirectBuffer(meshlet_count_dispatch_args);
        expandInstancesToMeshlets.WriteBuffer(intermediate_meshlets);
        expandInstancesToMeshlets.Execute([instance_meshlet_offsets, meshlet_count_dispatch_args, intermediate_meshlets, instanceBufferName = config.instanceBufferName,
                &graph, pipelineManager, instanceCount, highestMeshletCount, sceneDataIndex](VkCommandBuffer cmd) {
                ExpandMeshletsPushConstant pc{
                    .indirectDispatchBuffer = graph.GetBufferAddress(meshlet_count_dispatch_args),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                    .intermediateMeshlets = graph.GetBufferAddress(intermediate_meshlets),
                    .instanceBuffer = graph.GetBufferAddress(instanceBufferName),
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_BUFFER_PRIMITIVE),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_BUFFER_MODEL),
                    .meshletBuffer = graph.GetBufferAddress(GEOMETRY_BUFFER_MESHLET),
                    .sceneData = graph.GetBufferAddress(GEOMETRY_BUFFER_SCENE_DATA),
                    .sceneDataIndex = sceneDataIndex,
                    .instanceCount = instanceCount,
                    .currentFrameBufferMeshletLimit = highestMeshletCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_expand_instance_to_meshlet");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshlet_count_dispatch_args), offsetof(InstancingMeshletDispatchIndirect, x));
            });
    }

    // Prefix Sum for Compaction
    {
        uint32_t meshletLevel1BlockCount = (highestMeshletCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
        uint32_t meshletLevel2BlockCount = (meshletLevel1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

        RenderPass& meshletUpsweep1Pass = graph.AddPass(config.prefix + "Meshlet Visibility Prefix Sum Upsweep 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_meshlet_visibility_prefix_sum_up_1");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshlet_count_dispatch_args), offsetof(InstancingMeshletDispatchIndirect, x));
            });

        if (meshletLevel2BlockCount > 1) {
            RenderPass& meshletUpsweep2Pass = graph.AddPass(config.prefix + "Meshlet Visibility Prefix Sum Upsweep 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_up_2");
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
                });

            RenderPass& meshletScanBlocksPass = graph.AddPass(config.prefix + "Meshlet Visibility Prefix Sum Scan Blocks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletScanBlocksPass.ReadBuffer(meshlet_level2_block_sums);
            meshletScanBlocksPass.WriteBuffer(meshlet_scanned_level2_block_sums);
            meshletScanBlocksPass.Execute([meshlet_level2_block_sums, meshlet_scanned_level2_block_sums, &graph, pipelineManager, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress(meshlet_level2_block_sums),
                    .scannedLevel2BlockSums = graph.GetBufferAddress(meshlet_scanned_level2_block_sums),
                    .blockCount = meshletLevel2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& meshletDownsweep1Pass = graph.AddPass(config.prefix + "Meshlet Visibility Prefix Sum Downsweep 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletDownsweep1Pass.ReadBuffer(meshlet_scanned_level2_block_sums);
            meshletDownsweep1Pass.ReadWriteBuffer(meshlet_level2_sums);
            meshletDownsweep1Pass.Execute([meshlet_scanned_level2_block_sums, meshlet_level2_sums, &graph, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                PrefixSumDownsweep1PushConstant pc{
                    .scannedLevel2BlockSums = graph.GetBufferAddress(meshlet_scanned_level2_block_sums),
                    .level2Sums = graph.GetBufferAddress(meshlet_level2_sums),
                    .elementCount = meshletLevel1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_down_1");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
            });
        }
        else {
            RenderPass& meshletScanBlocksPass = graph.AddPass(config.prefix + "Meshlet Visibility Prefix Sum Scan Blocks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletScanBlocksPass.ReadBuffer(meshlet_level1_block_sums);
            meshletScanBlocksPass.WriteBuffer(meshlet_scanned_level2_block_sums);
            meshletScanBlocksPass.Execute([meshlet_level1_block_sums, meshlet_scanned_level2_block_sums, &graph, pipelineManager, meshletLevel1BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress(meshlet_level1_block_sums),
                    .scannedLevel2BlockSums = graph.GetBufferAddress(meshlet_scanned_level2_block_sums),
                    .blockCount = meshletLevel1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        RenderPass& meshletDownsweep2Pass = graph.AddPass(config.prefix + "Meshlet Visibility Prefix Sum Downsweep 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_meshlet_visibility_prefix_sum_down_2");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshlet_count_dispatch_args), offsetof(InstancingMeshletDispatchIndirect, x));
            });

        RenderPass& compactedDispatchCalc = graph.AddPass(config.prefix + "Compacted Meshlet Dispatch Calculation", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        compactedDispatchCalc.ReadWriteBuffer(compacted_meshlet_dispatch_args);
        compactedDispatchCalc.Execute([compacted_meshlet_dispatch_args, &graph, pipelineManager, highestMeshletCount](VkCommandBuffer cmd) {
            CompactedMeshletDispatchPushConstant pc{
                .compactedDispatchBuffer = graph.GetBufferAddress(compacted_meshlet_dispatch_args),
                .currentFrameBufferMeshletLimit = highestMeshletCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_compacted_meshlet_dispatch");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    RenderPass& maxMeshletCount = graph.AddPass("Max Meshlet Count", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    maxMeshletCount.ReadBuffer(meshlet_count_dispatch_args);
    maxMeshletCount.ReadWriteBuffer("readback_buffer");
    maxMeshletCount.Execute([&, pipelineManager, bufferSrc = meshlet_count_dispatch_args](VkCommandBuffer cmd) {
        MaxMeshletCountPushConstant pc{
            .indirectDispatchBuffer = graph.GetBufferAddress(bufferSrc),
            .currentHighest = graph.GetBufferAddress("readback_buffer") + offsetof(ReadbackStruct, meshletCount),
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_max_meshlet_count");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });

    return InstancedGeometryPassOutputs{
        .visibleMeshlets = visible_meshlets,
        .compactedDispatchArgs = compacted_meshlet_dispatch_args,
        .meshletCountDispatchArgs = meshlet_count_dispatch_args,
    };
}
} // Render
