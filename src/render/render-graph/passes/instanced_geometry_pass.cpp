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
    // todo: make the passes create and cache these, this is wildly expensive
    const StringID instance_meshlet_offsets = SID_CONCAT(config.prefix, "_instance_meshlet_offsets");
    const StringID level1_sums = SID_CONCAT(config.prefix, "_level1_sums");
    const StringID level1_block_sums = SID_CONCAT(config.prefix, "_level1_block_sums");
    const StringID level2_sums = SID_CONCAT(config.prefix, "_level2_sums");
    const StringID level2_block_sums = SID_CONCAT(config.prefix, "_level2_block_sums");
    const StringID scanned_level2_block_sums = SID_CONCAT(config.prefix, "_scanned_level2_block_sums");
    const StringID intermediate_meshlets = SID_CONCAT(config.prefix, "_intermediate_meshlets");
    const StringID meshlet_level1_sums = SID_CONCAT(config.prefix, "_meshlet_level1_sums");
    const StringID meshlet_level1_block_sums = SID_CONCAT(config.prefix, "_meshlet_level1_block_sums");
    const StringID meshlet_level2_sums = SID_CONCAT(config.prefix, "_meshlet_level2_sums");
    const StringID meshlet_level2_block_sums = SID_CONCAT(config.prefix, "_meshlet_level2_block_sums");
    const StringID meshlet_scanned_level2_block_sums = SID_CONCAT(config.prefix, "_meshlet_scanned_level2_block_sums");
    const StringID visible_meshlets = SID_CONCAT(config.prefix, "_visible_meshlets");
    const StringID meshlet_count_dispatch_args = SID_CONCAT(config.prefix, "_meshlet_count_dispatch_args");
    const StringID compacted_meshlet_dispatch_args = SID_CONCAT(config.prefix, "_compacted_meshlet_dispatch_args");

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

        RenderPass& clearPass = graph.AddPass(SID_CONCAT(config.prefix.c_str(), "Clear Temp Instancing Buffers"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
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
        RenderPass& instanceLODPass = graph.AddPass(
            SID_CONCAT(config.prefix, "Instance Visibility/LOD Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        instanceLODPass.ReadBuffer(SCENE_DATA_BUFFER);
        instanceLODPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
        instanceLODPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        instanceLODPass.WriteBuffer(instance_meshlet_offsets);
        instanceLODPass.Execute([instance_meshlet_offsets,
                instanceCount = config.instanceCount,
                lodBias = config.lodBias,
                instanceBufferOffset = config.instanceBufferOffset,
                &graph,
                pipelineManager,
                sceneDataIndex](VkCommandBuffer cmd) {
                InstanceLODPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER) + instanceBufferOffset,
                    .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                    .instanceCount = instanceCount,
                    .sceneDataIndex = sceneDataIndex,
                    .lodBias = lodBias,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_instance_lod"));
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

        RenderPass& upsweep1Pass = graph.AddPass(
            SID_CONCAT(config.prefix, "Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
            RenderPass& upsweep2Pass = graph.AddPass(
                SID_CONCAT(config.prefix, "Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            RenderPass& scanBlocksPass = graph.AddPass(
                SID_CONCAT(config.prefix, "Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            RenderPass& downsweep1Pass = graph.AddPass(
                SID_CONCAT(config.prefix, "Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
            RenderPass& scanBlocksPass = graph.AddPass(
                SID_CONCAT(config.prefix, "Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& downsweep2Pass = graph.AddPass(
            SID_CONCAT(config.prefix, "Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& totalMeshletCalculator = graph.AddPass(
            SID_CONCAT(config.prefix, "Total Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
        RenderPass& expandInstancesToMeshlets = graph.AddPass(
            SID_CONCAT(config.prefix, "Expand Instance To Meshlet"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        expandInstancesToMeshlets.ReadBuffer(SCENE_DATA_BUFFER);
        expandInstancesToMeshlets.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        expandInstancesToMeshlets.ReadBuffer(instance_meshlet_offsets);
        expandInstancesToMeshlets.ReadIndirectBuffer(meshlet_count_dispatch_args);
        expandInstancesToMeshlets.WriteBuffer(intermediate_meshlets);
        expandInstancesToMeshlets.Execute([instance_meshlet_offsets, meshlet_count_dispatch_args, intermediate_meshlets, instanceBufferOffset = config.instanceBufferOffset,
                &graph, pipelineManager, instanceCount, highestMeshletCount, sceneDataIndex](VkCommandBuffer cmd) {
                ExpandMeshletsPushConstant pc{
                    .indirectDispatchBuffer = graph.GetBufferAddress(meshlet_count_dispatch_args),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instance_meshlet_offsets),
                    .intermediateMeshlets = graph.GetBufferAddress(intermediate_meshlets),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER) + instanceBufferOffset,
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                    .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .sceneDataIndex = sceneDataIndex,
                    .instanceCount = instanceCount,
                    .currentFrameBufferMeshletLimit = highestMeshletCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_expand_instance_to_meshlet"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshlet_count_dispatch_args), offsetof(InstancingMeshletDispatchIndirect, x));
            });
    }

    // Prefix Sum for Compaction
    {
        uint32_t meshletLevel1BlockCount = (highestMeshletCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
        uint32_t meshletLevel2BlockCount = (meshletLevel1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

        RenderPass& meshletUpsweep1Pass = graph.AddPass(
            SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Upsweep 1"),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
            RenderPass& meshletUpsweep2Pass = graph.AddPass(
                SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Upsweep 2"),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            RenderPass& meshletScanBlocksPass = graph.AddPass(
                SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Scan Blocks"),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            RenderPass& meshletDownsweep1Pass = graph.AddPass(
                SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Downsweep 1"),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
            RenderPass& meshletScanBlocksPass = graph.AddPass(
                SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Scan Blocks"),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& meshletDownsweep2Pass = graph.AddPass(
            SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Downsweep 2"),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& compactedDispatchCalc = graph.AddPass(
            SID_CONCAT(config.prefix, "Compacted Meshlet Dispatch Calculation"),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
}

InstancedGeometryPassOutputs SetupInstancedGeometryShadowPass(RenderGraph& graph, const InstancedGeometryPassConfig& config, PipelineManager* pipelineManager, uint32_t sceneDataIndex,
                                                              uint32_t cascadeIndex, bool bAllowBufferAliasing)
{
    // Pre-build all buffer names
    const StringID instance_meshlet_offsets = SID_CONCAT(config.prefix, "_instance_meshlet_offsets");
    const StringID level1_sums = SID_CONCAT(config.prefix, "_level1_sums");
    const StringID level1_block_sums = SID_CONCAT(config.prefix, "_level1_block_sums");
    const StringID level2_sums = SID_CONCAT(config.prefix, "_level2_sums");
    const StringID level2_block_sums = SID_CONCAT(config.prefix, "_level2_block_sums");
    const StringID scanned_level2_block_sums = SID_CONCAT(config.prefix, "_scanned_level2_block_sums");
    const StringID intermediate_meshlets = SID_CONCAT(config.prefix, "_intermediate_meshlets");
    const StringID meshlet_level1_sums = SID_CONCAT(config.prefix, "_meshlet_level1_sums");
    const StringID meshlet_level1_block_sums = SID_CONCAT(config.prefix, "_meshlet_level1_block_sums");
    const StringID meshlet_level2_sums = SID_CONCAT(config.prefix, "_meshlet_level2_sums");
    const StringID meshlet_level2_block_sums = SID_CONCAT(config.prefix, "_meshlet_level2_block_sums");
    const StringID meshlet_scanned_level2_block_sums = SID_CONCAT(config.prefix, "_meshlet_scanned_level2_block_sums");
    const StringID visible_meshlets = SID_CONCAT(config.prefix, "_visible_meshlets");
    const StringID meshlet_count_dispatch_args = SID_CONCAT(config.prefix, "_meshlet_count_dispatch_args");
    const StringID compacted_meshlet_dispatch_args = SID_CONCAT(config.prefix, "_compacted_meshlet_dispatch_args");

    // Create and Clear
    {
        graph.CreateBuffer(instance_meshlet_offsets, config.instanceMeshletOffsetsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(level1_sums, config.level1SumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(level1_block_sums, config.level1BlockSumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(level2_sums, config.level2SumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(level2_block_sums, config.level2BlockSumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(scanned_level2_block_sums, config.scannedLevel2BlockSumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(intermediate_meshlets, config.intermediateMeshletBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_level1_sums, config.meshletLevel1SumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_level1_block_sums, config.meshletLevel1BlockSumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_level2_sums, config.meshletLevel2SumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_level2_block_sums, config.meshletLevel2BlockSumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_scanned_level2_block_sums, config.meshletScannedLevel2BlockSumsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(visible_meshlets, config.visibleMeshletsBufferSize, bAllowBufferAliasing);
        graph.CreateBuffer(meshlet_count_dispatch_args, sizeof(InstancingMeshletDispatchIndirect), bAllowBufferAliasing);
        graph.CreateBuffer(compacted_meshlet_dispatch_args, sizeof(InstancingCompactedMeshletDispatchIndirect), bAllowBufferAliasing);

        RenderPass& clearPass = graph.AddPass(SID_CONCAT(config.prefix, "Clear Temp Instancing Buffers"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
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
        RenderPass& instanceLODPass = graph.AddPass(SID_CONCAT(config.prefix, "Instance Visibility/LOD Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& upsweep1Pass = graph.AddPass(SID_CONCAT(config.prefix, "Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
            RenderPass& upsweep2Pass = graph.AddPass(SID_CONCAT(config.prefix, "Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            RenderPass& scanBlocksPass = graph.AddPass(SID_CONCAT(config.prefix, "Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            RenderPass& downsweep1Pass = graph.AddPass(SID_CONCAT(config.prefix, "Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
            RenderPass& scanBlocksPass = graph.AddPass(SID_CONCAT(config.prefix, "Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& downsweep2Pass = graph.AddPass(SID_CONCAT(config.prefix, "Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& totalMeshletCalculator = graph.AddPass(SID_CONCAT(config.prefix, "Total Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
        RenderPass& expandInstancesToMeshlets = graph.AddPass(SID_CONCAT(config.prefix, "Expand Instance To Meshlet"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& meshletUpsweep1Pass = graph.AddPass(SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
            RenderPass& meshletUpsweep2Pass = graph.AddPass(SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            RenderPass& meshletScanBlocksPass = graph.AddPass(SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

            RenderPass& meshletDownsweep1Pass = graph.AddPass(SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
            RenderPass& meshletScanBlocksPass = graph.AddPass(SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& meshletDownsweep2Pass = graph.AddPass(SID_CONCAT(config.prefix, "Meshlet Visibility Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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

        RenderPass& compactedDispatchCalc = graph.AddPass(SID_CONCAT(config.prefix, "Compacted Meshlet Dispatch Calculation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
}
} // Render
