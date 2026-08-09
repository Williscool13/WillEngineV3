//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

#include <chrono>
#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>
#include <stb/stb_image_write.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include "renderer.h"
#include "render_utils.h"
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
#include "light_bvh.h"
#include "shaders/constants_interop.h"
#include "shaders/push_constant_interop.h"
#include "shaders/flags_interop.h"

#include "types/render_types.h"
#include "render/vulkan/vk_imgui_wrapper.h"
#include "backends/imgui_impl_vulkan.h"
#include "core/containers/inline_string.h"
#include "core/containers/span.h"
#include "core/memory/arena_suballocator.h"
#include "core/memory/memory_manager.h"
#include "core/string_id.h"
#include "core/math/math_helpers.h"
#include "engine/logging/engine_log.h"
#include "pipelines/pipeline_manager.h"
#include "render-view/render_view_helpers.h"

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

    tempBufferBarriers = Core::Vector<VkBufferMemoryBarrier2>(&renderAlloc, Core::AllocTag::Render);
    tempImageBarriers = Core::Vector<VkImageMemoryBarrier2>(&renderAlloc, Core::AllocTag::Render);
    lightAliasScratch = LightAliasScratch(&renderAlloc);

    for (RenderSynchronization& frameSync : frameSynchronization) {
        frameSync = RenderSynchronization(context);
        frameSync.Initialize();
    }

    renderArena = Core::ManagedArena(memoryManager.ArenaPool(), 4ull * 1024 * 1024, Core::AllocTag::Render);
    renderGraph = new(memoryManager.RenderAllocRaw(sizeof(RenderGraph))) RenderGraph(context, resourceManager, renderAlloc, renderArena.Get());
    screenCapture = new(memoryManager.RenderAllocRaw(sizeof(RenderScreenCapture))) RenderScreenCapture(context, scheduler, memoryManager.AssetsScratch());
    // Vulkan-side NRD init is deferred to the first Record when DenoiserMode::NRD is selected
    nrdDenoiser = new(memoryManager.RenderAllocRaw(sizeof(NrdDenoiser))) NrdDenoiser(context, renderAlloc);
    pipelineStatsQuery.Init(context);
#if WILL_EDITOR
    RegisterDebugReadbacks();
#endif
    // The TLAS is now a first-class RDG acceleration-structure resource (CreateTLAS); the graph owns its buffer, handle and
    // descriptor and destroys them via the physical-pool death timer, so no persistent-buffer registration is needed.
}

RenderThread::~RenderThread()
{
    pipelineStatsQuery.Destroy(context->device);
    screenCapture->~RenderScreenCapture();

    for (auto& sync : frameSynchronization) {
        sync = RenderSynchronization{};
    }

    nrdDenoiser->~NrdDenoiser();
    pipelineManager->~PipelineManager();
    renderGraph->~RenderGraph();
    resourceManager->~ResourceManager();
    renderExtents->~RenderExtents();
    imgui->~ImguiWrapper();
    swapchain->~Swapchain();
    context->~VulkanContext();
}

void RenderThread::InitializePipelineManager(AssetLoad::AsyncAssetLoadManager* _asyncAssetLoadManager)
{
    asyncAssetLoadManager = _asyncAssetLoadManager;
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
    engineRenderSynchronization->renderFrames.fetch_add(1);
    engineRenderSynchronization->renderCV.notify_one();
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
            std::unique_lock lock(engineRenderSynchronization->renderMutex);
            bHasFrame = engineRenderSynchronization->renderCV.wait_for(lock, std::chrono::milliseconds(1), [&] {
                return engineRenderSynchronization->renderFrames.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            if (bHasFrame) {
                engineRenderSynchronization->renderFrames.fetch_sub(1);
            }
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
                    LOG_INFO(Renderer, "Viewport remade");

                    renderExtents->ApplyViewportResize(frameBuffer.viewportResizeCommand.offsetX, frameBuffer.viewportResizeCommand.offsetY, frameBuffer.viewportResizeCommand.sizeX,
                                                       frameBuffer.viewportResizeCommand.sizeY);
                    frameBuffer.viewportResizeCommand.bEngineCommandsResize = false;
                    renderGraph->InvalidateAllViewportAssociated();
                }

                if (frameBuffer.mainViewFamily.resolutionScale != lastResolutionScale) {
                    vkQueueWaitIdle(context->graphicsQueue);
                    renderExtents->UpdateScale(frameBuffer.mainViewFamily.resolutionScale);
                    lastResolutionScale = frameBuffer.mainViewFamily.resolutionScale;
                    renderGraph->InvalidateAllViewportAssociated();
                }

                // Wait for the frame N - 3 to finish using resources
                RenderSynchronization& currentRenderSynchronization = frameSynchronization[currentFrameInFlight];
                RenderFrame(currentFrameInFlight, currentRenderSynchronization, frameBuffer, imguiSnapshot);

                frameNumber++;
            }

            FrameMark;
            engineRenderSynchronization->gameFrames.fetch_add(1, std::memory_order_release);
        } {
            AssetLoad::GPUDispatchRequest req{};
            while (asyncAssetLoadManager->graphicsDispatchQueue.try_dequeue(req)) {
                VkCommandBufferSubmitInfo cmdSubmitInfo = Render::VkHelpers::CommandBufferSubmitInfo(req.cmd);
                VkSemaphoreSubmitInfo waitInfo = Render::VkHelpers::SemaphoreSubmitInfo(req.waitSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
                VkSemaphoreSubmitInfo signalInfo = Render::VkHelpers::SemaphoreSubmitInfo(req.signalSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
                const VkSemaphoreSubmitInfo* pWaitInfo = req.waitSemaphore != VK_NULL_HANDLE ? &waitInfo : nullptr;
                const VkSemaphoreSubmitInfo* pSignalInfo = req.signalSemaphore != VK_NULL_HANDLE ? &signalInfo : nullptr;
                VkSubmitInfo2 submitInfo = Render::VkHelpers::SubmitInfo(&cmdSubmitInfo, pWaitInfo, pSignalInfo);
                VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, req.fence));
                VK_CHECK(vkWaitForFences(context->device, 1, &req.fence, VK_TRUE, UINT64_MAX));
                req.completionSignal->release();
            }
        }
#ifdef WILL_EDITOR
        {
            AssetLoad::GPUDispatchRequest req{};
            while (editorGPUDispatchQueue.try_dequeue(req)) {
                VkCommandBufferSubmitInfo cmdSubmitInfo = Render::VkHelpers::CommandBufferSubmitInfo(req.cmd);
                VkSemaphoreSubmitInfo waitInfo = Render::VkHelpers::SemaphoreSubmitInfo(req.waitSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
                VkSemaphoreSubmitInfo signalInfo = Render::VkHelpers::SemaphoreSubmitInfo(req.signalSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
                const VkSemaphoreSubmitInfo* pWaitInfo = req.waitSemaphore != VK_NULL_HANDLE ? &waitInfo : nullptr;
                const VkSemaphoreSubmitInfo* pSignalInfo = req.signalSemaphore != VK_NULL_HANDLE ? &signalInfo : nullptr;
                VkSubmitInfo2 submitInfo = Render::VkHelpers::SubmitInfo(&cmdSubmitInfo, pWaitInfo, pSignalInfo);
                VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, req.fence));
                VK_CHECK(vkWaitForFences(context->device, 1, &req.fence, VK_TRUE, UINT64_MAX));
                req.completionSignal->release();
            }
        }
#endif
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
    VkCommandBufferBeginInfo beginInfo = VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(renderSync.commandBuffer, &beginInfo));
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

    RenderResponse res;
    //
    {
        TracyVkZone(context->tracyContext, renderSync.commandBuffer, "Frame");
        ProcessAcquisitions(renderSync.commandBuffer, frameBuffer.bufferAcquireOperations, frameBuffer.imageAcquireOperations);
        res = RecordFrame(currentFrameIndex, renderSync.commandBuffer, renderSync.swapchainSemaphore, frameBuffer, imguiSnapshot);
    }
    // ends if not already ended
    pipelineStatsQuery.End(renderSync.commandBuffer, currentFrameIndex);
    statisticsManager.Publish();
    TracyVkCollect(context->tracyContext, renderSync.commandBuffer);
    VK_CHECK(vkEndCommandBuffer(renderSync.commandBuffer));

    switch (res.code) {
        case RENDER_REQUESTED_RECREATE:
        {
            VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
            VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, nullptr, nullptr);
            VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
        }
        break;
        case SWAPCHAIN_OUTDATED:
        {
            VkCommandBufferSubmitInfo cmdInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
            VkSemaphoreSubmitInfo waitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
            VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdInfo, &waitInfo, nullptr);
            VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
            bRenderRequestsRecreate = true;
        }
        break;
        case SUCCESS:
        {
#ifdef WDEBUG
            if (renderGraph->IsFrameCorrupted()) {
                LOG_CRITICAL(Renderer, "[RDG] Frame recorded with undeclared resource accesses (see errors above); dropping submission and requesting engine shutdown");
                VkSemaphoreSubmitInfo swapchainSemaphoreWaitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
                VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
                submitInfo.waitSemaphoreInfoCount = 1;
                submitInfo.pWaitSemaphoreInfos = &swapchainSemaphoreWaitInfo;
                VK_CHECK(vkResetFences(context->device, 1, &renderSync.renderFence));
                VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
                bRenderRequestsShutdown.store(true, std::memory_order_relaxed);
                break;
            }
#endif
            //
            {
                ZoneScopedN("QueueSubmit");
                VkCommandBufferSubmitInfo commandBufferSubmitInfo = VkHelpers::CommandBufferSubmitInfo(renderSync.commandBuffer);
                VkSemaphoreSubmitInfo swapchainSemaphoreWaitInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.swapchainSemaphore, VK_PIPELINE_STAGE_2_BLIT_BIT);
                VkSemaphoreSubmitInfo renderSemaphoreSignalInfo = VkHelpers::SemaphoreSubmitInfo(renderSync.renderSemaphore, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
                VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&commandBufferSubmitInfo, &swapchainSemaphoreWaitInfo, &renderSemaphoreSignalInfo);
                VK_CHECK(vkResetFences(context->device, 1, &renderSync.renderFence));
                VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, renderSync.renderFence));
            }
            //
            {
                ZoneScopedN("QueuePresent");
                VkPresentInfoKHR presentInfo = VkHelpers::PresentInfo(&swapchain->handle, nullptr, &res.swapchainIndex);
                presentInfo.pWaitSemaphores = &renderSync.renderSemaphore;
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

RenderThread::RenderResponse RenderThread::RecordFrame(uint32_t frameIndex, VkCommandBuffer cmd, VkSemaphore swapchainSemaphore, Core::FrameBuffer& frameBuffer, ImDrawDataSnapshot& imguiSnapshot)
{
    ZoneScoped;

    if (bRenderRequestsRecreate) {
        return {RENDER_REQUESTED_RECREATE, ~0u};
    }

    uint32_t swapchainImageIndex;
    //
    {
        VkResult e;
        ZoneScopedN("AcquireSwapchainImage");
        e = vkAcquireNextImageKHR(context->device, swapchain->handle, UINT64_MAX, swapchainSemaphore, nullptr, &swapchainImageIndex);
        if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR) {
            SPDLOG_TRACE("[RenderThread::Render] Swapchain acquire failed ({})", string_VkResult(e));
            return {SWAPCHAIN_OUTDATED, ~0u};
        }
    }

    if (frameBuffer.cacheReset != Core::RenderCacheReset::None) {
        vkQueueWaitIdle(context->graphicsQueue);
        nrdDenoiser->RequestHistoryClear();
    }
    if (frameBuffer.cacheReset == Core::RenderCacheReset::ScreenHistory) {
        renderGraph->InvalidateAllViewportAssociated();
    }
    else if (frameBuffer.cacheReset == Core::RenderCacheReset::All) {
        renderGraph->InvalidateAllCarried();
        rtGroundTruthDIAccumCount = 0;
        rtGroundTruthGIAccumCount = 0;
        rtGroundTruthFullAccumCount = 0;
        previousRestirCheckerboardField = 0;
        previousRestirFullRateResolve = false;
        ddgiPreviousCascades = DDGICascades{};
    }

    renderGraph->Reset(frameIndex, frameNumber, RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD);

    Core::ViewFamily& viewFamily = frameBuffer.mainViewFamily;

    Core::Array<uint32_t, 2> renderExtent = renderExtents->GetScaledExtent();
    Core::Array<uint32_t, 2> outputExtent = renderExtents->GetViewportExtent();

    // For non-TAAU AA passes
    Core::Array<uint32_t, 2> postAaExtent = renderExtent;

    uint32_t debugReservoirCheckerboardField = 0u;

    VkImage currentSwapchainImage = swapchain->swapchainImages[swapchainImageIndex];
    VkImageView currentSwapchainImageView = swapchain->swapchainImageViews[swapchainImageIndex];
    ReadbackStruct* readbackData = renderGraph->GetReadbackData();
    frameBuffer.stableIdUnderCursor = readbackData->selectedStableId;
    statisticsManager.scratch.visibleMeshletCount = readbackData->meshletCount;
    statisticsManager.scratch.culledInstanceFrustum = readbackData->culledInstanceFrustum;
    statisticsManager.scratch.culledInstanceContribution = readbackData->culledInstanceContribution;
    statisticsManager.scratch.culledInstanceOcclusion = readbackData->culledInstanceOcclusion;
    statisticsManager.scratch.culledMeshletFrustum = readbackData->culledMeshletFrustum;
    statisticsManager.scratch.culledMeshletCone = readbackData->culledMeshletCone;
    statisticsManager.scratch.culledMeshletContribution = readbackData->culledMeshletContribution;
    statisticsManager.scratch.culledMeshletOcclusion = readbackData->culledMeshletOcclusion;
    statisticsManager.scratch.shadingDispatches = readbackData->shadingDispatches;
    statisticsManager.scratch.lightingDispatches = readbackData->lightingDispatches;
    statisticsManager.scratch.radianceCache.occupiedSlots = readbackData->wcOccupied;
    statisticsManager.scratch.radianceCache.cellsCarried = readbackData->wcCarried;
    statisticsManager.scratch.radianceCache.cellsEvicted = readbackData->wcEvicted;
    statisticsManager.scratch.radianceCache.insertsFailed = readbackData->wcInsertsFailed;
    statisticsManager.scratch.radianceCache.cellsDumped = readbackData->wcDumped;
    statisticsManager.scratch.radianceCache.cellsDark = readbackData->wcDark;
    statisticsManager.scratch.radianceCache.cellsShaded = readbackData->wcShaded;

    SanitizeViewFamily(viewFamily, pipelineManager, &renderArena.Get());
    PrepareRenderFamily(viewFamily);
    RenderFamilyProperties renderFamilyProperties = PrepareRenderFamilyProperties(viewFamily, readbackData, pipelineManager, frameResourceLimits);
    renderFamilyProperties.bWireframe = frameBuffer.debug.bWireframe;
    renderFamilyProperties.bOcclusionCulling = frameBuffer.debug.bOcclusionCulling;
    renderFamilyProperties.bOcclusionFreeze = frameBuffer.debug.bOcclusionFreeze;
    renderFamilyProperties.cullFlags =
            (frameBuffer.debug.bCullInstanceFrustum ? CULL_FLAG_INSTANCE_FRUSTUM : 0u) |
            (frameBuffer.debug.bCullInstanceContribution ? CULL_FLAG_INSTANCE_CONTRIBUTION : 0u) |
            (frameBuffer.debug.bCullMeshletFrustum ? CULL_FLAG_MESHLET_FRUSTUM : 0u) |
            (frameBuffer.debug.bCullMeshletCone ? CULL_FLAG_MESHLET_CONE : 0u) |
            (frameBuffer.debug.bCullMeshletContribution ? CULL_FLAG_MESHLET_CONTRIBUTION : 0u);

    //
    {
        ZoneScopedN("BindDescriptorBuffers");
        Core::Array<VkDescriptorBufferBindingInfoEXT, 3> bindings{
            resourceManager->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo(),
            resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo(),
            resourceManager->bindlessRDGRTDescriptorBuffer.GetBindingInfo()
        };
        Core::Array<uint32_t, 3> indices{0u, 1u, 2u};
        Core::Array<VkDeviceSize, 3> offsets{0, 0, 0};
        vkCmdBindDescriptorBuffersEXT(cmd, bindings.Size(), bindings.Data());
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineManager->GetGlobalPipelineLayout(), 0, bindings.Size(), indices.Data(), offsets.Data());
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineManager->GetGlobalPipelineLayout(), 0, bindings.Size(), indices.Data(), offsets.Data());
    }
    //
    {
        ZoneScopedN("SetupUniforms");
        UploadFrameUniforms(viewFamily, renderExtent, frameBuffer.timeFrame.renderDeltaTime);
        UploadModelUniforms(viewFamily, renderFamilyProperties);
        UploadTextUniforms(viewFamily, renderFamilyProperties);
        UploadUIUniforms(viewFamily, renderFamilyProperties);
        UploadSpriteUniforms(viewFamily);
    }
    //
    {
        ZoneScopedN("ImportBuffers");
        renderGraph->ImportBufferNoBarrier(GEOMETRY_VERTEX_POSITION_BUFFER, resourceManager->megaVertexPositionBuffer.handle, resourceManager->megaVertexPositionBuffer.address,
                                           {resourceManager->megaVertexPositionBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER, resourceManager->megaVertexAttributeBuffer.handle, resourceManager->megaVertexAttributeBuffer.address,
                                           {resourceManager->megaVertexAttributeBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(GEOMETRY_INDEX_BUFFER, resourceManager->megaIndexBuffer.handle, resourceManager->megaIndexBuffer.address,
                                           {resourceManager->megaIndexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(SID("meshlet_vertex_buffer"), resourceManager->megaMeshletVerticesBuffer.handle, resourceManager->megaMeshletVerticesBuffer.address,
                                           {resourceManager->megaMeshletVerticesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(SID("meshlet_triangle_buffer"), resourceManager->megaMeshletTrianglesBuffer.handle, resourceManager->megaMeshletTrianglesBuffer.address,
                                           {resourceManager->megaMeshletTrianglesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(SID("meshlet_buffer"), resourceManager->megaMeshletBuffer.handle, resourceManager->megaMeshletBuffer.address,
                                           {resourceManager->megaMeshletBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(SID("primitive_buffer"), resourceManager->primitiveBuffer.handle, resourceManager->primitiveBuffer.address,
                                           {resourceManager->primitiveBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
#if WILL_EDITOR
        renderGraph->ImportBuffer(SID("debug_readback_buffer"),
                                  resourceManager->debugReadback.GetHandle(),
                                  resourceManager->debugReadback.GetAddress(),
                                  {resourceManager->debugReadback.GetSize(), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT},
                                  resourceManager->debugReadback.GetLastKnownState());
#endif
    }

    renderGraph->ImportTexture(SID("dummy_black_rg32"),
                               resourceManager->blackDummyRG32Image.handle,
                               resourceManager->blackDummyRG32ImageView.handle,
                               TextureInfo{GBUFFER_STABLE_ID_FORMAT, 1, 1, 1},
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                               VK_IMAGE_LAYOUT_GENERAL,
                               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_IMAGE_LAYOUT_GENERAL);

    // Readback that will be copied into the FIF host memory at the end of the frame (to be read on frame N+3)
    renderGraph->CreateBuffer(SID("readback_buffer"), sizeof(ReadbackStruct), false);
    RenderPass& clearReadbackBuffer = renderGraph->AddPass(SID("Clear Readback Buffer"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::Untagged);
    clearReadbackBuffer.WriteTransferBuffer(SID("readback_buffer"));
    clearReadbackBuffer.Execute([&](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, renderGraph->GetBufferHandle(SID("readback_buffer")), 0, VK_WHOLE_SIZE, 0);
    });


    RenderTargets targets{
        .visibility = "visibility_target"_sid,
        .barycentric = "visibility_barycentric"_sid,
        .derivatives = "visibility_derivatives"_sid,
        .gbufferOne = "gbuffer_one"_sid,
        .gbufferTwo = "gbuffer_two"_sid,
        .shadows = "shadows_resolve_target"_sid,
        .intermediateOne = "intermediate_one"_sid,
        .intermediateTwo = "intermediate_two"_sid,
        .colorOutput = "shading_output"_sid,
        .depthStencil = "depth_target"_sid,
        .depthCopy = "depth_copy"_sid,
        .stableId = "stable_id"_sid,
    };

    renderGraph->CreateTexture(targets.visibility, TextureInfo{VISIBILITY_BUFFER_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_VISIBILITY_EMPTY, true);
    renderGraph->CreateTexture(targets.barycentric, TextureInfo{VISIBILITY_BARYCENTRIC_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.derivatives, TextureInfo{VISIBILITY_DERIVATIVES_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.gbufferOne, TextureInfo{GBUFFER_TARGET_ONE, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.gbufferTwo, TextureInfo{GBUFFER_TARGET_TWO, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.intermediateOne, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.intermediateTwo, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.colorOutput, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_DEPTH_FAR, true);
    renderGraph->CreateTexture(targets.depthCopy, TextureInfo{VK_FORMAT_R32_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    renderGraph->CreateTexture(targets.stableId, TextureInfo{GBUFFER_STABLE_ID_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    SetupSkyboxRendering(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0);

    if (renderFamilyProperties.bCanRender) {
        ZoneScopedN("SetupRenderGraph");

        if (frameBuffer.debug.bEnableGPUDebug) {
            SetupGPUDebugBegin(*renderGraph, frameBuffer.debug.bLockGPUDebug);
        }

        // Geometry
        if (!viewFamily.primitiveInstances.IsEmpty()) {
            SetupGeometryPass(*renderGraph, pipelineManager, viewFamily, renderFamilyProperties, renderExtent, targets, 0);

            SetupVisibilityBarycentricDerivativePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0);

            SetupVisibilityBucketingPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0);

            SetupVisibilityShadingPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, renderArena.Get());

            if (frameBuffer.debug.bEnableShadeDispatchBucketingVisualization) {
                SetupVisibilityBucketingDebugPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, renderArena.Get());
            }

            if (frameBuffer.debug.bEnableLightingBucketingVisualization) {
                SetupLightingBucketingDebugPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0);
            }


            SetupTLASBuild(*renderGraph, context, viewFamily, renderExtent, frameResourceLimits);

            const bool bNeedsWorldGrid = viewFamily.lightingMode == Core::LightingMode::Default
                                         || (viewFamily.lightingMode == Core::LightingMode::ReSTIR && frameBuffer.reflection.bEnabled)
                                         || (viewFamily.lightingMode == Core::LightingMode::ReSTIR && frameBuffer.restir.lightProposal == Core::ReSTIRParams::LightProposal::WorldGridBin)
                                         || frameBuffer.ddgi.bEnabled
                                         || viewFamily.reflectionProbes.Size() > 0u;

            const DDGICascades ddgiCascades = ComputeDDGICascades(frameBuffer.ddgi, viewFamily.mainView.currentViewData.cameraPos, viewFamily.localDDGIVolumes.Data(), static_cast<uint32_t>(viewFamily.localDDGIVolumes.Size()), ddgiPreviousCascades, frameNumber, frameBuffer.debug.bFreezeGIField);

            if (bNeedsWorldGrid) {
                SetupWorldGridBinningPass(*renderGraph, pipelineManager, viewFamily, 0, renderArena.Get(), ddgiCascades);
                if (frameBuffer.debug.bEnableGPUDebug && frameBuffer.debug.bWorldGridDebug && !frameBuffer.debug.bLockGPUDebug) {
                    SetupWorldGridDebug(*renderGraph, pipelineManager, 0, frameBuffer.debug.worldGridDebugLevel);
                }
            }

            const bool bDDGIApply = frameBuffer.ddgi.bEnabled && frameBuffer.ddgi.bApplyToLighting;
            if (frameBuffer.ddgi.bEnabled) {
                const RadianceCacheFrame radianceCache = SetupRadianceCacheBegin(*renderGraph, pipelineManager, frameNumber, viewFamily.mainView.currentViewData.cameraPos, frameBuffer.debug.bFreezeGIField);
                SetupDDGIProbeUpdate(*renderGraph, pipelineManager, renderArena.Get(), frameBuffer.ddgi, ddgiCascades, ddgiPreviousCascades, viewFamily.skyboxIndex, viewFamily.iblIntensity, frameNumber, frameBuffer.debug.bDDGIBounceOnly, radianceCache, static_cast<uint32_t>(viewFamily.reflectionProbes.Size()), viewFamily.bReflectionProbeBruteForce, viewFamily.mainView.currentViewData.cameraPos);
                ddgiPreviousCascades = ddgiCascades;
                const bool bRadianceCacheFeedback = frameBuffer.ddgi.bInfiniteBounce && !frameBuffer.debug.bDDGIBounceOnly;
                SetupRadianceCacheShade(*renderGraph, pipelineManager, radianceCache, 0, bRadianceCacheFeedback, viewFamily.skyboxIndex, viewFamily.iblIntensity, frameBuffer.ddgi.maxRayRadiance, frameBuffer.ddgi.bounceIntensity, frameBuffer.ddgi.radianceCacheAccumCap, static_cast<uint32_t>(viewFamily.reflectionProbes.Size()), viewFamily.bReflectionProbeBruteForce);
                SetupRadianceCacheEnd(*renderGraph, radianceCache);
                if (GPU_STATS_ENABLED && radianceCache.bValid && renderGraph->HasBuffer(SID("readback_buffer"))) {
                    RenderPass& wcStatsReadback = renderGraph->AddPass(SID("Radiance Cache Stats Readback"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::RadianceCache);
                    wcStatsReadback.ReadTransferBuffer(RADIANCE_CACHE_STATS);
                    wcStatsReadback.ReadTransferBuffer(RADIANCE_CACHE_ACTIVE_COUNT);
                    wcStatsReadback.WriteTransferBuffer(SID("readback_buffer"));
                    wcStatsReadback.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                        const VkBuffer dst = graph.GetBufferHandle(SID("readback_buffer"));
                        const VkBufferCopy statsCopy{0, offsetof(ReadbackStruct, wcOccupied), sizeof(RadianceCacheStats)};
                        vkCmdCopyBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_STATS), dst, 1, &statsCopy);
                        const VkBufferCopy shadedCopy{0, offsetof(ReadbackStruct, wcShaded), sizeof(uint32_t)};
                        vkCmdCopyBuffer(cmd, graph.GetBufferHandle(RADIANCE_CACHE_ACTIVE_COUNT), dst, 1, &shadedCopy);
                    });
                }
                if (frameBuffer.debug.bEnableGPUDebug && frameBuffer.debug.bDDGIProbeDebug && !frameBuffer.debug.bLockGPUDebug) {
                    SetupDDGIProbeDebug(*renderGraph, pipelineManager, ddgiCascades, frameBuffer.debug.ddgiProbeDebugExposure, frameBuffer.debug.ddgiProbeDebugCascade, frameBuffer.debug.bDDGIHideInactiveProbes, frameBuffer.debug.ddgiProbeDebugMode);
                }
                if (frameBuffer.debug.bEnableGPUDebug && frameBuffer.debug.bRadianceCacheDebug && !frameBuffer.debug.bLockGPUDebug) {
                    SetupRadianceCacheDebug(*renderGraph, pipelineManager, radianceCache, frameBuffer.debug.radianceCacheDebugExposure, frameBuffer.debug.radianceCacheDebugBucket);
                }
            }

            // Copy depth to R32_SFLOAT for all downstream compute passes.
            {
                auto& copyPass = renderGraph->AddPass(SID("Depth Copy"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged);
                copyPass.ReadSampledImage(targets.depthStencil);
                copyPass.WriteStorageImage(targets.depthCopy);
                copyPass.Execute([depth = targets.depthStencil, depthCopy = targets.depthCopy,
                        w = renderExtent[0], h = renderExtent[1], &pipelineManager = pipelineManager](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                        const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("depth_copy"));
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
                        DepthCopyPushConstant pc{
                            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                            .outputIndex = graph.GetStorageImageViewDescriptorIndex(depthCopy),
                            .extents = {w, h},
                        };
                        vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
                    });
            }

            if (frameBuffer.debug.hizDebugMip >= 0) {
                SetupHiZDebug(*renderGraph, pipelineManager, renderExtent, frameBuffer.debug.hizDebugMip);
            }

            if (frameBuffer.ddgi.bEnabled && frameBuffer.debug.giDeconstructMode != 0) {
                SetupGIDeconstruct(*renderGraph, pipelineManager, renderExtent, targets, 0, frameBuffer.debug.giDeconstructMode);
            }

            if (viewFamily.gtaoConfig.bEnabled) {
                SetupGroundTruthAmbientOcclusion(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, frameNumber, 0);
            }

            // Outputs "shadows_resolve_target"
            SetupShadowsResolve(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0);

            const Core::ReSTIRParams& restir = frameBuffer.restir;
            Core::RELAXParams relax = restir.relax;
            Core::ReBLURParams reblur = restir.reblur;
            const float renderFps = frameBuffer.timeFrame.renderFps;
            const float denoiserFramerateScale = glm::clamp(renderFps > 0.0f ? renderFps / 60.0f : 1.0f, 0.25f, 4.0f);
            relax.framerateScale = denoiserFramerateScale;
            reblur.framerateScale = denoiserFramerateScale;

            // Ground-truth reference overlays are orthogonal to LightingMode: when one is active it replaces the normal lighting path entirely.
            if (viewFamily.groundTruthMode != Core::GroundTruthMode::None) {
                switch (viewFamily.groundTruthMode) {
                    case Core::GroundTruthMode::DI:
                    {
                        if (viewFamily.bResetGroundTruth) { rtGroundTruthDIAccumCount = 0; }
                        SetupRTGroundTruthDI(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, viewFamily.bResetGroundTruth, rtGroundTruthDIAccumCount, frameNumber);
                        rtGroundTruthDIAccumCount += 1;
                        break;
                    }
                    case Core::GroundTruthMode::GI:
                    {
                        if (viewFamily.bResetGroundTruth) { rtGroundTruthGIAccumCount = 0; }
                        SetupRTGroundTruthGI(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, viewFamily.bResetGroundTruth, rtGroundTruthGIAccumCount, frameNumber);
                        rtGroundTruthGIAccumCount += 1;
                        break;
                    }
                    case Core::GroundTruthMode::Full:
                    {
                        if (viewFamily.bResetGroundTruth) { rtGroundTruthFullAccumCount = 0; }
                        SetupRTGroundTruthFull(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, viewFamily.bResetGroundTruth, rtGroundTruthFullAccumCount, frameNumber);
                        rtGroundTruthFullAccumCount += 1;
                        break;
                    }
                    case Core::GroundTruthMode::None:
                        break;
                }
            }
            else {
                uint32_t giGatherMode = 0u;
                const auto giGatherDebug = static_cast<uint32_t>(frameBuffer.debug.giGatherDebugMode);
                if (frameBuffer.ddgi.bEnabled && ((frameBuffer.ddgi.bFinalGather && bDDGIApply) || giGatherDebug != 0u)) {
                    const FinalGatherFrame giGather = SetupFinalGather(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameNumber, frameBuffer.ddgi.bFinalGatherDenoise, frameBuffer.ddgi.bFinalGatherChromaDenoise ? frameBuffer.ddgi.gatherChromaDenoisePasses : 0u, frameBuffer.ddgi.gatherChromaLumaPower, frameBuffer.ddgi.bFinalGatherTemporal, frameBuffer.ddgi.bGatherSkipRay || frameBuffer.debug.bFreezeGatherRay, frameBuffer.ddgi.gatherRaysPerPixel, false, frameBuffer.debug.bFreezeScreenFeedback);
                    if (giGather.bValid) {
                        giGatherMode = (frameBuffer.ddgi.bFinalGather && bDDGIApply) ? 1u : 0u;
                        SetupGIGatherDebug(*renderGraph, pipelineManager, renderExtent, frameBuffer.debug.giGatherDebugMode);
                    }
                }

                switch (viewFamily.lightingMode) {
                    case Core::LightingMode::Default:
                    {
                        if (frameBuffer.debug.bEnableGPUDebug && frameBuffer.debug.bClusterGridDebug && !frameBuffer.debug.bLockGPUDebug) {
                            constexpr float kDebugClusterZFar = 500.0f;
                            SetupClusterGridDebug(*renderGraph, pipelineManager, 0, viewFamily.mainView.currentViewData.nearPlane, kDebugClusterZFar);
                        }
                        // No ReSTIR BRDF ray to piggyback here, so reflections trace their own; the shade/denoise/composite path downstream is shared.
                        if (frameBuffer.reflection.bScreenSpaceTrace) {
                            SetupSSRTracePass(*renderGraph, pipelineManager, renderExtent, targets, 0, frameNumber, 0u, frameBuffer.reflection);
                        }
                        else {
                            SetupReflectionTracePass(*renderGraph, pipelineManager, renderExtent, targets, 0, frameNumber, frameBuffer.reflection);
                        }
                        SetupReflectionShadePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameNumber, 0u, frameBuffer.reflection, bDDGIApply, false, frameBuffer.debug.bFreezeScreenFeedback);
                        SetupVisibilityLightingResolvePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, renderArena.Get(), frameNumber, bDDGIApply, giGatherMode, frameBuffer.reflection);
                        break;
                    }
                    case Core::LightingMode::ReSTIR:
                    {
                        const uint32_t restirCheckerboardField = restir.bCheckerboard ? ((static_cast<uint32_t>(frameNumber) & 1u) ? 1u : 2u) : 0u;
                        debugReservoirCheckerboardField = restirCheckerboardField;
                        const bool bRestirFullRateResolve = restirCheckerboardField != 0u && restir.bCheckerboardFullRateResolve;
                        const bool bRestirDenoised = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX || restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ReBLUR;
                        const uint32_t restirCheckerboardPacked = (!bRestirFullRateResolve && bRestirDenoised) ? 1u : 0u;
                        const float restirCheckerboardResolveSpeed = ComputeCheckerboardResolveAccumSpeed(viewFamily.aaConfig.mode, frameNumber, renderFps);
                        const uint32_t denoiserCheckerboardField = bRestirFullRateResolve ? 0u : restirCheckerboardField;
                        const float denoiserCheckerboardResolveSpeed = bRestirFullRateResolve ? 0.0f : restirCheckerboardResolveSpeed;

                        const bool bResetReSTIRHistory = ((previousRestirCheckerboardField == 0u) != (restirCheckerboardField == 0u)) || (previousRestirFullRateResolve != bRestirFullRateResolve);
                        previousRestirCheckerboardField = restirCheckerboardField;
                        previousRestirFullRateResolve = bRestirFullRateResolve;
                        const bool bScreenSpaceTrace = frameBuffer.reflection.bScreenSpaceTrace;
                        SetupReSTIRPasses(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, renderArena.Get(), frameNumber, restir, restirCheckerboardField, frameBuffer.reflection, bResetReSTIRHistory, bScreenSpaceTrace);
                        if (bScreenSpaceTrace) {
                            SetupSSRTracePass(*renderGraph, pipelineManager, renderExtent, targets, 0, frameNumber, restirCheckerboardField, frameBuffer.reflection);
                        }
                        const bool bMergedReflections = frameBuffer.reflection.bMergedDenoise;
                        const bool bReflectionCheckerboardPacked = bMergedReflections && restirCheckerboardPacked != 0u;
                        SetupReflectionShadePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, frameNumber, restirCheckerboardField, frameBuffer.reflection, bDDGIApply, bReflectionCheckerboardPacked, frameBuffer.debug.bFreezeScreenFeedback);
                        SetupReSTIRLightingResolvePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, renderArena.Get(), frameNumber, restirCheckerboardField, restirCheckerboardPacked, bRestirFullRateResolve ? 1u : 0u, frameBuffer.reflection);

                        const uint32_t remodulateOutputMode = static_cast<uint32_t>(restir.remodulateOutput);

                        if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX) {
                            SetupRELAXDenoiser(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, relax, frameNumber, remodulateOutputMode, viewFamily.iblIntensity, denoiserCheckerboardField, denoiserCheckerboardResolveSpeed, bDDGIApply, frameBuffer.reflection, giGatherMode);
                        }
                        else if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ReBLUR) {
                            SetupReBLURDenoiser(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, reblur, frameNumber, remodulateOutputMode, viewFamily.iblIntensity, denoiserCheckerboardField, denoiserCheckerboardResolveSpeed, bDDGIApply, frameBuffer.reflection, giGatherMode);
                        }
                        else if (restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRD || restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRDReBLUR) {
                            const NrdBackend nrdBackend = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::NRDReBLUR ? NrdBackend::Reblur : NrdBackend::Relax;
                            // Declaration order defines the RDG read/write sequence: prep writes -> dispatch -> writeback
                            if (nrdDenoiser->Prepare(*renderGraph, viewFamily, renderExtent, nrdBackend, relax, reblur, frameNumber, frameIndex, renderFps)) {
                                SetupNRDPrepPasses(*renderGraph, pipelineManager, renderExtent, targets, nrdBackend, reblur);
                                nrdDenoiser->AddDispatchPass(*renderGraph, resourceManager, pipelineManager, frameIndex);
                                SetupNRDOutputPass(*renderGraph, pipelineManager, renderExtent, targets, nrdBackend);
                            }
                            SetupReSTIRRemodulatePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, remodulateOutputMode, viewFamily.iblIntensity, frameNumber, bDDGIApply, frameBuffer.reflection, giGatherMode);
                        }
                        else {
                            SetupReSTIRRemodulatePass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, 0, remodulateOutputMode, viewFamily.iblIntensity, frameNumber, bDDGIApply, frameBuffer.reflection, giGatherMode);
                        }
                        break;
                    }
                    case Core::LightingMode::PathTracing:
                    {
                        SetupRTShadowTest(*renderGraph, context, pipelineManager, viewFamily, renderExtent, targets, targets.colorOutput, 0);
                        break;
                    }
                }

                const bool bSunViaReSTIR = viewFamily.lightingMode == Core::LightingMode::ReSTIR && restir.bSunLight;
                if (viewFamily.directionalLight.bEnabled && !bSunViaReSTIR && (viewFamily.lightingMode == Core::LightingMode::Default || viewFamily.lightingMode == Core::LightingMode::ReSTIR)) {
                    const uint32_t sunShadowPixelScale = viewFamily.sigmaParams.bHalfRes ? 2u : 1u;
                    const Core::Array<uint32_t, 2> sunShadowExtent = viewFamily.sigmaParams.bHalfRes ? Core::Array<uint32_t, 2>{renderExtent[0] / 2, renderExtent[1] / 2} : renderExtent;
                    SetupRTSunShadow(*renderGraph, pipelineManager, viewFamily, sunShadowExtent, renderExtent, targets, 0, frameNumber, sunShadowPixelScale);
                    SetupSigmaShadowDenoise(*renderGraph, pipelineManager, viewFamily, sunShadowExtent, targets, 0, frameNumber);
                    SetupSigmaShadowTemporal(*renderGraph, pipelineManager, viewFamily, sunShadowExtent, targets, 0);
                    SetupDirectionalLightingPass(*renderGraph, pipelineManager, viewFamily, renderExtent, sunShadowExtent, targets, 0, sunShadowPixelScale);
                }
            }
            //SetupDeferredResolvePass(*renderGraph, pipelineManager, viewFamily, renderExtent, deferredResolveTargets, 0);
        }

        // fix portals. again.
        /*bool bHasPortalView = !viewFamily.portalViews.IsEmpty();
        if (bHasPortalView) {
            renderGraph->CreateTexture(portalTargets.albedo, TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1}, true);
            renderGraph->CreateTexture(portalTargets.normal, TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1}, true);
            renderGraph->CreateTexture(portalTargets.pbr, TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1}, true);
            renderGraph->CreateTexture(portalTargets.emissive, TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1}, true);
            renderGraph->CreateTexture(portalTargets.velocity, TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1}, true);
            renderGraph->CreateTexture(portalTargets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, true);

            SetupGeometryPasses(*renderGraph, viewFamily, renderFamilyProperties, renderExtent, portalTargets, 1, true);

            if (renderFamilyProperties.bHasGTAO) {
                SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
            }

            SetupShadowsResolve(*renderGraph, viewFamily, renderExtent, portalTargets, 1);

            if (renderFamilyProperties.bHasDeferred) {
                SetupDeferredLighting(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
            }

            SetupPortalComposite(*renderGraph, viewFamily, renderExtent, targets, portalTargets);
        }*/

        // Snapshot the lit HDR composite BEFORE any overlay/debug/text/sprite pass so the gather's screen tier reads GI, not UI glyphs or debug lines carried into lit_color_history.
        const bool bReflectionScreenSpace = viewFamily.lightingMode == Core::LightingMode::ReSTIR && frameBuffer.reflection.bEnabled && frameBuffer.reflection.bScreenSpaceLighting;
        const bool bGIGatherScreenSpace = frameBuffer.ddgi.bEnabled && (frameBuffer.ddgi.bFinalGather || frameBuffer.debug.giGatherDebugMode != 0);
        const bool bLitColorIsScene = frameBuffer.restir.remodulateOutput == Core::ReSTIRParams::RemodulateOutput::Both && viewFamily.lightingMode != Core::LightingMode::PathTracing;
        const bool bSnapshotLitColor = viewFamily.groundTruthMode == Core::GroundTruthMode::None && bLitColorIsScene && (bReflectionScreenSpace || bGIGatherScreenSpace);
        if (bSnapshotLitColor) {
            renderGraph->CreateTexture(SID("lit_color_preoverlay"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
            auto& snapshotPass = renderGraph->AddPass(SID("Lit Color Snapshot"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Untagged);
            snapshotPass.ReadSampledImage(targets.colorOutput);
            snapshotPass.WriteStorageImage(SID("lit_color_preoverlay"));
            snapshotPass.Execute([src = targets.colorOutput, dst = SID("lit_color_preoverlay"),
                    w = renderExtent[0], h = renderExtent[1], &pipelineManager = pipelineManager](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("color_copy"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
                    ColorCopyPushConstant pc{
                        .srcIndex = graph.GetSampledImageViewDescriptorIndex(src),
                        .dstIndex = graph.GetStorageImageViewDescriptorIndex(dst),
                        .extents = {w, h},
                    };
                    vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
                });
        }

#if WILL_EDITOR
        debugCursorReadback.litTexture = targets.colorOutput;
#endif

        SetupTextForwardPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets);
        SetupSpritesPass(*renderGraph, pipelineManager, viewFamily, renderExtent, targets);

        if (frameBuffer.selectedStableId != 0) {
            SetupSelectionOutlinePass(*renderGraph, pipelineManager, renderExtent, targets, frameBuffer.selectedStableId);
        }

        SetupDebugRender(*renderGraph, viewFamily, renderExtent, targets.depthStencil, targets.colorOutput, frameResourceLimits);

        SetupProbePreviewSpheres(*renderGraph, pipelineManager, renderExtent, targets.depthStencil, targets.colorOutput, viewFamily);

        if (frameBuffer.debug.bEnableGPUDebug) {
            SetupGPUDebugDraw(*renderGraph, pipelineManager, renderExtent, targets.depthStencil, targets.colorOutput, frameBuffer.debug.bLockGPUDebug);
        }

        if (bSnapshotLitColor) {
            renderGraph->CarryTextureToNextFrame(SID("lit_color_preoverlay"), SID("lit_color_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
        }

        switch (viewFamily.aaConfig.mode) {
            case Core::AntiAliasingMode::SMAA:
                targets.colorOutput = SetupSubpixelMorphologicalAntiAliasing(*renderGraph, pipelineManager, viewFamily, renderExtent, targets);
                break;
            case Core::AntiAliasingMode::TAA:
                targets.colorOutput = SetupTemporalAntiAliasing(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, SID("taa_main"));
                break;
            case Core::AntiAliasingMode::NaiveTAA:
                targets.colorOutput = SetupTemporalAntiAliasing(*renderGraph, pipelineManager, viewFamily, renderExtent, targets, SID("taa_naive"));
                break;
            case Core::AntiAliasingMode::DonutTAA:
                targets.colorOutput = SetupDonutTemporalAntiAliasing(*renderGraph, pipelineManager, viewFamily, renderExtent, outputExtent, targets);
                postAaExtent = outputExtent;
                break;
            case Core::AntiAliasingMode::SMAAT2X:
                targets.colorOutput = SetupSMAA_T2X(*renderGraph, pipelineManager, viewFamily, renderExtent, targets);
                break;
            default: break;
        }

        if (frameBuffer.bCaptureProbeFace && screenCapture->CanProbeCapture()) {
            const uint32_t minSquare = std::min(postAaExtent[0], postAaExtent[1]) & ~1u;
            uint32_t captureSquare = frameBuffer.probeCaptureCropSize > 0 ? std::min(frameBuffer.probeCaptureCropSize, minSquare) : minSquare;
            if (captureSquare >= 2) {
                screenCapture->PrepareProbeCaptureResources(captureSquare);
                renderGraph->CreateTexture(SID("probe_capture_intermediate"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, captureSquare, captureSquare, 1}, CLEAR_COLOR_EMPTY, true);

                auto& probeCaptureBlitPass = renderGraph->AddPass(SID("Probe Capture Blit"), VK_PIPELINE_STAGE_2_BLIT_BIT, Render::RenderCategory::Untagged);
                probeCaptureBlitPass.ReadBlitImage(targets.colorOutput);
                probeCaptureBlitPass.WriteBlitImage(SID("probe_capture_intermediate"));
                probeCaptureBlitPass.Execute([&, colorOutput = targets.colorOutput, s = captureSquare, w = postAaExtent[0], h = postAaExtent[1]](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
                    VkImageBlit2 blitRegion{};
                    blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
                    blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    blitRegion.srcOffsets[0] = {static_cast<int32_t>((w - s) / 2), static_cast<int32_t>((h - s) / 2), 0};
                    blitRegion.srcOffsets[1] = {static_cast<int32_t>((w + s) / 2), static_cast<int32_t>((h + s) / 2), 1};
                    blitRegion.dstOffsets[0] = {0, 0, 0};
                    blitRegion.dstOffsets[1] = {static_cast<int32_t>(s), static_cast<int32_t>(s), 1};

                    VkBlitImageInfo2 blitInfo{};
                    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
                    blitInfo.srcImage = renderGraph->GetImageHandle(colorOutput);
                    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    blitInfo.dstImage = renderGraph->GetImageHandle(SID("probe_capture_intermediate"));
                    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    blitInfo.regionCount = 1;
                    blitInfo.pRegions = &blitRegion;
                    blitInfo.filter = VK_FILTER_NEAREST;
                    vkCmdBlitImage2(_cmd, &blitInfo);
                });

                auto& probeCaptureCopyPass = renderGraph->AddPass(SID("Probe Capture Copy"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
                probeCaptureCopyPass.ReadCopyImage(SID("probe_capture_intermediate"));
                probeCaptureCopyPass.Execute([&, s = captureSquare](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
                    VkBufferImageCopy2 copyRegion{};
                    copyRegion.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
                    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    copyRegion.imageExtent = {s, s, 1};

                    VkCopyImageToBufferInfo2 copyInfo{};
                    copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
                    copyInfo.srcImage = renderGraph->GetImageHandle(SID("probe_capture_intermediate"));
                    copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    copyInfo.dstBuffer = screenCapture->probeCaptureReadbackBuffer.handle;
                    copyInfo.regionCount = 1;
                    copyInfo.pRegions = &copyRegion;
                    vkCmdCopyImageToBuffer2(_cmd, &copyInfo);
                });

                screenCapture->probeCapturePendingSlot = frameIndex;
                screenCapture->StartProbeCapture();
            }
        }

        targets.colorOutput = SetupPostProcessing(*renderGraph, pipelineManager, viewFamily, postAaExtent, renderExtent, outputExtent, targets, frameBuffer.timeFrame.renderDeltaTime, frameNumber);

        if (!viewFamily.screenFade.bDrawOverUI) {
            targets.colorOutput = PPScreenFade(*renderGraph, pipelineManager, viewFamily.screenFade, postAaExtent, targets.colorOutput);
        }

        SetupUIRender(*renderGraph, pipelineManager, viewFamily, postAaExtent, targets.colorOutput);

        if (viewFamily.screenFade.bDrawOverUI) {
            targets.colorOutput = PPScreenFade(*renderGraph, pipelineManager, viewFamily.screenFade, postAaExtent, targets.colorOutput);
        }


#if WILL_EDITOR
        if (frameBuffer.currentMousePosition[0] > 0 && frameBuffer.currentMousePosition[0] < outputExtent[0] &&
            frameBuffer.currentMousePosition[1] > 0 && frameBuffer.currentMousePosition[1] < outputExtent[1]) {
            debugCursorReadback.pixel[0] = std::min(renderExtent[0] - 1, static_cast<uint32_t>(std::lround(static_cast<float>(frameBuffer.currentMousePosition[0]) * renderExtent[0] / static_cast<float>(outputExtent[0]))));
            debugCursorReadback.pixel[1] = std::min(renderExtent[1] - 1, static_cast<uint32_t>(std::lround(static_cast<float>(frameBuffer.currentMousePosition[1]) * renderExtent[1] / static_cast<float>(outputExtent[1]))));
        } else {
            debugCursorReadback.litTexture = StringID{};
        }
        resourceManager->debugReadback.ScheduleCopies(*renderGraph, SID("debug_readback_buffer"));

        if (!viewFamily.debugResourceName.IsEmpty()) {
            StringID debugTargetName = StringID(viewFamily.debugResourceName.c_str(), viewFamily.debugResourceName.Size());

            bool bDebugBuffersReady = renderGraph->HasBuffer(SCENE_DATA_BUFFER);

            bool bDebugReservoirReady = true;
            switch (viewFamily.debugTransformationType) {
                case DebugTransformationType::ReservoirLightIdx:
                case DebugTransformationType::ReservoirGenerateW:
                    bDebugReservoirReady = renderGraph->HasBuffer(SID("restir_reservoir_base"));
                    break;
                case DebugTransformationType::ReservoirTemporalLightIdx:
                case DebugTransformationType::ReservoirTemporalW:
                    bDebugReservoirReady = renderGraph->HasBuffer(SID("restir_reservoir_temporal"));
                    break;
                case DebugTransformationType::ReservoirSpatialLightIdx:
                case DebugTransformationType::ReservoirSpatialW:
                    bDebugReservoirReady = renderGraph->HasBuffer(SID("restir_reservoir_spatial"));
                    break;
                case DebugTransformationType::ReservoirHistoryLightIdx:
                case DebugTransformationType::ReservoirHistoryW:
                    bDebugReservoirReady = renderGraph->HasBuffer(SID("restir_reservoir_history"));
                    break;
                default:
                    break;
            }

            if (bDebugBuffersReady && bDebugReservoirReady && renderGraph->HasTexture(debugTargetName)) {
                auto& debugVisPass = renderGraph->AddPass(SID("Debug Visualize"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::Debug);
                debugVisPass.ReadSampledImage(debugTargetName);
                debugVisPass.ReadSampledImage(targets.depthCopy);
                const StringID debugVisBuffers[] = {
                    SCENE_DATA_BUFFER,
                    GEOMETRY_VERTEX_POSITION_BUFFER,
                    GEOMETRY_VERTEX_ATTRIBUTE_BUFFER,
                    GEOMETRY_MESHLET_VERTEX_BUFFER,
                    GEOMETRY_MESHLET_TRIANGLE_BUFFER,
                    GEOMETRY_MESHLET_BUFFER,
                    GEOMETRY_PRIMITIVE_BUFFER,
                    GEOMETRY_INSTANCE_BUFFER,
                    GEOMETRY_MODEL_BUFFER,
                    GEOMETRY_MATERIAL_BUFFER,
                    SID("restir_reservoir_base"),
                    SID("restir_reservoir_temporal"),
                    SID("restir_reservoir_spatial"),
                    SID("restir_reservoir_history"),
                    REFLECTION_PROBE_BUFFER,
                    SID("world_grid_probe_grid"),
                };
                for (const StringID bufferId : debugVisBuffers) {
                    if (renderGraph->HasBuffer(bufferId)) {
                        debugVisPass.ReadBuffer(bufferId);
                    }
                }
                debugVisPass.WriteStorageImage(targets.colorOutput);
                debugVisPass.Execute([&, debugTargetName, colorOutput = targets.colorOutput](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
                    const ResourceDimensions& dims = renderGraph->GetImageDimensions(debugTargetName);
                    VkImageAspectFlags aspect = renderGraph->GetImageAspect(debugTargetName);

                    VkImageAspectFlags viewAspect = aspect;
                    if (viewFamily.debugViewAspect == Core::DebugViewAspect::Depth) {
                        viewAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                    }
                    else if (viewFamily.debugViewAspect == Core::DebugViewAspect::Stencil) {
                        viewAspect = VK_IMAGE_ASPECT_STENCIL_BIT;
                    }

                    ImageChannelType storageType = GetImageChannelType(dims.format, viewAspect);
                    uint32_t textureArrayIndex{3};
                    switch (storageType) {
                        case ImageChannelType::Float4:
                            textureArrayIndex = 0;
                            break;
                        case ImageChannelType::Float2:
                            textureArrayIndex = 1;
                            break;
                        case ImageChannelType::Float:
                            textureArrayIndex = 2;
                            break;
                        case ImageChannelType::UInt4:
                            textureArrayIndex = 3;
                            break;
                        case ImageChannelType::UInt2:
                            textureArrayIndex = 4;
                            break;
                        case ImageChannelType::UInt:
                            textureArrayIndex = 5;
                            break;
                    }

                    uint32_t textureIndexInArray = renderGraph->GetSampledImageViewDescriptorIndex(debugTargetName);
                    if (viewFamily.debugViewAspect == Core::DebugViewAspect::Depth) {
                        textureIndexInArray = renderGraph->GetDepthOnlySampledImageViewDescriptorIndex(debugTargetName);
                    }
                    else if (viewFamily.debugViewAspect == Core::DebugViewAspect::Stencil) {
                        // uint storage descriptor array
                        textureArrayIndex = 7;
                        textureIndexInArray = renderGraph->GetStencilOnlyStorageImageViewDescriptorIndex(debugTargetName);
                    }

                    uint32_t outputIndexIndex = renderGraph->GetStorageImageViewDescriptorIndex(colorOutput);


                    const uint32_t historyCheckerboardField = debugReservoirCheckerboardField == 0u ? 0u : (3u - debugReservoirCheckerboardField);

                    DebugVisualizePushConstant pc{
                        .sceneData = renderGraph->TryGetBufferAddress(SCENE_DATA_BUFFER),
                        .vertexPosBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
                        .vertexAttrBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
                        .meshletVerticesBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
                        .meshletTrianglesBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
                        .meshletBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                        .primitiveBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                        .instanceBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                        .modelBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MODEL_BUFFER),
                        .materialBuffer = renderGraph->TryGetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                        .reservoirBuffer = renderGraph->TryGetBufferAddress(SID("restir_reservoir_base")),
                        .reservoirTemporalBuffer = renderGraph->TryGetBufferAddress(SID("restir_reservoir_temporal")),
                        .reservoirSpatialBuffer = renderGraph->TryGetBufferAddress(SID("restir_reservoir_spatial")),
                        .reservoirHistoryBuffer = renderGraph->TryGetBufferAddress(SID("restir_reservoir_history")),
                        .srcExtent = {renderExtent[0], renderExtent[1]},
                        .dstExtent = {postAaExtent[0], postAaExtent[1]},
                        .nearPlane = viewFamily.mainView.currentViewData.nearPlane,
                        .textureArrayIndex = textureArrayIndex,
                        .textureIndexInArray = textureIndexInArray,
                        .valueTransformationType = static_cast<uint32_t>(viewFamily.debugTransformationType),
                        .outputImageIndex = outputIndexIndex,
                        .depthTextureIndex = renderGraph->GetSampledImageViewDescriptorIndex(targets.depthCopy),
                        .checkerboardField = debugReservoirCheckerboardField,
                        .historyCheckerboardField = historyCheckerboardField,
                        .reflectionProbes = viewFamily.reflectionProbes.Size() > 0u ? renderGraph->TryGetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
                        .reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size()),
                        .worldGridProbeGrid = viewFamily.bReflectionProbeBruteForce ? 0 : renderGraph->TryGetBufferAddress(SID("world_grid_probe_grid")),
                    };
                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("debug_visualize"));
                    vkCmdBindPipeline(_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(_cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    uint32_t xDispatch = (postAaExtent[0] + 15) / 16;
                    uint32_t yDispatch = (postAaExtent[1] + 15) / 16;
                    vkCmdDispatch(_cmd, xDispatch, yDispatch, 1);
                });
            }
        }
#endif
    }

    renderGraph->ImportTexture(SID("swapchain_image"), currentSwapchainImage, currentSwapchainImageView, TextureInfo{swapchain->format, swapchain->extent.width, swapchain->extent.height, 1},
                               swapchain->usages,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED, true);

    auto& blitPass = renderGraph->AddPass(SID("Blit To Swapchain"), VK_PIPELINE_STAGE_2_BLIT_BIT, Render::RenderCategory::Untagged);
    blitPass.ReadBlitImage(targets.colorOutput);
    blitPass.WriteBlitImage(SID("swapchain_image"));
    blitPass.Execute([&, colorOutput = targets.colorOutput](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
        VkImage drawImage = renderGraph->GetImageHandle(colorOutput);

        Core::Array<uint32_t, 2> vpOffset = renderExtents->GetViewportOffset();
        Core::Array<uint32_t, 2> vpExtent = renderExtents->GetViewportExtent();

        VkImageBlit2 blitRegion{};
        blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(postAaExtent[0]), static_cast<int32_t>(postAaExtent[1]), 1};
        blitRegion.dstOffsets[0] = {static_cast<int32_t>(vpOffset[0]), static_cast<int32_t>(vpOffset[1] + vpExtent[1]), 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>(vpOffset[0] + vpExtent[0]), static_cast<int32_t>(vpOffset[1]), 1};

        VkBlitImageInfo2 blitInfo{};
        blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blitInfo.srcImage = drawImage;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = currentSwapchainImage;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_LINEAR;

        vkCmdBlitImage2(_cmd, &blitInfo);
    });

    if (frameBuffer.bDrawImgui) {
        auto& imguiEditorPass = renderGraph->AddPass(SID("Imgui Draw"), VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, Render::RenderCategory::UI);
        imguiEditorPass.WriteColorAttachment(SID("swapchain_image"));
        imguiEditorPass.Execute([&, frameIndex](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
            // Try to end before imgui draws so they're not included in statistics
            pipelineStatsQuery.End(_cmd, frameIndex);

            const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(renderGraph->GetImageViewHandle(SID("swapchain_image")), nullptr,
                                                                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const ResourceDimensions& dims = renderGraph->GetImageDimensions(SID("swapchain_image"));
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &imguiAttachment, nullptr);
            vkCmdBeginRendering(_cmd, &renderInfo);
            ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, _cmd);

            vkCmdEndRendering(_cmd);
        });
    }

    if (frameBuffer.bTakeScreenshot && screenCapture->CanScreenshot()) {
        screenCapture->PrepareScreenshotResources(postAaExtent[0], postAaExtent[1]);
        renderGraph->CreateTexture(SID("screenshot_intermediate"), TextureInfo{VK_FORMAT_R8G8B8A8_SRGB, postAaExtent[0], postAaExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

        auto& screenshotBlitPass = renderGraph->AddPass(SID("Screenshot Blit"), VK_PIPELINE_STAGE_2_BLIT_BIT, Render::RenderCategory::Untagged);
        screenshotBlitPass.ReadBlitImage(targets.colorOutput);
        screenshotBlitPass.WriteBlitImage(SID("screenshot_intermediate"));
        screenshotBlitPass.Execute([&, colorOutput = targets.colorOutput](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
            VkImageBlit2 blitRegion{};
            blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blitRegion.srcOffsets[0] = {0, 0, 0};
            blitRegion.srcOffsets[1] = {static_cast<int32_t>(postAaExtent[0]), static_cast<int32_t>(postAaExtent[1]), 1};
            blitRegion.dstOffsets[0] = {0, 0, 0};
            blitRegion.dstOffsets[1] = {static_cast<int32_t>(postAaExtent[0]), static_cast<int32_t>(postAaExtent[1]), 1};

            VkBlitImageInfo2 blitInfo{};
            blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blitInfo.srcImage = renderGraph->GetImageHandle(colorOutput);
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.dstImage = renderGraph->GetImageHandle(SID("screenshot_intermediate"));
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &blitRegion;
            blitInfo.filter = VK_FILTER_NEAREST;
            vkCmdBlitImage2(_cmd, &blitInfo);
        });

        auto& screenshotCopyPass = renderGraph->AddPass(SID("Screenshot Copy"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
        screenshotCopyPass.ReadCopyImage(SID("screenshot_intermediate"));
        screenshotCopyPass.Execute([&](VkCommandBuffer _cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferImageCopy2 copyRegion{};
            copyRegion.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
            copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.imageExtent = {screenCapture->screenshotCaptureWidth, screenCapture->screenshotCaptureHeight, 1};

            VkCopyImageToBufferInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
            copyInfo.srcImage = renderGraph->GetImageHandle(SID("screenshot_intermediate"));
            copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyInfo.dstBuffer = screenCapture->screenshotReadbackBuffer.handle;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &copyRegion;
            vkCmdCopyImageToBuffer2(_cmd, &copyInfo);
        });

        if (frameBuffer.screenshotPath.IsEmpty()) {
            Core::Path screenshotDir = Platform::GetUserDataPath() / "screenshots";
            Platform::CreateDirectories(screenshotDir.c_str());

            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
#ifdef _WIN32
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            char timestamp[32];
            std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
            const auto filename = Core::InlineString<>::Format("%s_%llu.png", timestamp, static_cast<unsigned long long>(frameNumber));
            screenCapture->screenshotSavePath = screenshotDir / filename.c_str();
        }
        else {
            screenCapture->screenshotSavePath = Core::Path(frameBuffer.screenshotPath.c_str());
            Platform::CreateDirectories(screenCapture->screenshotSavePath.Parent().c_str());
        }
        screenCapture->screenshotPendingSlot = frameIndex;
        screenCapture->StartScreenshot();
    }

#if WILL_EDITOR
    const uint32_t scaledMouseX = std::min(renderExtent[0] - 1, static_cast<uint32_t>(std::lround(static_cast<float>(frameBuffer.currentMousePosition[0]) * renderExtent[0] / static_cast<float>(outputExtent[0]))));
    const uint32_t scaledMouseY = std::min(renderExtent[1] - 1, static_cast<uint32_t>(std::lround(static_cast<float>(frameBuffer.currentMousePosition[1]) * renderExtent[1] / static_cast<float>(outputExtent[1]))));
    if (frameBuffer.currentMousePosition[0] > 0 && frameBuffer.currentMousePosition[0] < outputExtent[0] &&
        frameBuffer.currentMousePosition[1] > 0 && frameBuffer.currentMousePosition[1] < outputExtent[1]) {
        RenderPass& copyStableId = renderGraph->AddPass(SID("Copy Stable ID"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
        copyStableId.ReadCopyImage(SID("stable_id"));
        copyStableId.WriteTransferBuffer(SID("readback_buffer"));
        copyStableId.Execute([&, mouseX = scaledMouseX, mouseY = scaledMouseY](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferImageCopy region{};
            region.bufferOffset = offsetof(ReadbackStruct, selectedStableId);
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {static_cast<int32_t>(mouseX), static_cast<int32_t>(mouseY), 0};
            region.imageExtent = {1, 1, 1};

            vkCmdCopyImageToBuffer(
                cmd,
                renderGraph->GetImageHandle(SID("stable_id")),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                renderGraph->GetBufferHandle(SID("readback_buffer")),
                1,
                &region
            );
        });
    }
#endif

    RenderPass& readbackMeshletCount = renderGraph->AddPass(SID("[Critical] Readback Copy"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
    readbackMeshletCount.ReadTransferBuffer(SID("readback_buffer"));
    readbackMeshletCount.Execute([&](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkBufferCopy copy;
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = sizeof(ReadbackStruct);

        vkCmdCopyBuffer(
            cmd,
            renderGraph->GetBufferHandle(SID("readback_buffer")),
            renderGraph->GetReadback(),
            1,
            &copy
        );
    });

    // For Hi-Z, ReSTIR-DI, SVGF
    renderGraph->CarryTextureToNextFrame(targets.depthCopy, SID("depth_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    renderGraph->CarryTextureToNextFrame(targets.gbufferOne, SID("gbuffer_one_history"), VK_IMAGE_USAGE_SAMPLED_BIT); {
        ZoneScopedN("RenderGraphCompile");
        renderGraph->SetDebugLogging(frameBuffer.bLogRDG);
#ifdef ENABLE_VULKAN_VALIDATION
        if (frameBuffer.bLogRDG) {
            pipelineManager->DumpExecutableStats(Platform::GetAssetPath() / "visualizations" / "pipeline_executable_stats.txt");
        }
#endif
        renderGraph->Compile(frameNumber);
        if (bVRAMReportShouldWrite.load(std::memory_order_relaxed)) {
            bVRAMReportShouldWrite.store(false, std::memory_order_relaxed);
            vramReport = renderGraph->GenerateVramReport();
            bVRAMReportShouldRead.store(true, std::memory_order_release);
        }
    } {
        ZoneScopedN("RenderGraphExecute");
        renderGraph->Execute(cmd);
        renderGraph->PrepareSwapchain(cmd, SID("swapchain_image"));
    }

#if WILL_EDITOR
    resourceManager->debugReadback.SetLastKnownState(renderGraph->GetBufferState(SID("debug_readback_buffer")));
#endif
    return {SUCCESS, swapchainImageIndex};
}

void RenderThread::ProcessAcquisitions(VkCommandBuffer cmd, Core::Span<Core::BufferAcquireOperation> bufferAcquireOperations, Core::Span<Core::ImageAcquireOperation> imageAcquireOperations)
{
    ZoneScoped;
    if (bufferAcquireOperations.IsEmpty() && imageAcquireOperations.IsEmpty()) {
        return;
    }

    tempBufferBarriers.Clear();
    tempBufferBarriers.Reserve(bufferAcquireOperations.Size());
    for (const auto& op : bufferAcquireOperations) {
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.pNext = nullptr;
        barrier.srcStageMask = op.srcStageMask;
        barrier.srcAccessMask = op.srcAccessMask;
        barrier.dstStageMask = op.dstStageMask;
        barrier.dstAccessMask = op.dstAccessMask;
        barrier.srcQueueFamilyIndex = op.srcQueueFamilyIndex;
        barrier.dstQueueFamilyIndex = op.dstQueueFamilyIndex;
        barrier.buffer = reinterpret_cast<VkBuffer>(op.buffer);
        barrier.offset = op.offset;
        barrier.size = op.size;
        tempBufferBarriers.PushBack(barrier);
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
    depInfo.bufferMemoryBarrierCount = tempBufferBarriers.Size();
    depInfo.pBufferMemoryBarriers = tempBufferBarriers.Data();
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
        ShadeDispatchParameters data[16];
    };
    struct LightDispatchReadback
    {
        LightingDispatchParameters data[16];
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
            RenderPass& pass = graph.AddPass(SID("[Debug] Readback Cursor Lit"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
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
        }
    );

    resourceManager->debugReadback.Register<ShadeDispatchReadback>(
        "Shade Dispatch Parameters",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer(SHADING_DISPATCH_BUCKETING_BUFFER)) { return; }
            RenderPass& pass = graph.AddPass(SID("[Debug] Readback Shade Dispatch"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(SHADING_DISPATCH_BUCKETING_BUFFER);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(ShadeDispatchReadback)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(SHADING_DISPATCH_BUCKETING_BUFFER), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const ShadeDispatchReadback& d) {
            if (ImGui::BeginTable("ShadeDispatchTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Material");
                ImGui::TableSetupColumn("Dispatch");
                ImGui::TableSetupColumn("MinX");
                ImGui::TableSetupColumn("MinY");
                ImGui::TableSetupColumn("MaxX");
                ImGui::TableSetupColumn("MaxY");
                ImGui::TableHeadersRow();
                for (int i = 0; i < 16; ++i) {
                    const ShadeDispatchParameters& p = d.data[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.shadingIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("(%u,%u,%u)", p.xDispatch, p.yDispatch, p.zDispatch);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.minX);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.minY);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.maxX);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.maxY);
                }
                ImGui::EndTable();
            }
        }
    );

    resourceManager->debugReadback.Register<LightDispatchReadback>(
        "Light Dispatch Parameters",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER)) { return; }
            RenderPass& pass = graph.AddPass(SID("[Debug] Readback Light Dispatch"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(LightDispatchReadback)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const LightDispatchReadback& d) {
            if (ImGui::BeginTable("LightDispatchTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Lighting");
                ImGui::TableSetupColumn("Dispatch");
                ImGui::TableSetupColumn("MinX");
                ImGui::TableSetupColumn("MinY");
                ImGui::TableSetupColumn("MaxX");
                ImGui::TableSetupColumn("MaxY");
                ImGui::TableHeadersRow();
                for (int i = 0; i < 16; ++i) {
                    const LightingDispatchParameters& p = d.data[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.lightingIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("(%u,%u,%u)", p.xDispatch, p.yDispatch, p.zDispatch);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.minX);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.minY);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.maxX);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", p.maxY);
                }
                ImGui::EndTable();
            }
        }
    );

    resourceManager->debugReadback.Register<InstanceMeshletOffsets>(
        "Instance Meshlet Offsets",
        [](RenderGraph& graph, StringID dst, size_t dstOffset) {
            if (!graph.HasBuffer(SID("instance_meshlet_offsets"))) { return; }
            RenderPass& pass = graph.AddPass(SID("[Debug] Readback Instance Meshlet Offsets"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(SID("instance_meshlet_offsets"));
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(InstanceMeshletOffsets)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(SID("instance_meshlet_offsets")), graph.GetBufferHandle(dst), 1, &copy);
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
            if (!graph.HasBuffer(SID("meshlet_count_dispatch_args"))) { return; }
            RenderPass& pass = graph.AddPass(SID("[Debug] Readback Meshlet Dispatch Args"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(SID("meshlet_count_dispatch_args"));
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(InstancingMeshletDispatchIndirect)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(SID("meshlet_count_dispatch_args")), graph.GetBufferHandle(dst), 1, &copy);
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
            if (!graph.HasBuffer(SID("intermediate_meshlets"))) { return; }
            RenderPass& pass = graph.AddPass(SID("[Debug] Readback Intermediate Meshlets"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(SID("intermediate_meshlets"));
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(IntermediateMeshlets)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(SID("intermediate_meshlets")), graph.GetBufferHandle(dst), 1, &copy);
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
            if (!graph.HasBuffer(SID("visible_meshlets"))) { return; }
            RenderPass& pass = graph.AddPass(SID("[Debug] Readback Visible Meshlets"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(SID("visible_meshlets"));
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(VisibleMeshlets)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(SID("visible_meshlets")), graph.GetBufferHandle(dst), 1, &copy);
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
            if (!graph.HasBuffer(SID("compacted_meshlet_dispatch_args"))) { return; }
            RenderPass& pass = graph.AddPass(SID("[Debug] Readback Compacted Dispatch Args"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
            pass.ReadTransferBuffer(SID("compacted_meshlet_dispatch_args"));
            pass.WriteTransferBuffer(dst);
            pass.Execute([dst, dstOffset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy copy{0, dstOffset, sizeof(InstancingCompactedMeshletDispatchIndirect)};
                vkCmdCopyBuffer(cmd, graph.GetBufferHandle(SID("compacted_meshlet_dispatch_args")), graph.GetBufferHandle(dst), 1, &copy);
            });
        },
        [](const InstancingCompactedMeshletDispatchIndirect& d) {
            ImGui::Text("Total Visible Meshlets: %u", d.totalVisibleMeshlets);
            ImGui::Text("Dispatch Groups: (%u, %u, %u)", d.x, d.y, d.z);
        }
    );
}
#endif

void RenderThread::UploadFrameUniforms(const Core::ViewFamily& viewFamily, const Core::Array<uint32_t, 2> renderExtent, float renderDeltaTime) const
{
    ZoneScoped;
    renderGraph->CreateBuffer(SCENE_DATA_BUFFER, SCENE_DATA_BUFFER_SIZE, false);
    renderGraph->CreateBuffer(LIGHT_DATA_BUFFER, LIGHT_DATA_BUFFER_SIZE, false);
    renderGraph->CreateBuffer(LIGHT_ALIAS_BUFFER, LIGHT_ALIAS_BUFFER_SIZE, false);
    renderGraph->CreateBuffer(REFLECTION_PROBE_BUFFER, REFLECTION_PROBE_BUFFER_SIZE, false);

    // Scene Data
    SceneData sceneData = GenerateSceneData(viewFamily.mainView, viewFamily.aaConfig.mode, renderExtent, frameNumber, renderDeltaTime);
    UploadAllocation sceneDataUploadAllocation = renderGraph->AllocateTransient(sizeof(SceneData));
    memcpy(sceneDataUploadAllocation.ptr, &sceneData, sizeof(SceneData));
    // Portal Scene Data
    UploadAllocation portalSceneDataUploadAllocation{};
    bool bHasPortal = !viewFamily.portalViews.IsEmpty();
    if (bHasPortal) {
        SceneData portalSceneData = GenerateSceneData(viewFamily.portalViews[0].view, viewFamily.aaConfig.mode, renderExtent, frameNumber, renderDeltaTime);
        portalSceneData.clipPlane = glm::vec4(viewFamily.portalViews[0].exitPortalNormal,
                                              -glm::dot(viewFamily.portalViews[0].exitPortalNormal, viewFamily.portalViews[0].exitPortalTransform.translation));
        portalSceneDataUploadAllocation = renderGraph->AllocateTransient(sizeof(SceneData));
        memcpy(portalSceneDataUploadAllocation.ptr, &portalSceneData, sizeof(SceneData));
    }

    // Lights
    UploadAllocation lightDataUploadAllocation = renderGraph->AllocateTransient(sizeof(LightData));
    auto* lightData = static_cast<LightData*>(lightDataUploadAllocation.ptr);
    //
    {
        const glm::vec3& dir = viewFamily.directionalLight.direction;
        const glm::vec3& col = viewFamily.directionalLight.color;
        lightData->directionalLight.directionIntensity = {dir, viewFamily.directionalLight.bEnabled ? viewFamily.directionalLight.intensity : 0.0f};
        lightData->directionalLight.angularRadius = glm::radians(viewFamily.directionalLight.angularRadiusDegrees);
        lightData->directionalLight.packedColor =
                (static_cast<uint32_t>(glm::clamp(col.r, 0.0f, 1.0f) * 255.0f + 0.5f)) |
                (static_cast<uint32_t>(glm::clamp(col.g, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
                (static_cast<uint32_t>(glm::clamp(col.b, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
                (0xFFu << 24);
        lightData->directionalLight._pad1 = 0.0f;
        lightData->directionalLight._pad2 = 0.0f;
        lightData->analyticLightCount = static_cast<int32_t>(viewFamily.analyticLightCount);
        lightData->_pad1 = 0.0f;


        // lightData is write-combined upload memory: never read through it (loop bounds included), only stream writes
        lightData->lightCount = static_cast<int32_t>(viewFamily.lights.Size());
        if (!viewFamily.lights.IsEmpty()) {
            memcpy(lightData->lights, viewFamily.lights.Data(), viewFamily.lights.Size() * sizeof(LightInfo));
        }

        const size_t groupCount = glm::min(viewFamily.emissiveGroups.Size(), static_cast<size_t>(MAX_EMISSIVE_GROUPS));
        lightData->emissiveGroupCount = static_cast<int32_t>(groupCount);
        if (groupCount > 0) {
            memcpy(lightData->emissiveGroups, viewFamily.emissiveGroups.Data(), groupCount * sizeof(EmissiveGroup));
        }
    }

    // Power alias table (rebuilt every frame on the CPU, world space)
    const auto lightCount = static_cast<uint32_t>(viewFamily.lights.Size());
    const size_t aliasBytes = static_cast<size_t>(lightCount) * sizeof(LightAliasEntry);
    UploadAllocation aliasUploadAllocation{};
    if (aliasBytes > 0) {
        aliasUploadAllocation = renderGraph->AllocateTransient(aliasBytes);
        BuildLightPowerAlias(viewFamily.lights.Data(), lightCount, lightAliasScratch, static_cast<LightAliasEntry*>(aliasUploadAllocation.ptr));
    }

    // Reflection probes
    const auto probeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
    const size_t probeBytes = static_cast<size_t>(probeCount) * sizeof(ReflectionProbeGPU);
    UploadAllocation probeUploadAllocation{};
    if (probeBytes > 0) {
        probeUploadAllocation = renderGraph->AllocateTransient(probeBytes);
        memcpy(probeUploadAllocation.ptr, viewFamily.reflectionProbes.Data(), probeBytes);
    }

    auto& uploadUniformsPass = renderGraph->AddPass(SID("Upload Uniforms"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
    uploadUniformsPass.WriteTransferBuffer(SCENE_DATA_BUFFER);
    uploadUniformsPass.WriteTransferBuffer(LIGHT_DATA_BUFFER);
    uploadUniformsPass.WriteTransferBuffer(LIGHT_ALIAS_BUFFER);
    uploadUniformsPass.WriteTransferBuffer(REFLECTION_PROBE_BUFFER);
    uploadUniformsPass.Execute([&,
            sceneOffset = sceneDataUploadAllocation.offset,
            portalOffset = portalSceneDataUploadAllocation.offset,
            hasPortal = bHasPortal,
            lightOffset = lightDataUploadAllocation.offset,
            aliasOffset = aliasUploadAllocation.offset,
            aliasBytes = aliasBytes,
            probeOffset = probeUploadAllocation.offset,
            probeBytes = probeBytes](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            Core::Array<VkBufferCopy2, 2> sceneDataRegions{};
            uint32_t sceneDataCount{1};
            sceneDataRegions[0].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            sceneDataRegions[0].srcOffset = sceneOffset;
            sceneDataRegions[0].dstOffset = 0;
            sceneDataRegions[0].size = sizeof(SceneData);
            if (hasPortal) {
                sceneDataRegions[1].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
                sceneDataRegions[1].srcOffset = portalOffset;
                sceneDataRegions[1].dstOffset = sizeof(SceneData);
                sceneDataRegions[1].size = sizeof(SceneData);
                sceneDataCount++;
            }

            const VkCopyBufferInfo2 sceneDataCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(SCENE_DATA_BUFFER),
                .regionCount = sceneDataCount,
                .pRegions = sceneDataRegions.Data()
            };
            vkCmdCopyBuffer2(cmd, &sceneDataCopyInfo);

            Core::Array<VkBufferCopy2, 1> lightDataRegions{};
            lightDataRegions[0].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            lightDataRegions[0].srcOffset = lightOffset;
            lightDataRegions[0].dstOffset = 0;
            lightDataRegions[0].size = sizeof(LightData);
            const VkCopyBufferInfo2 lightDataCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(LIGHT_DATA_BUFFER),
                .regionCount = lightDataRegions.Size(),
                .pRegions = lightDataRegions.Data()
            };
            vkCmdCopyBuffer2(cmd, &lightDataCopyInfo);

            if (aliasBytes > 0) {
                VkBufferCopy2 aliasRegion{};
                aliasRegion.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
                aliasRegion.srcOffset = aliasOffset;
                aliasRegion.dstOffset = 0;
                aliasRegion.size = aliasBytes;
                const VkCopyBufferInfo2 aliasCopyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                    .dstBuffer = renderGraph->GetBufferHandle(LIGHT_ALIAS_BUFFER),
                    .regionCount = 1,
                    .pRegions = &aliasRegion
                };
                vkCmdCopyBuffer2(cmd, &aliasCopyInfo);
            }

            if (probeBytes > 0) {
                VkBufferCopy2 probeRegion{};
                probeRegion.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
                probeRegion.srcOffset = probeOffset;
                probeRegion.dstOffset = 0;
                probeRegion.size = probeBytes;
                const VkCopyBufferInfo2 probeCopyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                    .dstBuffer = renderGraph->GetBufferHandle(REFLECTION_PROBE_BUFFER),
                    .regionCount = 1,
                    .pRegions = &probeRegion
                };
                vkCmdCopyBuffer2(cmd, &probeCopyInfo);
            }
        });
}

void RenderThread::UploadModelUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    ZoneScoped;

    size_t totalInstanceCount = viewFamily.primitiveInstances.Size();
    UploadAllocation instanceUpload = renderGraph->AllocateTransient(totalInstanceCount * sizeof(Instance));
    auto* instanceBuffer = static_cast<Instance*>(instanceUpload.ptr);

    Core::Array<uint32_t, Render::BINDLESS_MATERIAL_BUFFER_COUNT> lightingIndexByStable{};
    for (const Core::ActiveMaterial& active : viewFamily.activeMaterials) {
        lightingIndexByStable[active.stableIndex] = viewFamily.lightingBuckets[active.material.lightingShader];
    }

    for (size_t i = 0; i < viewFamily.primitiveInstances.Size(); ++i) {
        auto& inst = viewFamily.primitiveInstances[i];
        if (inst.primitiveIndex == DEAD_SLOT_PRIMITIVE_INDEX) {
            instanceBuffer[i] = {.primitiveIndex = DEAD_SLOT_PRIMITIVE_INDEX};
            continue;
        }
        uint32_t lightingIndex = lightingIndexByStable[inst.materialIndex];
        instanceBuffer[i] = {
            .primitiveIndex = inst.primitiveIndex,
            .modelIndex = inst.modelIndex,
            .materialIndex = inst.materialIndex,
            .lightingIndex = lightingIndex,
            .stableId = inst.stableId,
            .lightIndex = inst.lightIndex,
            .emissiveTriLightBase = inst.emissiveTriLightBase,
            .flags = 0u,
        };
    }

    if (totalInstanceCount > 0) {
        renderGraph->CreateBuffer(GEOMETRY_INSTANCE_BUFFER, totalInstanceCount * sizeof(Instance), false);

        RenderPass& uploadPass = renderGraph->AddPass(SID("Upload Instances"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
        uploadPass.WriteTransferBuffer(GEOMETRY_INSTANCE_BUFFER);
        uploadPass.Execute([&,srcOffset = instanceUpload.offset,totalSize = totalInstanceCount * sizeof(Instance)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferCopy2 copy{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = srcOffset,
                .dstOffset = 0,
                .size = totalSize,
            };
            VkCopyBufferInfo2 copyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(GEOMETRY_INSTANCE_BUFFER),
                .regionCount = 1,
                .pRegions = &copy,
            };
            vkCmdCopyBuffer2(cmd, &copyInfo);
        });
    }


    if (!viewFamily.modelMatrices.IsEmpty()) {
        renderGraph->CreateBuffer(GEOMETRY_MODEL_BUFFER, renderFamilyProperties.modelBufferSize, false);
        UploadAllocation modelUpload = renderGraph->AllocateTransient(viewFamily.modelMatrices.Size() * sizeof(Model));
        memcpy(modelUpload.ptr, viewFamily.modelMatrices.Data(), viewFamily.modelMatrices.Size() * sizeof(Model));

        RenderPass& uploadModelMatricesPass = renderGraph->AddPass(SID("Upload Model Matrices"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
        uploadModelMatricesPass.WriteTransferBuffer(SID("model_buffer"));
        uploadModelMatricesPass.Execute([&,
                modelOffset = modelUpload.offset,
                modelSize = viewFamily.modelMatrices.Size() * sizeof(Model)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy2 copy{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                    .srcOffset = modelOffset,
                    .dstOffset = 0,
                    .size = modelSize
                };

                VkCopyBufferInfo2 copyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                    .dstBuffer = renderGraph->GetBufferHandle(SID("model_buffer")),
                    .regionCount = 1,
                    .pRegions = &copy
                };
                vkCmdCopyBuffer2(cmd, &copyInfo);
            });
    }

    if (!viewFamily.activeMaterials.IsEmpty()) {
        renderGraph->CreateBuffer(GEOMETRY_MATERIAL_BUFFER, renderFamilyProperties.materialBufferSize, false);

        UploadAllocation materialUpload = renderGraph->AllocateTransient(Render::BINDLESS_MATERIAL_BUFFER_SIZE);
        auto* dst = static_cast<MaterialProperties*>(materialUpload.ptr);
        memset(dst, 0, Render::BINDLESS_MATERIAL_BUFFER_SIZE);
        for (Core::ActiveMaterial& active : viewFamily.activeMaterials) {
            assert(viewFamily.lightingBuckets.Contains(active.material.lightingShader) && "Lighting bucket missing lighting model for a material");
            active.material.props.shadingBucketIndex = active.stableIndex;
            active.material.props.lightingBucketIndex = viewFamily.lightingBuckets.At(active.material.lightingShader);
            dst[active.stableIndex] = active.material.props;
        }

        RenderPass& uploadMaterialsPass = renderGraph->AddPass(SID("Upload Materials"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
        uploadMaterialsPass.WriteTransferBuffer(SID("material_buffer"));
        uploadMaterialsPass.Execute([&,
                materialOffset = materialUpload.offset,
                materialSize = static_cast<size_t>(Render::BINDLESS_MATERIAL_BUFFER_SIZE)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy2 copy{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                    .srcOffset = materialOffset,
                    .dstOffset = 0,
                    .size = materialSize
                };

                VkCopyBufferInfo2 copyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                    .dstBuffer = renderGraph->GetBufferHandle(SID("material_buffer")),
                    .regionCount = 1,
                    .pRegions = &copy
                };
                vkCmdCopyBuffer2(cmd, &copyInfo);
            });

        renderGraph->CreateBuffer(SHADING_DISPATCH_BUCKETING_BUFFER, renderFamilyProperties.shadeDispatchBufferSize, false);

        UploadAllocation shadeDispatchUpload = renderGraph->AllocateTransient(Render::BINDLESS_MATERIAL_BUFFER_COUNT * sizeof(ShadeDispatchParameters));
        auto* shadeDispatchBuffer = static_cast<ShadeDispatchParameters*>(shadeDispatchUpload.ptr);
        for (int32_t i = 0; i < Render::BINDLESS_MATERIAL_BUFFER_COUNT; ++i) {
            shadeDispatchBuffer[i] = {
                .xDispatch = 0,
                .yDispatch = 0,
                .zDispatch = 0,
                .minX = UINT32_MAX,
                .maxX = 0,
                .minY = UINT32_MAX,
                .maxY = 0,
                .shadingIndex = static_cast<uint32_t>(i),
            };
        }

        RenderPass& uploadShadeDispatchPass = renderGraph->AddPass(SID("Upload Shade Dispatch"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
        uploadShadeDispatchPass.WriteTransferBuffer(SHADING_DISPATCH_BUCKETING_BUFFER);
        uploadShadeDispatchPass.Execute([&,
                shadeDispatchOffset = shadeDispatchUpload.offset,
                shadeDispatchSize = Render::BINDLESS_MATERIAL_BUFFER_COUNT * sizeof(ShadeDispatchParameters)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy2 copy{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                    .srcOffset = shadeDispatchOffset,
                    .dstOffset = 0,
                    .size = shadeDispatchSize,
                };
                VkCopyBufferInfo2 copyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                    .dstBuffer = renderGraph->GetBufferHandle(SHADING_DISPATCH_BUCKETING_BUFFER),
                    .regionCount = 1,
                    .pRegions = &copy,
                };
                vkCmdCopyBuffer2(cmd, &copyInfo);
            });

        renderGraph->CreateBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER, renderFamilyProperties.lightingDispatchBufferSize, false);

        UploadAllocation lightDispatchUpload = renderGraph->AllocateTransient(viewFamily.lightingBuckets.Size() * sizeof(LightingDispatchParameters));
        auto* lightDispatchBuffer = static_cast<LightingDispatchParameters*>(lightDispatchUpload.ptr);
        for (size_t i = 0; i < viewFamily.lightingBuckets.Size(); ++i) {
            lightDispatchBuffer[i] = {
                .xDispatch = 0,
                .yDispatch = 0,
                .zDispatch = 0,
                .minX = UINT32_MAX,
                .maxX = 0,
                .minY = UINT32_MAX,
                .maxY = 0,
                .lightingIndex = static_cast<uint32_t>(i),
            };
        }

        RenderPass& uploadLightDispatchPass = renderGraph->AddPass(SID("Upload Light Dispatch"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Untagged);
        uploadLightDispatchPass.WriteTransferBuffer(LIGHTING_DISPATCH_BUCKETING_BUFFER);
        uploadLightDispatchPass.Execute([&,
                lightDispatchOffset = lightDispatchUpload.offset,
                lightDispatchSize = viewFamily.lightingBuckets.Size() * sizeof(LightingDispatchParameters)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                VkBufferCopy2 copy{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                    .srcOffset = lightDispatchOffset,
                    .dstOffset = 0,
                    .size = lightDispatchSize,
                };
                VkCopyBufferInfo2 copyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                    .dstBuffer = renderGraph->GetBufferHandle(LIGHTING_DISPATCH_BUCKETING_BUFFER),
                    .regionCount = 1,
                    .pRegions = &copy,
                };
                vkCmdCopyBuffer2(cmd, &copyInfo);
            });
    }
}

void RenderThread::UploadTextUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    ZoneScoped;

    if (viewFamily.worldGlyphQuads.IsEmpty()) { return; }

    renderGraph->CreateBuffer(TEXT_GLYPH_QUAD_BUFFER, renderFamilyProperties.glyphQuadBufferSize, false);
    UploadAllocation glyphUpload = renderGraph->AllocateTransient(viewFamily.worldGlyphQuads.Size() * sizeof(WorldGlyphQuad));
    //
    {
        auto* dst = static_cast<WorldGlyphQuad*>(glyphUpload.ptr);
        for (uint32_t i = 0; i < viewFamily.worldGlyphQuads.Size(); ++i) {
            WorldGlyphQuad q = viewFamily.worldGlyphQuads[i];
            q.uvOrigMin = q.uvMin;
            q.uvOrigMax = q.uvMax;
            const Core::TextInstanceDataFull& inst = viewFamily.textInstances[q.drawCallIndex];
            const TextRenderMaterial& mat = viewFamily.textMaterials[inst.textMaterialIndex];
            Vec2 shadowPad = {glm::abs(mat.shadowOffset.x), glm::abs(mat.shadowOffset.y)};
            if (shadowPad.x > 0.0f || shadowPad.y > 0.0f) {
                Vec2 posDelta = abs(q.posMax - q.posMin);
                Vec2 uvPerPos = (q.uvMax - q.uvMin) / posDelta;
                q.posMin -= shadowPad * posDelta / uvPerPos;
                q.posMax += shadowPad * posDelta / uvPerPos;
                q.uvMin -= shadowPad * posDelta;
                q.uvMax += shadowPad * posDelta;
            }
            dst[i] = q;
        }
    }

    RenderPass& uploadGlyphPass = renderGraph->AddPass(SID("Upload Glyph Quads"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Scene);
    uploadGlyphPass.WriteTransferBuffer(TEXT_GLYPH_QUAD_BUFFER);
    uploadGlyphPass.Execute([&,
            srcOffset = glyphUpload.offset,
            totalSize = viewFamily.worldGlyphQuads.Size() * sizeof(WorldGlyphQuad)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferCopy2 copy{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = srcOffset,
                .dstOffset = 0,
                .size = totalSize,
            };
            VkCopyBufferInfo2 copyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(TEXT_GLYPH_QUAD_BUFFER),
                .regionCount = 1,
                .pRegions = &copy,
            };
            vkCmdCopyBuffer2(cmd, &copyInfo);
        });

    renderGraph->CreateBuffer(TEXT_INSTANCE_BUFFER, renderFamilyProperties.textInstanceBufferSize, false);
    const uint32_t instCount = viewFamily.textInstances.Size();
    UploadAllocation instUpload = renderGraph->AllocateTransient(instCount * sizeof(TextInstanceData)); {
        auto* dst = static_cast<TextInstanceData*>(instUpload.ptr);
        for (uint32_t i = 0; i < instCount; ++i) {
            const Core::TextInstanceDataFull& src = viewFamily.textInstances[i];
            dst[i] = {
                .modelIndex = src.modelIndex,
                .pxRange = src.pxRange,
                .stableIdLo = static_cast<uint32_t>(src.stableId & 0xFFFFFFFFu),
                .stableIdHi = static_cast<uint32_t>(src.stableId >> 32u),
            };
        }
    }

    RenderPass& uploadInstPass = renderGraph->AddPass(SID("Upload Text Instances"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Scene);
    uploadInstPass.WriteTransferBuffer(TEXT_INSTANCE_BUFFER);
    uploadInstPass.Execute([&,
            srcOffset = instUpload.offset,
            totalSize = instCount * sizeof(TextInstanceData)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferCopy2 copy{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = srcOffset,
                .dstOffset = 0,
                .size = totalSize,
            };
            VkCopyBufferInfo2 copyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(TEXT_INSTANCE_BUFFER),
                .regionCount = 1,
                .pRegions = &copy,
            };
            vkCmdCopyBuffer2(cmd, &copyInfo);
        });

    renderGraph->CreateBuffer(TEXT_MATERIAL_BUFFER, renderFamilyProperties.textMaterialBufferSize, false);
    UploadAllocation matUpload = renderGraph->AllocateTransient(viewFamily.textMaterials.Size() * sizeof(TextRenderMaterial));
    memcpy(matUpload.ptr, viewFamily.textMaterials.Data(), viewFamily.textMaterials.Size() * sizeof(TextRenderMaterial));

    RenderPass& uploadMatPass = renderGraph->AddPass(SID("Upload Text Materials"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Scene);
    uploadMatPass.WriteTransferBuffer(TEXT_MATERIAL_BUFFER);
    uploadMatPass.Execute([&,
            srcOffset = matUpload.offset,
            totalSize = viewFamily.textMaterials.Size() * sizeof(TextRenderMaterial)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferCopy2 copy{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = srcOffset,
                .dstOffset = 0,
                .size = totalSize,
            };
            VkCopyBufferInfo2 copyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(TEXT_MATERIAL_BUFFER),
                .regionCount = 1,
                .pRegions = &copy,
            };
            vkCmdCopyBuffer2(cmd, &copyInfo);
        });
}

void RenderThread::UploadSpriteUniforms(const Core::ViewFamily& viewFamily) const
{
    ZoneScoped;

    if (viewFamily.spriteBatches.IsEmpty()) {
        return;
    }

    const uint32_t spriteCount = static_cast<uint32_t>(viewFamily.sprites.Size());
    const size_t uploadSize = spriteCount * sizeof(SpriteData);

    renderGraph->CreateBuffer(SPRITE_BUFFER, uploadSize, false);
    UploadAllocation spriteUpload = renderGraph->AllocateTransient(uploadSize);
    auto* dst = static_cast<SpriteData*>(spriteUpload.ptr);

    for (uint32_t i = 0; i < spriteCount; i++) {
        const Core::Sprite& s = viewFamily.sprites[i];
        const glm::vec4& c = s.color;
        dst[i] = {
            .worldPosition = s.worldPosition,
            .pixelSize = s.pixelSize,
            .packedColor =
            (static_cast<uint32_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f)) |
            (static_cast<uint32_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
            (static_cast<uint32_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
            (static_cast<uint32_t>(glm::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24),
            .stableIdLo = static_cast<uint32_t>(s.stableId & 0xFFFFFFFFu),
            .stableIdHi = static_cast<uint32_t>(s.stableId >> 32u),
            .flags = s.billboard ? SPRITE_FLAG_BILLBOARD : 0u,
        };
    }

    RenderPass& uploadPass = renderGraph->AddPass(SID("Upload Sprites"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Scene);
    uploadPass.WriteTransferBuffer(SPRITE_BUFFER);
    uploadPass.Execute([&, srcOffset = spriteUpload.offset, size = uploadSize](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const VkBufferCopy2 region{
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = srcOffset,
            .dstOffset = 0,
            .size = size,
        };
        const VkCopyBufferInfo2 copyInfo{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer = renderGraph->GetTransientUploadBuffer(),
            .dstBuffer = renderGraph->GetBufferHandle(SPRITE_BUFFER),
            .regionCount = 1,
            .pRegions = &region,
        };
        vkCmdCopyBuffer2(cmd, &copyInfo);
    });
}

void RenderThread::UploadUIUniforms(const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    ZoneScoped;

    if (!viewFamily.uiGlyphQuads.IsEmpty()) {
        renderGraph->CreateBuffer(UI_GLYPH_QUAD_BUFFER, renderFamilyProperties.uiGlyphQuadBufferSize, false);
        const uint32_t quadCount = viewFamily.uiGlyphQuads.Size();
        UploadAllocation upload = renderGraph->AllocateTransient(quadCount * sizeof(UIGlyphQuad));
        memcpy(upload.ptr, viewFamily.uiGlyphQuads.Data(), quadCount * sizeof(UIGlyphQuad));
        RenderPass& uploadPass = renderGraph->AddPass(SID("Upload UI Glyph Quads"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::UI);
        uploadPass.WriteTransferBuffer(UI_GLYPH_QUAD_BUFFER);
        uploadPass.Execute([&, srcOffset = upload.offset, totalSize = quadCount * sizeof(UIGlyphQuad)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferCopy2 copy{.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2, .srcOffset = srcOffset, .dstOffset = 0, .size = totalSize};
            VkCopyBufferInfo2 copyInfo{.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2, .srcBuffer = renderGraph->GetTransientUploadBuffer(), .dstBuffer = renderGraph->GetBufferHandle(UI_GLYPH_QUAD_BUFFER), .regionCount = 1, .pRegions = &copy};
            vkCmdCopyBuffer2(cmd, &copyInfo);
        });
    }
}

/*void RenderThread::SetupPortalComposite(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets,
                                        const RenderTargets& portal) const
{
    RenderPass& portalCompositePass = graph.AddPass(SID("Portal Composite"), VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, Render::RenderCategory::Scene);
    portalCompositePass.ReadSampledImage(portalTargets.outputColor);
    portalCompositePass.ReadSampledImage(portalTargets.gbufferTwo);
    portalCompositePass.ReadSampledImage(portalTargets.depthStencil);
    portalCompositePass.WriteColorAttachment(targets.outputColor);
    portalCompositePass.WriteColorAttachment(targets.gbufferTwo);
    portalCompositePass.ReadWriteDepthAttachment(targets.depthStencil);
    portalCompositePass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.outputColor), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo gbufferTwoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.gbufferTwo), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        Core::Array<VkRenderingAttachmentInfo, 2> colorAttachments{colorAttachment, gbufferTwoAttachment};
        VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments.Data(), 2, &depthAttachment, &stencilAttachment);
        vkCmdBeginRendering(cmd, &renderInfo);

        PortalCompositePushConstant pc{
            .portalColorIndex = graph.GetSampledImageViewDescriptorIndex(portalTargets.outputColor),
            .portalVelocityIndex = graph.GetSampledImageViewDescriptorIndex(portalTargets.gbufferTwo),
            .portalDepthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(portalTargets.depthStencil),
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("portal_composite"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 1);

        // Because this writes to SV_Depth, apparently all future draw calls to this depth will not use early Z out lol. (stackoverflow 2018)
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);
    });
}*/

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

    graph.CreateBuffer(SID("debug_segment_buffer"), limits.highestDebugSegmentCount * sizeof(DebugLineSegment), false);

    UploadAllocation segmentUpload = graph.AllocateTransient(totalSegments * sizeof(DebugLineSegment));
    auto* segments = static_cast<DebugLineSegment*>(segmentUpload.ptr);

    uint32_t segmentOffset = 0;

    const glm::mat4 viewMatrix = viewFamily.mainView.currentViewData.view;
    const glm::mat4 projMatrix = viewFamily.mainView.currentViewData.proj;
    Frustum mainViewFrustum = CreateFrustum(projMatrix * viewMatrix);

    for (const auto& sphere : viewFamily.debugSpheres) {
        if (!IntersectsSphere(mainViewFrustum, sphere.center, sphere.radius)) {
            continue;
        }

        const int segs = GetSphereSegments(sphere.center, viewFamily.mainView.currentViewData.cameraPos, sphere.radius);
        for (int i = 0; i < segs; ++i) {
            float a0 = static_cast<float>(i) / segs * 2.0f * glm::pi<float>();
            float a1 = static_cast<float>(i + 1) / segs * 2.0f * glm::pi<float>();
            glm::vec3 s = sphere.center;
            float r = sphere.radius;
            // XY
            segments[segmentOffset++] = {
                .a = s + glm::vec3(glm::cos(a0), glm::sin(a0), 0.0f) * r, .width = sphere.width, .b = s + glm::vec3(glm::cos(a1), glm::sin(a1), 0.0f) * r, .pad = 0.0f, .color = sphere.color
            };
            // XZ
            segments[segmentOffset++] = {
                .a = s + glm::vec3(glm::cos(a0), 0.0f, glm::sin(a0)) * r, .width = sphere.width, .b = s + glm::vec3(glm::cos(a1), 0.0f, glm::sin(a1)) * r, .pad = 0.0f, .color = sphere.color
            };
            // YZ
            segments[segmentOffset++] = {
                .a = s + glm::vec3(0.0f, glm::cos(a0), glm::sin(a0)) * r, .width = sphere.width, .b = s + glm::vec3(0.0f, glm::cos(a1), glm::sin(a1)) * r, .pad = 0.0f, .color = sphere.color
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
        for (int i = 0; i < N; ++i) {
            const float a0 = static_cast<float>(i) / N * 2.0f * glm::pi<float>();
            const float a1 = static_cast<float>(i + 1) / N * 2.0f * glm::pi<float>();
            const glm::vec3 d0 = (glm::cos(a0) * ex + glm::sin(a0) * ez) * cyl.radius;
            const glm::vec3 d1 = (glm::cos(a1) * ex + glm::sin(a1) * ez) * cyl.radius;
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
        for (int i = 0; i < N; ++i) {
            const float a0 = static_cast<float>(i) / N * 2.0f * glm::pi<float>();
            const float a1 = static_cast<float>(i + 1) / N * 2.0f * glm::pi<float>();
            const glm::vec3 d0 = (glm::cos(a0) * ex + glm::sin(a0) * ez) * cap.radius;
            const glm::vec3 d1 = (glm::cos(a1) * ex + glm::sin(a1) * ez) * cap.radius;
            segments[segmentOffset++] = {.a = top + d0, .width = cap.width, .b = top + d1, .pad = 0.0f, .color = cap.color};
            segments[segmentOffset++] = {.a = bot + d0, .width = cap.width, .b = bot + d1, .pad = 0.0f, .color = cap.color};
            if (i % (N / 4) == 0) {
                segments[segmentOffset++] = {.a = top + d0, .width = cap.width, .b = bot + d0, .pad = 0.0f, .color = cap.color};
            }
        }
        // Hemispherical caps: a great-circle half-arc in each of the ex/ez planes, per end.
        constexpr int H = 12;
        for (int i = 0; i < H; ++i) {
            const float t0 = static_cast<float>(i) / H * glm::pi<float>();
            const float t1 = static_cast<float>(i + 1) / H * glm::pi<float>();
            const float c0 = glm::cos(t0), s0 = glm::sin(t0), c1 = glm::cos(t1), s1 = glm::sin(t1);
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

    RenderPass& uploadDebugPass = graph.AddPass(SID("Upload Debug Geometry"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::Debug);
    uploadDebugPass.WriteTransferBuffer(SID("debug_segment_buffer"));

    VkBuffer srcBuffer = graph.GetTransientUploadBuffer();
    uploadDebugPass.Execute([&, srcBuffer,
            uploadOffset = segmentUpload.offset,
            uploadSize = totalLineSegments * sizeof(DebugLineSegment)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferCopy2 copy{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = uploadOffset,
                .dstOffset = 0,
                .size = uploadSize
            };
            VkCopyBufferInfo2 copyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = srcBuffer,
                .dstBuffer = graph.GetBufferHandle(SID("debug_segment_buffer")),
                .regionCount = 1,
                .pRegions = &copy
            };
            vkCmdCopyBuffer2(cmd, &copyInfo);
        });

    RenderPass& debugDrawPass = graph.AddPass(SID("Debug Draw"), VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, Render::RenderCategory::Debug);
    debugDrawPass.WriteColorAttachment(targetImage);
    bool bHasDepth = graph.HasTexture(depthTarget);
    if (bHasDepth) {
        debugDrawPass.ReadWriteDepthAttachment(depthTarget);
    }
    debugDrawPass.ReadBuffer(SCENE_DATA_BUFFER);
    debugDrawPass.ReadBuffer(SID("debug_segment_buffer"));
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
            .segmentBuffer = graph.GetBufferAddress(SID("debug_segment_buffer")),
            .sceneDataIndex = 0,
            .totalLineSegments = totalLineSegments,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("debug_render"));
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
