//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_render_extents.h"
#include "render/vulkan/vk_render_targets.h"
#include "render/vulkan/vk_resource_manager.h"
#include "render/vulkan/vk_swapchain.h"
#include "render/vulkan/vk_utils.h"
#include "engine/will_engine.h"
#include "platform/paths.h"

#if WILL_EDITOR
#include "render/vulkan/vk_imgui_wrapper.h"
#include "backends/imgui_impl_vulkan.h"
#include "editor/model-generation/model_generator.h"
#endif


namespace Render
{
RenderThread::RenderThread() = default;

RenderThread::~RenderThread() = default;

void RenderThread::Initialize(Core::FrameSync* engineRenderSync, enki::TaskScheduler* scheduler_, SDL_Window* window_, uint32_t width, uint32_t height)
{
    engineRenderSynchronization = engineRenderSync;
    scheduler = scheduler_;
    window = window_;

    context = std::make_unique<VulkanContext>(window);
    swapchain = std::make_unique<Swapchain>(context.get(), width, height);
#if WILL_EDITOR
    imgui = std::make_unique<ImguiWrapper>(context.get(), window, Core::FRAME_BUFFER_COUNT, COLOR_ATTACHMENT_FORMAT);
#endif
    renderTargets = std::make_unique<RenderTargets>(context.get(), width, height);
    renderExtents = std::make_unique<RenderExtents>(width, height, 1.0f);
    resourceManager = std::make_unique<ResourceManager>(context.get());

    resourceManager->bindlessRenderTargetDescriptorBuffer.ForceAllocateStorageImage({COLOR_TARGET_INDEX, 0}, {nullptr, renderTargets->colorTargetView.handle, VK_IMAGE_LAYOUT_GENERAL});
    resourceManager->bindlessRenderTargetDescriptorBuffer.ForceAllocateStorageImage({DEPTH_TARGET_INDEX, 0}, {nullptr, renderTargets->depthTargetView.handle, VK_IMAGE_LAYOUT_GENERAL});

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
    for (FrameResources& frameResource : frameResources) {
        bufferInfo.size = sizeof(SceneData);
        frameResource.sceneDataBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));

        bufferInfo.size = BINDLESS_INSTANCE_BUFFER_SIZE;
        frameResource.instanceBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResource.modelBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResource.jointMatrixBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        bufferInfo.size = BINDLESS_MATERIAL_BUFFER_SIZE;
        frameResource.materialBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
    }

    modelMatrixOperationRingBuffer.Initialize(FRAME_BUFFER_OPERATION_COUNT_LIMIT);
    instanceOperationRingBuffer.Initialize(FRAME_BUFFER_OPERATION_COUNT_LIMIT);
    jointMatrixOperationRingBuffer.Initialize(FRAME_BUFFER_OPERATION_COUNT_LIMIT);

    basicComputePipeline = BasicComputePipeline(context.get(), resourceManager->bindlessRenderTargetDescriptorBuffer.descriptorSetLayout);
    basicRenderPipeline = BasicRenderPipeline(context.get());
    meshShaderPipeline = MeshShaderPipeline(context.get(), resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout);

    if (basicComputePipeline.pipeline.handle == VK_NULL_HANDLE || basicRenderPipeline.pipeline.handle == VK_NULL_HANDLE) {
        SPDLOG_ERROR("Failed to compile shaders");
        exit(1);
    }
}

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
    while (!bShouldExit.load()) {
        engineRenderSynchronization->renderFrames.acquire();

        if (bShouldExit.load()) { break; }

        const uint32_t currentFrameInFlight = frameNumber % Core::FRAME_BUFFER_COUNT;
        Core::FrameBuffer& frameBuffer = engineRenderSynchronization->frameBuffers[currentFrameInFlight];
        assert(frameBuffer.currentFrameBuffer == currentFrameInFlight);


        bEngineRequestsRecreate |= frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate;
        bool bShouldRecreate = !frameBuffer.swapchainRecreateCommand.bIsMinimized && bEngineRequestsRecreate;
        if (bShouldRecreate) {
            SPDLOG_INFO("[RenderThread::ThreadMain] Swapchain Recreated");
            vkDeviceWaitIdle(context->device);

            swapchain->Recreate(frameBuffer.swapchainRecreateCommand.width, frameBuffer.swapchainRecreateCommand.height);
            renderExtents->ApplyResize(frameBuffer.swapchainRecreateCommand.width, frameBuffer.swapchainRecreateCommand.height);
            std::array<uint32_t, 2> newExtents = renderExtents->GetExtent();

            renderTargets->Recreate(newExtents[0], newExtents[1]);
            resourceManager->bindlessRenderTargetDescriptorBuffer.ForceAllocateStorageImage({COLOR_TARGET_INDEX, 0}, {nullptr, renderTargets->colorTargetView.handle, VK_IMAGE_LAYOUT_GENERAL});
            resourceManager->bindlessRenderTargetDescriptorBuffer.ForceAllocateStorageImage({DEPTH_TARGET_INDEX, 0}, {nullptr, renderTargets->depthTargetView.handle, VK_IMAGE_LAYOUT_GENERAL});

            bRenderRequestsRecreate = false;
            bEngineRequestsRecreate = false;
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

    ProcessBufferOperations(frameBuffer, frameResource);

    VK_CHECK(vkResetCommandBuffer(renderSync.commandBuffer, 0));
    VkCommandBufferBeginInfo beginInfo = VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(renderSync.commandBuffer, &beginInfo));
    VkCommandBuffer cmd = renderSync.commandBuffer;

    ProcessAcquisitions(cmd, frameBuffer);

    if (bRenderRequestsRecreate) {
        VK_CHECK(vkEndCommandBuffer(cmd));
        VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
        VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, nullptr, nullptr);
        VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
        return SWAPCHAIN_OUTDATED;
    }

    uint32_t swapchainImageIndex;
    VkResult e = vkAcquireNextImageKHR(context->device, swapchain->handle, UINT64_MAX, renderSync.swapchainSemaphore, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR) {
        SPDLOG_TRACE("[RenderThread::Render] Swapchain acquire failed ({})", string_VkResult(e));
        VK_CHECK(vkEndCommandBuffer(cmd));
        VkCommandBufferSubmitInfo cmdInfo = VkHelpers::CommandBufferSubmitInfo(cmd);
        VkSemaphoreSubmitInfo waitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdInfo, &waitInfo, nullptr);
        VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
        return SWAPCHAIN_OUTDATED;
    }

    std::array<uint32_t, 2> renderExtent = renderExtents->GetScaledExtent();
    VkImage currentSwapchainImage = swapchain->swapchainImages[swapchainImageIndex];
    AllocatedBuffer& currentSceneDataBuffer = frameResource.sceneDataBuffer;
    //
    {
        const glm::vec3 cameraPos = frameBuffer.rawCameraData.cameraWorldPos;
        const glm::vec3 cameraLook = frameBuffer.rawCameraData.cameraLook;
        const glm::vec3 up = frameBuffer.rawCameraData.cameraUp;
        glm::mat4 view = glm::lookAt(cameraPos, cameraLook, up);
        glm::mat4 proj = glm::perspective(
            frameBuffer.rawCameraData.fovDegrees,
            frameBuffer.rawCameraData.aspectRatio,
            frameBuffer.rawCameraData.farPlane,
            frameBuffer.rawCameraData.nearPlane
        );

        sceneData.view = view;
        sceneData.proj = proj;
        sceneData.viewProj = proj * view;
        sceneData.deltaTime = 0.1f;

        auto currentSceneData = static_cast<SceneData*>(currentSceneDataBuffer.allocationInfo.pMappedData);
        memcpy(currentSceneData, &sceneData, sizeof(SceneData));
    }


    VkViewport viewport = VkHelpers::GenerateViewport(renderExtent[0], renderExtent[1]);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = VkHelpers::GenerateScissor(renderExtent[0], renderExtent[1]);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Transition to GENERAL for compute shader access
    {
        auto subresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
        auto barrier = VkHelpers::ImageMemoryBarrier(
            renderTargets->colorTarget.handle,
            subresource,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, renderTargets->colorTarget.layout,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
        );
        renderTargets->colorTarget.layout = VK_IMAGE_LAYOUT_GENERAL;
        auto dependencyInfo = VkHelpers::DependencyInfo(&barrier);
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }
    //
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, basicComputePipeline.pipeline.handle);
        BasicComputePushConstant pushConstant{
            .color1 = {0.0f, 0.0f, 0.0f, 0.0f},
            .color2 = {1.0f, 1.0f, 1.0f, 1.0f},
            .extent = {renderExtent[0], renderExtent[1]},
        };
        vkCmdPushConstants(cmd, basicComputePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BasicComputePushConstant), &pushConstant);

        VkDescriptorBufferBindingInfoEXT bindingInfo = resourceManager->bindlessRenderTargetDescriptorBuffer.GetBindingInfo();
        vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
        uint32_t bufferIndexImage = 0;
        VkDeviceSize bufferOffset = 0;
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, basicComputePipeline.pipelineLayout.handle, 0, 1, &bufferIndexImage, &bufferOffset);


        uint32_t xDispatch = renderExtent[0] + 15 / 16;
        uint32_t yDispatch = renderExtent[1] + 15 / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    }

    //
    {
        auto subresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
        auto barrier = VkHelpers::ImageMemoryBarrier(
            renderTargets->colorTarget.handle,
            subresource,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, renderTargets->colorTarget.layout,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        );
        renderTargets->colorTarget.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        auto dependencyInfo = VkHelpers::DependencyInfo(&barrier);
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    // Main Render Pass
    {
        const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(renderTargets->colorTargetView.handle, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(renderTargets->depthTargetView.handle, &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({renderExtent[0], renderExtent[1]}, &colorAttachment, &depthAttachment);
        vkCmdBeginRendering(cmd, &renderInfo);

        //
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, basicRenderPipeline.pipeline.handle);

            BasicRenderPushConstant pushData{
                .modelMatrix = glm::mat4(1.0f),
                .sceneData = currentSceneDataBuffer.address,
            };

            vkCmdPushConstants(cmd, basicRenderPipeline.pipelineLayout.handle, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(BasicRenderPushConstant), &pushData);
            vkCmdDrawMeshTasksEXT(cmd, 1, 1, 1);
        }
        //
        if (highestInstanceIndex != -1) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshShaderPipeline.pipeline.handle);
            MeshShaderPushConstants pushConstants{
                .sceneData = currentSceneDataBuffer.address,
                .vertexBuffer = resourceManager->megaVertexBuffer.address,
                .primitiveBuffer = resourceManager->primitiveBuffer.address,
                .meshletVerticesBuffer = resourceManager->megaMeshletVerticesBuffer.address,
                .meshletTrianglesBuffer = resourceManager->megaMeshletTrianglesBuffer.address,
                .meshletBuffer = resourceManager->megaMeshletBuffer.address,
                .materialBuffer = frameResource.materialBuffer.address,
                .modelBuffer = frameResource.modelBuffer.address,
                .instanceBuffer = frameResource.instanceBuffer.address,
                .instanceIndex = 0
            };

            vkCmdPushConstants(cmd, meshShaderPipeline.pipelineLayout.handle, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(MeshShaderPushConstants), &pushConstants);
            vkCmdDrawMeshTasksEXT(cmd, 1, 1, 1);
        }

        vkCmdEndRendering(cmd);
    }


#if WILL_EDITOR
    // Imgui Draw
    {
        const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(renderTargets->colorTargetView.handle, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({renderExtent[0], renderExtent[1]}, &imguiAttachment, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);
        ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[currentFrameIndex];
        ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, cmd);

        vkCmdEndRendering(cmd);
    }
#endif


    // Prepare for blit
    {
        VkImageMemoryBarrier2 barriers[2];
        barriers[0] = VkHelpers::ImageMemoryBarrier(
            renderTargets->colorTarget.handle,
            VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, renderTargets->colorTarget.layout,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        renderTargets->colorTarget.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[1] = VkHelpers::ImageMemoryBarrier(
            currentSwapchainImage,
            VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        depInfo.imageMemoryBarrierCount = 2;
        depInfo.pImageMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    // Blit
    {
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
        blitInfo.srcImage = renderTargets->colorTarget.handle;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = currentSwapchainImage;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_LINEAR;

        vkCmdBlitImage2(cmd, &blitInfo);
    }

    //
    {
        auto subresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
        auto barrier = VkHelpers::ImageMemoryBarrier(
            currentSwapchainImage,
            subresource,
            VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        );
        auto dependencyInfo = VkHelpers::DependencyInfo(&barrier);
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
    VkSemaphoreSubmitInfo swapchainSemaphoreWaitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_BLIT_BIT);
    VkSemaphoreSubmitInfo renderSemaphoreSignalInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.renderSemaphore, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, &swapchainSemaphoreWaitInfo, &renderSemaphoreSignalInfo);
    VK_CHECK(vkResetFences(context->device, 1, &renderSync.renderFence));
    VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));

    VkPresentInfoKHR presentInfo = VkHelpers::PresentInfo(&swapchain->handle, nullptr, &swapchainImageIndex);
    presentInfo.pWaitSemaphores = &renderSync.renderSemaphore;
    const VkResult presentResult = vkQueuePresentKHR(context->graphicsQueue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        SPDLOG_TRACE("[RenderThread::Render] Swapchain presentation failed ({})", string_VkResult(e));
        return SWAPCHAIN_OUTDATED;
    }

    return SUCCESS;
}

void RenderThread::ProcessBufferOperations(Core::FrameBuffer& frameBuffer, FrameResources& frameResource)
{
    modelMatrixOperationRingBuffer.Enqueue(frameBuffer.modelMatrixOperations);
    instanceOperationRingBuffer.Enqueue(frameBuffer.instanceOperations);
    jointMatrixOperationRingBuffer.Enqueue(frameBuffer.jointMatrixOperations);
    frameBuffer.modelMatrixOperations.clear();
    frameBuffer.instanceOperations.clear();
    frameBuffer.jointMatrixOperations.clear();

    const AllocatedBuffer& currentModelBuffer = frameResource.modelBuffer;
    const AllocatedBuffer& currentInstanceBuffer = frameResource.instanceBuffer;
    const AllocatedBuffer& currentJointMatrixBuffers = frameResource.jointMatrixBuffer;

    // Copy instance and model changes to CPU mapped memory
    modelMatrixOperationRingBuffer.ProcessOperations(static_cast<char*>(currentModelBuffer.allocationInfo.pMappedData), Core::FRAME_BUFFER_COUNT + 1);
    instanceOperationRingBuffer.ProcessOperations(static_cast<char*>(currentInstanceBuffer.allocationInfo.pMappedData), Core::FRAME_BUFFER_COUNT, highestInstanceIndex);
    jointMatrixOperationRingBuffer.ProcessOperations(static_cast<char*>(currentJointMatrixBuffers.allocationInfo.pMappedData), Core::FRAME_BUFFER_COUNT + 1);
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
} // Render
