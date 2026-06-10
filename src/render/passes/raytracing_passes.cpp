//
// Created by William on 2026-06-10.
//

#include "raytracing_passes.h"

#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "render/render_config.h"
#include "render/render-graph/render_pass.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/pipelines/pipeline_data.h"
#include "render/interface/render_interface.h"
#include "render/shaders/push_constant_interop.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
void SetupTLASBuild(RenderGraph& graph,
                    VulkanContext* context,
                    const Core::ViewFamily& viewFamily,
                    Core::Array<uint32_t, 2> renderExtent)
{
    size_t instanceCount = 0;
    for (const auto& p : viewFamily.primitiveInstances) {
        if (p.blasDeviceAddress != 0) { ++instanceCount; }
    }
    if (instanceCount == 0) { return; }

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

    const uint32_t primitiveCount = static_cast<uint32_t>(instanceCount);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    const VkDeviceSize alignedTLASSize = (sizeInfo.accelerationStructureSize + 255ull) & ~255ull;
    const VkDeviceSize scratchSize = sizeInfo.buildScratchSize;

    // Grow buffer if needed; rebuild AS handle when buffer was reallocated
    const bool bReallocated = graph.EnsurePersistentBufferCapacity(RT_TLAS_BUFFER, alignedTLASSize);
    PersistentBuffer& tlas = graph.GetPersistentBuffer(RT_TLAS_BUFFER);
    if (bReallocated || tlas.userData == 0) {
        VkAccelerationStructureCreateInfoKHR createInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.buffer = tlas.buffer;
        createInfo.offset = 0;
        createInfo.size = tlas.capacity;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        VkAccelerationStructureKHR handle{};
        VK_CHECK(vkCreateAccelerationStructureKHR(context->device, &createInfo, nullptr, &handle));
        tlas.userData = reinterpret_cast<uint64_t>(handle);
    }

    graph.ImportPersistentBuffer(RT_TLAS_BUFFER);

    // Upload TLAS instance buffer
    const size_t instanceDataSize = instanceCount * sizeof(VkAccelerationStructureInstanceKHR);
    UploadAllocation instanceUpload = graph.AllocateTransient(instanceDataSize);
    auto* instanceData = static_cast<VkAccelerationStructureInstanceKHR*>(instanceUpload.ptr);

    size_t instanceSlot = 0;
    for (size_t i = 0; i < viewFamily.primitiveInstances.Size(); ++i) {
        const Core::PrimitiveInstanceData& src = viewFamily.primitiveInstances[i];
        if (src.blasDeviceAddress == 0) { continue; }
        const Model& model = viewFamily.modelMatrices[src.modelIndex];

        // VkTransformMatrixKHR is row-major 3x4; glm mat4 is column-major, so transpose
        const glm::mat4 m = glm::transpose(model.modelMatrix);
        VkTransformMatrixKHR transform{};
        memcpy(&transform, &m, sizeof(VkTransformMatrixKHR));

        VkAccelerationStructureInstanceKHR& inst = instanceData[instanceSlot++];
        inst.transform = transform;
        inst.instanceCustomIndex = static_cast<uint32_t>(i);
        inst.mask = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference = src.blasDeviceAddress;
    }

    graph.CreateBuffer(RT_TLAS_INSTANCE_BUFFER, instanceDataSize, false, false);

    RenderPass& uploadPass = graph.AddPass(SID("RT Upload TLAS Instances"), VK_PIPELINE_STAGE_2_COPY_BIT, ResourceCategory::Untagged);
    uploadPass.WriteTransferBuffer(RT_TLAS_INSTANCE_BUFFER);
    uploadPass.Execute([&graph, srcOffset = instanceUpload.offset, totalSize = instanceDataSize](VkCommandBuffer cmd) {
        VkBufferCopy2 copy{
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = srcOffset,
            .dstOffset = 0,
            .size = totalSize,
        };
        VkCopyBufferInfo2 copyInfo{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer = graph.GetTransientUploadBuffer(),
            .dstBuffer = graph.GetBufferHandle(RT_TLAS_INSTANCE_BUFFER),
            .regionCount = 1,
            .pRegions = &copy,
        };
        vkCmdCopyBuffer2(cmd, &copyInfo);
    });

    graph.CreateBuffer(RT_TLAS_SCRATCH_BUFFER, scratchSize, false);

    RenderPass& buildPass = graph.AddPass(SID("RT Build TLAS"), VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, ResourceCategory::Untagged);
    buildPass.ReadTransferBuffer(RT_TLAS_INSTANCE_BUFFER);
    buildPass.WriteTLASBuffer(RT_TLAS_BUFFER);
    buildPass.WriteScratchBuffer(RT_TLAS_SCRATCH_BUFFER);
    buildPass.Execute([&graph, primitiveCount, tlasHandle = tlas.userData](VkCommandBuffer cmd) {
        VkAccelerationStructureGeometryKHR geom{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geom.geometry.instances.arrayOfPointers = VK_FALSE;
        geom.geometry.instances.data.deviceAddress = graph.GetBufferAddress(RT_TLAS_INSTANCE_BUFFER);

        VkAccelerationStructureBuildGeometryInfoKHR build{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.dstAccelerationStructure = reinterpret_cast<VkAccelerationStructureKHR>(tlasHandle);
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
                       uint32_t sceneIndex)
{
    if (!graph.HasBuffer(RT_TLAS_BUFFER)) { return; }

    RenderPass& pass = graph.AddPass(SID("RT Shadow Test"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Untagged);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.depthCopy);
    pass.WriteStorageImage(targets.intermediateOne);
    pass.Execute([&graph, pipelineManager, sceneIndex, renderExtent,
                  depth = targets.depthCopy, output = targets.intermediateOne, device = context->device](VkCommandBuffer cmd) {
        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("rt_shadow_test"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

        const PersistentBuffer& tlas = graph.GetPersistentBuffer(RT_TLAS_BUFFER);
        VkAccelerationStructureDeviceAddressInfoKHR addrInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        addrInfo.accelerationStructure = reinterpret_cast<VkAccelerationStructureKHR>(tlas.userData);
        const uint64_t tlasAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);

        RTShadowTestPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .tlasAddress = tlasAddress,
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupsX = (renderExtent[0] + 7) / 8;
        const uint32_t groupsY = (renderExtent[1] + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    });
}

} // Render
