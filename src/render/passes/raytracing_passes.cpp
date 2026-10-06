//
// Created by William on 2026-06-10.
//

#include "render/passes/raytracing_passes.h"

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
RDGBufferRing SetupTLASBuild(RenderGraph& graph,
                             VulkanContext* context,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Extent2D renderExtent,
                             const FrameResourceLimits& limits,
                             const SceneResources& scene)
{
    ZoneScoped;
    const uint32_t slotCount = viewFamily.instanceCount;
    if (slotCount == 0) { return {}; }
    if (!scene.instances.IsValid() || !scene.models.IsValid() || !scene.materials.IsValid()) { return {}; }

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

    const RDGBufferRing tlasRing = graph.CreateVersionedTLAS(RT_TLAS_BUFFER, alignedTLASSize, RenderCategory::RayTracing);
    const RDGBuffer tlas = tlasRing.Current();

    const VkDeviceSize instanceBufferSize = static_cast<VkDeviceSize>(maxPrimitiveCount) * sizeof(VkAccelerationStructureInstanceKHR);
    const RDGBuffer tlasInstances = graph.CreateBufferAligned(RT_TLAS_INSTANCE_BUFFER, instanceBufferSize, 16, false);

    RenderPass& fillPass = graph.AddPass("RT TLAS Instances"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::RayTracing);
    fillPass.AsyncCompute();
    fillPass.ReadBuffer(scene.instances);
    fillPass.ReadBuffer(scene.models);
    fillPass.ReadBuffer(scene.materials);
    fillPass.WriteBuffer(tlasInstances);
    fillPass.Execute([pipelineManager, slotCount, &scene, tlasInstances](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("tlas_instances"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        TLASInstancePushConstant pc{
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .outInstances = graph.GetBufferAddress(tlasInstances),
            .instanceCount = slotCount,
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (slotCount + 63) / 64, 1, 1);
    });

    const VkDeviceSize scratchAlignment = VulkanContext::deviceInfo.accelerationStructureProps.minAccelerationStructureScratchOffsetAlignment;
    const RDGBuffer tlasScratch = graph.CreateBufferAligned(RT_TLAS_SCRATCH_BUFFER, scratchSize, scratchAlignment, false);

    RenderPass& buildPass = graph.AddPass("RT Build TLAS"_sid, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, RenderCategory::RayTracing);
    buildPass.AsyncCompute();
    buildPass.ReadASInputBuffer(tlasInstances);
    buildPass.WriteTLASBuffer(tlas);
    buildPass.WriteScratchBuffer(tlasScratch);
    buildPass.Execute([primitiveCount, tlas, tlasInstances, tlasScratch](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkAccelerationStructureGeometryKHR geom{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geom.geometry.instances.arrayOfPointers = VK_FALSE;
        geom.geometry.instances.data.deviceAddress = graph.GetBufferAddress(tlasInstances);

        VkAccelerationStructureBuildGeometryInfoKHR build{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.dstAccelerationStructure = graph.GetAccelerationStructureHandle(tlas);
        build.geometryCount = 1;
        build.pGeometries = &geom;
        build.scratchData.deviceAddress = graph.GetBufferAddress(tlasScratch);

        VkAccelerationStructureBuildRangeInfoKHR range{.primitiveCount = primitiveCount};
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &pRange);
    });

    return tlasRing;
}

void SetupRTShadowTest(RenderGraph& graph,
                       VulkanContext* context,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Extent2D renderExtent,
                       const RenderTargets& targets,
                       const SceneResources& scene,
                       RDGTexture outputTarget,
                       uint32_t sceneIndex)
{
    ZoneScoped;
    if (!scene.tlas.IsValid()) { return; }

    RenderPass& pass = graph.AddPass("RT Shadow Test"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Untagged);
    pass.ReadTLASBuffer(scene.tlas);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadSampledImage(targets.depthCopy);
    pass.WriteStorageImage(outputTarget);
    pass.Execute([pipelineManager, sceneIndex, renderExtent, &scene,
                  depth = targets.depthCopy, output = outputTarget](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_shadow_test"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTShadowTestPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .sceneDataIndex = sceneIndex,
            .renderExtent = {renderExtent.width, renderExtent.height},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent.width + 7) / 8;
        const uint32_t groupsY = (renderExtent.height + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });
}

SunShadowFrame SetupRTSunShadow(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Extent2D shadowExtent,
                                Core::Extent2D fullExtent,
                                const RenderTargets& targets,
                                const SceneResources& scene,
                                uint32_t sceneIndex,
                                uint64_t frameNumber,
                                uint32_t pixelScale)
{
    ZoneScoped;
    if (!scene.tlas.IsValid()) { return {}; }

    const bool bHalfRes = pixelScale > 1u;

    SunShadowFrame sunShadow{};
    // R = binary visibility (1 lit, 0 occluded), G = closest-occluder distance (penumbra input for SIGMA)
    sunShadow.shadow = graph.CreateTexture("rt_sun_shadow"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, shadowExtent.width, shadowExtent.height, 1}, {std::nullopt}, true);
    if (bHalfRes) {
        sunShadow.depth = graph.CreateTexture("rt_sun_depth"_sid, TextureInfo{VK_FORMAT_R32_SFLOAT, shadowExtent.width, shadowExtent.height, 1}, {std::nullopt}, true);
        sunShadow.gbuffer = graph.CreateTexture("rt_sun_gbuffer"_sid, TextureInfo{VK_FORMAT_R32G32B32A32_UINT, shadowExtent.width, shadowExtent.height, 1}, {std::nullopt}, true);
    }

    RenderPass& pass = graph.AddPass("[SIGMA] RT Sun Shadow"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DirectionalLighting);
    pass.ReadTLASBuffer(scene.tlas);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.primitives);
    pass.ReadBuffer(scene.materials);
    pass.ReadBuffer(scene.indices);
    pass.ReadBuffer(scene.vertexAttributes);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.WriteStorageImage(sunShadow.shadow);
    if (bHalfRes) {
        pass.WriteStorageImage(sunShadow.depth);
        pass.WriteStorageImage(sunShadow.gbuffer);
    }
    pass.Execute([pipelineManager, sceneIndex, shadowExtent, fullExtent, pixelScale, bHalfRes, frameNumber, bAlphaTest = viewFamily.sigmaParams.bAlphaTest, &scene,
                  depth = targets.depthCopy, gbufferOne = targets.gbufferOne,
                  shadowOut = sunShadow.shadow, depthOut = sunShadow.depth, gbufferOut = sunShadow.gbuffer](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_sun_shadow"_sid);
        if (!pipeline) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTSunShadowPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .indexBuffer = graph.GetBufferAddress(scene.indices),
            .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
            .renderExtent = {shadowExtent.width, shadowExtent.height},
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(shadowOut),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .fullExtent = {fullExtent.width, fullExtent.height},
            .pixelScale = pixelScale,
            .outputDepthIndex = bHalfRes ? graph.GetStorageImageViewDescriptorIndex(depthOut) : ~0x0u,
            .outputGbufferIndex = bHalfRes ? graph.GetStorageImageViewDescriptorIndex(gbufferOut) : ~0x0u,
            .bAlphaTest = bAlphaTest ? 1u : 0u,
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (shadowExtent.width + 7) / 8;
        const uint32_t groupsY = (shadowExtent.height + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    return sunShadow;
}

bool SetupRTGroundTruthDI(RenderGraph& graph,
                           PipelineManager* pipelineManager,
                           const Core::ViewFamily& viewFamily,
                           Core::Extent2D renderExtent,
                           const RenderTargets& targets,
                           const SceneResources& scene,
                           uint32_t sceneIndex,
                           bool bReset,
                           uint32_t& accumulationCount,
                           uint64_t frameNumber)
{
    ZoneScoped;
    if (!scene.tlas.IsValid()) { return false; }
    if (!pipelineManager->GetPipelineEntry("rt_ground_truth_di"_sid)) { return false; }

    const uint32_t pixelCount = renderExtent.width * renderExtent.height;
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    const bool bHistory = graph.ResourceHasBufferVersion("rt_gt_di_accum"_sid, bufferSize);
    if (!bHistory) { bReset = true; }
    if (bReset) { accumulationCount = 0; }
    const RDGBuffer accum = graph.CreateVersionedBuffer("rt_gt_di_accum"_sid, bufferSize, 0, bHistory ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();

    if (bReset) {
        RenderPass& clearPass = graph.AddPass("RT GT DI Accum Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer(accum);
        clearPass.Execute([accum](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(accum), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass("RT Ground Truth DI"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::GroundTruth);
    pass.ReadTLASBuffer(scene.tlas);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.primitives);
    pass.ReadBuffer(scene.materials);
    pass.ReadBuffer(scene.indices);
    pass.ReadBuffer(scene.vertexAttributes);
    pass.ReadWriteBuffer(accum);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.WriteStorageImage(targets.colorOutput);
    pass.Execute([pipelineManager, sceneIndex, accumulationCount, frameNumber, renderExtent, skyboxIndex = viewFamily.skyboxIndex, &scene, accum,
                  depth = targets.depthCopy, gbufferOne = targets.gbufferOne,
                  gbufferTwo = targets.gbufferTwo, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_ground_truth_di"_sid);
        if (!pipeline) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTGroundTruthDIPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .accumulationBuffer = graph.GetBufferAddress(accum),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .indexBuffer = graph.GetBufferAddress(scene.indices),
            .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
            .skyboxIndex = skyboxIndex,
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .accumulationCount = accumulationCount,
            .renderExtent = {renderExtent.width, renderExtent.height},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent.width + 7) / 8;
        const uint32_t groupsY = (renderExtent.height + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    return true;
}

bool SetupRTGroundTruthGI(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Extent2D renderExtent,
                          const RenderTargets& targets,
                          const SceneResources& scene,
                          uint32_t sceneIndex,
                          bool bReset,
                          uint32_t& accumulationCount,
                          uint64_t frameNumber)
{
    ZoneScoped;
    if (!scene.tlas.IsValid() || !scene.instances.IsValid() || !scene.models.IsValid() || !scene.materials.IsValid()) { return false; }
    if (!pipelineManager->GetPipelineEntry("rt_ground_truth_gi"_sid)) { return false; }

    const uint32_t pixelCount = renderExtent.width * renderExtent.height;
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    const bool bHistory = graph.ResourceHasBufferVersion("rt_gt_gi_accum"_sid, bufferSize);
    if (!bHistory) { bReset = true; }
    if (bReset) { accumulationCount = 0; }
    const RDGBuffer accum = graph.CreateVersionedBuffer("rt_gt_gi_accum"_sid, bufferSize, 0, bHistory ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();

    if (bReset) {
        RenderPass& clearPass = graph.AddPass("RT GT GI Accum Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer(accum);
        clearPass.Execute([accum](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(accum), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass("RT Ground Truth GI"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::GroundTruth);
    pass.ReadTLASBuffer(scene.tlas);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.primitives);
    pass.ReadBuffer(scene.models);
    pass.ReadBuffer(scene.materials);
    pass.ReadBuffer(scene.indices);
    pass.ReadBuffer(scene.vertexAttributes);
    pass.ReadWriteBuffer(accum);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.WriteStorageImage(targets.colorOutput);
    pass.Execute([pipelineManager, sceneIndex, accumulationCount, frameNumber, renderExtent, skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity, &scene, accum,
                  depth = targets.depthCopy, gbufferOne = targets.gbufferOne,
                  gbufferTwo = targets.gbufferTwo, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_ground_truth_gi"_sid);
        if (!pipeline) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTGroundTruthGIPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .accumulationBuffer = graph.GetBufferAddress(accum),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .indexBuffer = graph.GetBufferAddress(scene.indices),
            .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
            .skyboxIndex = skyboxIndex,
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .accumulationCount = accumulationCount,
            .iblIntensity = iblIntensity,
            .renderExtent = {renderExtent.width, renderExtent.height},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent.width + 7) / 8;
        const uint32_t groupsY = (renderExtent.height + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    return true;
}

bool SetupRTGroundTruthFull(RenderGraph& graph,
                            PipelineManager* pipelineManager,
                            const Core::ViewFamily& viewFamily,
                            Core::Extent2D renderExtent,
                            const RenderTargets& targets,
                            const SceneResources& scene,
                            uint32_t sceneIndex,
                            bool bReset,
                            uint32_t& accumulationCount,
                            uint64_t frameNumber,
                            uint32_t samplesPerFrame)
{
    ZoneScoped;
    if (!scene.tlas.IsValid() || !scene.instances.IsValid() || !scene.models.IsValid() || !scene.materials.IsValid()) { return false; }
    if (!pipelineManager->GetPipelineEntry("rt_ground_truth_full"_sid)) { return false; }

    const uint32_t pixelCount = renderExtent.width * renderExtent.height;
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(pixelCount) * sizeof(float[4]);

    const bool bHistory = graph.ResourceHasBufferVersion("rt_gt_full_accum"_sid, bufferSize);
    if (!bHistory) { bReset = true; }
    if (bReset) { accumulationCount = 0; }
    const RDGBuffer accum = graph.CreateVersionedBuffer("rt_gt_full_accum"_sid, bufferSize, 0, bHistory ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();

    if (bReset) {
        RenderPass& clearPass = graph.AddPass("RT GT Full Accum Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::GroundTruth);
        clearPass.WriteTransferBuffer(accum);
        clearPass.Execute([accum](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(accum), 0, VK_WHOLE_SIZE, 0);
        });
    }

    RenderPass& pass = graph.AddPass("RT Ground Truth Full"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::GroundTruth);
    pass.ReadTLASBuffer(scene.tlas);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.primitives);
    pass.ReadBuffer(scene.models);
    pass.ReadBuffer(scene.materials);
    pass.ReadBuffer(scene.indices);
    pass.ReadBuffer(scene.vertexAttributes);
    pass.ReadWriteBuffer(accum);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.gbufferTwo);
    pass.WriteStorageImage(targets.colorOutput);
    const uint32_t dofPacked = glm::packHalf2x16(glm::vec2(glm::max(0.0f, viewFamily.groundTruthDofAperture), viewFamily.postProcessConfig.dofFocusDistance));

    pass.Execute([pipelineManager, sceneIndex, accumulationCount, frameNumber, renderExtent, samplesPerFrame, dofPacked, skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity,
                  &scene, accum, depth = targets.depthCopy, gbufferOne = targets.gbufferOne,
                  gbufferTwo = targets.gbufferTwo, output = targets.colorOutput](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("rt_ground_truth_full"_sid);
        if (!pipeline) { return; }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        RTGroundTruthGIPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .lightData = graph.GetBufferAddress(scene.lightData),
            .accumulationBuffer = graph.GetBufferAddress(accum),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .indexBuffer = graph.GetBufferAddress(scene.indices),
            .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
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
            .renderExtent = {renderExtent.width, renderExtent.height},
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent.width + 7) / 8;
        const uint32_t groupsY = (renderExtent.height + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });

    return true;
}

} // Render
