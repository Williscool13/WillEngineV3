//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_render_extents.h"
#include "resource_manager.h"
#include "render/vulkan/vk_swapchain.h"
#include "render/vulkan/vk_utils.h"
#include "engine/will_engine.h"
#include "platform/paths.h"
#include "platform/thread_utils.h"
#include "render-graph/render_graph.h"
#include "render-graph/render_pass.h"
#include "shaders/push_constant_interop.h"

#if WILL_EDITOR
#include "render/vulkan/vk_imgui_wrapper.h"
#include "backends/imgui_impl_vulkan.h"
#include "editor/asset-generation/asset_generator.h"
#endif


namespace Render
{
RenderThread::RenderThread() = default;

RenderThread::RenderThread(Core::FrameSync* engineRenderSynchronization, enki::TaskScheduler* scheduler, SDL_Window* window, uint32_t width, uint32_t height)
    : engineRenderSynchronization(engineRenderSynchronization), scheduler(scheduler), window(window)
{
    context = std::make_unique<VulkanContext>(window);
    swapchain = std::make_unique<Swapchain>(context.get(), width, height);
#if WILL_EDITOR
    imgui = std::make_unique<ImguiWrapper>(context.get(), window, Core::FRAME_BUFFER_COUNT, COLOR_ATTACHMENT_FORMAT);
#endif
    renderExtents = std::make_unique<RenderExtents>(width, height, 1.0f);
    resourceManager = std::make_unique<ResourceManager>(context.get());
    graph = std::make_unique<RenderGraph>(context.get(), resourceManager.get());

    for (RenderSynchronization& frameSync : frameSynchronization) {
        frameSync = RenderSynchronization(context.get());
        frameSync.Initialize();
    }

    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.usage = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (int32_t i = 0; i < frameResources.size(); ++i) {
        bufferInfo.size = sizeof(SceneData);
        frameResources[i].sceneDataBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        frameResources[i].sceneDataBuffer.SetDebugName(("sceneData_" + std::to_string(i)).c_str());

        bufferInfo.size = BINDLESS_INSTANCE_BUFFER_SIZE;
        frameResources[i].instanceBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        frameResources[i].instanceBuffer.SetDebugName(("instanceBuffer_" + std::to_string(i)).c_str());
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResources[i].modelBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        frameResources[i].modelBuffer.SetDebugName(("modelBuffer_" + std::to_string(i)).c_str());
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResources[i].jointMatrixBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        frameResources[i].jointMatrixBuffer.SetDebugName(("jointMatrixBuffer_" + std::to_string(i)).c_str());
        bufferInfo.size = BINDLESS_MATERIAL_BUFFER_SIZE;
        frameResources[i].materialBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        frameResources[i].materialBuffer.SetDebugName(("materialBuffer_" + std::to_string(i)).c_str());
    }

    CreatePipelines();

    if (basicComputePipeline.pipeline.handle == VK_NULL_HANDLE || basicRenderPipeline.pipeline.handle == VK_NULL_HANDLE) {
        SPDLOG_ERROR("Failed to compile shaders");
        exit(1);
    }
}

RenderThread::~RenderThread() = default;

void RenderThread::Start()
{
    bShouldExit.store(false, std::memory_order_release);

    uint32_t renderThreadNum = scheduler->GetNumTaskThreads() - 1;
    pinnedTask = std::make_unique<enki::LambdaPinnedTask>(
        renderThreadNum,
        [this] { ThreadMain(); }
    );

    scheduler->AddPinnedTask(pinnedTask.get());
}

void RenderThread::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
}

void RenderThread::Join()
{
    if (pinnedTask) {
        scheduler->WaitforTask(pinnedTask.get());
    }
}

void RenderThread::ThreadMain()
{
    Platform::SetThreadName("RenderThread");
    while (!bShouldExit.load()) {
        if (!engineRenderSynchronization->renderFrames.try_acquire_for(std::chrono::milliseconds(100))) {
            continue;
        }

        if (bShouldExit.load()) { break; }

        currentFrameInFlight = frameNumber % Core::FRAME_BUFFER_COUNT;
        Core::FrameBuffer& frameBuffer = engineRenderSynchronization->frameBuffers[currentFrameInFlight];
        assert(frameBuffer.currentFrameBuffer == currentFrameInFlight);


        bEngineRequestsRecreate |= frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate;
        bool bShouldRecreate = !frameBuffer.swapchainRecreateCommand.bIsMinimized && bEngineRequestsRecreate;
        if (bShouldRecreate) {
            SPDLOG_INFO("[RenderThread::ThreadMain] Swapchain Recreated");
            vkDeviceWaitIdle(context->device);

            swapchain->Recreate(frameBuffer.swapchainRecreateCommand.width, frameBuffer.swapchainRecreateCommand.height);
            renderExtents->ApplyResize(frameBuffer.swapchainRecreateCommand.width, frameBuffer.swapchainRecreateCommand.height);

            bRenderRequestsRecreate = false;
            bEngineRequestsRecreate = false;

            graph->InvalidateAll();
        }

        // Wait for the frame N - 3 to finish using resources
        RenderSynchronization& currentRenderSynchronization = frameSynchronization[currentFrameInFlight];
        FrameResources& currentFrameResources = frameResources[currentFrameInFlight];
        RenderResponse renderResponse = Render(currentFrameInFlight, currentRenderSynchronization, frameBuffer, currentFrameResources);
        if (renderResponse == SWAPCHAIN_OUTDATED) {
            bRenderRequestsRecreate = true;
        }

        frameNumber++;
        engineRenderSynchronization->gameFrames.release();
    }

    vkDeviceWaitIdle(context->device);
}

RenderThread::RenderResponse RenderThread::Render(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer, FrameResources& frameResource)
{
    vkWaitForFences(context->device, 1, &renderSync.renderFence, true, UINT64_MAX);
    VK_CHECK(vkResetFences(context->device, 1, &renderSync.renderFence));

    VK_CHECK(vkResetCommandBuffer(renderSync.commandBuffer, 0));
    VkCommandBufferBeginInfo beginInfo = VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(renderSync.commandBuffer, &beginInfo));

    ProcessAcquisitions(renderSync.commandBuffer, frameBuffer);

    if (bRenderRequestsRecreate) {
        VK_CHECK(vkEndCommandBuffer(renderSync.commandBuffer));
        VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
        VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, nullptr, nullptr);
        VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
        return SWAPCHAIN_OUTDATED;
    }

    uint32_t swapchainImageIndex;
    VkResult e = vkAcquireNextImageKHR(context->device, swapchain->handle, UINT64_MAX, renderSync.swapchainSemaphore, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR) {
        SPDLOG_TRACE("[RenderThread::Render] Swapchain acquire failed ({})", string_VkResult(e));
        VK_CHECK(vkEndCommandBuffer(renderSync.commandBuffer));
        VkCommandBufferSubmitInfo cmdInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
        VkSemaphoreSubmitInfo waitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdInfo, &waitInfo, nullptr);
        VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
        return SWAPCHAIN_OUTDATED;
    }

    std::array<uint32_t, 2> renderExtent = renderExtents->GetScaledExtent();
    VkImage currentSwapchainImage = swapchain->swapchainImages[swapchainImageIndex];
    VkImageView currentSwapchainImageView = swapchain->swapchainImageViews[swapchainImageIndex];

    AllocatedBuffer& currentSceneDataBuffer = frameResource.sceneDataBuffer;

    //
    {
        auto* modelBuffer = static_cast<Model*>(frameResource.modelBuffer.allocationInfo.pMappedData);
        for (size_t i = 0; i < frameBuffer.mainViewFamily.modelMatrices.size(); ++i) {
            modelBuffer[i] = {
                .modelMatrix = frameBuffer.mainViewFamily.modelMatrices[i],
                .prevModelMatrix = frameBuffer.mainViewFamily.modelMatrices[i], // TODO: actual prev frame
                .flags = glm::vec4(0.0f)
            };
        }

        auto* materialBuffer = static_cast<MaterialProperties*>(frameResource.materialBuffer.allocationInfo.pMappedData);
        memcpy(materialBuffer, frameBuffer.mainViewFamily.materials.data(), frameBuffer.mainViewFamily.materials.size() * sizeof(MaterialProperties));


        auto* instanceBuffer = static_cast<Instance*>(frameResource.instanceBuffer.allocationInfo.pMappedData);
        for (size_t i = 0; i < frameBuffer.mainViewFamily.instances.size(); ++i) {
            auto& inst = frameBuffer.mainViewFamily.instances[i];
            instanceBuffer[i] = {
                .primitiveIndex = inst.primitiveIndex,
                .modelIndex = inst.modelIndex,
                .materialIndex = inst.gpuMaterialIndex,
                .jointMatrixOffset = 0,
                .bIsAllocated = 1
            };
        }

        // todo: address what happens if views is 0
        const Core::RenderView& view = frameBuffer.mainViewFamily.views[0];

        const glm::mat4 viewMatrix = glm::lookAt(view.cameraPos, view.cameraLookAt, view.cameraUp);
        const glm::mat4 projMatrix = glm::perspective(view.fovRadians, view.aspectRatio, view.farPlane, view.nearPlane);

        sceneData.view = viewMatrix;
        sceneData.proj = projMatrix;
        sceneData.viewProj = projMatrix * viewMatrix;
        sceneData.cameraWorldPos = glm::vec4(view.cameraPos, 1.0f);
        sceneData.frustum = CreateFrustum(projMatrix * viewMatrix);
        sceneData.deltaTime = 0.1f;

        auto currentSceneData = static_cast<SceneData*>(currentSceneDataBuffer.allocationInfo.pMappedData);
        memcpy(currentSceneData, &sceneData, sizeof(SceneData));
    }

    VkViewport viewport = VkHelpers::GenerateViewport(renderExtent[0], renderExtent[1]);
    vkCmdSetViewport(renderSync.commandBuffer, 0, 1, &viewport);
    VkRect2D scissor = VkHelpers::GenerateScissor(renderExtent[0], renderExtent[1]);
    vkCmdSetScissor(renderSync.commandBuffer, 0, 1, &scissor);

    graph->Reset();
    RenderPass& computePass = graph->AddPass("ComputeDraw");
    computePass.WriteStorageImage("drawImage", {COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1],});
    computePass.Execute([&](VkCommandBuffer cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, basicComputePipeline.pipeline.handle);
        const ResourceDimensions& dims = graph->GetImageDimensions("drawImage");
        BasicComputePushConstant pushConstant{
            .color1 = {0.0f, 0.0f, 0.0f, 0.0f},
            .color2 = {1.0f, 1.0f, 1.0f, 1.0f},
            .extent = {dims.width, dims.height},
            .index = graph->GetDescriptorIndex("drawImage"),
        };
        vkCmdPushConstants(cmd, basicComputePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BasicComputePushConstant), &pushConstant);

        VkDescriptorBufferBindingInfoEXT bindingInfo = resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo();
        vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
        uint32_t bufferIndexImage = 0;
        VkDeviceSize bufferOffset = 0;
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, basicComputePipeline.pipelineLayout.handle, 0, 1, &bufferIndexImage, &bufferOffset);

        uint32_t xDispatch = (dims.width + 15) / 16;
        uint32_t yDispatch = (dims.height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    graph->ImportBufferNoBarrier("vertexBuffer", resourceManager->megaVertexBuffer.handle, resourceManager->megaVertexBuffer.address,
                                 {resourceManager->megaVertexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("skinnedVertexBuffer", resourceManager->megaSkinnedVertexBuffer.handle, resourceManager->megaSkinnedVertexBuffer.address,
                                 {resourceManager->megaSkinnedVertexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("meshletVertexBuffer", resourceManager->megaMeshletVerticesBuffer.handle, resourceManager->megaMeshletVerticesBuffer.address,
                                 {resourceManager->megaMeshletVerticesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("meshletTriangleBuffer", resourceManager->megaMeshletTrianglesBuffer.handle, resourceManager->megaMeshletTrianglesBuffer.address,
                                 {resourceManager->megaMeshletTrianglesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("meshletBuffer", resourceManager->megaMeshletBuffer.handle, resourceManager->megaMeshletBuffer.address,
                                 {resourceManager->megaMeshletBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("primitiveBuffer", resourceManager->primitiveBuffer.handle, resourceManager->primitiveBuffer.address,
                                 {resourceManager->primitiveBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("sceneData", frameResource.sceneDataBuffer.handle, frameResource.sceneDataBuffer.address,
                                 {frameResource.sceneDataBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("instanceBuffer", frameResource.instanceBuffer.handle, frameResource.instanceBuffer.address,
                                 {frameResource.instanceBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("modelBuffer", frameResource.modelBuffer.handle, frameResource.modelBuffer.address,
                                 {frameResource.modelBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("jointMatrixBuffer", frameResource.jointMatrixBuffer.handle, frameResource.jointMatrixBuffer.address,
                                 {frameResource.jointMatrixBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("materialBuffer", frameResource.materialBuffer.handle, frameResource.materialBuffer.address,
                                 {frameResource.materialBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    graph->ImportBufferNoBarrier("debugReadbackBuffer", resourceManager->debugReadbackBuffer.handle, resourceManager->debugReadbackBuffer.address,
                                 {resourceManager->debugReadbackBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});

    graph->CreateBuffer("packedVisibilityBuffer", INSTANCING_PACKED_VISIBILITY_SIZE);
    graph->CreateBuffer("instanceOffsetBuffer", INSTANCING_INSTANCE_OFFSET_SIZE);
    graph->CreateBuffer("primitiveCountBuffer", INSTANCING_PRIMITIVE_COUNT_SIZE);
    graph->CreateBuffer("compactedInstanceBuffer", INSTANCING_COMPACTED_INSTANCE_BUFFER);
    graph->CreateBuffer("indirectCountBuffer", INSTANCING_MESH_INDIRECT_COUNT);
    graph->CreateBuffer("indirectBuffer", INSTANCING_MESH_INDIRECT_PARAMETERS);

    RenderPass& clearPass = graph->AddPass("ClearInstancingBuffers");
    clearPass.WriteTransferBuffer("packedVisibilityBuffer", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearPass.WriteTransferBuffer("primitiveCountBuffer", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearPass.WriteTransferBuffer("indirectCountBuffer", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearPass.Execute([&](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, graph->GetBuffer("packedVisibilityBuffer"), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph->GetBuffer("primitiveCountBuffer"), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph->GetBuffer("indirectCountBuffer"), 0, VK_WHOLE_SIZE, 0);
    });

    RenderPass& visibilityPass = graph->AddPass("ComputeVisibility");
    visibilityPass.ReadBuffer("primitiveBuffer", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visibilityPass.ReadBuffer("modelBuffer", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visibilityPass.ReadBuffer("instanceBuffer", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visibilityPass.ReadBuffer("sceneData", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visibilityPass.WriteBuffer("packedVisibilityBuffer", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visibilityPass.WriteBuffer("instanceOffsetBuffer", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visibilityPass.WriteBuffer("primitiveCountBuffer", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visibilityPass.Execute([&](VkCommandBuffer cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingVisibility.pipeline.handle);

        VisibilityPushConstant visibilityPushData{
            .sceneData = graph->GetBufferAddress("sceneData"),
            .primitiveBuffer = graph->GetBufferAddress("primitiveBuffer"),
            .modelBuffer = graph->GetBufferAddress("modelBuffer"),
            .instanceBuffer = graph->GetBufferAddress("instanceBuffer"),
            .packedVisibilityBuffer = graph->GetBufferAddress("packedVisibilityBuffer"),
            .instanceOffsetBuffer = graph->GetBufferAddress("instanceOffsetBuffer"),
            .primitiveCountBuffer = graph->GetBufferAddress("primitiveCountBuffer"),
        };

        vkCmdPushConstants(cmd, instancingVisibility.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityPushConstant), &visibilityPushData);
        // todo 64/63 should be an interop property
        // todo: this should be "highest instance index", which we should store as an atomic in the resource manager. But not sure.
        // vkCmdDispatch(cmd, (200 + 63) / 64, 1, 1);
        vkCmdDispatch(cmd, 1, 1, 1);
    });


    /*
    RenderPass& readbackPass = graph->AddPass("DebugReadback");
    readbackPass.ReadTransferBuffer("packedVisibilityBuffer", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    readbackPass.WriteTransferBuffer("debugReadbackBuffer", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    readbackPass.Execute([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = 10 * sizeof(uint32_t);
        vkCmdCopyBuffer(cmd, graph->GetBuffer("packedVisibilityBuffer"), graph->GetBuffer("debugReadbackBuffer"), 1, &copyRegion);
    });
    */

    RenderPass& colorPass = graph->AddPass("MainRender");
    colorPass.WriteColorAttachment("drawImage");
    colorPass.WriteDepthAttachment("depthTarget", {VK_FORMAT_D32_SFLOAT, renderExtent[0], renderExtent[1]});
    colorPass.ReadBuffer("sceneData", VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    colorPass.Execute([&](VkCommandBuffer cmd) {
        const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph->GetImageView("drawImage"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph->GetImageView("depthTarget"), &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        const ResourceDimensions& dims = graph->GetImageDimensions("drawImage");
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &colorAttachment, &depthAttachment);

        vkCmdBeginRendering(cmd, &renderInfo);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, basicRenderPipeline.pipeline.handle);

        BasicRenderPushConstant pushData{
            .modelMatrix = glm::mat4(1.0f),
            .sceneData = frameResource.sceneDataBuffer.address,
        };

        vkCmdPushConstants(cmd, basicRenderPipeline.pipelineLayout.handle, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(BasicRenderPushConstant), &pushData);
        vkCmdDrawMeshTasksEXT(cmd, 1, 1, 1);

        vkCmdEndRendering(cmd);
    });

    if (!frameBuffer.mainViewFamily.instances.empty()) {
        RenderPass& meshPass = graph->AddPass("MeshRender");
        meshPass.WriteColorAttachment("drawImage");
        meshPass.WriteDepthAttachment("depthTarget");
        meshPass.ReadBuffer("sceneData", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        meshPass.ReadBuffer("vertexBuffer", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        meshPass.ReadBuffer("primitiveBuffer", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        meshPass.ReadBuffer("meshletVertexBuffer", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        meshPass.ReadBuffer("meshletTriangleBuffer", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        meshPass.ReadBuffer("meshletBuffer", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        meshPass.ReadBuffer("materialBuffer", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        meshPass.ReadBuffer("modelBuffer", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        meshPass.ReadBuffer("instanceBuffer", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);

        meshPass.Execute([&](VkCommandBuffer cmd) {
            const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph->GetImageView("drawImage"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph->GetImageView("depthTarget"), nullptr, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            const ResourceDimensions& dims = graph->GetImageDimensions("drawImage");
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &colorAttachment, &depthAttachment);

            vkCmdBeginRendering(cmd, &renderInfo);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshShaderPipeline.pipeline.handle);
            VkDescriptorBufferBindingInfoEXT bindingInfo = graph->GetResourceManager()->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo();
            vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
            uint32_t bufferIndexImage = 0;
            VkDeviceSize bufferOffset = 0;
            vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshShaderPipeline.pipelineLayout.handle, 0, 1, &bufferIndexImage, &bufferOffset);

            for (int32_t i = 0; i < frameBuffer.mainViewFamily.instances.size(); ++i) {
                MeshShaderPushConstants pushConstants{
                    .sceneData = graph->GetBufferAddress("sceneData"),
                    .vertexBuffer = graph->GetBufferAddress("vertexBuffer"),
                    .primitiveBuffer = graph->GetBufferAddress("primitiveBuffer"),
                    .meshletVerticesBuffer = graph->GetBufferAddress("meshletVertexBuffer"),
                    .meshletTrianglesBuffer = graph->GetBufferAddress("meshletTriangleBuffer"),
                    .meshletBuffer = graph->GetBufferAddress("meshletBuffer"),
                    .materialBuffer = graph->GetBufferAddress("materialBuffer"),
                    .modelBuffer = graph->GetBufferAddress("modelBuffer"),
                    .instanceBuffer = graph->GetBufferAddress("instanceBuffer"),
                    .instanceIndex = static_cast<uint32_t>(i)
                };

                vkCmdPushConstants(cmd, meshShaderPipeline.pipelineLayout.handle, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(MeshShaderPushConstants), &pushConstants);

                vkCmdDrawMeshTasksEXT(cmd, 1, 1, 1);
            }

            vkCmdEndRendering(cmd);
        });
    }

    if (frameBuffer.mainViewFamily.views[0].debug != 0) {
        auto& depthDebugPass = graph->AddPass("DepthDebug");
        depthDebugPass.ReadSampledImage("depthTarget");
        depthDebugPass.WriteStorageImage("drawImage");
        depthDebugPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthDebugPipeline.pipeline.handle);

            DepthDebugPushConstant pushData{
                .extent = {renderExtent[0], renderExtent[1]},
                .nearPlane = frameBuffer.mainViewFamily.views[0].nearPlane,
                .farPlane = frameBuffer.mainViewFamily.views[0].farPlane,
                .depthTextureIndex = graph->GetDescriptorIndex("depthTarget"),
                .samplerIndex = resourceManager->linearSamplerIndex,
                .outputImageIndex = graph->GetDescriptorIndex("drawImage")
            };

            vkCmdPushConstants(cmd, depthDebugPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DepthDebugPushConstant), &pushData);

            // Bind your bindless descriptor buffers
            VkDescriptorBufferBindingInfoEXT bindingInfo = resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo();
            vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
            uint32_t bufferIndex = 0;
            VkDeviceSize offset = 0;
            vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, depthDebugPipeline.pipelineLayout.handle, 0, 1, &bufferIndex, &offset);

            uint32_t xDispatch = (renderExtent[0] + 15) / 16;
            uint32_t yDispatch = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }


#if WILL_EDITOR
    auto& imguiEditorPass = graph->AddPass("ImguiEditor");
    imguiEditorPass.ReadSampledImage("depthTarget");
    imguiEditorPass.Execute([&](VkCommandBuffer cmd) {
        const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(graph->GetImageView("drawImage"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const ResourceDimensions& dims = graph->GetImageDimensions("drawImage");
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &imguiAttachment, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);
        ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[currentFrameIndex];
        ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, cmd);

        vkCmdEndRendering(cmd);
    });
#endif

    std::string swapchainName = "swapchain_" + std::to_string(swapchainImageIndex);
    graph->ImportTexture(swapchainName, currentSwapchainImage, currentSwapchainImageView, {swapchain->format, swapchain->extent.width, swapchain->extent.height}, swapchain->usages,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED);

    auto& blitPass = graph->AddPass("BlitToSwapchain");
    blitPass.ReadBlitImage("drawImage");
    blitPass.WriteBlitImage(swapchainName);
    blitPass.Execute([&](VkCommandBuffer cmd) {
        VkImage drawImage = graph->GetImage("drawImage");

        VkOffset3D renderOffset = {static_cast<int32_t>(renderExtent[0]), static_cast<int32_t>(renderExtent[1]), 1};
        VkOffset3D swapchainOffset = {static_cast<int32_t>(swapchain->extent.width), static_cast<int32_t>(swapchain->extent.height), 1};

        VkImageBlit2 blitRegion{};
        blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = renderOffset;
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = swapchainOffset;

        VkBlitImageInfo2 blitInfo{};
        blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blitInfo.srcImage = drawImage;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = currentSwapchainImage;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_LINEAR;

        vkCmdBlitImage2(cmd, &blitInfo);
    });

    graph->SetDebugLogging(frameNumber % 180 == 0);
    graph->Compile();
    graph->Execute(renderSync.commandBuffer);
    graph->PrepareSwapchain(renderSync.commandBuffer, swapchainName);

    // auto [stages, access] = graph->GetBufferFinalState("debugReadbackBuffer");

    VK_CHECK(vkEndCommandBuffer(renderSync.commandBuffer));

    VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
    VkSemaphoreSubmitInfo swapchainSemaphoreWaitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_BLIT_BIT);
    VkSemaphoreSubmitInfo renderSemaphoreSignalInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.renderSemaphore, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, &swapchainSemaphoreWaitInfo, &renderSemaphoreSignalInfo);
    VK_CHECK(vkResetFences(context->device, 1, &renderSync.renderFence));
    VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));

    VkPresentInfoKHR presentInfo = VkHelpers::PresentInfo(&swapchain->handle, nullptr, &swapchainImageIndex);
    presentInfo.pWaitSemaphores = &renderSync.renderSemaphore;
    const VkResult presentResult = vkQueuePresentKHR(context->graphicsQueue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        SPDLOG_TRACE("[RenderThread::Render] Swapchain presentation failed ({})", string_VkResult(presentResult));
        return SWAPCHAIN_OUTDATED;
    }

    return SUCCESS;
}

void RenderThread::ProcessAcquisitions(VkCommandBuffer cmd, Core::FrameBuffer& frameBuffer)
{
    if (frameBuffer.bufferAcquireOperations.empty() && frameBuffer.imageAcquireOperations.empty()) {
        return;
    }

    tempBufferBarriers.clear();
    tempBufferBarriers.reserve(frameBuffer.bufferAcquireOperations.size());
    for (const auto& op : frameBuffer.bufferAcquireOperations) {
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.pNext = nullptr;
        barrier.srcStageMask = op.srcStageMask;
        barrier.srcAccessMask = op.srcAccessMask;
        barrier.dstStageMask = op.dstStageMask;
        barrier.dstAccessMask = op.dstAccessMask;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = reinterpret_cast<VkBuffer>(op.buffer);
        barrier.offset = op.offset;
        barrier.size = op.size;
        tempBufferBarriers.push_back(barrier);
    }

    tempImageBarriers.clear();
    tempImageBarriers.reserve(frameBuffer.imageAcquireOperations.size());
    for (const auto& op : frameBuffer.imageAcquireOperations) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.pNext = nullptr;
        barrier.srcStageMask = op.srcStageMask;
        barrier.srcAccessMask = op.srcAccessMask;
        barrier.dstStageMask = op.dstStageMask;
        barrier.dstAccessMask = op.dstAccessMask;
        barrier.oldLayout = static_cast<VkImageLayout>(op.oldLayout);
        barrier.newLayout = static_cast<VkImageLayout>(op.newLayout);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = reinterpret_cast<VkImage>(op.image);
        barrier.subresourceRange.aspectMask = op.aspectMask;
        barrier.subresourceRange.baseMipLevel = op.baseMipLevel;
        barrier.subresourceRange.levelCount = op.levelCount;
        barrier.subresourceRange.baseArrayLayer = op.baseArrayLayer;
        barrier.subresourceRange.layerCount = op.layerCount;
        tempImageBarriers.push_back(barrier);
    }

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;
    depInfo.dependencyFlags = 0;
    depInfo.bufferMemoryBarrierCount = tempBufferBarriers.size();
    depInfo.pBufferMemoryBarriers = tempBufferBarriers.data();
    depInfo.imageMemoryBarrierCount = tempImageBarriers.size();
    depInfo.pImageMemoryBarriers = tempImageBarriers.data();
    vkCmdPipelineBarrier2(cmd, &depInfo);

    frameBuffer.bufferAcquireOperations.clear();
    frameBuffer.imageAcquireOperations.clear();
}

void RenderThread::CreatePipelines()
{
    basicComputePipeline = BasicComputePipeline(context.get(), resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout);
    basicRenderPipeline = BasicRenderPipeline(context.get());
    meshShaderPipeline = MeshShaderPipeline(context.get(), resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout);
    //
    {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(DepthDebugPushConstant);
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkPipelineLayoutCreateInfo piplineLayoutCreateInfo{};
        piplineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        piplineLayoutCreateInfo.pSetLayouts = &resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle;
        piplineLayoutCreateInfo.setLayoutCount = 1;
        piplineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
        piplineLayoutCreateInfo.pushConstantRangeCount = 1;

        depthDebugPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "debugDepth_compute.spv");
    } {
        VkPipelineLayoutCreateInfo computePipelineLayoutCreateInfo{};
        computePipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computePipelineLayoutCreateInfo.pNext = nullptr;
        computePipelineLayoutCreateInfo.pSetLayouts = nullptr;
        computePipelineLayoutCreateInfo.setLayoutCount = 0;
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VisibilityPushConstant);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        computePipelineLayoutCreateInfo.pPushConstantRanges = &pushConstant;
        computePipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        instancingVisibility = ComputePipeline(context.get(), computePipelineLayoutCreateInfo, Platform::GetShaderPath() / "instancingVisibility_compute.spv");

        pushConstant.size = sizeof(PrefixSumPushConstant);
        instancingPrefixSum = ComputePipeline(context.get(), computePipelineLayoutCreateInfo, Platform::GetShaderPath() / "instancingPrefixSum_compute.spv");

        pushConstant.size = sizeof(IndirectWritePushConstant);
        instancingIndirectConstruction = ComputePipeline(context.get(), computePipelineLayoutCreateInfo, Platform::GetShaderPath() / "instancingCompactAndGenerateIndirect_compute.spv");
    }
}
} // Render
