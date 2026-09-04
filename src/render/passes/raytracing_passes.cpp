//
// Created by William on 2026-06-10.
//

#include "raytracing_passes.h"

#include <tracy/Tracy.hpp>

#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include "render/render_config.h"
#include "render/frame_resources.h"
#include "render/render-graph/render_pass.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/pipelines/pipeline_data.h"
#include "render/interface/render_interface.h"
#include "render/shaders/push_constant_interop.h"
#include "render/shaders/instance_mask_interop.h"
#include "render/shaders/model_interop.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
void SetupTLASBuild(RenderGraph& graph,
                    VulkanContext* context,
                    PipelineManager* pipelineManager,
                    const Core::ViewFamily& viewFamily,
                    Core::Array<uint32_t, 2> renderExtent,
                    const FrameResourceLimits& limits)
{
    ZoneScoped;
    const uint32_t slotCount = viewFamily.instanceCount;
    if (slotCount == 0) { return; }
    if (!graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) { return; }

    // Query required TLAS size
    VkAccelerationStructureGeometryKHR geometry{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    const uint32_t primitiveCount = slotCount;
    const uint32_t maxPrimitiveCount = glm::max(static_cast<uint32_t>(limits.highestTLASInstanceCount), slotCount);

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxPrimitiveCount, &sizeInfo);

    const VkDeviceSize alignedTLASSize = (sizeInfo.accelerationStructureSize + 255ull) & ~255ull;
    const VkDeviceSize scratchSize = sizeInfo.buildScratchSize;

    graph.CreateVersionedTLAS(RT_TLAS_BUFFER, alignedTLASSize, RenderCategory::RayTracing);

    const VkDeviceSize instanceBufferSize = static_cast<VkDeviceSize>(maxPrimitiveCount) * sizeof(VkAccelerationStructureInstanceKHR);
    graph.CreateBufferAligned(RT_TLAS_INSTANCE_BUFFER, instanceBufferSize, 16, false);

    RenderPass& fillPass = graph.AddPass("RT TLAS Instances"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::RayTracing);
    fillPass.AsyncCompute();
    fillPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    fillPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    fillPass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    fillPass.WriteBuffer(RT_TLAS_INSTANCE_BUFFER);
    fillPass.Execute([pipelineManager, slotCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("tlas_instances"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        TLASInstancePushConstant pc{
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .outInstances = graph.GetBufferAddress(RT_TLAS_INSTANCE_BUFFER),
            .instanceCount = slotCount,
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (slotCount + 63) / 64, 1, 1);
    });

    const VkDeviceSize scratchAlignment = VulkanContext::deviceInfo.accelerationStructureProps.minAccelerationStructureScratchOffsetAlignment;
    graph.CreateBufferAligned(RT_TLAS_SCRATCH_BUFFER, scratchSize, scratchAlignment, false);

    RenderPass& buildPass = graph.AddPass("RT Build TLAS"_sid, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, RenderCategory::RayTracing);
    buildPass.AsyncCompute();
    buildPass.ReadASInputBuffer(RT_TLAS_INSTANCE_BUFFER);
    buildPass.WriteTLASBuffer(RT_TLAS_BUFFER);
    buildPass.WriteScratchBuffer(RT_TLAS_SCRATCH_BUFFER);
    buildPass.Execute([primitiveCount](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkAccelerationStructureGeometryKHR geom{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geom.geometry.instances.arrayOfPointers = VK_FALSE;
        geom.geometry.instances.data.deviceAddress = graph.GetBufferAddress(RT_TLAS_INSTANCE_BUFFER);

        VkAccelerationStructureBuildGeometryInfoKHR build{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.dstAccelerationStructure = graph.GetAccelerationStructureHandle(RT_TLAS_BUFFER);
        build.geometryCount = 1;
        build.pGeometries = &geom;
        build.scratchData.deviceAddress = graph.GetBufferAddress(RT_TLAS_SCRATCH_BUFFER);

        VkAccelerationStructureBuildRangeInfoKHR range{.primitiveCount = primitiveCount};
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &pRange);
    });
}

void SetupRTShadowTest(RenderGraph& graph,
                       VulkanContext* context,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       StringID outputTarget,
                       uint32_t sceneIndex)
{
    ZoneScoped;
    if (!graph.HasBuffer(RT_TLAS_BUFFER)) { return; }

    RenderPass& pass = graph.AddPass("RT Shadow Test"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Untagged);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.depthCopy);
    pass.WriteStorageImage(outputTarget);
    pass.Execute([pipelineManager, sceneIndex, renderExtent,
                  depth = targets.depthCopy, output = outputTarget](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_shadow_test"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTShadowTestPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .sceneDataIndex = sceneIndex,
            .renderExtent = {renderExtent[0], renderExtent[1]},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 7) / 8;
        const uint32_t groupsY = (renderExtent[1] + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });
}

void SetupRTSunShadow(RenderGraph& graph,
                      PipelineManager* pipelineManager,
                      const Core::ViewFamily& viewFamily,
                      Core::Array<uint32_t, 2> shadowExtent,
                      Core::Array<uint32_t, 2> fullExtent,
                      const RenderTargets& targets,
                      uint32_t sceneIndex,
                      uint64_t frameNumber,
                      uint32_t pixelScale)
{
    ZoneScoped;
    if (!graph.HasBuffer(RT_TLAS_BUFFER)) { return; }

    const bool bHalfRes = pixelScale > 1u;

    // R = binary visibility (1 lit, 0 occluded), G = closest-occluder distance (penumbra input for SIGMA)
    graph.CreateTexture("rt_sun_shadow"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, shadowExtent[0], shadowExtent[1], 1}, {std::nullopt}, true);
    if (bHalfRes) {
        graph.CreateTexture("rt_sun_depth"_sid, TextureInfo{VK_FORMAT_R32_SFLOAT, shadowExtent[0], shadowExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture("rt_sun_gbuffer"_sid, TextureInfo{VK_FORMAT_R32G32B32A32_UINT, shadowExtent[0], shadowExtent[1], 1}, {std::nullopt}, true);
    }

    RenderPass& pass = graph.AddPass("[SIGMA] RT Sun Shadow"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DirectionalLighting);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(LIGHT_DATA_BUFFER);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.WriteStorageImage("rt_sun_shadow"_sid);
    if (bHalfRes) {
        pass.WriteStorageImage("rt_sun_depth"_sid);
        pass.WriteStorageImage("rt_sun_gbuffer"_sid);
    }
    pass.Execute([pipelineManager, sceneIndex, shadowExtent, fullExtent, pixelScale, bHalfRes, frameNumber, bAlphaTest = viewFamily.sigmaParams.bAlphaTest,
                  depth = targets.depthCopy, gbufferOne = targets.gbufferOne](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_sun_shadow"_sid);
        if (!pipeline) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTSunShadowPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .renderExtent = {shadowExtent[0], shadowExtent[1]},
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex("rt_sun_shadow"_sid),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .fullExtent = {fullExtent[0], fullExtent[1]},
            .pixelScale = pixelScale,
            .outputDepthIndex = bHalfRes ? graph.GetStorageImageViewDescriptorIndex("rt_sun_depth"_sid) : ~0x0u,
            .outputGbufferIndex = bHalfRes ? graph.GetStorageImageViewDescriptorIndex("rt_sun_gbuffer"_sid) : ~0x0u,
            .bAlphaTest = bAlphaTest ? 1u : 0u,
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (shadowExtent[0] + 7) / 8;
        const uint32_t groupsY = (shadowExtent[1] + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });
}

bool SetupRTGroundTruthDI(RenderGraph& graph,
                           PipelineManager* pipelineManager,
                           const Core::ViewFamily& viewFamily,
                           Core::Array<uint32_t, 2> renderExtent,
                           const RenderTargets& targets,
                           uint32_t sceneIndex,
                           bool bReset,
                           uint32_t accumulationCount,
                           uint64_t frameNumber)
{
    ZoneScoped;
    if (!graph.HasBuffer(RT_TLAS_BUFFER)) { return false; }
    if (!pipelineManager->GetPipelineEntry("rt_ground_truth_di"_sid)) { return false; }

    const uint32_t pixelCount = renderExtent[0] * renderExtent[1];
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    if (!graph.ResourceHasVersion("rt_gt_di_accum"_sid, 0)) { bReset = true; }
    graph.CreateVersionedBuffer("rt_gt_di_accum"_sid, bufferSize, 0, graph.ResourceHasVersion("rt_gt_di_accum"_sid, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);

    if (bReset) {
        RenderPass& clearPass = graph.AddPass("RT GT DI Accum Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer("rt_gt_di_accum"_sid);
        clearPass.Execute([&](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("rt_gt_di_accum"_sid), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass("RT Ground Truth DI"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::GroundTruth);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer("light_data"_sid);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadWriteBuffer("rt_gt_di_accum"_sid);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.WriteStorageImage(targets.colorOutput);
    pass.Execute([pipelineManager, sceneIndex, accumulationCount, frameNumber, renderExtent, skyboxIndex = viewFamily.skyboxIndex,
                  depth = targets.depthCopy, gbufferOne = targets.gbufferOne,
                  gbufferTwo = targets.gbufferTwo, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_ground_truth_di"_sid);
        if (!pipeline) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTGroundTruthDIPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress("light_data"_sid),
            .accumulationBuffer = graph.GetBufferAddress("rt_gt_di_accum"_sid),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .skyboxIndex = skyboxIndex,
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .accumulationCount = accumulationCount,
            .renderExtent = {renderExtent[0], renderExtent[1]},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 7) / 8;
        const uint32_t groupsY = (renderExtent[1] + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    return true;
}

bool SetupRTGroundTruthGI(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          const RenderTargets& targets,
                          uint32_t sceneIndex,
                          bool bReset,
                          uint32_t accumulationCount,
                          uint64_t frameNumber)
{
    ZoneScoped;
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) { return false; }
    if (!pipelineManager->GetPipelineEntry("rt_ground_truth_gi"_sid)) { return false; }

    const uint32_t pixelCount = renderExtent[0] * renderExtent[1];
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    if (!graph.ResourceHasVersion("rt_gt_gi_accum"_sid, 0)) { bReset = true; }
    graph.CreateVersionedBuffer("rt_gt_gi_accum"_sid, bufferSize, 0, graph.ResourceHasVersion("rt_gt_gi_accum"_sid, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);

    if (bReset) {
        RenderPass& clearPass = graph.AddPass("RT GT GI Accum Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer("rt_gt_gi_accum"_sid);
        clearPass.Execute([&](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("rt_gt_gi_accum"_sid), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass("RT Ground Truth GI"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::GroundTruth);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer("light_data"_sid);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadWriteBuffer("rt_gt_gi_accum"_sid);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.WriteStorageImage(targets.colorOutput);
    pass.Execute([pipelineManager, sceneIndex, accumulationCount, frameNumber, renderExtent, skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity,
                  depth = targets.depthCopy, gbufferOne = targets.gbufferOne,
                  gbufferTwo = targets.gbufferTwo, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_ground_truth_gi"_sid);
        if (!pipeline) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTGroundTruthGIPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress("light_data"_sid),
            .accumulationBuffer = graph.GetBufferAddress("rt_gt_gi_accum"_sid),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .skyboxIndex = skyboxIndex,
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .accumulationCount = accumulationCount,
            .iblIntensity = iblIntensity,
            .renderExtent = {renderExtent[0], renderExtent[1]},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 7) / 8;
        const uint32_t groupsY = (renderExtent[1] + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    return true;
}

bool SetupRTGroundTruthFull(RenderGraph& graph,
                            PipelineManager* pipelineManager,
                            const Core::ViewFamily& viewFamily,
                            Core::Array<uint32_t, 2> renderExtent,
                            const RenderTargets& targets,
                            uint32_t sceneIndex,
                            bool bReset,
                            uint32_t accumulationCount,
                            uint64_t frameNumber,
                            uint32_t samplesPerFrame)
{
    ZoneScoped;
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) { return false; }
    if (!pipelineManager->GetPipelineEntry("rt_ground_truth_full"_sid)) { return false; }

    const uint32_t pixelCount = renderExtent[0] * renderExtent[1];
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    if (!graph.ResourceHasVersion("rt_gt_full_accum"_sid, 0)) { bReset = true; }
    graph.CreateVersionedBuffer("rt_gt_full_accum"_sid, bufferSize, 0, graph.ResourceHasVersion("rt_gt_full_accum"_sid, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);

    if (bReset) {
        RenderPass& clearPass = graph.AddPass("RT GT Full Accum Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer("rt_gt_full_accum"_sid);
        clearPass.Execute([&](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("rt_gt_full_accum"_sid), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass("RT Ground Truth Full"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::GroundTruth);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer("light_data"_sid);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadWriteBuffer("rt_gt_full_accum"_sid);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.WriteStorageImage(targets.colorOutput);
    const uint32_t dofPacked = glm::packHalf2x16(glm::vec2(glm::max(0.0f, viewFamily.groundTruthDofAperture), viewFamily.postProcessConfig.dofFocusDistance));

    pass.Execute([pipelineManager, sceneIndex, accumulationCount, frameNumber, renderExtent, samplesPerFrame, dofPacked, skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity,
                  depth = targets.depthCopy, gbufferOne = targets.gbufferOne,
                  gbufferTwo = targets.gbufferTwo, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_ground_truth_full"_sid);
        if (!pipeline) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTGroundTruthGIPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .lightData = graph.GetBufferAddress("light_data"_sid),
            .accumulationBuffer = graph.GetBufferAddress("rt_gt_full_accum"_sid),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .skyboxIndex = skyboxIndex,
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .accumulationCount = accumulationCount,
            .iblIntensity = iblIntensity,
            .samplesPerFrame = samplesPerFrame,
            .dofPackedApertureFocus = dofPacked,
            .renderExtent = {renderExtent[0], renderExtent[1]},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 7) / 8;
        const uint32_t groupsY = (renderExtent[1] + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    return true;
}

} // Render
