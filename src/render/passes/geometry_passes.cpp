//
// Created by William on 2026-06-03.
//

#include "render/passes/geometry_passes.h"

#include <tracy/Tracy.hpp>

#include "render/passes/occlusion_passes.h"
#include "render/render_utils.h"
#include "render/shaders/constants_interop.h"
#include "core/containers/inline_string.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
RDGTexture SetupGeometryPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             const SceneBufferSizes& bufferSizes,
                             const Core::DebugRenderParams& debug,
                             Core::Extent2D renderExtent,
                             const RenderTargets& targets,
                             const SceneResources& scene,
                             uint32_t sceneIndex)
{
    ZoneScoped;
    if (viewFamily.instanceCount == 0) {
        return {};
    }

    const uint32_t instanceCount = viewFamily.instanceCount;
    auto lodBias = static_cast<int32_t>(LOD_BIAS);
    auto highestMeshletCount = bufferSizes.visibleMeshletUpperBound;

    const StringID visBitsId = "instance_vis_bits"_sid;
    const bool bOcclusion = sceneIndex == 0 && debug.bOcclusionCulling;
    const bool bOcclusionFreeze = bOcclusion && debug.bOcclusionFreeze;
    const uint32_t cullFlags = (debug.bCullInstanceFrustum ? CULL_FLAG_INSTANCE_FRUSTUM : 0u) |
                               (debug.bCullInstanceContribution ? CULL_FLAG_INSTANCE_CONTRIBUTION : 0u) |
                               (debug.bCullMeshletFrustum ? CULL_FLAG_MESHLET_FRUSTUM : 0u) |
                               (debug.bCullMeshletCone ? CULL_FLAG_MESHLET_CONE : 0u) |
                               (debug.bCullMeshletContribution ? CULL_FLAG_MESHLET_CONTRIBUTION : 0u);

    // Shared buffers
    const RDGBuffer instanceMeshletOffsets = graph.CreateBuffer("instance_meshlet_offsets"_sid, bufferSizes.instanceMeshletOffsetsBufferSize, false);
    const RDGBuffer level1Sums = graph.CreateBuffer("level1_sums"_sid, bufferSizes.level1SumsBufferSize, false);
    const RDGBuffer level1BlockSums = graph.CreateBuffer("level1_block_sums"_sid, bufferSizes.level1BlockSumsBufferSize, false);
    const RDGBuffer level2Sums = graph.CreateBuffer("level2_sums"_sid, bufferSizes.level2SumsBufferSize, false);
    const RDGBuffer level2BlockSums = graph.CreateBuffer("level2_block_sums"_sid, bufferSizes.level2BlockSumsBufferSize, false);
    const RDGBuffer scannedLevel2BlockSums = graph.CreateBuffer("scanned_level2_block_sums"_sid, bufferSizes.scannedLevel2BlockSumsBufferSize, false);
    const RDGBuffer intermediateMeshlets = graph.CreateBuffer("intermediate_meshlets"_sid, bufferSizes.intermediateMeshletBufferSize, false);
    const RDGBuffer meshletLevel1Sums = graph.CreateBuffer("meshlet_level1_sums"_sid, bufferSizes.meshletLevel1SumsBufferSize, false);
    const RDGBuffer meshletLevel1BlockSums = graph.CreateBuffer("meshlet_level1_block_sums"_sid, bufferSizes.meshletLevel1BlockSumsBufferSize, false);
    const RDGBuffer meshletLevel2Sums = graph.CreateBuffer("meshlet_level2_sums"_sid, bufferSizes.meshletLevel2SumsBufferSize, false);
    const RDGBuffer meshletLevel2BlockSums = graph.CreateBuffer("meshlet_level2_block_sums"_sid, bufferSizes.meshletLevel2BlockSumsBufferSize, false);
    const RDGBuffer meshletScannedLevel2BlockSums = graph.CreateBuffer("meshlet_scanned_level2_block_sums"_sid, bufferSizes.meshletScannedLevel2BlockSumsBufferSize, false);
    const RDGBuffer visibleMeshlets = graph.CreateBuffer("visible_meshlets"_sid, bufferSizes.visibleMeshletsBufferSize, false);
    const RDGBuffer meshletCountDispatchArgs = graph.CreateBuffer("meshlet_count_dispatch_args"_sid, sizeof(InstancingMeshletDispatchIndirect), false);
    const RDGBuffer compactedMeshletDispatchArgs = graph.CreateBuffer("compacted_meshlet_dispatch_args"_sid, sizeof(InstancingCompactedMeshletDispatchIndirect), false);
    RDGBuffer visBits;
    if (bOcclusion) {
        // Fixed size so the persistent ring never resizes; garbage content on first use is safe (phase 2 corrects)
        visBits = graph.CreateVersionedBuffer(visBitsId, MAX_INSTANCE_SLOTS / 8, 0, graph.ResourceHasVersion(visBitsId, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();
    }
    RDGTexture hizPyramid;
    const RDGBuffer readback = scene.readback;

    auto addCullChain = [&](bool bPhase2) {
        const RenderCategory chainCategory = bPhase2 ? RenderCategory::GeometryPhase2 : RenderCategory::Geometry;
        auto chainID = [bPhase2](const char* base) {
            Core::InlineString<96> name = Core::InlineString<96>::Format("[Geometry P%u] %s", bPhase2 ? 2u : 1u, base);
            return StringID(name.c_str(), name.Size());
        };

        // Clear; phase 1 also zeroes the per-frame cull tallies (contiguous ReadbackStruct region)
        {
            RenderPass& clearDispatchArgs = graph.AddPass(chainID("Clear Compacted Dispatch Args"), VK_PIPELINE_STAGE_2_CLEAR_BIT, chainCategory);
            clearDispatchArgs.WriteTransferBuffer(compactedMeshletDispatchArgs);
            clearDispatchArgs.WriteTransferBuffer(meshletLevel1BlockSums);
            clearDispatchArgs.Execute([compactedMeshletDispatchArgs, meshletLevel1BlockSums](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(compactedMeshletDispatchArgs), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle(meshletLevel1BlockSums), 0, VK_WHOLE_SIZE, 0);
            });
        }

        // Instance Visibility/LOD: phase 1 gates on last frame's bit, phase 2 tests against the fresh pyramid
        if (bPhase2) {
            RenderPass& instanceLODPass = graph.AddPass(chainID("Instance Occlusion/LOD Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
            instanceLODPass.ReadBuffer(scene.sceneData);
            instanceLODPass.ReadBuffer(scene.primitives);
            instanceLODPass.ReadBuffer(scene.models);
            instanceLODPass.ReadBuffer(scene.instances);
            instanceLODPass.ReadSampledImage(hizPyramid);
            instanceLODPass.ReadWriteBuffer(visBits);
            instanceLODPass.ReadWriteBuffer(instanceMeshletOffsets);
            if (GPU_STATS_ENABLED) {
                instanceLODPass.ReadWriteBuffer(readback);
            }
            instanceLODPass.Execute([&scene, instanceMeshletOffsets, visBits, hizPyramid, readback, instanceCount, lodBias, cullFlags, pipelineManager, sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const ResourceDimensions& dims = graph.GetImageDimensions(hizPyramid);
                InstanceLODOcclusionPushConstant pc{
                    .sceneData = graph.GetBufferAddress(scene.sceneData),
                    .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
                    .modelBuffer = graph.GetBufferAddress(scene.models),
                    .instanceBuffer = graph.GetBufferAddress(scene.instances),
                    .visBits = graph.GetBufferAddress(visBits),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                    .occludedCounter = GPU_STATS_ENABLED ? graph.GetBufferAddress(readback) + offsetof(ReadbackStruct, culledInstanceOcclusion) : 0,
                    .hizExtent = {dims.width, dims.height},
                    .instanceCount = instanceCount,
                    .sceneDataIndex = sceneIndex,
                    .lodBias = lodBias,
                    .hizIndex = graph.GetSampledImageViewDescriptorIndex(hizPyramid),
                    .hizMipCount = dims.levels,
                    .cullFlags = cullFlags,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_instance_lod_occlusion"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                uint32_t xDispatch = (instanceCount + INSTANCING_VISIBILITY_DISPATCH_X - 1) / INSTANCING_VISIBILITY_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });
        }
        else {
            RenderPass& instanceLODPass = graph.AddPass(chainID("Instance Visibility/LOD Selection"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
            instanceLODPass.ReadBuffer(scene.sceneData);
            instanceLODPass.ReadBuffer(scene.primitives);
            instanceLODPass.ReadBuffer(scene.models);
            instanceLODPass.ReadBuffer(scene.instances);
            if (bOcclusion) {
                instanceLODPass.ReadBuffer(visBits);
            }
            instanceLODPass.WriteBuffer(instanceMeshletOffsets);
            if (GPU_STATS_ENABLED) {
                instanceLODPass.ReadWriteBuffer(readback);
            }
            instanceLODPass.Execute(
                [&scene,
                    instanceMeshletOffsets,
                    visBits,
                    readback,
                    bOcclusion,
                    instanceCount,
                    lodBias,
                    cullFlags,
                    pipelineManager,
                    sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    InstanceLODPushConstant pc{
                        .sceneData = graph.GetBufferAddress(scene.sceneData),
                        .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
                        .modelBuffer = graph.GetBufferAddress(scene.models),
                        .instanceBuffer = graph.GetBufferAddress(scene.instances),
                        .visBits = bOcclusion ? graph.GetBufferAddress(visBits) : 0,
                        .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                        .cullStats = GPU_STATS_ENABLED ? graph.GetBufferAddress(readback) + offsetof(ReadbackStruct, culledInstanceFrustum) : 0,
                        .instanceCount = instanceCount,
                        .sceneDataIndex = sceneIndex,
                        .lodBias = lodBias,
                        .cullFlags = cullFlags,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_instance_lod"_sid);
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

            RenderPass& upsweep1Pass = graph.AddPass(chainID("Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
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

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_up_1"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level1BlockCount, 1, 1);
            });

            uint32_t level2BlockCount = (level1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

            if (level2BlockCount > 1) {
                RenderPass& upsweep2Pass = graph.AddPass(chainID("Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
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

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_up_2"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, level2BlockCount, 1, 1);
                });

                RenderPass& scanBlocksPass = graph.AddPass(chainID("Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
                scanBlocksPass.ReadBuffer(level2BlockSums);
                scanBlocksPass.WriteBuffer(scannedLevel2BlockSums);
                scanBlocksPass.Execute([level2BlockSums, scannedLevel2BlockSums, pipelineManager, level2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    PrefixSumScanBlocksPushConstant pc{
                        .level2BlockSums = graph.GetBufferAddress(level2BlockSums),
                        .scannedLevel2BlockSums = graph.GetBufferAddress(scannedLevel2BlockSums),
                        .blockCount = level2BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, 1, 1, 1);
                });

                RenderPass& downsweep1Pass = graph.AddPass(chainID("Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
                downsweep1Pass.ReadBuffer(scannedLevel2BlockSums);
                downsweep1Pass.ReadWriteBuffer(level2Sums);
                downsweep1Pass.Execute([scannedLevel2BlockSums, level2Sums, pipelineManager, level1BlockCount, level2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    PrefixSumDownsweep1PushConstant pc{
                        .scannedLevel2BlockSums = graph.GetBufferAddress(scannedLevel2BlockSums),
                        .level2Sums = graph.GetBufferAddress(level2Sums),
                        .elementCount = level1BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_down_1"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, level2BlockCount, 1, 1);
                });
            }
            else {
                RenderPass& scanBlocksPass = graph.AddPass(chainID("Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
                scanBlocksPass.ReadBuffer(level1BlockSums);
                scanBlocksPass.WriteBuffer(scannedLevel2BlockSums);
                scanBlocksPass.Execute([level1BlockSums, scannedLevel2BlockSums, pipelineManager, level1BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    PrefixSumScanBlocksPushConstant pc{
                        .level2BlockSums = graph.GetBufferAddress(level1BlockSums),
                        .scannedLevel2BlockSums = graph.GetBufferAddress(scannedLevel2BlockSums),
                        .blockCount = level1BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, 1, 1, 1);
                });
            }

            RenderPass& downsweep2Pass = graph.AddPass(chainID("Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
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

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_down_2"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, level1BlockCount, 1, 1);
                });

            RenderPass& totalMeshletCalculator = graph.AddPass(
                chainID("Total Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
            totalMeshletCalculator.ReadBuffer(instanceMeshletOffsets);
            totalMeshletCalculator.WriteBuffer(meshletCountDispatchArgs);
            totalMeshletCalculator.Execute([instanceMeshletOffsets, meshletCountDispatchArgs, pipelineManager, instanceCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                TotalMeshletCountPushConstant pc{
                    .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
                    .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                    .instanceCount = instanceCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_total_meshlet_count"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        // Expand Instance to Meshlet
        {
            RenderPass& expandInstancesToMeshlets = graph.AddPass(
                chainID("Expand Instance To Meshlet"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
            expandInstancesToMeshlets.ReadBuffer(scene.sceneData);
            expandInstancesToMeshlets.ReadBuffer(scene.instances);
            expandInstancesToMeshlets.ReadBuffer(scene.primitives);
            expandInstancesToMeshlets.ReadBuffer(scene.models);
            expandInstancesToMeshlets.ReadBuffer(scene.meshlets);
            expandInstancesToMeshlets.ReadBuffer(scene.materials);
            expandInstancesToMeshlets.ReadBuffer(instanceMeshletOffsets);
            expandInstancesToMeshlets.ReadIndirectBuffer(meshletCountDispatchArgs);
            expandInstancesToMeshlets.WriteBuffer(intermediateMeshlets);
            if (bPhase2) {
                expandInstancesToMeshlets.ReadSampledImage(hizPyramid);
            }
            if (GPU_STATS_ENABLED) {
                expandInstancesToMeshlets.ReadWriteBuffer(readback);
            }
            expandInstancesToMeshlets.Execute([&scene, instanceMeshletOffsets, meshletCountDispatchArgs, intermediateMeshlets, hizPyramid, readback,
                    pipelineManager, instanceCount, highestMeshletCount, sceneIndex, bPhase2, cullFlags](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    uint2 hizExtent{0u, 0u};
                    uint32_t hizIndex = 0;
                    uint32_t hizMipCount = 1;
                    if (bPhase2) {
                        const ResourceDimensions& dims = graph.GetImageDimensions(hizPyramid);
                        hizExtent = {dims.width, dims.height};
                        hizIndex = graph.GetSampledImageViewDescriptorIndex(hizPyramid);
                        hizMipCount = dims.levels;
                    }
                    ExpandMeshletsPushConstant pc{
                        .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
                        .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
                        .intermediateMeshlets = graph.GetBufferAddress(intermediateMeshlets),
                        .instanceBuffer = graph.GetBufferAddress(scene.instances),
                        .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
                        .modelBuffer = graph.GetBufferAddress(scene.models),
                        .meshletBuffer = graph.GetBufferAddress(scene.meshlets),
                        .materialBuffer = graph.GetBufferAddress(scene.materials),
                        .sceneData = graph.GetBufferAddress(scene.sceneData),
                        .cullStats = GPU_STATS_ENABLED ? graph.GetBufferAddress(readback) + offsetof(ReadbackStruct, culledMeshletFrustum) : 0,
                        .hizExtent = hizExtent,
                        .sceneDataIndex = sceneIndex,
                        .instanceCount = instanceCount,
                        .currentFrameBufferMeshletLimit = highestMeshletCount,
                        .hizIndex = hizIndex,
                        .hizMipCount = hizMipCount,
                        .bHiZ = bPhase2 ? 1u : 0u,
                        .cullFlags = cullFlags,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_expand_instance_to_meshlet"_sid);
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
                chainID("Meshlet Visibility Prefix Sum Upsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
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

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_meshlet_visibility_prefix_sum_up_1"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshletCountDispatchArgs), offsetof(InstancingMeshletDispatchIndirect, x));
                });

            if (meshletLevel2BlockCount > 1) {
                RenderPass& meshletUpsweep2Pass = graph.AddPass(
                    chainID("Meshlet Visibility Prefix Sum Upsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
                meshletUpsweep2Pass.ReadBuffer(meshletLevel1BlockSums);
                meshletUpsweep2Pass.WriteBuffer(meshletLevel2Sums);
                meshletUpsweep2Pass.WriteBuffer(meshletLevel2BlockSums);
                meshletUpsweep2Pass.Execute(
                    [meshletLevel1BlockSums, meshletLevel2Sums, meshletLevel2BlockSums, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                        RegionPrefixSumUpsweep2PushConstant pc{
                            .level1BlockSums = graph.GetBufferAddress(meshletLevel1BlockSums),
                            .level2Sums = graph.GetBufferAddress(meshletLevel2Sums),
                            .level2BlockSums = graph.GetBufferAddress(meshletLevel2BlockSums),
                            .elementCount = meshletLevel1BlockCount,
                            .blockCount = meshletLevel2BlockCount,
                        };

                        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_region_prefix_sum_up_2"_sid);
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                        vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
                    });

                RenderPass& meshletScanBlocksPass = graph.AddPass(
                    chainID("Meshlet Visibility Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
                meshletScanBlocksPass.ReadBuffer(meshletLevel2BlockSums);
                meshletScanBlocksPass.WriteBuffer(meshletScannedLevel2BlockSums);
                meshletScanBlocksPass.WriteBuffer(compactedMeshletDispatchArgs);
                meshletScanBlocksPass.Execute([meshletLevel2BlockSums, meshletScannedLevel2BlockSums, compactedMeshletDispatchArgs, pipelineManager, meshletLevel2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    RegionPrefixSumScanBlocksPushConstant pc{
                        .level2BlockSums = graph.GetBufferAddress(meshletLevel2BlockSums),
                        .scannedLevel2BlockSums = graph.GetBufferAddress(meshletScannedLevel2BlockSums),
                        .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
                        .blockCount = meshletLevel2BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_region_scan_blocks"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, 1, 1, 1);
                });

                RenderPass& meshletDownsweep1Pass = graph.AddPass(
                    chainID("Meshlet Visibility Prefix Sum Downsweep 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
                meshletDownsweep1Pass.ReadBuffer(meshletScannedLevel2BlockSums);
                meshletDownsweep1Pass.ReadWriteBuffer(meshletLevel2Sums);
                meshletDownsweep1Pass.Execute([meshletScannedLevel2BlockSums, meshletLevel2Sums, pipelineManager, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    RegionPrefixSumDownsweep1PushConstant pc{
                        .scannedLevel2BlockSums = graph.GetBufferAddress(meshletScannedLevel2BlockSums),
                        .level2Sums = graph.GetBufferAddress(meshletLevel2Sums),
                        .elementCount = meshletLevel1BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_region_prefix_sum_down_1"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
                });
            }
            else {
                RenderPass& meshletScanBlocksPass = graph.AddPass(
                    chainID("Meshlet Visibility Prefix Sum Scan Blocks"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
                meshletScanBlocksPass.ReadBuffer(meshletLevel1BlockSums);
                meshletScanBlocksPass.WriteBuffer(meshletScannedLevel2BlockSums);
                meshletScanBlocksPass.WriteBuffer(compactedMeshletDispatchArgs);
                meshletScanBlocksPass.Execute([meshletLevel1BlockSums, meshletScannedLevel2BlockSums, compactedMeshletDispatchArgs, pipelineManager, meshletLevel1BlockCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    RegionPrefixSumScanBlocksPushConstant pc{
                        .level2BlockSums = graph.GetBufferAddress(meshletLevel1BlockSums),
                        .scannedLevel2BlockSums = graph.GetBufferAddress(meshletScannedLevel2BlockSums),
                        .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
                        .blockCount = meshletLevel1BlockCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_region_scan_blocks"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, 1, 1, 1);
                });
            }

            RenderPass& meshletDownsweep2Pass = graph.AddPass(
                chainID("Meshlet Visibility Prefix Sum Downsweep 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
            meshletDownsweep2Pass.ReadBuffer(meshletLevel1Sums);
            meshletDownsweep2Pass.ReadBuffer(intermediateMeshlets);
            if (meshletLevel2BlockCount > 1) {
                meshletDownsweep2Pass.ReadBuffer(meshletLevel2Sums);
            }
            else {
                meshletDownsweep2Pass.ReadBuffer(meshletScannedLevel2BlockSums);
            }
            meshletDownsweep2Pass.ReadBuffer(compactedMeshletDispatchArgs);
            meshletDownsweep2Pass.WriteBuffer(visibleMeshlets);
            meshletDownsweep2Pass.ReadIndirectBuffer(meshletCountDispatchArgs);
            meshletDownsweep2Pass.Execute([meshletLevel1Sums, meshletLevel2Sums, meshletScannedLevel2BlockSums, intermediateMeshlets,
                    meshletCountDispatchArgs, visibleMeshlets, compactedMeshletDispatchArgs,
                    pipelineManager, meshletLevel2BlockCount, highestMeshletCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    MeshletVisibilityPrefixSumDownsweep2PushConstant pc{
                        .meshletLevel1Sums = graph.GetBufferAddress(meshletLevel1Sums),
                        .meshletLevel2Sums = meshletLevel2BlockCount > 1 ? graph.GetBufferAddress(meshletLevel2Sums) : graph.GetBufferAddress(meshletScannedLevel2BlockSums),
                        .intermediateMeshlets = graph.GetBufferAddress(intermediateMeshlets),
                        .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
                        .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
                        .visibleMeshlets = graph.GetBufferAddress(visibleMeshlets),
                        .currentFrameBufferMeshletLimit = highestMeshletCount,
                    };

                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_meshlet_visibility_prefix_sum_down_2"_sid);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshletCountDispatchArgs), offsetof(InstancingMeshletDispatchIndirect, x));
                });

            RenderPass& compactedDispatchCalc = graph.AddPass(
                chainID("Compacted Meshlet Dispatch Calculation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
            compactedDispatchCalc.ReadWriteBuffer(compactedMeshletDispatchArgs);
            if (GPU_STATS_ENABLED) {
                compactedDispatchCalc.ReadWriteBuffer(readback);
            }
            compactedDispatchCalc.Execute([compactedMeshletDispatchArgs, readback, pipelineManager, highestMeshletCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                CompactedMeshletDispatchPushConstant pc{
                    .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
                    .regionVisibleStats = GPU_STATS_ENABLED ? graph.GetBufferAddress(readback) + offsetof(ReadbackStruct, meshletRegionVisible) : 0,
                    .currentFrameBufferMeshletLimit = highestMeshletCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_compacted_meshlet_dispatch"_sid);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        RenderPass& maxMeshletCount = graph.AddPass(chainID("Max Meshlet Count"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, chainCategory);
        maxMeshletCount.ReadBuffer(meshletCountDispatchArgs);
        maxMeshletCount.ReadWriteBuffer(readback);
        maxMeshletCount.Execute([&, pipelineManager, readback, bufferSrc = meshletCountDispatchArgs](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            MaxMeshletCountPushConstant pc{
                .indirectDispatchBuffer = graph.GetBufferAddress(bufferSrc),
                .currentHighest = graph.GetBufferAddress(readback) + offsetof(ReadbackStruct, meshletCount),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_max_meshlet_count"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });

        RenderPass& instancedMeshShading = graph.AddPass(
            chainID("Instanced Mesh Shading"), VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                                                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, chainCategory);
        instancedMeshShading.WriteColorAttachment(targets.visibility);
        instancedMeshShading.WriteColorAttachment(targets.gbufferOne);
#if WILL_EDITOR
        instancedMeshShading.WriteColorAttachment(targets.stableId);
#endif
        instancedMeshShading.WriteDepthAttachment(targets.depthStencil);
        instancedMeshShading.ReadBuffer(scene.sceneData);
        instancedMeshShading.ReadBuffer(scene.models);
        instancedMeshShading.ReadBuffer(scene.materials);
        instancedMeshShading.ReadBuffer(scene.instances);
        instancedMeshShading.ReadBuffer(scene.primitives);
        instancedMeshShading.ReadBuffer(scene.meshlets);
        instancedMeshShading.ReadBuffer(scene.meshletVertices);
        instancedMeshShading.ReadBuffer(scene.meshletTriangles);
        instancedMeshShading.ReadBuffer(scene.vertexPositions);
        instancedMeshShading.ReadBuffer(scene.vertexAttributes);
        instancedMeshShading.ReadBuffer(visibleMeshlets);
        instancedMeshShading.ReadIndirectBuffer(compactedMeshletDispatchArgs);
        instancedMeshShading.Execute([&, pipelineManager, visibleMeshlets, compactedMeshletDispatchArgs, sceneIndex, renderExtent,
                bWireframe = debug.bWireframe,
                visibility = targets.visibility, stableId = targets.stableId, depthStencil = targets.depthStencil](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkViewport viewport = VkHelpers::GenerateViewport(renderExtent.width, renderExtent.height);
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor = VkHelpers::GenerateScissor(renderExtent.width, renderExtent.height);
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                vkCmdSetPolygonModeEXT(cmd, bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL);

                auto visibilityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(visibility), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                auto depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                auto stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

#if WILL_EDITOR
                auto stableIdAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(stableId), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                const VkRenderingAttachmentInfo colorAttachments[] = {visibilityAttachment, stableIdAttachment};
                const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({renderExtent.width, renderExtent.height}, colorAttachments, 2, &depthAttachment, &stencilAttachment);
#else
                const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({renderExtent.width, renderExtent.height}, &visibilityAttachment, 1, &depthAttachment, &stencilAttachment);
#endif

                vkCmdBeginRendering(cmd, &renderInfo);

                VisibilityBufferAccumulatePushConstant pushConstants{
                    .sceneData = graph.GetBufferAddress(scene.sceneData) + sceneIndex * sizeof(SceneData),
                    .vertexPosBuffer = graph.GetBufferAddress(scene.vertexPositions),
                    .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
                    .meshletVerticesBuffer = graph.GetBufferAddress(scene.meshletVertices),
                    .meshletTrianglesBuffer = graph.GetBufferAddress(scene.meshletTriangles),
                    .meshletBuffer = graph.GetBufferAddress(scene.meshlets),
                    .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
                    .instanceBuffer = graph.GetBufferAddress(scene.instances),
                    .modelBuffer = graph.GetBufferAddress(scene.models),
                    .materialBuffer = graph.GetBufferAddress(scene.materials),
                    .visibleMeshlets = graph.GetBufferAddress(visibleMeshlets),
                    .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
                };

                const PipelineEntry* regionPipelines[MESHLET_REGION_COUNT] = {
                    pipelineManager->GetPipelineEntry("visibility_buffer_accumulate"_sid),
                    pipelineManager->GetPipelineEntry("visibility_buffer_accumulate"_sid),
                    pipelineManager->GetPipelineEntry("visibility_buffer_accumulate_cutout"_sid),
                    pipelineManager->GetPipelineEntry("visibility_buffer_accumulate_cutout"_sid),
                };
                for (uint32_t region = 0; region < MESHLET_REGION_COUNT; region++) {
                    const PipelineEntry* entry = regionPipelines[region];
                    if (region == 0 || entry != regionPipelines[region - 1]) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, entry->pipeline);
                    }
                    vkCmdSetCullMode(cmd, (region & 1u) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
                    pushConstants.drawRegion = region;
                    vkCmdPushConstants(cmd, entry->layout, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(VisibilityBufferAccumulatePushConstant), &pushConstants);

                    vkCmdDrawMeshTasksIndirectEXT(
                        cmd,
                        graph.GetBufferHandle(compactedMeshletDispatchArgs),
                        offsetof(InstancingCompactedMeshletDispatchIndirect, regionArgs) + region * sizeof(uint4),
                        1,
                        sizeof(uint4));
                }

                vkCmdEndRendering(cmd);
            });
    };

    addCullChain(false);

    if (!bOcclusion || bOcclusionFreeze) {
        return {};
    }

    // Phase 2: pyramid from phase-1 depth, then the whole chain again with the Hi-Z variants
    hizPyramid = SetupHiZPyramid(graph, pipelineManager, renderExtent, targets);

    RenderPass& occlusionClear = graph.AddPass("[Geometry P2] Occlusion Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::GeometryPhase2);
    occlusionClear.WriteTransferBuffer(visBits);
    occlusionClear.Execute([visBits](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(visBits), 0, VK_WHOLE_SIZE, 0);
    });

    addCullChain(true);
    return hizPyramid;
}

GeometryFrame SetupVisibilityBucketingPass(RenderGraph& graph,
                                           PipelineManager* pipelineManager,
                                           const Core::ViewFamily& viewFamily,
                                           Core::Extent2D renderExtent,
                                           const RenderTargets& targets,
                                           const SceneResources& scene,
                                           uint32_t sceneIndex,
                                           Core::BucketDebugMode bucketDebugMode,
                                           VisibilityBucketTiles& outTiles)
{
    ZoneScoped;
    outTiles = {};
    if (!scene.shadingBucketingDispatches.IsValid()) { return {}; }
    if (!scene.lightingBucketingDispatches.IsValid()) { return {}; }

    const uint32_t tilesX = (renderExtent.width + BUCKET_TILE_SIZE - 1) / BUCKET_TILE_SIZE;
    const uint32_t tilesY = (renderExtent.height + BUCKET_TILE_SIZE - 1) / BUCKET_TILE_SIZE;
    const uint32_t tileCapacity = BucketTileCapacity(renderExtent.width, renderExtent.height);
    const uint32_t lightingCount = static_cast<uint32_t>(pipelineManager->GetLightingPipelines().Size());
    const RDGBuffer shadingTileList = graph.CreateBuffer(SHADING_TILE_LIST_BUFFER, static_cast<VkDeviceSize>(viewFamily.materialCount) * tileCapacity * sizeof(uint32_t));
    const RDGBuffer lightingTileList = graph.CreateBuffer(LIGHTING_TILE_LIST_BUFFER, static_cast<VkDeviceSize>(lightingCount) * tileCapacity * sizeof(uint32_t));
    outTiles.shadingTileList = shadingTileList;

    const bool bShadeDebug = bucketDebugMode == Core::BucketDebugMode::ShadeBuckets || bucketDebugMode == Core::BucketDebugMode::ShadeHeat;
    const bool bLightDebug = bucketDebugMode == Core::BucketDebugMode::LightBuckets || bucketDebugMode == Core::BucketDebugMode::LightHeat;
    const GeometryFrame frame{.lightingTileList = lightingTileList};
    if (bShadeDebug || bLightDebug) {
        const uint32_t words = bShadeDebug ? MAX_SHADE_BUCKETS / 32 : MAX_LIGHTING_BUCKETS / 32;
        outTiles.tileBits = graph.CreateBuffer(BUCKET_TILE_BITS_BUFFER, static_cast<VkDeviceSize>(tileCapacity) * words * sizeof(uint32_t));
    }

    RenderPass& boundsPass = graph.AddPass("Shade Bucketing Bounds"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    boundsPass.ReadSampledImage(targets.visibility);
    boundsPass.ReadBuffer(scene.instances);
    boundsPass.ReadBuffer(scene.materials);
    boundsPass.ReadWriteBuffer(scene.shadingBucketingDispatches);
    boundsPass.ReadWriteBuffer(scene.lightingBucketingDispatches);
    boundsPass.WriteBuffer(shadingTileList);
    boundsPass.WriteBuffer(lightingTileList);
    if (bShadeDebug || bLightDebug) {
        boundsPass.WriteBuffer(outTiles.tileBits);
    }
    boundsPass.Execute([&, pipelineManager, renderExtent, tilesX, tilesY, tileCapacity, bShadeDebug, bLightDebug,
            visibility = targets.visibility, shadingTileList, lightingTileList, tileBits = outTiles.tileBits](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ShadeBucketingPushConstant pc{
                .instanceBuffer = graph.GetBufferAddress(scene.instances),
                .materialBuffer = graph.GetBufferAddress(scene.materials),
                .shadeDispatchBuffer = graph.GetBufferAddress(scene.shadingBucketingDispatches),
                .lightDispatchBuffer = graph.GetBufferAddress(scene.lightingBucketingDispatches),
                .shadeTileListBuffer = graph.GetBufferAddress(shadingTileList),
                .lightTileListBuffer = graph.GetBufferAddress(lightingTileList),
                .shadeTileBitsBuffer = bShadeDebug ? graph.GetBufferAddress(tileBits) : 0,
                .lightTileBitsBuffer = bLightDebug ? graph.GetBufferAddress(tileBits) : 0,
                .extents = {renderExtent.width, renderExtent.height},
                .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                .tileCapacity = tileCapacity,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("visibility_bucketing_bounds_calculation"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesX, tilesY, 1);
        });

    if constexpr (!GPU_STATS_ENABLED) {
        return frame;
    }

    // Technically not "critical", used for stats. But since we write to readback_buffer, it becomes critical.
    RenderPass& dispatchCountPass = graph.AddPass("Bucket Dispatch Count"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    dispatchCountPass.ReadBuffer(scene.shadingBucketingDispatches);
    dispatchCountPass.ReadBuffer(scene.lightingBucketingDispatches);
    dispatchCountPass.ReadWriteBuffer(scene.readback);
    dispatchCountPass.Execute([&, pipelineManager,
            materialCount = viewFamily.materialCount,
            lightingCount = static_cast<uint32_t>(pipelineManager->GetLightingPipelines().Size())](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            BucketDispatchCountPushConstant pc{
                .shadeDispatchBuffer = graph.GetBufferAddress(scene.shadingBucketingDispatches),
                .lightDispatchBuffer = graph.GetBufferAddress(scene.lightingBucketingDispatches),
                .countBuffer = graph.GetBufferAddress(scene.readback) + offsetof(ReadbackStruct, shadingDispatches),
                .materialCount = materialCount,
                .lightingCount = lightingCount,
            };
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("visibility_bucketing_dispatch_count"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    return frame;
}

void SetupVisibilityShadingPass(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Extent2D renderExtent,
                                const RenderTargets& targets,
                                const SceneResources& scene,
                                const VisibilityBucketTiles& tiles,
                                uint32_t sceneIndex,
                                Core::Arena& arena)
{
    ZoneScoped;
    if (!scene.shadingBucketingDispatches.IsValid()) { return; }

    struct MaterialEntry
    {
        uint32_t materialIndex{};
        StringID fragmentShader{};
    };

    const auto materialCount = static_cast<uint32_t>(viewFamily.activeMaterials.Size());
    auto* sortedMaterials = arena.AllocArray<MaterialEntry>(materialCount);
    for (uint32_t i = 0; i < materialCount; ++i) {
        sortedMaterials[i] = {viewFamily.activeMaterials[i].materialSlot, viewFamily.activeMaterials[i].material.fragmentShader};
    }
    std::sort(sortedMaterials, sortedMaterials + materialCount, [](const MaterialEntry& a, const MaterialEntry& b) {
        return a.fragmentShader < b.fragmentShader;
    });

    RenderPass& visShading = graph.AddPass("Visibility Shading"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Geometry);
    visShading.ReadSampledImage(targets.visibility);
    visShading.ReadBuffer(scene.sceneData);
    visShading.ReadBuffer(scene.vertexPositions);
    visShading.ReadBuffer(scene.vertexAttributes);
    visShading.ReadBuffer(scene.meshletVertices);
    visShading.ReadBuffer(scene.meshletTriangles);
    visShading.ReadBuffer(scene.meshlets);
    visShading.ReadBuffer(scene.primitives);
    visShading.ReadBuffer(scene.instances);
    visShading.ReadBuffer(scene.models);
    visShading.ReadBuffer(scene.materials);
    visShading.ReadIndirectBuffer(scene.shadingBucketingDispatches);
    if (tiles.shadingTileList.IsValid()) { visShading.ReadBuffer(tiles.shadingTileList); }
    visShading.WriteStorageImage(targets.gbufferOne);
    visShading.WriteStorageImage(targets.gbufferTwo);
    visShading.WriteStorageImage(targets.shadowOriginOffset);
    visShading.Execute([&, pipelineManager, sceneIndex,
            visibility = targets.visibility,
            gbufferOne = targets.gbufferOne, gbufferTwo = targets.gbufferTwo, shadowOriginOffset = targets.shadowOriginOffset,
            sortedMaterials, materialCount, renderExtent, shadingTileList = tiles.shadingTileList](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkDeviceAddress tileListAddress = graph.GetBufferAddress(shadingTileList);

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
                    .sceneData = graph.GetBufferAddress(scene.sceneData) + sceneIndex * sizeof(SceneData),
                    .vertexPosBuffer = graph.GetBufferAddress(scene.vertexPositions),
                    .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
                    .meshletVerticesBuffer = graph.GetBufferAddress(scene.meshletVertices),
                    .meshletTrianglesBuffer = graph.GetBufferAddress(scene.meshletTriangles),
                    .meshletBuffer = graph.GetBufferAddress(scene.meshlets),
                    .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
                    .instanceBuffer = graph.GetBufferAddress(scene.instances),
                    .modelBuffer = graph.GetBufferAddress(scene.models),
                    .materialBuffer = graph.GetBufferAddress(scene.materials),
                    .tileListBuffer = tileListAddress,
                    .tileCapacity = BucketTileCapacity(renderExtent.width, renderExtent.height),
                    .extents = {renderExtent.width, renderExtent.height},
                    .materialIndex = entry.materialIndex,
                    .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
                    .gbufferOneIndex = graph.GetStorageImageViewDescriptorIndex(gbufferOne),
                    .gbufferTwoIndex = graph.GetStorageImageViewDescriptorIndex(gbufferTwo),
                    .shadowOriginOffsetIndex = graph.GetStorageImageViewDescriptorIndex(shadowOriginOffset),
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(scene.shadingBucketingDispatches), entry.materialIndex * sizeof(BucketDispatchParameters) + offsetof(BucketDispatchParameters, xDispatch));
            }
        });
}

void SetupBucketDebugPass(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Extent2D renderExtent,
                          const RenderTargets& targets,
                          const SceneResources& scene,
                          const VisibilityBucketTiles& tiles,
                          Core::BucketDebugMode bucketDebugMode)
{
    ZoneScoped;
    if (bucketDebugMode == Core::BucketDebugMode::Off) {
        return;
    }
    if (!tiles.tileBits.IsValid()) {
        return;
    }
    const bool bLighting = bucketDebugMode == Core::BucketDebugMode::LightBuckets || bucketDebugMode == Core::BucketDebugMode::LightHeat;
    const bool bHeat = bucketDebugMode == Core::BucketDebugMode::ShadeHeat || bucketDebugMode == Core::BucketDebugMode::LightHeat;
    const uint32_t tilesX = (renderExtent.width + BUCKET_TILE_SIZE - 1) / BUCKET_TILE_SIZE;
    const uint32_t tilesY = (renderExtent.height + BUCKET_TILE_SIZE - 1) / BUCKET_TILE_SIZE;
    const RDGTexture debugTarget = graph.CreateTexture(BUCKET_DEBUG_TARGET, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass("Bucket Debug"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Debug);
    pass.ReadSampledImage(targets.visibility);
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.materials);
    pass.ReadBuffer(tiles.tileBits);
    pass.WriteStorageImage(debugTarget);
    pass.Execute([&scene, pipelineManager, renderExtent, tilesX, tilesY, bLighting, bHeat, visibility = targets.visibility, tileBits = tiles.tileBits, debugTarget](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("bucket_debug"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        BucketDebugPushConstant pc{
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .tileBitsBuffer = graph.GetBufferAddress(tileBits),
            .extents = {renderExtent.width, renderExtent.height},
            .tilesX = tilesX,
            .bLighting = bLighting ? 1u : 0u,
            .bHeat = bHeat ? 1u : 0u,
            .visibilityBufferIndex = graph.GetSampledImageViewDescriptorIndex(visibility),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(debugTarget),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, tilesX, tilesY, 1);
    });
}
} // Render
