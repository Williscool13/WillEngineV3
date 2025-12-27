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
#include "resource_manager.h"
#include "render/vulkan/vk_swapchain.h"
#include "render/vulkan/vk_utils.h"
#include "engine/will_engine.h"
#include "platform/paths.h"
#include "platform/thread_utils.h"
#include "render-graph/render_graph.h"
#include "render-graph/render_pass.h"

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
    renderTargets = std::make_unique<RenderTargets>(context.get(), width, height);
    renderExtents = std::make_unique<RenderExtents>(width, height, 1.0f);
    resourceManager = std::make_unique<ResourceManager>(context.get());
    graph = std::make_unique<RenderGraph>(context.get(), resourceManager.get());

    resourceManager->bindlessRenderTargetDescriptorBuffer.WriteDescriptor(COLOR_TARGET_INDEX, {nullptr, renderTargets->colorTargetView.handle, VK_IMAGE_LAYOUT_GENERAL});
    resourceManager->bindlessRenderTargetDescriptorBuffer.WriteDescriptor(DEPTH_TARGET_INDEX, {nullptr, renderTargets->depthTargetView.handle, VK_IMAGE_LAYOUT_GENERAL});

    for (RenderSynchronization& frameSync : frameSynchronization) {
        frameSync = RenderSynchronization(context.get());
        frameSync.Initialize();
    }

    basicComputePipeline = BasicComputePipeline(context.get(), resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout);
    basicRenderPipeline = BasicRenderPipeline(context.get());
    meshShaderPipeline = MeshShaderPipeline(context.get(), resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout);

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
        engineRenderSynchronization->renderFrames.acquire();

        if (bShouldExit.load()) { break; }

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
            resourceManager->bindlessRenderTargetDescriptorBuffer.WriteDescriptor(COLOR_TARGET_INDEX, {nullptr, renderTargets->colorTargetView.handle, VK_IMAGE_LAYOUT_GENERAL});
            resourceManager->bindlessRenderTargetDescriptorBuffer.WriteDescriptor(DEPTH_TARGET_INDEX, {nullptr, renderTargets->depthTargetView.handle, VK_IMAGE_LAYOUT_GENERAL});

            bRenderRequestsRecreate = false;
            bEngineRequestsRecreate = false;
        }

        // Wait for the frame N - 3 to finish using resources
        RenderSynchronization& currentRenderSynchronization = frameSynchronization[currentFrameInFlight];
        FrameResources& currentFrameResources = resourceManager->frameResources[currentFrameInFlight];
        RenderResponse renderResponse = Render(currentFrameInFlight, currentRenderSynchronization, frameBuffer, currentFrameResources);
        if (renderResponse == SWAPCHAIN_OUTDATED) {
            bRenderRequestsRecreate = true;
        }

        frameNumber++;
        currentFrameInFlight = frameNumber % Core::FRAME_BUFFER_COUNT;

        // Wait for next frame's fence. To allow game thread to directly modify the FIF's data
        RenderSynchronization& nextFrameRenderSynchronization = frameSynchronization[currentFrameInFlight];
        vkWaitForFences(context->device, 1, &nextFrameRenderSynchronization.renderFence, true, UINT64_MAX);
        VK_CHECK(vkResetFences(context->device, 1, &nextFrameRenderSynchronization.renderFence));
        engineRenderSynchronization->gameFrames.release();
    }

    vkDeviceWaitIdle(context->device);
}

RenderThread::RenderResponse RenderThread::Render(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer, FrameResources& frameResource)
{
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
    VkImageView currentSwapchainImageView = swapchain->swapchainImageViews[swapchainImageIndex];
    graph->Reset();

    // AllocatedBuffer& currentSceneDataBuffer = frameResource.sceneDataBuffer;

    //
    /*{
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
    }*/

    RenderPass& computePass = graph->AddPass("ComputeDraw");
    computePass.WriteStorageImage("drawImage", {
                                      VK_FORMAT_R8G8B8A8_UNORM, renderExtent[0], renderExtent[1],
                                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                  });
    computePass.Execute([&](VkCommandBuffer cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, basicComputePipeline.pipeline.handle);

        BasicComputePushConstant pushConstant{
            .color1 = {0.0f, 0.0f, 0.0f, 0.0f},
            .color2 = {1.0f, 1.0f, 1.0f, 1.0f},
            .extent = {renderExtent[0], renderExtent[1]},
            .index = graph->GetDescriptorIndex("drawImage"),
        };
        vkCmdPushConstants(cmd, basicComputePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BasicComputePushConstant), &pushConstant);

        VkDescriptorBufferBindingInfoEXT bindingInfo = resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo();
        vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
        uint32_t bufferIndexImage = 0;
        VkDeviceSize bufferOffset = 0;
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, basicComputePipeline.pipelineLayout.handle, 0, 1, &bufferIndexImage, &bufferOffset);

        uint32_t xDispatch = (renderExtent[0] + 15) / 16;
        uint32_t yDispatch = (renderExtent[1] + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    std::string swapchainName = "swapchain_" + std::to_string(swapchainImageIndex);
    graph->ImportTexture(swapchainName, currentSwapchainImage, currentSwapchainImageView, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED);

    RenderPass& blitPass = graph->AddPass("BlitToSwapchain");
    blitPass.ReadTransferImage("drawImage");
    blitPass.WriteTransferImage(swapchainName);
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
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // Wrong, but you'll fix with barriers
        blitInfo.dstImage = currentSwapchainImage;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_LINEAR;

        vkCmdBlitImage2(cmd, &blitInfo);
    });

    graph->SetDebugLogging(true);
    graph->Compile();
    graph->Execute(cmd);

    auto swapchainState = graph->GetResourceState(swapchainName);

    VkImageMemoryBarrier2 presentBarrier = VkHelpers::ImageMemoryBarrier(
        currentSwapchainImage,
        VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        swapchainState.stages, swapchainState.access, swapchainState.layout,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

    VkDependencyInfo depInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &presentBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    VK_CHECK(vkEndCommandBuffer(cmd));

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
} // Render
