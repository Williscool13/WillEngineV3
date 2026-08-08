//
// Created by William on 2026-06-03.
//

#include "render/passes/geometry_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupGeometryPass(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       const RenderFamilyProperties& renderFamilyProperties,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex)
{
    if (viewFamily.primitiveInstances.IsEmpty()) {
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
    auto instanceCount = static_cast<uint32_t>(viewFamily.primitiveInstances.Size());
    auto lodBias = static_cast<int32_t>(LOD_BIAS);
    auto highestMeshletCount = renderFamilyProperties.visibleMeshletUpperBound;

    const StringID visBits = SID("instance_vis_bits");
    const bool bOcclusion = sceneIndex == 0 && renderFamilyProperties.bOcclusionCulling;
    const bool bOcclusionFreeze = bOcclusion && renderFamilyProperties.bOcclusionFreeze;
    const uint32_t cullFlags = renderFamilyProperties.cullFlags;

    // Shared buffers
    {
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
            if (bOcclusion) {
                // Fixed size so the cross-frame carry never resizes; garbage content on first use is safe (phase 2 corrects)
                graph.CreateBuffer(visBits, MAX_INSTANCE_SLOTS / 8, false);
                graph.CarryBufferToNextFrame(visBits, visBits, 0);
            }
        }
    }

    auto addCullChain = [&](bool bPhase2) {
        // Clear; phase 1 also zeroes the per-frame cull tallies (contiguous ReadbackStruct region)
        {
            RenderPass& clearDispatchArgs = graph.AddPass(SID("Clear Compacted Dispatch Args"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::Geometry);
            clearDispatchArgs.WriteTransferBuffer(compactedMeshletDispatchArgs);
            clearDispatchArgs.Execute([compactedMeshletDispatchArgs](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(compactedMeshletDispatchArgs), 0, VK_WHOLE_SIZE, 0);
            });
        }

        // Instance Visibility/LOD: phase 1 gates on last frame's bit, phase 2 tests against the fresh pyramid
        if (bPhase2) {
            RenderPass& instanceLODPass = graph.AddPass(SID("Instance Occlusion/LOD Selection P2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
            instanceLODPass.ReadBuffer(SCENE_DATA_BUFFER);
            instanceLODPass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
            instanceLODPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
            instanceLODPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            instanceLODPass.ReadSampledImage(HIZ_PYRAMID);
            instanceLODPass.ReadWriteBuffer(visBits);
            instanceLODPass.ReadWriteBuffer(instanceMeshletOffsets);
            if (GPU_STATS_ENABLED) {
                instanceLODPass.ReadWriteBuffer(SID("readback_buffer"));
            }
            instanceLODPass.Execute([instanceMeshletOffsets, visBits, instanceCount, lodBias, cullFlags, pipelineManager, sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const ResourceDimensions& dims = graph.GetImageDimensions(HIZ_PYRAMID);
                InstanceLODOcclusionPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .visBits = graph.GetBufferAddress(visBits),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                    .occludedCounter = GPU_STATS_ENABLED ? graph.GetBufferAddress(SID("readback_buffer")) + offsetof(ReadbackStruct, culledInstanceOcclusion) : 0,
                    .hizExtent = {dims.width, dims.height},
                    .instanceCount = instanceCount,
                    .sceneDataIndex = sceneIndex,
                    .lodBias = lodBias,
                    .hizIndex = graph.GetSampledImageViewDescriptorIndex(HIZ_PYRAMID),
                    .hizMipCount = dims.levels,
                    .cullFlags = cullFlags,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_instance_lod_occlusion"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                uint32_t xDispatch = (instanceCount + INSTANCING_VISIBILITY_DISPATCH_X - 1) / INSTANCING_VISIBILITY_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });
        }
        else {
            RenderPass& instanceLODPass = graph.AddPass(SID("Instance Visibility/LOD Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
            instanceLODPass.ReadBuffer(SCENE_DATA_BUFFER);
            instanceLODPass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
            instanceLODPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
            instanceLODPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            if (bOcclusion) {
                instanceLODPass.ReadBuffer(visBits);
            }
            instanceLODPass.WriteBuffer(instanceMeshletOffsets);
            if (GPU_STATS_ENABLED) {
                instanceLODPass.ReadWriteBuffer(SID("readback_buffer"));
            }
            instanceLODPass.Execute(
                [instanceMeshletOffsets,
                    visBits,
                    bOcclusion,
                    instanceCount,
                    lodBias,
                    cullFlags,
                    pipelineManager,
                    sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    InstanceLODPushConstant pc{
                        .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                        .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                        .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                        .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                        .visBits = bOcclusion ? graph.GetBufferAddress(visBits) : 0,
                        .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                        .cullStats = GPU_STATS_ENABLED ? graph.GetBufferAddress(SID("readback_buffer")) + offsetof(ReadbackStruct, culledInstanceFrustum) : 0,
                        .instanceCount = instanceCount,
                        .sceneDataIndex = sceneIndex,
                        .lodBias = lodBias,
                        .cullFlags = cullFlags,
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

            RenderPass& upsweep1Pass = graph.AddPass(SID("Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
            upsweep1Pass.ReadBuffer(instanceMeshletOffsets);
            upsweep1Pass.WriteBuffer(level1Sums);
            upsweep1Pass.WriteBuffer(level1BlockSums);
            upsweep1Pass.Execute([instanceMeshletOffsets, level1Sums, level1BlockSums, pipelineManager, instanceCount, level1BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                RenderPass& upsweep2Pass = graph.AddPass(SID("Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
                upsweep2Pass.ReadBuffer(level1BlockSums);
                upsweep2Pass.WriteBuffer(level2Sums);
                upsweep2Pass.WriteBuffer(level2BlockSums);
                upsweep2Pass.Execute([level1BlockSums, level2Sums, level2BlockSums, pipelineManager, level1BlockCount, level2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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

                RenderPass& scanBlocksPass = graph.AddPass(SID("Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
                scanBlocksPass.ReadBuffer(level2BlockSums);
                scanBlocksPass.WriteBuffer(scannedLevel2BlockSums);
                scanBlocksPass.Execute([level2BlockSums, scannedLevel2BlockSums, pipelineManager, level2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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

                RenderPass& downsweep1Pass = graph.AddPass(SID("Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
                downsweep1Pass.ReadBuffer(scannedLevel2BlockSums);
                downsweep1Pass.ReadWriteBuffer(level2Sums);
                downsweep1Pass.Execute([scannedLevel2BlockSums, level2Sums, pipelineManager, level1BlockCount, level2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                RenderPass& scanBlocksPass = graph.AddPass(SID("Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
                scanBlocksPass.ReadBuffer(level1BlockSums);
                scanBlocksPass.WriteBuffer(scannedLevel2BlockSums);
                scanBlocksPass.Execute([level1BlockSums, scannedLevel2BlockSums, pipelineManager, level1BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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

            RenderPass& downsweep2Pass = graph.AddPass(SID("Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
            downsweep2Pass.ReadBuffer(level1Sums);
            if (level2BlockCount > 1) {
                downsweep2Pass.ReadBuffer(level2Sums);
            }
            else {
                downsweep2Pass.ReadBuffer(scannedLevel2BlockSums);
            }
            downsweep2Pass.WriteBuffer(instanceMeshletOffsets);
            downsweep2Pass.Execute(
                [level1Sums, level2Sums, scannedLevel2BlockSums, instanceMeshletOffsets, pipelineManager, level2BlockCount, instanceCount, level1BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                SID("Total Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
            totalMeshletCalculator.ReadBuffer(instanceMeshletOffsets);
            totalMeshletCalculator.WriteBuffer(meshletCountDispatchArgs);
            totalMeshletCalculator.Execute([instanceMeshletOffsets, meshletCountDispatchArgs, pipelineManager, instanceCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                SID("Expand Instance To Meshlet"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
            expandInstancesToMeshlets.ReadBuffer(SCENE_DATA_BUFFER);
            expandInstancesToMeshlets.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            expandInstancesToMeshlets.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
            expandInstancesToMeshlets.ReadBuffer(GEOMETRY_MODEL_BUFFER);
            expandInstancesToMeshlets.ReadBuffer(GEOMETRY_MESHLET_BUFFER);
            expandInstancesToMeshlets.ReadBuffer(instanceMeshletOffsets);
            expandInstancesToMeshlets.ReadIndirectBuffer(meshletCountDispatchArgs);
            expandInstancesToMeshlets.WriteBuffer(intermediateMeshlets);
            if (bPhase2) {
                expandInstancesToMeshlets.ReadSampledImage(HIZ_PYRAMID);
            }
            if (GPU_STATS_ENABLED) {
                expandInstancesToMeshlets.ReadWriteBuffer(SID("readback_buffer"));
            }
            expandInstancesToMeshlets.Execute([instanceMeshletOffsets, meshletCountDispatchArgs, intermediateMeshlets,
                    pipelineManager, instanceCount, highestMeshletCount, sceneIndex, bPhase2, cullFlags](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    uint2 hizExtent{0u, 0u};
                    uint32_t hizIndex = 0;
                    uint32_t hizMipCount = 1;
                    if (bPhase2) {
                        const ResourceDimensions& dims = graph.GetImageDimensions(HIZ_PYRAMID);
                        hizExtent = {dims.width, dims.height};
                        hizIndex = graph.GetSampledImageViewDescriptorIndex(HIZ_PYRAMID);
                        hizMipCount = dims.levels;
                    }
                    ExpandMeshletsPushConstant pc{
                        .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
                        .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                        .intermediateMeshlets = graph.GetBufferAddress(intermediateMeshlets),
                        .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                        .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                        .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                        .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                        .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                        .cullStats = GPU_STATS_ENABLED ? graph.GetBufferAddress(SID("readback_buffer")) + offsetof(ReadbackStruct, culledMeshletFrustum) : 0,
                        .hizExtent = hizExtent,
                        .sceneDataIndex = sceneIndex,
                        .instanceCount = instanceCount,
                        .currentFrameBufferMeshletLimit = highestMeshletCount,
                        .hizIndex = hizIndex,
                        .hizMipCount = hizMipCount,
                        .bHiZ = bPhase2 ? 1u : 0u,
                        .cullFlags = cullFlags,
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
                SID("Meshlet Visibility Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
            meshletUpsweep1Pass.ReadBuffer(intermediateMeshlets);
            meshletUpsweep1Pass.WriteBuffer(meshletLevel1Sums);
            meshletUpsweep1Pass.WriteBuffer(meshletLevel1BlockSums);
            meshletUpsweep1Pass.ReadIndirectBuffer(meshletCountDispatchArgs);
            meshletUpsweep1Pass.Execute(
                [intermediateMeshlets, meshletCountDispatchArgs, meshletLevel1Sums, meshletLevel1BlockSums, pipelineManager, meshletLevel1BlockCount, highestMeshletCount
                ](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                    SID("Meshlet Visibility Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
                meshletUpsweep2Pass.ReadBuffer(meshletLevel1BlockSums);
                meshletUpsweep2Pass.WriteBuffer(meshletLevel2Sums);
                meshletUpsweep2Pass.WriteBuffer(meshletLevel2BlockSums);
                meshletUpsweep2Pass.Execute(
                    [meshletLevel1BlockSums, meshletLevel2Sums, meshletLevel2BlockSums, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                    SID("Meshlet Visibility Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
                meshletScanBlocksPass.ReadBuffer(meshletLevel2BlockSums);
                meshletScanBlocksPass.WriteBuffer(meshletScannedLevel2BlockSums);
                meshletScanBlocksPass.Execute([meshletLevel2BlockSums, meshletScannedLevel2BlockSums, pipelineManager, meshletLevel2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                    SID("Meshlet Visibility Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
                meshletDownsweep1Pass.ReadBuffer(meshletScannedLevel2BlockSums);
                meshletDownsweep1Pass.ReadWriteBuffer(meshletLevel2Sums);
                meshletDownsweep1Pass.Execute([meshletScannedLevel2BlockSums, meshletLevel2Sums, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                    SID("Meshlet Visibility Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
                meshletScanBlocksPass.ReadBuffer(meshletLevel1BlockSums);
                meshletScanBlocksPass.WriteBuffer(meshletScannedLevel2BlockSums);
                meshletScanBlocksPass.Execute([meshletLevel1BlockSums, meshletScannedLevel2BlockSums, pipelineManager, meshletLevel1BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                SID("Meshlet Visibility Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
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
                    pipelineManager, meshletLevel2BlockCount, highestMeshletCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                SID("Compacted Meshlet Dispatch Calculation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
            compactedDispatchCalc.ReadWriteBuffer(compactedMeshletDispatchArgs);
            compactedDispatchCalc.Execute([compactedMeshletDispatchArgs, pipelineManager, highestMeshletCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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

        RenderPass& maxMeshletCount = graph.AddPass(SID("Max Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
        maxMeshletCount.ReadBuffer(meshletCountDispatchArgs);
        maxMeshletCount.ReadWriteBuffer(SID("readback_buffer"));
        maxMeshletCount.Execute([&, pipelineManager, bufferSrc = meshletCountDispatchArgs](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            MaxMeshletCountPushConstant pc{
                .indirectDispatchBuffer = graph.GetBufferAddress(bufferSrc),
                .currentHighest = graph.GetBufferAddress(SID("readback_buffer")) + offsetof(ReadbackStruct, meshletCount),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("instancing_max_meshlet_count"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });

        RenderPass& instancedMeshShading = graph.AddPass(
            bPhase2 ? SID("Phase 2 Mesh Shading") : SID("Instanced Mesh Shading"), VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                                                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, Render::RenderCategory::Geometry);
        instancedMeshShading.WriteColorAttachment(targets.visibility);
        instancedMeshShading.WriteColorAttachment(targets.gbufferOne);
        instancedMeshShading.WriteColorAttachment(targets.stableId);
        instancedMeshShading.WriteDepthAttachment(targets.depthStencil);
        instancedMeshShading.ReadBuffer(SCENE_DATA_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_MODEL_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_MESHLET_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_MESHLET_VERTEX_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_MESHLET_TRIANGLE_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_VERTEX_POSITION_BUFFER);
        instancedMeshShading.ReadBuffer(visibleMeshlets);
        instancedMeshShading.ReadIndirectBuffer(compactedMeshletDispatchArgs);
        instancedMeshShading.Execute([&, pipelineManager, visibleMeshlets, compactedMeshletDispatchArgs, sceneIndex, width = renderExtent[0], height = renderExtent[1],
                bWireframe = renderFamilyProperties.bWireframe,
                visibility = targets.visibility, stableId = targets.stableId, depthStencil = targets.depthStencil](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkViewport viewport = VkHelpers::GenerateViewport(width, height);
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                vkCmdSetPolygonModeEXT(cmd, bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL);

                auto visibilityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(visibility), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                auto stableIdAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(stableId), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                auto depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                auto stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

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
    };

    addCullChain(false);

    if (!bOcclusion || bOcclusionFreeze) {
        return;
    }

    // Phase 2: pyramid from phase-1 depth, then the whole chain again with the Hi-Z variants
    SetupHiZPyramid(graph, pipelineManager, renderExtent, targets);

    RenderPass& occlusionClear = graph.AddPass(SID("Occlusion Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::Geometry);
    occlusionClear.WriteTransferBuffer(visBits);
    occlusionClear.Execute([visBits](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(visBits), 0, VK_WHOLE_SIZE, 0);
    });

    addCullChain(true);
}

void SetupVisibilityBarycentricDerivativePass(RenderGraph& graph,
                                              PipelineManager* pipelineManager,
                                              const Core::ViewFamily& viewFamily,
                                              Core::Array<uint32_t, 2> renderExtent,
                                              const RenderTargets& targets,
                                              uint32_t sceneIndex)
{
    RenderPass& visBarDer = graph.AddPass(SID("Visibility Barycentric Derivative"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
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
            visibility = targets.visibility, barycentric = targets.barycentric, derivatives = targets.derivatives](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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

void SetupVisibilityBucketingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  const RenderTargets& targets,
                                  uint32_t sceneIndex)
{
    if (!graph.HasBuffer(SHADING_DISPATCH_BUCKETING_BUFFER)) { return; }
    if (!graph.HasBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER)) { return; }

    RenderPass& boundsPass = graph.AddPass(SID("Shade Bucketing Bounds"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    boundsPass.ReadSampledImage(targets.visibility);
    boundsPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    boundsPass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    boundsPass.ReadWriteBuffer(SHADING_DISPATCH_BUCKETING_BUFFER);
    boundsPass.ReadWriteBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    boundsPass.Execute([&, pipelineManager, width = renderExtent[0], height = renderExtent[1],
            visibility = targets.visibility](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ShadeBucketingPushConstant pc{
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                .shadeDispatchBuffer = graph.GetBufferAddress(SHADING_DISPATCH_BUCKETING_BUFFER),
                .lightDispatchBuffer = graph.GetBufferAddress(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                .extents = {width, height},
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("visibility_bucketing_bounds_calculation"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
        });

    RenderPass& resolvePass = graph.AddPass(SID("Shade Bucketing Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    resolvePass.ReadWriteBuffer(SHADING_DISPATCH_BUCKETING_BUFFER);
    resolvePass.Execute([&, pipelineManager, materialCount = static_cast<uint32_t>(Render::BINDLESS_MATERIAL_BUFFER_COUNT)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        ShadeBucketingResolvePushConstant pc{
            .shadeDispatchBuffer = graph.GetBufferAddress(SHADING_DISPATCH_BUCKETING_BUFFER),
            .materialCount = materialCount,
        };
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("visibility_shading_bucketing_resolve"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (materialCount + 255) / 256, 1, 1);
    });

    RenderPass& lightResolvePass = graph.AddPass(SID("Light Bucketing Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    lightResolvePass.ReadWriteBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    lightResolvePass.Execute([&, pipelineManager, lightingCount = static_cast<uint32_t>(viewFamily.lightingBuckets.Size())](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        LightingBucketingResolvePushConstant pc{
            .lightDispatchBuffer = graph.GetBufferAddress(LIGHTING_DISPATCH_BUCKETING_BUFFER),
            .lightingCount = lightingCount,
        };
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("visibility_lighting_bucketing_resolve"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (lightingCount + 255) / 256, 1, 1);
    });

    if (!GPU_STATS_ENABLED) {
        return;
    }

    // Technically not "critical", used for stats. But since we write to readback_buffer, it becomes critical.
    RenderPass& dispatchCountPass = graph.AddPass(SID("Bucket Dispatch Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    dispatchCountPass.ReadBuffer(SHADING_DISPATCH_BUCKETING_BUFFER);
    dispatchCountPass.ReadBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    dispatchCountPass.ReadWriteBuffer(SID("readback_buffer"));
    dispatchCountPass.Execute([&, pipelineManager,
            materialCount = static_cast<uint32_t>(Render::BINDLESS_MATERIAL_BUFFER_COUNT),
            lightingCount = static_cast<uint32_t>(viewFamily.lightingBuckets.Size())](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            BucketDispatchCountPushConstant pc{
                .shadeDispatchBuffer = graph.GetBufferAddress(SHADING_DISPATCH_BUCKETING_BUFFER),
                .lightDispatchBuffer = graph.GetBufferAddress(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                .countBuffer = graph.GetBufferAddress(SID("readback_buffer")) + offsetof(ReadbackStruct, shadingDispatches),
                .materialCount = materialCount,
                .lightingCount = lightingCount,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("visibility_bucketing_dispatch_count"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
}

void SetupVisibilityShadingPass(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Array<uint32_t, 2> renderExtent,
                                const RenderTargets& targets,
                                uint32_t sceneIndex,
                                Core::Arena& arena)
{
    if (!graph.HasBuffer(SHADING_DISPATCH_BUCKETING_BUFFER)) { return; }

    struct MaterialEntry
    {
        uint32_t materialIndex{};
        StringID fragmentShader{};
    };

    const auto materialCount = static_cast<uint32_t>(viewFamily.activeMaterials.Size());
    auto* sortedMaterials = arena.AllocArray<MaterialEntry>(materialCount);
    for (uint32_t i = 0; i < materialCount; ++i) {
        sortedMaterials[i] = {viewFamily.activeMaterials[i].stableIndex, viewFamily.activeMaterials[i].material.fragmentShader};
    }
    std::sort(sortedMaterials, sortedMaterials + materialCount, [](const MaterialEntry& a, const MaterialEntry& b) {
        return a.fragmentShader < b.fragmentShader;
    });

    RenderPass& visShading = graph.AddPass(SID("Visibility Shading"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
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
    visShading.ReadIndirectBuffer(SHADING_DISPATCH_BUCKETING_BUFFER);
    visShading.WriteStorageImage(targets.gbufferOne);
    visShading.WriteStorageImage(targets.gbufferTwo);
    visShading.Execute([&, pipelineManager, sceneIndex,
            visibility = targets.visibility, barycentric = targets.barycentric, derivatives = targets.derivatives,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            sortedMaterials, materialCount, renderExtent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkDeviceAddress shadeDispatchAddress = graph.GetBufferAddress(SHADING_DISPATCH_BUCKETING_BUFFER);

            StringID boundShader{};
            const PipelineEntry* pipelineEntry = nullptr;

            for (uint32_t i = 0; i < materialCount; ++i) {
                const MaterialEntry& entry = sortedMaterials[i];
                if (!entry.fragmentShader) { continue; }

                StringID shaderToUse = viewFamily.shadingShaderOverride ? viewFamily.shadingShaderOverride : entry.fragmentShader;
                if (shaderToUse != boundShader) {
                    pipelineEntry = pipelineManager->GetPipelineEntry(shaderToUse);
                    assert(pipelineEntry && "Pipeline missing even after sanitization");
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    boundShader = shaderToUse;
                }

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
                    .shadeDispatchBuffer = shadeDispatchAddress,
                    .extents = {renderExtent[0], renderExtent[1]},
                    .materialIndex = entry.materialIndex,
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .barycentricBufferIndex = graph.GetStorageImageViewDescriptorIndex(barycentric),
                    .derivativeBufferIndex = graph.GetStorageImageViewDescriptorIndex(derivatives),
                    .gbufferOneIndex = graph.GetStorageImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetStorageImageViewDescriptorIndex(gbufferTwo),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(SHADING_DISPATCH_BUCKETING_BUFFER),
                                      entry.materialIndex * sizeof(ShadeDispatchParameters) + offsetof(ShadeDispatchParameters, xDispatch));
            }
        });
}

void SetupVisibilityBucketingDebugPass(RenderGraph& graph,
                                       PipelineManager* pipelineManager,
                                       const Core::ViewFamily& viewFamily,
                                       Core::Array<uint32_t, 2> renderExtent,
                                       const RenderTargets& targets,
                                       uint32_t sceneIndex,
                                       Core::Arena& arena)
{
    RenderPass& bucketVisualizePass = graph.AddPass(SID("Bucket Visualize"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    bucketVisualizePass.ReadSampledImage(targets.visibility);
    bucketVisualizePass.ReadStorageImage(targets.barycentric);
    bucketVisualizePass.ReadStorageImage(targets.derivatives);
    bucketVisualizePass.ReadBuffer(SCENE_DATA_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_VERTEX_POSITION_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_MESHLET_VERTEX_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_MESHLET_TRIANGLE_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_MESHLET_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    bucketVisualizePass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    bucketVisualizePass.ReadIndirectBuffer(SHADING_DISPATCH_BUCKETING_BUFFER);
    bucketVisualizePass.WriteStorageImage(targets.gbufferOne);
    bucketVisualizePass.WriteStorageImage(targets.gbufferTwo);
    bucketVisualizePass.Execute([&, pipelineManager, sceneIndex,
            visibility = targets.visibility, barycentric = targets.barycentric, derivatives = targets.derivatives,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            materialCount = static_cast<uint32_t>(viewFamily.activeMaterials.Size())](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("shading_bucket_visualize"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            VkDeviceAddress shadeDispatchAddress = graph.GetBufferAddress(SHADING_DISPATCH_BUCKETING_BUFFER);
            for (uint32_t i = 0; i < materialCount; ++i) {
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
                    .shadeDispatchBuffer = shadeDispatchAddress,
                    .materialIndex = viewFamily.activeMaterials[i].stableIndex,
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .barycentricBufferIndex = graph.GetStorageImageViewDescriptorIndex(barycentric),
                    .derivativeBufferIndex = graph.GetStorageImageViewDescriptorIndex(derivatives),
                    .gbufferOneIndex = graph.GetStorageImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetStorageImageViewDescriptorIndex(gbufferTwo),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(SHADING_DISPATCH_BUCKETING_BUFFER),
                                      i * sizeof(ShadeDispatchParameters) + offsetof(ShadeDispatchParameters, xDispatch));
            }
        });
}

void SetupLightingBucketingDebugPass(RenderGraph& graph,
                                     PipelineManager* pipelineManager,
                                     const Core::ViewFamily& viewFamily,
                                     Core::Array<uint32_t, 2> renderExtent,
                                     const RenderTargets& targets,
                                     uint32_t sceneIndex)
{
    RenderPass& lightBucketVisualizePass = graph.AddPass(SID("Light Bucket Visualize"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    lightBucketVisualizePass.ReadIndirectBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
    lightBucketVisualizePass.WriteStorageImage(targets.gbufferOne);
    lightBucketVisualizePass.WriteStorageImage(targets.gbufferTwo);
    lightBucketVisualizePass.Execute([&, pipelineManager, sceneIndex,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo,
            lightingCount = static_cast<uint32_t>(viewFamily.lightingBuckets.Size())](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("lighting_bucket_visualize"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            VkDeviceAddress lightDispatchAddress = graph.GetBufferAddress(LIGHTING_DISPATCH_BUCKETING_BUFFER);
            for (uint32_t i = 0; i < lightingCount; ++i) {
                LightingBucketVisualizePushConstant pc{
                    .lightDispatchBuffer = lightDispatchAddress,
                    .lightingIndex = i,
                    .gbufferOneIndex = graph.GetStorageImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetStorageImageViewDescriptorIndex(gbufferTwo),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                                      i * sizeof(LightingDispatchParameters) + offsetof(LightingDispatchParameters, xDispatch));
            }
        });
}
} // Render
