//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

#include <cstring>
#include <chrono>
#include <enkiTS/src/TaskScheduler.h>
#include <glm/gtc/packing.hpp>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include "renderer.h"
#include "render_utils.h"
#include "gpu_dispatcher.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_render_extents.h"
#include "resource_manager.h"
#include "render/vulkan/vk_swapchain.h"
#include "render/vulkan/vk_utils.h"
#include "engine/will_engine.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render-graph/render_graph.h"
#include "render-graph/render_pass.h"
#include "shaders/constants_interop.h"
#include "shaders/push_constant_interop.h"
#include "shaders/flags_interop.h"

#include "types/render_types.h"
#include "render/vulkan/vk_imgui_wrapper.h"
#include "backends/imgui_impl_vulkan.h"
#include "core/containers/inline_string.h"
#include "core/containers/span.h"
#include "core/memory/memory_manager.h"
#include "core/string_id.h"
#include "core/time/frame_stamp.h"
#include "core/math/math_helpers.h"
#include "engine/logging/engine_log.h"
#include "pipelines/pipeline_manager.h"
#include "render-view/render_view_helpers.h"
#include "post-processing/post_processing.h"

#if WILL_EDITOR
#include "editor/renderer/debug_readback_buffer.h"
#include "shaders/instancing_interop.h"
#endif


namespace Render
{
RenderThread::RenderThread() = default;

RenderThread::RenderThread(Core::MemoryManager& memoryManager, Core::FrameSync* engineRenderSynchronization, enki::TaskScheduler* scheduler,
                           SDL_Window* window, uint32_t width, uint32_t height)
    : memoryManager(&memoryManager), window(window), engineRenderSynchronization(engineRenderSynchronization), scheduler(scheduler)
{
    Core::TlsfAllocator& renderAlloc = memoryManager.Render();
    Core::TlsfAllocator& assetScratchAlloc = memoryManager.AssetsScratch();

    context = new(memoryManager.RenderAllocRaw(sizeof(VulkanContext))) VulkanContext(window, memoryManager);
    swapchain = new(memoryManager.RenderAllocRaw(sizeof(Swapchain))) Swapchain(context, width, height);
    renderExtents = new(memoryManager.RenderAllocRaw(sizeof(RenderExtents))) RenderExtents(width, height, 1.0f);
    resourceManager = new(memoryManager.RenderAllocRaw(sizeof(ResourceManager))) ResourceManager(context);
    Core::Array<VkDescriptorSetLayout, 3> layouts{
        resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGRTDescriptorBuffer.descriptorSetLayout.handle
    };
    pipelineManager = new(memoryManager.RenderAllocRaw(sizeof(PipelineManager))) PipelineManager(context, resourceManager, renderAlloc, assetScratchAlloc, layouts);
    imgui = new(memoryManager.RenderAllocRaw(sizeof(ImguiWrapper))) ImguiWrapper(context, window, Core::FRAME_BUFFER_COUNT, swapchain->format, pipelineManager->GetPipelineCache());

    tempImageBarriers = Core::Vector<VkImageMemoryBarrier2>(&renderAlloc, Core::AllocTag::Render);

    for (RenderSynchronization& frameSync : frameSynchronization) {
        frameSync = RenderSynchronization(context);
        frameSync.Initialize();
    }

    //
    {
        VkSemaphoreTypeCreateInfo timelineTypeInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        timelineTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineTypeInfo.initialValue = 0;
        VkSemaphoreCreateInfo semaphoreCreateInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineTypeInfo};
        VK_CHECK(vkCreateSemaphore(context->device, &semaphoreCreateInfo, context->HostAllocCallbacks(), &asyncComputeTimelineSemaphore));
    }

    renderArena = Core::VirtualArena(memoryManager.Virtual(), 16ull * 1024 * 1024, Core::AllocTag::Render, "render");
    renderGraph = new(memoryManager.RenderAllocRaw(sizeof(RenderGraph))) RenderGraph(context, resourceManager, renderAlloc, renderArena.Get());
    screenCapture = new(memoryManager.RenderAllocRaw(sizeof(RenderScreenCapture))) RenderScreenCapture(context, scheduler);
    // Vulkan-side NRD init is deferred to the first Record when DenoiserMode::NRD is selected
    nrdDenoiser = new(memoryManager.RenderAllocRaw(sizeof(NrdDenoiser))) NrdDenoiser(context, renderAlloc);
    pipelineStatsQuery.Init(context);
#if WILL_EDITOR
    RegisterDebugReadbacks();
#endif
}

RenderThread::~RenderThread()
{
    pipelineStatsQuery.Destroy(context->device, context->HostAllocCallbacks());
    screenCapture->~RenderScreenCapture();

    for (auto& sync : frameSynchronization) {
        sync = RenderSynchronization{};
    }

    vkDestroySemaphore(context->device, asyncComputeTimelineSemaphore, context->HostAllocCallbacks());

    nrdDenoiser->~NrdDenoiser();
    pipelineManager->~PipelineManager();
    renderGraph->~RenderGraph();
    resourceManager->~ResourceManager();
    renderExtents->~RenderExtents();
    imgui->~ImguiWrapper();
    swapchain->~Swapchain();
    context->~VulkanContext();
}

void RenderThread::InitializePipelineManager(AssetLoad::AsyncAssetLoadManager* _asyncAssetLoadManager, GPUDispatcher* _gpuDispatcher)
{
    gpuDispatcher = _gpuDispatcher;
    pipelineManager->SetAssetLoadThread(_asyncAssetLoadManager);
    pipelineManager->RegisterPipelines();
}

void RenderThread::Start()
{
    bShouldExit.store(false, std::memory_order_release);

    thisThread = std::jthread([this] { ThreadMain(); });
}

void RenderThread::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
    engineRenderSynchronization->renderFrames.Release();
}

void RenderThread::Join()
{
    thisThread.join();
}

void RenderThread::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("RenderThread");
    scheduler->RegisterExternalTaskThread();


    while (!bShouldExit.load()) {
        pipelineManager->Update(frameNumber);
        // Wait for frame
        bool bHasFrame; {
            ZoneScopedN("Idle - WaitForFrame");
            bHasFrame = engineRenderSynchronization->renderFrames.AcquireFor(std::chrono::milliseconds(1));
        }

        if (bShouldExit.load()) {
            break;
        }

        if (bHasFrame) {
            // Render Frame
            {
                currentFrameInFlight = frameNumber % Core::FRAME_BUFFER_COUNT;
                uint32_t frameBufferIndex = engineRenderSynchronization->renderFrameBuffer[currentFrameInFlight];
                Core::FrameBuffer& frameBuffer = engineRenderSynchronization->frameBuffers[frameBufferIndex];
                ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[frameBufferIndex];
                assert(frameBuffer.currentFrameBuffer == currentFrameInFlight);


                bEngineRequestsRecreate |= frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate;
                if (!frameBuffer.swapchainRecreateCommand.bIsMinimized && bEngineRequestsRecreate) {
                    ZoneScopedN("SwapchainRecreate");
                    vkQueueWaitIdle(context->graphicsQueue);
                    gpuDispatcher->WaitAsyncComputeIdle();
                    LOG_INFO(Renderer, "Swapchain Recreated");

                    swapchain->Recreate(frameBuffer.swapchainRecreateCommand.windowWidth, frameBuffer.swapchainRecreateCommand.windowHeight);
                    renderExtents->ApplyResize(frameBuffer.swapchainRecreateCommand.windowWidth, frameBuffer.swapchainRecreateCommand.windowHeight);
                    renderGraph->InvalidateAllSwapchainAssociated();

                    bRenderRequestsRecreate = false;
                    bEngineRequestsRecreate = false;
                    frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = false;
                }

                if (frameBuffer.viewportResizeCommand.bEngineCommandsResize) {
                    vkQueueWaitIdle(context->graphicsQueue);
                    gpuDispatcher->WaitAsyncComputeIdle();
                    LOG_INFO(Renderer, "Viewport remade");

                    renderExtents->ApplyViewportResize(frameBuffer.viewportResizeCommand.offsetX, frameBuffer.viewportResizeCommand.offsetY, frameBuffer.viewportResizeCommand.sizeX,
                                                       frameBuffer.viewportResizeCommand.sizeY);
                    frameBuffer.viewportResizeCommand.bEngineCommandsResize = false;
                    renderGraph->InvalidateAllViewportAssociated();
                }

                if (frameBuffer.mainViewFamily.resolutionScale != lastResolutionScale) {
                    vkQueueWaitIdle(context->graphicsQueue);
                    gpuDispatcher->WaitAsyncComputeIdle();
                    renderExtents->UpdateScale(frameBuffer.mainViewFamily.resolutionScale);
                    lastResolutionScale = frameBuffer.mainViewFamily.resolutionScale;
                    renderGraph->InvalidateAllViewportAssociated();
                }

                // Wait for the frame N - 3 to finish using resources
                RenderSynchronization& currentRenderSynchronization = frameSynchronization[currentFrameInFlight];
                RenderFrame(currentFrameInFlight, currentRenderSynchronization, frameBuffer, imguiSnapshot);

                frameNumber++;
                Core::gRenderFrame.store(frameNumber, std::memory_order_relaxed);
            }

            FrameMark;
            engineRenderSynchronization->gameFrames.Release();
        }

        gpuDispatcher->DrainGraphics();
    }

    while (!gpuDispatcher->IsGraphicsIdle()) {
        gpuDispatcher->DrainGraphics();
    }

    vkDeviceWaitIdle(context->device);
}

void RenderThread::RenderFrame(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer, ImDrawDataSnapshot& imguiSnapshot)
{
    ZoneScoped;

    const auto wallNow = std::chrono::steady_clock::now();
    if (lastWallFrameTime.time_since_epoch().count() != 0) {
        const float wallMs = std::chrono::duration<float, std::milli>(wallNow - lastWallFrameTime).count();
        if (wallMs < 1000.0f) {
            smoothedWallFrameMs = smoothedWallFrameMs <= 0.0f ? wallMs : smoothedWallFrameMs + (wallMs - smoothedWallFrameMs) * 0.02f;
        }
    }
    lastWallFrameTime = wallNow;
    statisticsManager.scratch.wallFrameMs = smoothedWallFrameMs;

    //
    {
        ZoneScopedN("WaitForFence");
        VK_CHECK(vkWaitForFences(context->device, 1, &renderSync.renderFence, true, UINT64_MAX));
        VK_CHECK(vkResetFences(context->device, 1, &renderSync.renderFence));
    }

    const PipelineStatsResults pipelineStats = pipelineStatsQuery.Collect(context->device, currentFrameIndex);
    statisticsManager.scratch.clippingInvocations = pipelineStats.clippingInvocations;
    statisticsManager.scratch.clippingPrimitives = pipelineStats.clippingPrimitives;
    statisticsManager.scratch.fragmentInvocations = pipelineStats.fragmentInvocations;
    statisticsManager.scratch.computeInvocations = pipelineStats.computeInvocations;
    statisticsManager.scratch.meshInvocations = pipelineStats.meshInvocations;
    statisticsManager.scratch.gpuProfile = renderGraph->CollectGPUProfile(currentFrameIndex);
    const float gpuSpanMs = statisticsManager.scratch.gpuProfile.spanMs;
    if (gpuSpanMs > 0.0f) {
        smoothedGpuSpanMs = smoothedGpuSpanMs <= 0.0f ? gpuSpanMs : smoothedGpuSpanMs + (gpuSpanMs - smoothedGpuSpanMs) * 0.02f;
    }
    statisticsManager.scratch.gpuSpanMs = smoothedGpuSpanMs;
    screenCapture->ResolveScreenshot(currentFrameIndex);
    screenCapture->ResolveProbeCapture(currentFrameIndex);

    VK_CHECK(vkResetCommandBuffer(renderSync.commandBuffer, 0));
    VK_CHECK(vkResetCommandBuffer(renderSync.asyncComputeCommandBuffer, 0));
    VkCommandBufferBeginInfo beginInfo = VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(renderSync.commandBuffer, &beginInfo));
    VK_CHECK(vkBeginCommandBuffer(renderSync.asyncComputeCommandBuffer, &beginInfo));

    //
    {
        VkMemoryBarrier2 cutHeadBarrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        };
        VkDependencyInfo cutHeadDepInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        cutHeadDepInfo.memoryBarrierCount = 1;
        cutHeadDepInfo.pMemoryBarriers = &cutHeadBarrier;
        vkCmdPipelineBarrier2(renderSync.asyncComputeCommandBuffer, &cutHeadDepInfo);
    }
    pipelineStatsQuery.Begin(renderSync.commandBuffer, currentFrameIndex);

#ifdef ENABLE_VULKAN_VALIDATION
    VkDebugUtilsObjectNameInfoEXT nameInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = VK_OBJECT_TYPE_COMMAND_BUFFER;
    nameInfo.objectHandle = reinterpret_cast<uint64_t>(renderSync.commandBuffer);
    Core::InlineString<64> cmdBufferName;
    cmdBufferName.len = snprintf(cmdBufferName.buf, 64, "CommandBuffer %llu", static_cast<unsigned long long>(frameNumber));
    nameInfo.pObjectName = cmdBufferName.c_str();
    vkSetDebugUtilsObjectNameEXT(context->device, &nameInfo);
#endif

#ifdef WDEBUG
    Core::InlineString<32> asyncCutLabelName = Core::InlineString<32>::Format("Async Cut F%llu", static_cast<unsigned long long>(frameNumber));
    Core::InlineString<32> frameLabelName = Core::InlineString<32>::Format("Frame F%llu", static_cast<unsigned long long>(frameNumber));
    VkDebugUtilsLabelEXT asyncCutLabel = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = asyncCutLabelName.c_str()};
    VkDebugUtilsLabelEXT frameLabel = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = frameLabelName.c_str()};
    vkCmdBeginDebugUtilsLabelEXT(renderSync.asyncComputeCommandBuffer, &asyncCutLabel);
    vkCmdBeginDebugUtilsLabelEXT(renderSync.commandBuffer, &frameLabel);
#endif

    RenderResponseCode res;
    //
    {
        TracyVkZone(context->tracyContext, renderSync.commandBuffer, "Frame");
        ProcessAcquisitions(renderSync.commandBuffer, frameBuffer.imageAcquireOperations);
        res = RecordFrame(currentFrameIndex, renderSync.commandBuffer, renderSync.asyncComputeCommandBuffer, frameBuffer, imguiSnapshot);
    }
    // ends if not already ended
    pipelineStatsQuery.End(renderSync.commandBuffer, currentFrameIndex);
    statisticsManager.Publish();
    TracyVkCollect(context->tracyContext, renderSync.commandBuffer);
#ifdef WDEBUG
    vkCmdEndDebugUtilsLabelEXT(renderSync.commandBuffer);
    vkCmdEndDebugUtilsLabelEXT(renderSync.asyncComputeCommandBuffer);
#endif
    VK_CHECK(vkEndCommandBuffer(renderSync.commandBuffer));
    VK_CHECK(vkEndCommandBuffer(renderSync.asyncComputeCommandBuffer));

    //
    {
        ZoneScopedN("AsyncComputeSubmit");
        ++asyncComputeTimelineValue;
        VkCommandBufferSubmitInfo asyncCmdSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.asyncComputeCommandBuffer);
        VkSemaphoreSubmitInfo timelineSignalInfo = VkHelpers::TimelineSemaphoreSubmitInfo(asyncComputeTimelineSemaphore, asyncComputeTimelineValue, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&asyncCmdSubmitInfo, nullptr, &timelineSignalInfo);
        gpuDispatcher->SubmitAsyncCompute(submitInfo);
    }

    const VkPipelineStageFlags2 crossCutMask = renderGraph->GetCrossCutWaitStageMask();
    const VkPipelineStageFlags2 timelineWaitStageMask = crossCutMask != 0 ? crossCutMask : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    switch (res) {
        case RENDER_REQUESTED_RECREATE:
        {
            VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
            VkSemaphoreSubmitInfo timelineWaitInfo = VkHelpers::TimelineSemaphoreSubmitInfo(asyncComputeTimelineSemaphore, asyncComputeTimelineValue, timelineWaitStageMask);
            VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, &timelineWaitInfo, nullptr);
            VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
        }
        break;
        case SUCCESS:
        {
#ifdef WDEBUG
            if (renderGraph->IsFrameCorrupted()) {
                LOG_CRITICAL(Renderer, "[RDG] Frame recorded with undeclared resource accesses (see errors above); dropping submission and requesting engine shutdown");
                VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
                VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
                bRenderRequestsShutdown.store(true, std::memory_order_relaxed);
                break;
            }
#endif
            //
            {
                ZoneScopedN("MainSubmit");
                VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
                VkSemaphoreSubmitInfo timelineWaitInfo = VkHelpers::TimelineSemaphoreSubmitInfo(asyncComputeTimelineSemaphore, asyncComputeTimelineValue, timelineWaitStageMask);
                VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, &timelineWaitInfo, nullptr);
                VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
            }

            uint32_t swapchainImageIndex;
            //
            {
                ZoneScopedN("AcquireSwapchainImage");
                const VkResult e = vkAcquireNextImageKHR(context->device, swapchain->handle, UINT64_MAX, renderSync.swapchainSemaphore, nullptr, &swapchainImageIndex);
                if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR) {
                    SPDLOG_TRACE("[RenderThread::Render] Swapchain acquire failed ({})", string_VkResult(e));
                    VkSemaphoreSubmitInfo swapchainSemaphoreWaitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
                    VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
                    submitInfo.waitSemaphoreInfoCount = 1;
                    submitInfo.pWaitSemaphoreInfos = &swapchainSemaphoreWaitInfo;
                    VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
                    bRenderRequestsRecreate = true;
                    break;
                }
            }

            //
            {
                ZoneScopedN("PresentRecord");
                VK_CHECK(vkResetCommandBuffer(renderSync.presentCommandBuffer, 0));
                VK_CHECK(vkBeginCommandBuffer(renderSync.presentCommandBuffer, &beginInfo));
                RecordPresent(renderSync.presentCommandBuffer, swapchainImageIndex, frameBuffer, imguiSnapshot);
                VK_CHECK(vkEndCommandBuffer(renderSync.presentCommandBuffer));
            }
            //
            {
                ZoneScopedN("QueueSubmit");
                VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.presentCommandBuffer);
                VkSemaphoreSubmitInfo swapchainWaitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_BLIT_BIT);
                VkSemaphoreSubmitInfo presentSemaphoreSignalInfo = VkHelpers::SemaphoreSubmitInfo(swapchain->presentSemaphores[swapchainImageIndex], VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
                VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, &swapchainWaitInfo, &presentSemaphoreSignalInfo);
                VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
            }
            //
            {
                ZoneScopedN("QueuePresent");
                VkPresentInfoKHR presentInfo = VkHelpers::PresentInfo(&swapchain->handle, nullptr, &swapchainImageIndex);
                presentInfo.pWaitSemaphores = &swapchain->presentSemaphores[swapchainImageIndex];
                const VkResult presentResult = vkQueuePresentKHR(context->graphicsQueue, &presentInfo);

                if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
                    SPDLOG_TRACE("[RenderThread::Render] Swapchain presentation failed ({})", string_VkResult(presentResult));
                    bRenderRequestsRecreate = true;
                }
            }
        }
        break;
    }
}

void RenderThread::RecordPresent(VkCommandBuffer cmd, uint32_t swapchainImageIndex, const Core::FrameBuffer& frameBuffer, ImDrawDataSnapshot& imguiSnapshot)
{
    ZoneScoped;
#ifdef WDEBUG
    Core::InlineString<32> presentLabelName = Core::InlineString<32>::Format("Present F%llu", static_cast<unsigned long long>(frameNumber));
    VkDebugUtilsLabelEXT presentLabel = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = presentLabelName.c_str()};
    vkCmdBeginDebugUtilsLabelEXT(cmd, &presentLabel);
#endif

    const VkImage swapchainImage = swapchain->swapchainImages[swapchainImageIndex];
    const VkImageSubresourceRange colorRange = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;

    VkImageMemoryBarrier2 toTransferDst = VkHelpers::ImageMemoryBarrier(swapchainImage, colorRange,
                                                                        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    depInfo.pImageMemoryBarriers = &toTransferDst;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    const ResourceDimensions& srcDims = renderGraph->GetImageDimensions(presentSourceTexture);
    const Core::Array<uint32_t, 2> vpOffset = renderExtents->GetViewportOffset();
    const Core::Array<uint32_t, 2> vpExtent = renderExtents->GetViewportExtent();

    VkImageBlit2 blitRegion{};
    blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {static_cast<int32_t>(srcDims.width), static_cast<int32_t>(srcDims.height), 1};
    blitRegion.dstOffsets[0] = {static_cast<int32_t>(vpOffset[0]), static_cast<int32_t>(vpOffset[1] + vpExtent[1]), 0};
    blitRegion.dstOffsets[1] = {static_cast<int32_t>(vpOffset[0] + vpExtent[0]), static_cast<int32_t>(vpOffset[1]), 1};

    VkBlitImageInfo2 blitInfo{};
    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitInfo.srcImage = renderGraph->GetImageHandle(presentSourceTexture);
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.dstImage = swapchainImage;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;
    blitInfo.filter = VK_FILTER_LINEAR;
    vkCmdBlitImage2(cmd, &blitInfo);

    VkPipelineStageFlags2 lastStage = VK_PIPELINE_STAGE_2_BLIT_BIT;
    VkAccessFlags2 lastAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkImageLayout lastLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    if (frameBuffer.bDrawImgui) {
        VkImageMemoryBarrier2 toColorAttachment = VkHelpers::ImageMemoryBarrier(swapchainImage, colorRange,
                                                                                lastStage, lastAccess, lastLayout,
                                                                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        depInfo.pImageMemoryBarriers = &toColorAttachment;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(swapchain->swapchainImageViews[swapchainImageIndex], nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo(swapchain->extent, &imguiAttachment, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);
        ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, cmd);
        vkCmdEndRendering(cmd);

        lastStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        lastAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        lastLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkImageMemoryBarrier2 toPresent = VkHelpers::ImageMemoryBarrier(swapchainImage, colorRange,
                                                                    lastStage, lastAccess, lastLayout,
                                                                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    depInfo.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(cmd, &depInfo);

#ifdef WDEBUG
    vkCmdEndDebugUtilsLabelEXT(cmd);
#endif
}

void RenderThread::ProcessAcquisitions(VkCommandBuffer cmd, Core::Span<Core::ImageAcquireOperation> imageAcquireOperations)
{
    ZoneScoped;
    if (imageAcquireOperations.IsEmpty()) {
        return;
    }

    tempImageBarriers.Clear();
    tempImageBarriers.Reserve(imageAcquireOperations.Size());
    for (const auto& op : imageAcquireOperations) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.pNext = nullptr;
        barrier.srcStageMask = op.srcStageMask;
        barrier.srcAccessMask = op.srcAccessMask;
        barrier.dstStageMask = op.dstStageMask;
        barrier.dstAccessMask = op.dstAccessMask;
        barrier.oldLayout = static_cast<VkImageLayout>(op.oldLayout);
        barrier.newLayout = static_cast<VkImageLayout>(op.newLayout);
        barrier.srcQueueFamilyIndex = op.srcQueueFamilyIndex;
        barrier.dstQueueFamilyIndex = op.dstQueueFamilyIndex;
        barrier.image = reinterpret_cast<VkImage>(op.image);
        barrier.subresourceRange.aspectMask = op.aspectMask;
        barrier.subresourceRange.baseMipLevel = op.baseMipLevel;
        barrier.subresourceRange.levelCount = op.levelCount;
        barrier.subresourceRange.baseArrayLayer = op.baseArrayLayer;
        barrier.subresourceRange.layerCount = op.layerCount;
        tempImageBarriers.PushBack(barrier);
    }

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;
    depInfo.dependencyFlags = 0;
    depInfo.imageMemoryBarrierCount = tempImageBarriers.Size();
    depInfo.pImageMemoryBarriers = tempImageBarriers.Data();
    vkCmdPipelineBarrier2(cmd, &depInfo);
}

#if WILL_EDITOR
void RenderThread::RegisterDebugReadbacks()
{
    struct InstanceMeshletOffsets
    {
        InstanceMeshletOffsetPrefixSum data[1024];
    };
    struct IntermediateMeshlets
    {
        IntermediateMeshlet data[128];
    };
    struct VisibleMeshlets
    {
        CompactedMeshlet data[128];
    };
    struct ShadeDispatchReadback
    {
        BucketDispatchParameters data[16];
    };
    struct LightDispatchReadback
    {
        BucketDispatchParameters data[16];
    };
    struct CursorLitPixel
    {
        uint16_t rgba[4];
    };

    resourceManager->debugReadback.Register<CursorLitPixel>(
        "Cursor Lit HDR",
        [this](RenderGraph& graph, StringID dst, size_t dstOffset) {
            const StringID lit = debugCursorReadback.litTexture;
            if (lit == StringID{} || !graph.HasTexture(lit)) { return; }
            RenderPass& pass = graph.AddPass("[Debug] Readback Cursor Lit"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadCopyImage(lit);
            pass.WriteTransferBuffer(dst);
            pass.Execute([this, lit, dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferImageCopy region{};
                region.bufferOffset = dstOffset;
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageOffset = {static_cast<int32_t>(debugCursorReadback.pixel[0]), static_cast<int32_t>(debugCursorReadback.pixel[1]), 0};
                region.imageExtent = {1, 1, 1};
                vkCmdCopyImageToBuffer(cmd, graph.GetImageHandle(lit), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, graph.GetBufferHandle(dst), 1, &region);
            });
        },
        [this](const CursorLitPixel& d) {
            const glm::vec2 rg = glm::unpackHalf2x16(static_cast<uint32_t>(d.rgba[0]) | (static_cast<uint32_t>(d.rgba[1]) << 16u));
            const glm::vec2 ba = glm::unpackHalf2x16(static_cast<uint32_t>(d.rgba[2]) | (static_cast<uint32_t>(d.rgba[3]) << 16u));
            const float luminance = 0.2126f * rg.x + 0.7152f * rg.y + 0.0722f * ba.x;
            ImGui::Text("Pixel (%u, %u)", debugCursorReadback.pixel[0], debugCursorReadback.pixel[1]);
            ImGui::Text("HDR: %.5f  %.5f  %.5f  (A %.3f)", rg.x, rg.y, ba.x, ba.y);
            ImGui::Text("Luminance: %.5f", luminance);
            ImGui::Text("Scene luminance: %.5f", luminance / preExposure);
        }
    );

    resourceManager->debugReadback.Register<ShadeDispatchReadback>(
        "Shade Dispatch Parameters",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer(SHADING_DISPATCH_BUCKETING_BUFFER)) { return; }
            RenderPass& pass = graph.AddPass("[Debug] Readback Shade Dispatch"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(SHADING_DISPATCH_BUCKETING_BUFFER);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(ShadeDispatchReadback)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(SHADING_DISPATCH_BUCKETING_BUFFER), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const ShadeDispatchReadback& d) {
            if (ImGui::BeginTable("ShadeDispatchTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Material");
                ImGui::TableSetupColumn("Tiles");
                ImGui::TableHeadersRow();
                for (int i = 0; i < 16; ++i) {
                    const BucketDispatchParameters& p = d.data[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.bucketIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.xDispatch);
                }
                ImGui::EndTable();
            }
        }
    );

    resourceManager->debugReadback.Register<LightDispatchReadback>(
        "Light Dispatch Parameters",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER)) { return; }
            RenderPass& pass = graph.AddPass("[Debug] Readback Light Dispatch"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(LightDispatchReadback)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const LightDispatchReadback& d) {
            if (ImGui::BeginTable("LightDispatchTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Lighting");
                ImGui::TableSetupColumn("Tiles");
                ImGui::TableHeadersRow();
                for (int i = 0; i < 16; ++i) {
                    const BucketDispatchParameters& p = d.data[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.bucketIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.xDispatch);
                }
                ImGui::EndTable();
            }
        }
    );

    resourceManager->debugReadback.Register<InstanceMeshletOffsets>(
        "Instance Meshlet Offsets",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer("instance_meshlet_offsets"_sid)) { return; }
            RenderPass& pass = graph.AddPass("[Debug] Readback Instance Meshlet Offsets"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer("instance_meshlet_offsets"_sid);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(InstanceMeshletOffsets)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle("instance_meshlet_offsets"_sid), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const InstanceMeshletOffsets& d) {
            if (ImGui::BeginTable("InstanceMeshletOffsetsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Instance");
                ImGui::TableSetupColumn("Offset");
                ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("LOD");
                ImGui::TableSetupColumn("Primitive Index");
                ImGui::TableHeadersRow();
                for (int i = 0; i < 1024; ++i) {
                    if (d.data[i].count == 0) { continue; }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", d.data[i].offset);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", d.data[i].count);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", d.data[i].lod);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", d.data[i].primitiveIndex);
                }
                ImGui::EndTable();
            }
        }
    );

    resourceManager->debugReadback.Register<InstancingMeshletDispatchIndirect>(
        "Meshlet Dispatch Args",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer("meshlet_count_dispatch_args"_sid)) { return; }
            RenderPass& pass = graph.AddPass("[Debug] Readback Meshlet Dispatch Args"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer("meshlet_count_dispatch_args"_sid);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(InstancingMeshletDispatchIndirect)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle("meshlet_count_dispatch_args"_sid), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const InstancingMeshletDispatchIndirect& d) {
            ImGui::Text("Total Meshlets: %u", d.totalMeshlets);
            ImGui::Text("Dispatch Groups: (%u, %u, %u)", d.x, d.y, d.z);
        }
    );

    resourceManager->debugReadback.Register<IntermediateMeshlets>(
        "Intermediate Meshlets",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer("intermediate_meshlets"_sid)) { return; }
            RenderPass& pass = graph.AddPass("[Debug] Readback Intermediate Meshlets"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer("intermediate_meshlets"_sid);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(IntermediateMeshlets)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle("intermediate_meshlets"_sid), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const IntermediateMeshlets& d) {
            if (ImGui::BeginTable("IntermediateMeshletsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Instance Index");
                ImGui::TableSetupColumn("Visible");
                ImGui::TableSetupColumn("Local Meshlet Index");
                ImGui::TableSetupColumn("LOD");
                ImGui::TableHeadersRow();
                for (int i = 0; i < 128; ++i) {
                    uint32_t instanceIndex = d.data[i].instanceIndex & 0x7FFFFFFF;
                    bool visible = (d.data[i].instanceIndex >> 31) & 1;
                    uint32_t meshletIndex = d.data[i].meshletIndexWithinLOD & 0x3FFFFFFF;
                    uint32_t lod = d.data[i].meshletIndexWithinLOD >> 30;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", instanceIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", visible ? "Yes" : "No");
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", meshletIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", lod);
                }
                ImGui::EndTable();
            }
        }
    );

    resourceManager->debugReadback.Register<VisibleMeshlets>(
        "Visible Meshlets",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer("visible_meshlets"_sid)) { return; }
            RenderPass& pass = graph.AddPass("[Debug] Readback Visible Meshlets"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer("visible_meshlets"_sid);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(VisibleMeshlets)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle("visible_meshlets"_sid), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const VisibleMeshlets& d) {
            if (ImGui::BeginTable("VisibleMeshletsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Instance Index");
                ImGui::TableSetupColumn("Local Meshlet Index");
                ImGui::TableSetupColumn("LOD");
                ImGui::TableHeadersRow();
                for (int i = 0; i < 128; ++i) {
                    uint32_t meshletIndex = d.data[i].meshletIndexWithinLOD & 0x3FFFFFFF;
                    uint32_t lod = d.data[i].meshletIndexWithinLOD >> 30;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", d.data[i].instanceIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", meshletIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", lod);
                }
                ImGui::EndTable();
            }
        }
    );

    resourceManager->debugReadback.Register<InstancingCompactedMeshletDispatchIndirect>(
        "Compacted Dispatch Args",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer("compacted_meshlet_dispatch_args"_sid)) { return; }
            RenderPass& pass = graph.AddPass("[Debug] Readback Compacted Dispatch Args"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer("compacted_meshlet_dispatch_args"_sid);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(InstancingCompactedMeshletDispatchIndirect)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle("compacted_meshlet_dispatch_args"_sid), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const InstancingCompactedMeshletDispatchIndirect& d) {
            ImGui::Text("Total Visible Meshlets: %u", d.totalVisibleMeshlets);
            static constexpr const char* REGION_NAMES[MESHLET_REGION_COUNT] = {"Opaque", "Opaque 2S", "Cutout", "Cutout 2S"};
            for (uint32_t r = 0; r < MESHLET_REGION_COUNT; r++) {
                ImGui::Text("%-10s base %u count %u groups (%u, %u, %u)", REGION_NAMES[r], d.regionBase[r], d.regionArgs[r].w, d.regionArgs[r].x, d.regionArgs[r].y, d.regionArgs[r].z);
            }
        }
    );
}
#endif

void RenderThread::UploadFrameUniforms(const Core::ViewFamily& viewFamily, const Core::Array<uint32_t, 2> renderExtent, float renderDeltaTime) const
{
    ZoneScoped;
    // Scene Data
    auto* sceneData = static_cast<SceneData*>(renderGraph->OpenHostBuffer(SCENE_DATA_BUFFER, SCENE_DATA_BUFFER_SIZE));
    sceneData[0] = GenerateSceneData(viewFamily.mainView, viewFamily.aaConfig, renderExtent, frameNumber, renderDeltaTime, viewFamily.resolutionScale);
    sceneData[0].preExposure = preExposure;
    sceneData[0].prevPreExposure = prevPreExposure;
    sceneData[0].framerateScale = framerateScale;

    const uint32_t analyticLightCount = viewFamily.analyticLightCount;
    const uint32_t triLightCount = viewFamily.triLightCount;
    const uint32_t totalLightLimit = triLightCount > 0 ? static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS) + triLightCount : analyticLightCount;
    const size_t emissiveWorkCount = viewFamily.emissiveTriWork.Size();

    const HostBufferWrite lightDst = renderGraph->OpenHostBufferMirrored(LIGHT_DATA_BUFFER, LIGHT_DATA_BUFFER_SIZE);
    {
        ZoneScopedN("Lights");
        const glm::vec3& dir = viewFamily.directionalLight.direction;
        DirectionalLightData directional{};
        directional.directionIntensity = {dir, viewFamily.directionalLight.bEnabled ? viewFamily.directionalLight.intensity : 0.0f};
        directional.angularRadius = glm::radians(viewFamily.directionalLight.angularRadiusDegrees);
        directional.volumetricScale = viewFamily.directionalLight.volumetricScale;
        directional.packedColor = PackColorRGB8(viewFamily.directionalLight.color);
        lightDst.Write(offsetof(LightData, directionalLight), &directional, sizeof(directional));

        const int32_t counts[4] = {static_cast<int32_t>(totalLightLimit), static_cast<int32_t>(viewFamily.analyticLightCount), static_cast<int32_t>(viewFamily.emissiveMeshletCount), static_cast<int32_t>(viewFamily.emissiveMeshCount)};
        lightDst.Write(offsetof(LightData, lightCount), counts, sizeof(counts));

        const LightInfo* payload = viewFamily.lightPayload.Data();
        size_t cursor = 0;
        for (const Core::DirtyRun& run : viewFamily.lightRuns) {
            lightDst.Write(offsetof(LightData, lights) + run.offset * sizeof(LightInfo), payload + cursor, run.count * sizeof(LightInfo));
            cursor += run.count;
        }
    }

    if (emissiveWorkCount > 0) {
        auto* work = static_cast<EmissiveTriLightWork*>(renderGraph->OpenHostBuffer(EMISSIVE_TRI_WORK_BUFFER, emissiveWorkCount * sizeof(EmissiveTriLightWork)));
        memcpy(work, viewFamily.emissiveTriWork.Data(), emissiveWorkCount * sizeof(EmissiveTriLightWork));
    }

    // Reflection probes
    const auto probeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
    void* probeDst = renderGraph->OpenHostBuffer(REFLECTION_PROBE_BUFFER, REFLECTION_PROBE_BUFFER_SIZE);
    if (probeCount > 0) {
        memcpy(probeDst, viewFamily.reflectionProbes.Data(), probeCount * sizeof(ReflectionProbeGPU));
    }

}

void RenderThread::UploadModelUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    ZoneScoped;

    if (viewFamily.instanceCount > 0) {
        ZoneScopedN("Instances");
        const HostBufferWrite dst = renderGraph->OpenHostBufferMirrored(GEOMETRY_INSTANCE_BUFFER, renderFamilyProperties.instanceBufferSize);
        const Instance* payload = viewFamily.instancePayload.Data();
        size_t cursor = 0;
        for (const Core::DirtyRun& run : viewFamily.instanceRuns) {
            dst.Write(run.offset * sizeof(Instance), payload + cursor, run.count * sizeof(Instance));
            cursor += run.count;
        }
    }

    if (viewFamily.modelCount > 0) {
        ZoneScopedN("Models");
        const HostBufferWrite dst = renderGraph->OpenHostBufferMirrored(GEOMETRY_MODEL_BUFFER, renderFamilyProperties.modelBufferSize);
        const Model* payload = viewFamily.modelPayload.Data();
        size_t cursor = 0;
        for (const Core::DirtyRun& run : viewFamily.modelRuns) {
            dst.Write(run.offset * sizeof(Model), payload + cursor, run.count * sizeof(Model));
            cursor += run.count;
        }
    }

    if (viewFamily.materialCount > 0) {
        {
            ZoneScopedN("Materials");
            const HostBufferWrite dst = renderGraph->OpenHostBufferMirrored(GEOMETRY_MATERIAL_BUFFER, renderFamilyProperties.materialBufferSize);
            const MaterialProperties* payload = viewFamily.materialPayload.Data();
            size_t cursor = 0;
            for (const Core::DirtyRun& run : viewFamily.materialRuns) {
                dst.Write(run.offset * sizeof(MaterialProperties), payload + cursor, run.count * sizeof(MaterialProperties));
                cursor += run.count;
            }
        }

        ZoneScopedN("Dispatch Resets");
        auto* shadeDispatchBuffer = static_cast<BucketDispatchParameters*>(renderGraph->OpenHostBuffer(SHADING_DISPATCH_BUCKETING_BUFFER, renderFamilyProperties.shadeDispatchBufferSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
        for (uint32_t i = 0; i < viewFamily.materialCount; ++i) {
            shadeDispatchBuffer[i] = {.xDispatch = 0, .yDispatch = 1, .zDispatch = 1, .bucketIndex = i};
        }

        auto* lightDispatchBuffer = static_cast<BucketDispatchParameters*>(renderGraph->OpenHostBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER, renderFamilyProperties.lightingDispatchBufferSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));
        for (size_t i = 0; i < pipelineManager->GetLightingPipelines().Size(); ++i) {
            lightDispatchBuffer[i] = {.xDispatch = 0, .yDispatch = 1, .zDispatch = 1, .bucketIndex = static_cast<uint32_t>(i)};
        }
    }
}

void RenderThread::UploadTextUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    ZoneScoped;

    if (viewFamily.worldGlyphQuads.IsEmpty()) { return; }

    void* glyphDst = renderGraph->OpenHostBuffer(TEXT_GLYPH_QUAD_BUFFER, renderFamilyProperties.glyphQuadBufferSize);
    memcpy(glyphDst, viewFamily.worldGlyphQuads.Data(), viewFamily.worldGlyphQuads.Size() * sizeof(WorldGlyphQuad));

    const uint32_t instCount = viewFamily.textInstances.Size();
    auto* instDst = static_cast<TextInstanceData*>(renderGraph->OpenHostBuffer(TEXT_INSTANCE_BUFFER, renderFamilyProperties.textInstanceBufferSize));
    for (uint32_t i = 0; i < instCount; ++i) {
        const Core::TextInstanceDataFull& src = viewFamily.textInstances[i];
        instDst[i] = {
            .modelIndex = src.modelIndex,
            .stableIdLo = static_cast<uint32_t>(src.stableId & 0xFFFFFFFFu),
            .stableIdHi = static_cast<uint32_t>(src.stableId >> 32u),
        };
    }

    void* matDst = renderGraph->OpenHostBuffer(TEXT_MATERIAL_BUFFER, renderFamilyProperties.textMaterialBufferSize);
    memcpy(matDst, viewFamily.textMaterials.Data(), viewFamily.textMaterials.Size() * sizeof(TextRenderMaterial));
}

void RenderThread::UploadSpriteUniforms(const Core::ViewFamily& viewFamily) const
{
    ZoneScoped;

    if (viewFamily.spriteBatches.IsEmpty()) {
        return;
    }

    const uint32_t spriteCount = static_cast<uint32_t>(viewFamily.sprites.Size());
    const size_t uploadSize = spriteCount * sizeof(SpriteData);

    auto* dst = static_cast<SpriteData*>(renderGraph->OpenHostBuffer(SPRITE_BUFFER, uploadSize));

    for (uint32_t i = 0; i < spriteCount; i++) {
        const Core::Sprite& s = viewFamily.sprites[i];
        dst[i] = {
            .worldPosition = s.worldPosition,
            .pixelSize = s.pixelSize,
            .packedColor = PackColorRGBA8(s.color),
            .stableIdLo = static_cast<uint32_t>(s.stableId & 0xFFFFFFFFu),
            .stableIdHi = static_cast<uint32_t>(s.stableId >> 32u),
            .flags = s.billboard ? SPRITE_FLAG_BILLBOARD : 0u,
        };
    }

}

void RenderThread::UploadUIUniforms(const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    ZoneScoped;

    if (!viewFamily.uiGlyphQuads.IsEmpty()) {
        const uint32_t quadCount = viewFamily.uiGlyphQuads.Size();
        void* quadDst = renderGraph->OpenHostBuffer(UI_GLYPH_QUAD_BUFFER, renderFamilyProperties.uiGlyphQuadBufferSize);
        memcpy(quadDst, viewFamily.uiGlyphQuads.Data(), quadCount * sizeof(UIGlyphQuad));
    }
}

#ifdef WDEBUG
struct DebugCircleTable
{
    glm::vec2 c8[9];
    glm::vec2 c16[17];
    glm::vec2 c24[25];
    glm::vec2 c32[33];
};

static const DebugCircleTable& GetDebugCircleTable()
{
    static const DebugCircleTable table = [] {
        DebugCircleTable t{};
        auto fill = [](glm::vec2* out, int n) {
            for (int i = 0; i <= n; ++i) {
                const float a = static_cast<float>(i) / static_cast<float>(n) * 2.0f * glm::pi<float>();
                out[i] = {glm::cos(a), glm::sin(a)};
            }
        };
        fill(t.c8, 8);
        fill(t.c16, 16);
        fill(t.c24, 24);
        fill(t.c32, 32);
        return t;
    }();
    return table;
}
#endif

void RenderThread::SetupDebugRender(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, StringID depthTarget, StringID targetImage, FrameResourceLimits& limits) const
{
#ifdef WDEBUG
    // Worst-case segment counts for buffer allocation
    size_t totalSegments = 0;
    totalSegments += viewFamily.debugLines.Size(); // 1 segment per line
    totalSegments += viewFamily.debugBoxes.Size() * 12; // 12 edges per box
    totalSegments += viewFamily.debugSpheres.Size() * 96; // 32 segs * 3 circles (max LOD)
    totalSegments += viewFamily.debugRects.Size() * 4; // 4 edges per rect
    totalSegments += viewFamily.debugArrows.Size() * 20; // 4 head lines + 4 head base + 4 shaft edges + 4 start ring + 4 front ring
    totalSegments += viewFamily.debugCylinders.Size() * 52; // 24 top + 24 bottom ring + 4 verticals
    totalSegments += viewFamily.debugCapsules.Size() * 100; // 24 top + 24 bottom ring + 4 verticals + 4 cap arcs * 12

    if (totalSegments == 0) {
        return;
    }

    limits.highestDebugSegmentCount = std::max(limits.highestDebugSegmentCount, NextPowerOfTwo(totalSegments));

    auto* segments = static_cast<DebugLineSegment*>(graph.OpenHostBuffer("debug_segment_buffer"_sid, limits.highestDebugSegmentCount * sizeof(DebugLineSegment)));

    uint32_t segmentOffset = 0;

    const glm::mat4 viewMatrix = viewFamily.mainView.currentViewData.view;
    const glm::mat4 projMatrix = viewFamily.mainView.currentViewData.proj;
    Frustum mainViewFrustum = CreateFrustum(projMatrix * viewMatrix);

    for (const auto& sphere : viewFamily.debugSpheres) {
        if (!IntersectsSphere(mainViewFrustum, sphere.center, sphere.radius)) {
            continue;
        }

        const int segs = GetSphereSegments(sphere.center, viewFamily.mainView.currentViewData.cameraPos, sphere.radius);
        const DebugCircleTable& circles = GetDebugCircleTable();
        const glm::vec2* dirs = segs == 32 ? circles.c32 : (segs == 16 ? circles.c16 : circles.c8);
        for (int i = 0; i < segs; ++i) {
            const glm::vec2 d0 = dirs[i] * sphere.radius;
            const glm::vec2 d1 = dirs[i + 1] * sphere.radius;
            glm::vec3 s = sphere.center;
            // XY
            segments[segmentOffset++] = {
                .a = s + glm::vec3(d0.x, d0.y, 0.0f), .width = sphere.width, .b = s + glm::vec3(d1.x, d1.y, 0.0f), .pad = 0.0f, .color = sphere.color
            };
            // XZ
            segments[segmentOffset++] = {
                .a = s + glm::vec3(d0.x, 0.0f, d0.y), .width = sphere.width, .b = s + glm::vec3(d1.x, 0.0f, d1.y), .pad = 0.0f, .color = sphere.color
            };
            // YZ
            segments[segmentOffset++] = {
                .a = s + glm::vec3(0.0f, d0.x, d0.y), .width = sphere.width, .b = s + glm::vec3(0.0f, d1.x, d1.y), .pad = 0.0f, .color = sphere.color
            };
        }
    }

    for (const auto& cyl : viewFamily.debugCylinders) {
        const float bound = std::sqrt(cyl.radius * cyl.radius + cyl.halfHeight * cyl.halfHeight);
        if (!IntersectsSphere(mainViewFrustum, cyl.center, bound)) {
            continue;
        }
        const glm::mat3 rot = glm::mat3_cast(cyl.rotation);
        const glm::vec3 ax = rot * glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 ex = rot * glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 ez = rot * glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 top = cyl.center + ax * cyl.halfHeight;
        const glm::vec3 bot = cyl.center - ax * cyl.halfHeight;
        constexpr int N = 24;
        const glm::vec2* dirs = GetDebugCircleTable().c24;
        for (int i = 0; i < N; ++i) {
            const glm::vec3 d0 = (dirs[i].x * ex + dirs[i].y * ez) * cyl.radius;
            const glm::vec3 d1 = (dirs[i + 1].x * ex + dirs[i + 1].y * ez) * cyl.radius;
            segments[segmentOffset++] = {.a = top + d0, .width = cyl.width, .b = top + d1, .pad = 0.0f, .color = cyl.color};
            segments[segmentOffset++] = {.a = bot + d0, .width = cyl.width, .b = bot + d1, .pad = 0.0f, .color = cyl.color};
            if (i % (N / 4) == 0) {
                segments[segmentOffset++] = {.a = top + d0, .width = cyl.width, .b = bot + d0, .pad = 0.0f, .color = cyl.color};
            }
        }
    }

    for (const auto& cap : viewFamily.debugCapsules) {
        const float bound = cap.halfHeight + cap.radius;
        if (!IntersectsSphere(mainViewFrustum, cap.center, bound)) {
            continue;
        }
        const glm::mat3 rot = glm::mat3_cast(cap.rotation);
        const glm::vec3 ax = rot * glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 ex = rot * glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 ez = rot * glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 top = cap.center + ax * cap.halfHeight;
        const glm::vec3 bot = cap.center - ax * cap.halfHeight;
        constexpr int N = 24;
        const glm::vec2* dirs = GetDebugCircleTable().c24;
        for (int i = 0; i < N; ++i) {
            const glm::vec3 d0 = (dirs[i].x * ex + dirs[i].y * ez) * cap.radius;
            const glm::vec3 d1 = (dirs[i + 1].x * ex + dirs[i + 1].y * ez) * cap.radius;
            segments[segmentOffset++] = {.a = top + d0, .width = cap.width, .b = top + d1, .pad = 0.0f, .color = cap.color};
            segments[segmentOffset++] = {.a = bot + d0, .width = cap.width, .b = bot + d1, .pad = 0.0f, .color = cap.color};
            if (i % (N / 4) == 0) {
                segments[segmentOffset++] = {.a = top + d0, .width = cap.width, .b = bot + d0, .pad = 0.0f, .color = cap.color};
            }
        }
        // The 12-step half-arc angles land exactly on c24 entries 0..12.
        constexpr int H = 12;
        for (int i = 0; i < H; ++i) {
            const float c0 = dirs[i].x, s0 = dirs[i].y, c1 = dirs[i + 1].x, s1 = dirs[i + 1].y;
            const glm::vec3 exr = ex * cap.radius, ezr = ez * cap.radius, axr = ax * cap.radius;
            segments[segmentOffset++] = {.a = top + c0 * exr + s0 * axr, .width = cap.width, .b = top + c1 * exr + s1 * axr, .pad = 0.0f, .color = cap.color};
            segments[segmentOffset++] = {.a = top + c0 * ezr + s0 * axr, .width = cap.width, .b = top + c1 * ezr + s1 * axr, .pad = 0.0f, .color = cap.color};
            segments[segmentOffset++] = {.a = bot + c0 * exr - s0 * axr, .width = cap.width, .b = bot + c1 * exr - s1 * axr, .pad = 0.0f, .color = cap.color};
            segments[segmentOffset++] = {.a = bot + c0 * ezr - s0 * axr, .width = cap.width, .b = bot + c1 * ezr - s1 * axr, .pad = 0.0f, .color = cap.color};
        }
    }

    for (const auto& line : viewFamily.debugLines) {
        segments[segmentOffset++] = {.a = line.start, .width = line.width, .b = line.end, .pad = 0.0f, .color = line.color};
    }

    for (const auto& box : viewFamily.debugBoxes) {
        glm::mat3 rot = glm::mat3_cast(box.rotation);
        if (!IntersectsOBB(mainViewFrustum, box.center, box.extents, rot)) {
            continue;
        }

        glm::vec3 c[8] = {
            box.center + rot * glm::vec3(-box.extents.x, -box.extents.y, -box.extents.z),
            box.center + rot * glm::vec3(box.extents.x, -box.extents.y, -box.extents.z),
            box.center + rot * glm::vec3(box.extents.x, box.extents.y, -box.extents.z),
            box.center + rot * glm::vec3(-box.extents.x, box.extents.y, -box.extents.z),
            box.center + rot * glm::vec3(-box.extents.x, -box.extents.y, box.extents.z),
            box.center + rot * glm::vec3(box.extents.x, -box.extents.y, box.extents.z),
            box.center + rot * glm::vec3(box.extents.x, box.extents.y, box.extents.z),
            box.center + rot * glm::vec3(-box.extents.x, box.extents.y, box.extents.z),
        };
        static constexpr uint32_t edges[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
        for (int i = 0; i < 12; ++i) {
            segments[segmentOffset++] = {.a = c[edges[i * 2]], .width = box.width, .b = c[edges[i * 2 + 1]], .pad = 0.0f, .color = box.color};
        }
    }

    for (const auto& rect : viewFamily.debugRects) {
        glm::vec3 cx = rect.axisX * rect.halfX;
        glm::vec3 cy = rect.axisY * rect.halfY;
        glm::vec3 corners[4] = {rect.center - cx - cy, rect.center + cx - cy, rect.center + cx + cy, rect.center - cx + cy};
        segments[segmentOffset++] = {.a = corners[0], .width = rect.width, .b = corners[1], .pad = 0.0f, .color = rect.color};
        segments[segmentOffset++] = {.a = corners[1], .width = rect.width, .b = corners[2], .pad = 0.0f, .color = rect.color};
        segments[segmentOffset++] = {.a = corners[2], .width = rect.width, .b = corners[3], .pad = 0.0f, .color = rect.color};
        segments[segmentOffset++] = {.a = corners[3], .width = rect.width, .b = corners[0], .pad = 0.0f, .color = rect.color};
    }

    for (const auto& arrow : viewFamily.debugArrows) {
        glm::vec3 dir = arrow.end - arrow.start;
        const float len = glm::length(dir);
        if (len < 1e-6f) { continue; }
        dir /= len;

        const glm::vec3 perp1 = glm::normalize(glm::cross(dir, glm::abs(dir.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f)));
        const glm::vec3 perp2 = glm::cross(dir, perp1);

        const float sh = arrow.shaftWidth;
        const float hh = arrow.headSize;
        const glm::vec3 headBase = arrow.end - dir * arrow.headSize;

        // shaft corners at start
        const glm::vec3 sb[4] = {
            arrow.start + perp1 * sh + perp2 * sh,
            arrow.start - perp1 * sh + perp2 * sh,
            arrow.start - perp1 * sh - perp2 * sh,
            arrow.start + perp1 * sh - perp2 * sh,
        };
        // shaft corners at head base (front ring)
        const glm::vec3 se[4] = {
            headBase + perp1 * sh + perp2 * sh,
            headBase - perp1 * sh + perp2 * sh,
            headBase - perp1 * sh - perp2 * sh,
            headBase + perp1 * sh - perp2 * sh,
        };
        // head base corners
        const glm::vec3 hb[4] = {
            headBase + perp1 * hh + perp2 * hh,
            headBase - perp1 * hh + perp2 * hh,
            headBase - perp1 * hh - perp2 * hh,
            headBase + perp1 * hh - perp2 * hh,
        };

        const float w = arrow.width;
        const glm::vec4 col = arrow.color;

        // 4 lines: tip to head base corners
        for (int i = 0; i < 4; ++i) { segments[segmentOffset++] = {.a = arrow.end, .width = w, .b = hb[i], .pad = 0.0f, .color = col}; }
        // 4 edges: head base quad
        for (int i = 0; i < 4; ++i) { segments[segmentOffset++] = {.a = hb[i], .width = w, .b = hb[(i + 1) % 4], .pad = 0.0f, .color = col}; }
        // 4 edges: shaft long edges
        for (int i = 0; i < 4; ++i) { segments[segmentOffset++] = {.a = sb[i], .width = w, .b = se[i], .pad = 0.0f, .color = col}; }
        // 4 edges: start cap ring
        for (int i = 0; i < 4; ++i) { segments[segmentOffset++] = {.a = sb[i], .width = w, .b = sb[(i + 1) % 4], .pad = 0.0f, .color = col}; }
        // 4 edges: front ring (shaft meets head)
        for (int i = 0; i < 4; ++i) { segments[segmentOffset++] = {.a = se[i], .width = w, .b = se[(i + 1) % 4], .pad = 0.0f, .color = col}; }
    }

    if (segmentOffset == 0) {
        return;
    }

    const uint32_t totalLineSegments = segmentOffset;

    RenderPass& debugDrawPass = graph.AddPass("Debug Draw"_sid, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Debug);
    debugDrawPass.WriteColorAttachment(targetImage);
    bool bHasDepth = graph.HasTexture(depthTarget);
    if (bHasDepth) {
        debugDrawPass.ReadWriteDepthAttachment(depthTarget);
    }
    debugDrawPass.ReadBuffer(SCENE_DATA_BUFFER);
    debugDrawPass.ReadBuffer("debug_segment_buffer"_sid);
    debugDrawPass.Execute([&, width = renderExtent[0], height = renderExtent[1], totalLineSegments, bHasDepth, depthTarget, targetImage](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targetImage), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingInfo renderInfo;
        if (bHasDepth) {
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthTarget), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            renderInfo = VkHelpers::RenderingInfo({width, height}, &colorAttachment, 1, &depthAttachment, nullptr);
        }
        else {
            renderInfo = VkHelpers::RenderingInfo({width, height}, &colorAttachment, 1, nullptr, nullptr);
        }

        vkCmdBeginRendering(cmd, &renderInfo);

        DebugDrawPushConstant pushConstants{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .segmentBuffer = graph.GetBufferAddress("debug_segment_buffer"_sid),
            .sceneDataIndex = 0,
            .totalLineSegments = totalLineSegments,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("debug_render"_sid);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(DebugDrawPushConstant), &pushConstants);

        const uint32_t groupCount = (totalLineSegments + 31) / 32;
        vkCmdDrawMeshTasksEXT(cmd, groupCount, 1, 1);

        vkCmdEndRendering(cmd);
    });

    return;
#else
    return;
#endif
}
} // Render
