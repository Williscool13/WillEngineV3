//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

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
#include "shaders/constants_interop.h"
#include "shaders/push_constant_interop.h"

#include "types/render_types.h"
#include "render/vulkan/vk_imgui_wrapper.h"
#include "backends/imgui_impl_vulkan.h"
#include "core/containers/inline_string.h"
#include "core/containers/span.h"
#include "core/memory/memory_manager.h"
#include "core/string_id.h"
#include "core/math/math_helpers.h"
#include "engine/logging/engine_log.h"
#include "pipelines/pipeline_manager.h"
#include "pipelines/graphics_pipeline_builder.h"
#include "render-view/render_view_helpers.h"
#include "shadows/shadow_helpers.h"


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
    Core::Array<VkDescriptorSetLayout, 2> layouts{
        resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle
    };
    pipelineManager = new(memoryManager.RenderAllocRaw(sizeof(PipelineManager))) PipelineManager(context, renderAlloc, assetScratchAlloc, layouts);
    imgui = new(memoryManager.RenderAllocRaw(sizeof(ImguiWrapper))) ImguiWrapper(context, window, Core::FRAME_BUFFER_COUNT, swapchain->format, pipelineManager->GetPipelineCache());

    tempBufferBarriers = Core::Vector<VkBufferMemoryBarrier2>(&renderAlloc, Core::AllocTag::Render);
    tempImageBarriers = Core::Vector<VkImageMemoryBarrier2>(&renderAlloc, Core::AllocTag::Render);

    for (RenderSynchronization& frameSync : frameSynchronization) {
        frameSync = RenderSynchronization(context);
        frameSync.Initialize();
    }

    renderGraph = new(memoryManager.RenderAllocRaw(sizeof(RenderGraph))) RenderGraph(context, resourceManager, renderAlloc, memoryManager.RenderArena());
    screenCapture = new(memoryManager.RenderAllocRaw(sizeof(RenderScreenCapture))) RenderScreenCapture(context, scheduler, memoryManager.AssetsScratch());
}

RenderThread::~RenderThread()
{
    screenCapture->~RenderScreenCapture();

    for (auto& sync : frameSynchronization) {
        sync = RenderSynchronization{};
    }

    pipelineManager->~PipelineManager();
    globalPipelineLayout = {};
    renderGraph->~RenderGraph();
    resourceManager->~ResourceManager();
    renderExtents->~RenderExtents();
    imgui->~ImguiWrapper();
    swapchain->~Swapchain();
    context->~VulkanContext();
}

void RenderThread::InitializePipelineManager(AssetLoad::AsyncAssetLoadManager* _asyncAssetLoadManager)
{
    pipelineManager->SetAssetLoadThread(_asyncAssetLoadManager);
    CreatePipelines();
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
        {
            ZoneScopedN("Idle - WaitForFrame");
            std::unique_lock lock(engineRenderSynchronization->renderMutex);
            engineRenderSynchronization->renderCV.wait(lock, [&] {
                return engineRenderSynchronization->renderFrames.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            engineRenderSynchronization->renderFrames.fetch_sub(1);
        }

        if (bShouldExit.load()) {
            break;
        }

        // Render Frame
        {
            currentFrameInFlight = frameNumber % Core::FRAME_BUFFER_COUNT;
            Core::FrameBuffer& frameBuffer = engineRenderSynchronization->frameBuffers[currentFrameInFlight];
            assert(frameBuffer.currentFrameBuffer == currentFrameInFlight);


            bEngineRequestsRecreate |= frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate;
            if (!frameBuffer.swapchainRecreateCommand.bIsMinimized && bEngineRequestsRecreate) {
                ZoneScopedN("SwapchainRecreate");
                vkDeviceWaitIdle(context->device);
                LOG_INFO(Renderer, "Swapchain Recreated");

                swapchain->Recreate(frameBuffer.swapchainRecreateCommand.windowWidth, frameBuffer.swapchainRecreateCommand.windowHeight);
                renderExtents->ApplyResize(frameBuffer.swapchainRecreateCommand.windowWidth, frameBuffer.swapchainRecreateCommand.windowHeight);
                renderGraph->InvalidateAllSwapchainAssociated();

                bRenderRequestsRecreate = false;
                bEngineRequestsRecreate = false;
                frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = false;
            }

            if (frameBuffer.viewportResizeCommand.bEngineCommandsResize) {
                vkDeviceWaitIdle(context->device);
                LOG_INFO(Renderer, "Viewport remade");

                renderExtents->ApplyViewportResize(frameBuffer.viewportResizeCommand.offsetX, frameBuffer.viewportResizeCommand.offsetY, frameBuffer.viewportResizeCommand.sizeX,
                                                   frameBuffer.viewportResizeCommand.sizeY);
                frameBuffer.viewportResizeCommand.bEngineCommandsResize = false;
                renderGraph->InvalidateAllViewportAssociated();
            }

            // Wait for the frame N - 3 to finish using resources
            RenderSynchronization& currentRenderSynchronization = frameSynchronization[currentFrameInFlight];
            RenderFrame(currentFrameInFlight, currentRenderSynchronization, frameBuffer);

            frameNumber++;
        }

        FrameMark;
        engineRenderSynchronization->gameFrames.fetch_add(1, std::memory_order_release);
    }

    vkDeviceWaitIdle(context->device);
}

void RenderThread::RenderFrame(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer)
{
    ZoneScoped;

    //
    {
        ZoneScopedN("WaitForFence");
        VK_CHECK(vkWaitForFences(context->device, 1, &renderSync.renderFence, true, UINT64_MAX));
        VK_CHECK(vkResetFences(context->device, 1, &renderSync.renderFence));
    }

    screenCapture->ResolveScreenshot(currentFrameIndex);

    VK_CHECK(vkResetCommandBuffer(renderSync.commandBuffer, 0));
    VkCommandBufferBeginInfo beginInfo = VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(renderSync.commandBuffer, &beginInfo));

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
        res = RecordFrame(currentFrameIndex, renderSync.commandBuffer, renderSync.swapchainSemaphore, frameBuffer);
    }
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

#ifdef WILL_EDITOR
    {
        AssetLoad::GPUDispatchRequest req{};
        while (editorGPUDispatchQueue.try_dequeue(req)) {
            VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &req.cmd;
            VK_CHECK(vkQueueSubmit(context->graphicsQueue, 1, &submitInfo, req.fence));
            VK_CHECK(vkWaitForFences(context->device, 1, &req.fence, VK_TRUE, UINT64_MAX));
            req.completionSignal->release();
        }
    }
#endif
}

RenderThread::RenderResponse RenderThread::RecordFrame(uint32_t frameIndex, VkCommandBuffer cmd, VkSemaphore swapchainSemaphore, Core::FrameBuffer& frameBuffer)
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

    renderGraph->Reset(frameIndex, frameNumber, RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD);

    Core::Array<uint32_t, 2> renderExtent = renderExtents->GetScaledExtent();
    VkImage currentSwapchainImage = swapchain->swapchainImages[swapchainImageIndex];
    VkImageView currentSwapchainImageView = swapchain->swapchainImageViews[swapchainImageIndex];

    Core::ViewFamily& viewFamily = frameBuffer.mainViewFamily;
    ReadbackStruct* readbackData = renderGraph->GetReadbackData();
    frameBuffer.stableIdUnderCursor = readbackData->selectedStableId;

    RenderFamilyProperties renderFamilyProperties = PrepareRenderFamilyProperties(viewFamily, readbackData, pipelineManager, frameResourceLimits);

    //
    {
        ZoneScopedN("BindDescriptorBuffers");
        Core::Array<VkDescriptorBufferBindingInfoEXT, 2> bindings{
            resourceManager->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo(), resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo()
        };
        Core::Array<uint32_t, 2> indices{0u, 1u};
        Core::Array<VkDeviceSize, 2> offsets{0, 0};
        vkCmdBindDescriptorBuffersEXT(cmd, bindings.Size(), bindings.Data());
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, globalPipelineLayout.handle, 0, bindings.Size(), indices.Data(), offsets.Data());
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, globalPipelineLayout.handle, 0, bindings.Size(), indices.Data(), offsets.Data());
    } {
        ZoneScopedN("SetupUniforms");
        UploadFrameUniforms(viewFamily, renderExtent, frameBuffer.timeFrame.renderDeltaTime);
        UploadModelUniforms(viewFamily, renderFamilyProperties);
    } {
        ZoneScopedN("ImportBuffers");
        renderGraph->ImportBufferNoBarrier(GEOMETRY_VERTEX_POSITION_BUFFER, resourceManager->megaVertexPositionBuffer.handle, resourceManager->megaVertexPositionBuffer.address,
                                           {resourceManager->megaVertexPositionBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER, resourceManager->megaVertexAttributeBuffer.handle, resourceManager->megaVertexAttributeBuffer.address,
                                           {resourceManager->megaVertexAttributeBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(SID("meshlet_vertex_buffer"), resourceManager->megaMeshletVerticesBuffer.handle, resourceManager->megaMeshletVerticesBuffer.address,
                                           {resourceManager->megaMeshletVerticesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(SID("meshlet_triangle_buffer"), resourceManager->megaMeshletTrianglesBuffer.handle, resourceManager->megaMeshletTrianglesBuffer.address,
                                           {resourceManager->megaMeshletTrianglesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(SID("meshlet_buffer"), resourceManager->megaMeshletBuffer.handle, resourceManager->megaMeshletBuffer.address,
                                           {resourceManager->megaMeshletBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier(SID("primitive_buffer"), resourceManager->primitiveBuffer.handle, resourceManager->primitiveBuffer.address,
                                           {resourceManager->primitiveBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBuffer(SID("debug_readback_buffer"), resourceManager->debugReadbackBuffer.handle, resourceManager->debugReadbackBuffer.address,
                                  {resourceManager->debugReadbackBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT}, resourceManager->debugReadbackLastKnownState);
    }

    renderGraph->ImportTexture(SID("dummy_black_rg32"),
                               resourceManager->blackDummyRG32Image.handle,
                               resourceManager->blackDummyRG32ImageView.handle,
                               TextureInfo{GBUFFER_STABLE_ID_FORMAT, 1, 1, 1},
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                               VK_IMAGE_LAYOUT_GENERAL,
                               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_IMAGE_LAYOUT_GENERAL);

    // Readback that will be copied into the FIF host memory at the end of the frame (to be read in N+3 frames)
    renderGraph->CreateBuffer(SID("readback_buffer"), sizeof(ReadbackStruct), false);
    RenderPass& clearReadbackBuffer = renderGraph->AddPass(SID("Clear Readback Buffer"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    clearReadbackBuffer.WriteTransferBuffer(SID("readback_buffer"));
    clearReadbackBuffer.Execute([&](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, renderGraph->GetBufferHandle(SID("readback_buffer")), 0, VK_WHOLE_SIZE, 0);
    });


    VisibilityBufferTargets targets{
        .visibility = SID("visibility_target"),
        .stableId = SID("stable_id"),
        .gbufferOne = SID("gbuffer_one"),
        .depthStencil = SID("depth_target"),
    };

    renderGraph->CreateTexture(targets.visibility, TextureInfo{VISIBILITY_BUFFER_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.stableId, TextureInfo{GBUFFER_STABLE_ID_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.gbufferOne, TextureInfo{GBUFFER_TARGET_ONE, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(targets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_DEPTH_FAR, true);

    VisibilityBufferBarycentricDerivativeTargets visBarDerTargets{
        .visibility = targets.visibility,
        .barycentric = SID("visibility_barycentric"),
        .derivatives = SID("visibility_derivatives"),
    };

    renderGraph->CreateTexture(visBarDerTargets.barycentric, TextureInfo{VISIBILITY_BARYCENTRIC_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CreateTexture(visBarDerTargets.derivatives, TextureInfo{VISIBILITY_DERIVATIVES_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    VisibilityShadingTargets visShadingTargets{
        .visibility = targets.visibility,
        .barycentric = visBarDerTargets.barycentric,
        .derivatives = visBarDerTargets.derivatives,
        .gbufferOne = targets.gbufferOne,
        .gbufferTwo = SID("gbuffer_two"),
    };

    renderGraph->CreateTexture(visShadingTargets.gbufferTwo, TextureInfo{GBUFFER_TARGET_TWO, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    StringID shadingOutputTarget = SID("shade_output");
    renderGraph->CreateTexture(shadingOutputTarget, TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, VkClearValue{0.0f, 0.1f, 0.2f, 1.0f}, true);


    MainRenderTargets mainTargets{
        .gbufferOne = visShadingTargets.gbufferOne,
        .gbufferTwo = visShadingTargets.gbufferTwo,
        .depthStencil = targets.depthStencil,
        .outputColor = shadingOutputTarget,
    };


    StringID finalOutput = shadingOutputTarget;

    if (renderFamilyProperties.bCanRender) {
        ZoneScopedN("SetupRenderGraph");

        // Geometry
        if (!viewFamily.mainPassInstances.IsEmpty()) {
            if (viewFamily.shadowConfig.enabled) {
                SetupCascadedShadows(*renderGraph, viewFamily, renderFamilyProperties, 0);
            }

            SetupGeometryPass(*renderGraph, pipelineManager, viewFamily, renderFamilyProperties, renderExtent, targets, 0);

            SetupVisibilityBarycentricDerivativePass(*renderGraph, pipelineManager, viewFamily, renderExtent, visBarDerTargets, 0);

            SetupVisibilityShadingPass(*renderGraph, pipelineManager, viewFamily, renderExtent, visShadingTargets, 0);


            if (viewFamily.gtaoConfig.bEnabled) {
                SetupGroundTruthAmbientOcclusion(*renderGraph, pipelineManager, viewFamily, renderExtent, mainTargets, frameNumber, 0);
            }

            // Outputs "shadows_resolve_target"
            SetupShadowsResolve(*renderGraph, pipelineManager, viewFamily, renderExtent, mainTargets, 0);

            DeferredResolveTargets deferredResolveTargets{
                .gbufferOne = visShadingTargets.gbufferOne,
                .gbufferTwo = visShadingTargets.gbufferTwo,
                .depthStencil = targets.depthStencil,
                .shadows = SID("shadows_resolve_target"),
                .output = shadingOutputTarget,
            };

            SetupDeferredResolvePass(*renderGraph, pipelineManager, viewFamily, renderExtent, deferredResolveTargets, 0);
        }

        if (viewFamily.skyboxIndex != -1) {
            SetupSkyboxRendering(*renderGraph, pipelineManager, viewFamily, renderExtent, mainTargets, 0);
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


        finalOutput = shadingOutputTarget;
        // bool bHasTAAPass = pipelineManager->IsCategoryReady(PipelineCategory::TAA) && viewFamily.postProcessConfig.bEnableTemporalAntialiasing;
        // if (bHasTAAPass) {
        //     taaTargets.finalColor = SetupTemporalAntialiasing(*renderGraph, viewFamily, renderExtent, ppTargets);
        // }

        finalOutput = SetupPostProcessing(*renderGraph, pipelineManager, viewFamily, renderExtent, mainTargets, frameBuffer.timeFrame.renderDeltaTime, frameNumber);

        SetupDebugRender(*renderGraph, viewFamily, renderExtent, targets.depthStencil, finalOutput, frameResourceLimits);


#if WILL_EDITOR
        size_t offset = 0;
        if (renderGraph->HasBuffer(SID("portal_rendering_instance_meshlet_offsets"))) {
            RenderPass& debugReadbackPass = renderGraph->AddPass(
                SID("Debug Readback Meshlet Instancing"),
                VK_PIPELINE_STAGE_2_COPY_BIT
            );

            debugReadbackPass.ReadTransferBuffer(SID("portal_rendering_instance_meshlet_offsets"));
            debugReadbackPass.WriteTransferBuffer(SID("debug_readback_buffer"));

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                // Instance meshlet offsets
                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = 640 * sizeof(InstanceMeshletOffsetPrefixSum);

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle(SID("portal_rendering_instance_meshlet_offsets")),
                    renderGraph->GetBufferHandle(SID("debug_readback_buffer")),
                    1,
                    &copies[0]
                );
            });
        }
        offset += 640 * sizeof(InstanceMeshletOffsetPrefixSum);

        if (renderGraph->HasBuffer(SID("portal_rendering_meshlet_count_dispatch_args"))) {
            RenderPass& debugReadbackPass = renderGraph->AddPass(SID("Debug Readback meshlet dispatch args"), VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer(SID("portal_rendering_meshlet_count_dispatch_args"));
            debugReadbackPass.WriteTransferBuffer(SID("debug_readback_buffer"));

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(InstancingMeshletDispatchIndirect);

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle(SID("portal_rendering_meshlet_count_dispatch_args")),
                    renderGraph->GetBufferHandle(SID("debug_readback_buffer")),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(InstancingMeshletDispatchIndirect);

        if (renderGraph->HasBuffer(SID("portal_rendering_intermediate_meshlets"))) {
            RenderPass& debugReadbackPass = renderGraph->AddPass(SID("Debug Readback intermediate meshlets"), VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer(SID("portal_rendering_intermediate_meshlets"));
            debugReadbackPass.WriteTransferBuffer(SID("debug_readback_buffer"));

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(IntermediateMeshlet) * 128;

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle(SID("portal_rendering_intermediate_meshlets")),
                    renderGraph->GetBufferHandle(SID("debug_readback_buffer")),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(IntermediateMeshlet) * 128;

        if (renderGraph->HasBuffer(SID("portal_rendering_visible_meshlets"))) {
            RenderPass& debugReadbackPass = renderGraph->AddPass(SID("Debug Readback visible meshlets"), VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer(SID("portal_rendering_visible_meshlets"));
            debugReadbackPass.WriteTransferBuffer(SID("debug_readback_buffer"));

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(CompactedMeshlet) * 128;

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle(SID("portal_rendering_visible_meshlets")),
                    renderGraph->GetBufferHandle(SID("debug_readback_buffer")),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(CompactedMeshlet) * 128;

        if (renderGraph->HasBuffer(SID("portal_rendering_meshlet_scanned_level2_block_sums"))) {
            RenderPass& debugReadbackPass = renderGraph->AddPass(SID("Debug Readback meshlet scanned level2 block sums"), VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer(SID("portal_rendering_meshlet_scanned_level2_block_sums"));
            debugReadbackPass.WriteTransferBuffer(SID("debug_readback_buffer"));

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(uint32_t) * 256;

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle(SID("portal_rendering_meshlet_scanned_level2_block_sums")),
                    renderGraph->GetBufferHandle(SID("debug_readback_buffer")),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(uint32_t) * 256;

        if (renderGraph->HasBuffer(SID("portal_rendering_compacted_meshlet_dispatch_args"))) {
            RenderPass& debugReadbackPass = renderGraph->AddPass(SID("Debug Readback compacted dispatch args"), VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer(SID("portal_rendering_compacted_meshlet_dispatch_args"));
            debugReadbackPass.WriteTransferBuffer(SID("debug_readback_buffer"));

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(InstancingCompactedMeshletDispatchIndirect);

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle(SID("portal_rendering_compacted_meshlet_dispatch_args")),
                    renderGraph->GetBufferHandle(SID("debug_readback_buffer")),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(InstancingCompactedMeshletDispatchIndirect);

        if (renderGraph->HasBuffer(SID("portal_rendering_visible_meshlets"))) {
            RenderPass& readbackVisibleMeshlets = renderGraph->AddPass(SID("Readback Visible Meshlets"), VK_PIPELINE_STAGE_2_COPY_BIT);
            readbackVisibleMeshlets.ReadTransferBuffer(SID("portal_rendering_visible_meshlets"));
            readbackVisibleMeshlets.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copy;
                copy.srcOffset = 0;
                copy.dstOffset = offset;
                copy.size = sizeof(CompactedMeshlet) * 128;

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle(SID("portal_rendering_visible_meshlets")),
                    renderGraph->GetBufferHandle(SID("debug_readback_buffer")),
                    1,
                    &copy
                );
            });
        }
        offset += sizeof(CompactedMeshlet) * 128;
#endif

        if (!viewFamily.debugResourceName.IsEmpty()) {
            StringID debugTargetName = StringID(viewFamily.debugResourceName.c_str(), viewFamily.debugResourceName.Size());

            if (renderGraph->HasTexture(debugTargetName)) {
                auto& debugVisPass = renderGraph->AddPass(SID("Debug Visualize"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
                debugVisPass.ReadSampledImage(debugTargetName);
                debugVisPass.WriteStorageImage(finalOutput);
                debugVisPass.Execute([&, debugTargetName, finalOutput](VkCommandBuffer _cmd) {
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

                    uint32_t outputIndexIndex = renderGraph->GetStorageImageViewDescriptorIndex(finalOutput);

                    DebugVisualizePushConstant pc{
                        .sceneData = renderGraph->GetBufferAddress(SID("scene_data")),
                        .srcExtent = {dims.width, dims.height},
                        .dstExtent = {renderExtent[0], renderExtent[1]},
                        .nearPlane = viewFamily.mainView.currentViewData.nearPlane,
                        .farPlane = viewFamily.mainView.currentViewData.farPlane,
                        .textureArrayIndex = textureArrayIndex,
                        .textureIndexInArray = textureIndexInArray,
                        .valueTransformationType = static_cast<uint32_t>(viewFamily.debugTransformationType),
                        .outputImageIndex = outputIndexIndex,
                    };
                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("debug_visualize"));
                    vkCmdBindPipeline(_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(_cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    uint32_t xDispatch = (renderExtent[0] + 15) / 16;
                    uint32_t yDispatch = (renderExtent[1] + 15) / 16;
                    vkCmdDispatch(_cmd, xDispatch, yDispatch, 1);
                });
            }
        }
    }

    renderGraph->ImportTexture(SID("swapchain_image"), currentSwapchainImage, currentSwapchainImageView, TextureInfo{swapchain->format, swapchain->extent.width, swapchain->extent.height, 1},
                               swapchain->usages,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED, true);

    auto& blitPass = renderGraph->AddPass(SID("Blit To Swapchain"), VK_PIPELINE_STAGE_2_BLIT_BIT);
    blitPass.ReadBlitImage(finalOutput);
    blitPass.WriteBlitImage(SID("swapchain_image"));
    blitPass.Execute([&, finalOutput](VkCommandBuffer _cmd) {
        VkImage drawImage = renderGraph->GetImageHandle(finalOutput);

        Core::Array<uint32_t, 2> scaledExtent = renderExtents->GetScaledExtent();
        Core::Array<uint32_t, 2> vpOffset = renderExtents->GetViewportOffset();
        Core::Array<uint32_t, 2> vpExtent = renderExtents->GetViewportExtent();

        VkImageBlit2 blitRegion{};
        blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(renderExtent[0]), static_cast<int32_t>(renderExtent[1]), 1};
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
        auto& imguiEditorPass = renderGraph->AddPass(SID("Imgui Draw"), VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
        imguiEditorPass.WriteColorAttachment(SID("swapchain_image"));
        imguiEditorPass.Execute([&](VkCommandBuffer _cmd) {
            const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(renderGraph->GetImageViewHandle(SID("swapchain_image")), nullptr,
                                                                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const ResourceDimensions& dims = renderGraph->GetImageDimensions(SID("swapchain_image"));
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &imguiAttachment, nullptr);
            vkCmdBeginRendering(_cmd, &renderInfo);
            ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[frameIndex];
            ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, _cmd);

            vkCmdEndRendering(_cmd);
        });
    }

    if (frameBuffer.bTakeScreenshot && screenCapture->CanScreenshot()) {
        screenCapture->PrepareScreenshotResources(renderExtent[0], renderExtent[1]);
        renderGraph->CreateTexture(SID("screenshot_intermediate"), TextureInfo{VK_FORMAT_R8G8B8A8_UNORM, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

        auto& screenshotBlitPass = renderGraph->AddPass(SID("Screenshot Blit"), VK_PIPELINE_STAGE_2_BLIT_BIT);
        screenshotBlitPass.ReadBlitImage(finalOutput);
        screenshotBlitPass.WriteBlitImage(SID("screenshot_intermediate"));
        screenshotBlitPass.Execute([&, finalOutput](VkCommandBuffer _cmd) {
            VkImageBlit2 blitRegion{};
            blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blitRegion.srcOffsets[0] = {0, 0, 0};
            blitRegion.srcOffsets[1] = {static_cast<int32_t>(renderExtent[0]), static_cast<int32_t>(renderExtent[1]), 1};
            blitRegion.dstOffsets[0] = {0, 0, 0};
            blitRegion.dstOffsets[1] = {static_cast<int32_t>(renderExtent[0]), static_cast<int32_t>(renderExtent[1]), 1};

            VkBlitImageInfo2 blitInfo{};
            blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
            blitInfo.srcImage = renderGraph->GetImageHandle(finalOutput);
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.dstImage = renderGraph->GetImageHandle(SID("screenshot_intermediate"));
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &blitRegion;
            blitInfo.filter = VK_FILTER_NEAREST;
            vkCmdBlitImage2(_cmd, &blitInfo);
        });

        auto& screenshotCopyPass = renderGraph->AddPass(SID("Screenshot Copy"), VK_PIPELINE_STAGE_2_COPY_BIT);
        screenshotCopyPass.ReadCopyImage(SID("screenshot_intermediate"));
        screenshotCopyPass.Execute([&](VkCommandBuffer _cmd) {
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

        Core::Path screenshotDir = Platform::GetUserDataPath() / "screenshots";
        Platform::CreateDirectories(screenshotDir.c_str());

        Core::InlineString<> filename;
        filename.len = snprintf(filename.buf, 64, "screenshot_%llu.png", static_cast<unsigned long long>(frameNumber));
        screenCapture->screenshotSavePath = screenshotDir / filename.c_str();
        screenCapture->screenshotPendingSlot = frameIndex;
        screenCapture->StartScreenshot();
    }

    if (frameBuffer.currentMousePosition[0] > 0 && frameBuffer.currentMousePosition[0] < renderExtent[0] &&
        frameBuffer.currentMousePosition[1] > 0 && frameBuffer.currentMousePosition[1] < renderExtent[1]) {
        RenderPass& copyStableId = renderGraph->AddPass(SID("Copy Stable ID"), VK_PIPELINE_STAGE_2_COPY_BIT);
        copyStableId.ReadCopyImage(SID("stable_id"));
        copyStableId.WriteTransferBuffer(SID("readback_buffer"));
        copyStableId.Execute([&, mouseX = frameBuffer.currentMousePosition[0], mouseY = frameBuffer.currentMousePosition[1]](VkCommandBuffer cmd) {
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
                renderGraph->GetTextureHandle(SID("stable_id")),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                renderGraph->GetBufferHandle(SID("readback_buffer")),
                1,
                &region
            );
        });
    }

    RenderPass& readbackMeshletCount = renderGraph->AddPass(SID("Readback Copy"), VK_PIPELINE_STAGE_2_COPY_BIT);
    readbackMeshletCount.ReadTransferBuffer(SID("readback_buffer"));
    readbackMeshletCount.Execute([&](VkCommandBuffer cmd) {
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
    }); {
        ZoneScopedN("RenderGraphCompile");
        renderGraph->SetDebugLogging(frameBuffer.bLogRDG);
        renderGraph->Compile(frameNumber);
    } {
        ZoneScopedN("RenderGraphExecute");
        renderGraph->Execute(cmd);
        renderGraph->PrepareSwapchain(cmd, SID("swapchain_image"));
    }

    resourceManager->debugReadbackLastKnownState = renderGraph->GetBufferState(SID("debug_readback_buffer"));
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

void RenderThread::CreatePipelines()
{
    Core::Array<VkDescriptorSetLayout, 2> layouts{
        resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pSetLayouts = layouts.Data();
    layoutInfo.setLayoutCount = layouts.Size();
    globalPipelineLayout = PipelineLayout::CreatePipelineLayout(context, layoutInfo);

    pipelineManager->RegisterComputePipeline(SID("instancing_instance_lod"), Platform::GetShaderPath() / "instancing_instance_lod_compute.spv",
                                             sizeof(InstanceLODPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_prefix_sum_up_1"), Platform::GetShaderPath() / "instancing_prefix_sum_up_1_compute.spv",
                                             sizeof(PrefixSumUpsweep1PushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_prefix_sum_up_2"), Platform::GetShaderPath() / "instancing_prefix_sum_up_2_compute.spv",
                                             sizeof(PrefixSumUpsweep2PushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_scan_blocks"), Platform::GetShaderPath() / "instancing_scan_blocks_compute.spv",
                                             sizeof(PrefixSumScanBlocksPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_prefix_sum_down_1"), Platform::GetShaderPath() / "instancing_prefix_sum_down_1_compute.spv",
                                             sizeof(PrefixSumDownsweep1PushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_prefix_sum_down_2"), Platform::GetShaderPath() / "instancing_prefix_sum_down_2_compute.spv",
                                             sizeof(PrefixSumDownsweep2PushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_total_meshlet_count"), Platform::GetShaderPath() / "instancing_total_meshlet_count_compute.spv",
                                             sizeof(TotalMeshletCountPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_expand_instance_to_meshlet"), Platform::GetShaderPath() / "instancing_expand_instance_to_meshlet_compute.spv",
                                             sizeof(ExpandMeshletsPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_meshlet_visibility_prefix_sum_up_1"), Platform::GetShaderPath() / "instancing_meshlet_visibility_prefix_sum_up_1_compute.spv",
                                             sizeof(MeshletVisibilityPrefixSumUpsweep1PushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_meshlet_visibility_prefix_sum_down_2"), Platform::GetShaderPath() / "instancing_meshlet_visibility_prefix_sum_down_2_compute.spv",
                                             sizeof(MeshletVisibilityPrefixSumDownsweep2PushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_compacted_meshlet_dispatch"), Platform::GetShaderPath() / "instancing_compacted_meshlet_dispatch_compute.spv",
                                             sizeof(CompactedMeshletDispatchPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_max_meshlet_count"), Platform::GetShaderPath() / "instancing_max_meshlet_count_compute.spv",
                                             sizeof(MaxMeshletCountPushConstant), PipelineCategory::Critical);

    pipelineManager->RegisterComputePipeline(SID("visibility_buffer_barycentric_derivative"), Platform::GetShaderPath() / "visibility_buffer_barycentric_derivative_compute.spv",
                                             sizeof(VisibilityBufferResolvePushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("visibility_shading"), Platform::GetShaderPath() / "visibility_shading_compute.spv",
                                             sizeof(VisibilityShadingPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("instancing_instance_lod_shadows"), Platform::GetShaderPath() / "instancing_instance_lod_shadows_compute.spv",
                                             sizeof(InstanceLODShadowsPushConstant), PipelineCategory::Legacy);
    pipelineManager->RegisterComputePipeline(SID("instancing_expand_instance_to_meshlet_shadows"), Platform::GetShaderPath() / "instancing_expand_instance_to_meshlet_shadows_compute.spv",
                                             sizeof(ExpandMeshletsShadowsPushConstant), PipelineCategory::Legacy);

    pipelineManager->RegisterComputePipeline(SID("shadows_resolve"), Platform::GetShaderPath() / "shadows_resolve_compute.spv",
                                             sizeof(ShadowsResolvePushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("deferred_resolve"), Platform::GetShaderPath() / "deferred_resolve_compute.spv",
                                             sizeof(DeferredResolvePushConstant), PipelineCategory::Critical);

    pipelineManager->RegisterComputePipeline(SID("temporal_antialiasing"), Platform::GetShaderPath() / "temporal_antialiasing_compute.spv",
                                             sizeof(TemporalAntialiasingPushConstant), PipelineCategory::Legacy);

    pipelineManager->RegisterComputePipeline(SID("gtao_depth_prepass"), Platform::GetShaderPath() / "gtao_depth_prepass_compute.spv",
                                             sizeof(GTAODepthPrepassPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("gtao_main"), Platform::GetShaderPath() / "gtao_main_compute.spv",
                                             sizeof(GTAOMainPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("gtao_denoise"), Platform::GetShaderPath() / "gtao_denoise_compute.spv",
                                             sizeof(GTAODenoisePushConstant), PipelineCategory::Critical);


    pipelineManager->RegisterComputePipeline(SID("exposure_build_histogram"), Platform::GetShaderPath() / "exposure_build_histogram_compute.spv",
                                             sizeof(HistogramBuildPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("exposure_calculate_average"), Platform::GetShaderPath() / "exposure_calculate_average_compute.spv",
                                             sizeof(ExposureCalculatePushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("tonemap_sdr"), Platform::GetShaderPath() / "tonemap_sdr_compute.spv",
                                             sizeof(TonemapSDRPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("motion_blur_tile_max"), Platform::GetShaderPath() / "motion_blur_tile_max_compute.spv",
                                             sizeof(MotionBlurTileVelocityPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("motion_blur_neighbor_max"), Platform::GetShaderPath() / "motion_blur_neighbor_max_compute.spv",
                                             sizeof(MotionBlurNeighborMaxPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("motion_blur_reconstruction"), Platform::GetShaderPath() / "motion_blur_reconstruction_compute.spv",
                                             sizeof(MotionBlurReconstructionPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("bloom_threshold"), Platform::GetShaderPath() / "bloom_threshold_compute.spv",
                                             sizeof(BloomThresholdPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("bloom_downsample"), Platform::GetShaderPath() / "bloom_downsample_compute.spv",
                                             sizeof(BloomDownsamplePushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("bloom_upsample"), Platform::GetShaderPath() / "bloom_upsample_compute.spv",
                                             sizeof(BloomUpsamplePushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("vignette_aberration"), Platform::GetShaderPath() / "vignette_aberration_compute.spv",
                                             sizeof(VignetteChromaticAberrationPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("film_grain"), Platform::GetShaderPath() / "film_grain_compute.spv",
                                             sizeof(FilmGrainPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("sharpening"), Platform::GetShaderPath() / "sharpening_compute.spv",
                                             sizeof(SharpeningPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("color_grading"), Platform::GetShaderPath() / "color_grading_compute.spv",
                                             sizeof(ColorGradingPushConstant), PipelineCategory::Critical);
    pipelineManager->RegisterComputePipeline(SID("panini_projection"), Platform::GetShaderPath() / "panini_projection_compute.spv",
                                             sizeof(PaniniProjectionPushConstant), PipelineCategory::Critical);

    pipelineManager->RegisterComputePipeline(SID("debug_visualize"), Platform::GetShaderPath() / "debug_visualize_compute.spv",
                                             sizeof(DebugVisualizePushConstant), PipelineCategory::Critical);

#if WILL_EDITOR
    const VkDescriptorSetLayout emapLayout = resourceManager->environmentMapGenerateResources.descriptorSetLayout.handle;
    pipelineManager->RegisterComputePipelineCustomLayout(SID("ibl_equirect_to_cubemap"), Platform::GetShaderPath() / "ibl_equirect_to_cubemap_compute.spv",
                                                         sizeof(EquirectToCubemapPushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    pipelineManager->RegisterComputePipelineCustomLayout(SID("ibl_convolve_diffuse"), Platform::GetShaderPath() / "ibl_convolve_diffuse_compute.spv",
                                                         sizeof(ConvolveDiffusePushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    pipelineManager->RegisterComputePipelineCustomLayout(SID("ibl_prefilter_specular"), Platform::GetShaderPath() / "ibl_prefilter_specular_compute.spv",
                                                         sizeof(PrefilterSpecularPushConstant), PipelineCategory::AssetGeneration, Core::Span(&emapLayout, 1));

    const VkDescriptorSetLayout brdfLutLayout = resourceManager->brdfLutGenerateResources.descriptorSetLayout.handle;
    pipelineManager->RegisterComputePipelineCustomLayout(SID("ibl_brdf_lut"), Platform::GetShaderPath() / "brdf_lut_generate_compute.spv",
                                                         sizeof(BRDFLUTPushConstant), PipelineCategory::AssetGeneration, Core::Span(&brdfLutLayout, 1));
#endif

    GraphicsPipelineBuilder builder;

    // Shadow cascade pipeline
    {
        builder.AddShaderStage(Platform::GetShaderPath() / "shadow_mesh_shading_instanced_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        builder.EnableDepthBias();
        builder.SetupRenderer(nullptr, 0, SHADOW_CASCADE_FORMAT);
        builder.AddDynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS);

        pipelineManager->RegisterGraphicsPipeline(
            SID("shadow_cascade_instanced"),
            builder,
            sizeof(ShadowMeshShadingPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    constexpr Core::Array<VkFormat, 2> graphicsColorFormats{
        VISIBILITY_BUFFER_FORMAT,
        GBUFFER_STABLE_ID_FORMAT,
    };

    // Visibility Buffer
    {
        builder.AddShaderStage(Platform::GetShaderPath() / "visibility_buffer_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(Platform::GetShaderPath() / "visibility_buffer_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);


        builder.SetupRenderer(graphicsColorFormats.Data(), graphicsColorFormats.Size(), DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);

        pipelineManager->RegisterGraphicsPipeline(
            SID("visibility_buffer_accumulate"),
            builder,
            sizeof(VisibilityBufferAccumulatePushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Portal Graphics Pipeline
    {
        builder.AddShaderStage(Platform::GetShaderPath() / "visibility_buffer_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(Platform::GetShaderPath() / "visibility_buffer_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        builder.SetupStencilState(VK_TRUE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
        builder.SetupRenderer(graphicsColorFormats.Data(), graphicsColorFormats.Size(), DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);
        builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

        pipelineManager->RegisterGraphicsPipeline(
            SID("portal_rendering"),
            builder,
            sizeof(VisibilityBufferAccumulatePushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Portal Composite
    {
        builder.AddShaderStage(Platform::GetShaderPath() / "fullscreen_pass_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage(Platform::GetShaderPath() / "portal_composite_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_ALWAYS);
        builder.SetupStencilState(VK_TRUE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL);

        VkFormat colorFormats[2] = {
            COLOR_ATTACHMENT_FORMAT,
            GBUFFER_TARGET_TWO
        };
        builder.SetupRenderer(colorFormats, 2, DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);
        builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

        pipelineManager->RegisterGraphicsPipeline(
            SID("portal_composite"),
            builder,
            sizeof(PortalCompositePushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Cubemap Visualizer
    {
        builder.AddShaderStage(Platform::GetShaderPath() / "cubemap_visualizer_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(Platform::GetShaderPath() / "cubemap_visualizer_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        builder.SetupRenderer(graphicsColorFormats.Data(), graphicsColorFormats.Size(), DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);

        pipelineManager->RegisterGraphicsPipeline(
            SID("cubemap_visualize"),
            builder,
            sizeof(BaseMeshShadingPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }


    // Skybox Rendering
    {
        builder.AddShaderStage(Platform::GetShaderPath() / "fullscreen_pass_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage(Platform::GetShaderPath() / "environment_map_skybox_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_FALSE, VK_COMPARE_OP_EQUAL);

        VkFormat colorFormats[1] = {
            COLOR_ATTACHMENT_FORMAT,
        };
        builder.SetupRenderer(colorFormats, 1, DEPTH_ATTACHMENT_FORMAT, VK_FORMAT_UNDEFINED);

        pipelineManager->RegisterGraphicsPipeline(
            SID("environment_skybox"),
            builder,
            sizeof(EnvironmentSkyboxPushConstant),
            VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }

    // Debug Render
    {
        builder.AddShaderStage(Platform::GetShaderPath() / "debug_render_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage(Platform::GetShaderPath() / "debug_render_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_FALSE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        VkPipelineColorBlendAttachmentState blendState{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        builder.SetupBlending(&blendState, 1);

        VkFormat colorFormats[1] = {
            POST_PROCESS_OUTPUT_FORMAT,
        };
        builder.SetupRenderer(colorFormats, 1, DEPTH_ATTACHMENT_FORMAT);

        pipelineManager->RegisterGraphicsPipeline(
            SID("debug_render"),
            builder,
            sizeof(DebugDrawPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT,
            PipelineCategory::Critical
        );
        builder.Clear();
    }
}

void RenderThread::UploadFrameUniforms(const Core::ViewFamily& viewFamily, const Core::Array<uint32_t, 2> renderExtent, float renderDeltaTime) const
{
    renderGraph->CreateBuffer(SID("scene_data"), SCENE_DATA_BUFFER_SIZE, false);
    renderGraph->CreateBuffer(SID("shadow_data"), SHADOW_DATA_BUFFER_SIZE, false);
    renderGraph->CreateBuffer(SID("light_data"), LIGHT_DATA_BUFFER_SIZE, false);

    // Scene Data
    SceneData sceneData = GenerateSceneData(viewFamily.mainView, viewFamily.postProcessConfig, renderExtent, frameNumber, renderDeltaTime);
    UploadAllocation sceneDataUploadAllocation = renderGraph->AllocateTransient(sizeof(SceneData));
    memcpy(sceneDataUploadAllocation.ptr, &sceneData, sizeof(SceneData));
    // Portal Scene Data
    UploadAllocation portalSceneDataUploadAllocation{};
    bool bHasPortal = !viewFamily.portalViews.IsEmpty();
    if (bHasPortal) {
        SceneData portalSceneData = GenerateSceneData(viewFamily.portalViews[0].view, viewFamily.postProcessConfig, renderExtent, frameNumber, renderDeltaTime);
        portalSceneData.clipPlane = glm::vec4(viewFamily.portalViews[0].exitPortalNormal,
                                              -glm::dot(viewFamily.portalViews[0].exitPortalNormal, viewFamily.portalViews[0].exitPortalTransform.translation));
        portalSceneDataUploadAllocation = renderGraph->AllocateTransient(sizeof(SceneData));
        memcpy(portalSceneDataUploadAllocation.ptr, &portalSceneData, sizeof(SceneData));
    }

    // Shadow Data
    Core::ShadowConfiguration shadowConfig = viewFamily.shadowConfig;
    Core::DirectionalLight directionalLight = viewFamily.directionalLight;
    directionalLight.direction = normalize(directionalLight.direction);
    const Core::RenderView& selectedShadowView = viewFamily.mainView;

    ShadowData shadowData{};
    //
    {
        const float ratio = shadowConfig.cascadeFarPlane / shadowConfig.cascadeNearPlane;
        shadowData.nearSplits[0] = shadowConfig.cascadeNearPlane;
        for (size_t i = 1; i < SHADOW_CASCADE_COUNT; i++) {
            const float si = static_cast<float>(i) / static_cast<float>(SHADOW_CASCADE_COUNT);

            const float uniformTerm = shadowConfig.cascadeNearPlane + (shadowConfig.cascadeFarPlane - shadowConfig.cascadeNearPlane) * si;
            const float logTerm = shadowConfig.cascadeNearPlane * std::pow(ratio, si);
            const float nearValue = shadowConfig.splitLambda * logTerm + (1.0f - shadowConfig.splitLambda) * uniformTerm;

            const float farValue = nearValue * shadowConfig.splitOverlap;

            shadowData.nearSplits[i] = nearValue;
            shadowData.farSplits[i - 1] = farValue;
        }
        shadowData.farSplits[SHADOW_CASCADE_COUNT - 1] = shadowConfig.cascadeFarPlane;

        for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i) {
            ViewProjMatrix viewProj = GenerateLightSpaceMatrix(
                shadowConfig.cascadePreset.extents[i][0],
                shadowData.nearSplits[i],
                shadowData.farSplits[i],
                directionalLight.direction,
                selectedShadowView.currentViewData
            );
            shadowData.lightSpaceMatrices[i] = viewProj.proj * viewProj.view;
            shadowData.lightFrustums[i] = CreateFrustum(shadowData.lightSpaceMatrices[i]);
            shadowData.lightSizes[i] = shadowConfig.cascadePreset.lightSizes[i];
            shadowData.blockerSearchSamples[i] = shadowConfig.cascadePreset.pcssSamples[i].blockerSearchSamples;
            shadowData.pcfSamples[i] = shadowConfig.cascadePreset.pcssSamples[i].pcfSamples;
        }

        shadowData.shadowIntensity = shadowConfig.shadowIntensity;
    }

    UploadAllocation shadowDataUploadAllocation = renderGraph->AllocateTransient(sizeof(ShadowData));
    memcpy(shadowDataUploadAllocation.ptr, &shadowData, sizeof(ShadowData));

    // Lights
    LightData lightData{};
    lightData.mainLightDirection = {viewFamily.directionalLight.direction, viewFamily.directionalLight.intensity};
    lightData.mainLightColor = {viewFamily.directionalLight.color, 0.0f};

    UploadAllocation lightDataUploadAllocation = renderGraph->AllocateTransient(sizeof(LightData));
    memcpy(lightDataUploadAllocation.ptr, &lightData, sizeof(LightData));

    auto& uploadUniformsPass = renderGraph->AddPass(SID("Upload Uniforms"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    uploadUniformsPass.WriteTransferBuffer(SID("scene_data"));
    uploadUniformsPass.WriteTransferBuffer(SID("shadow_data"));
    uploadUniformsPass.WriteTransferBuffer(SID("light_data"));
    uploadUniformsPass.Execute([&,
            sceneOffset = sceneDataUploadAllocation.offset,
            portalOffset = portalSceneDataUploadAllocation.offset,
            hasPortal = bHasPortal,
            shadowOffset = shadowDataUploadAllocation.offset,
            lightOffset = lightDataUploadAllocation.offset](VkCommandBuffer cmd) {
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
                .dstBuffer = renderGraph->GetBufferHandle(SID("scene_data")),
                .regionCount = sceneDataCount,
                .pRegions = sceneDataRegions.Data()
            };
            vkCmdCopyBuffer2(cmd, &sceneDataCopyInfo);

            Core::Array<VkBufferCopy2, 1> shadowDataRegions{};
            shadowDataRegions[0].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            shadowDataRegions[0].srcOffset = shadowOffset;
            shadowDataRegions[0].dstOffset = 0;
            shadowDataRegions[0].size = sizeof(ShadowData);
            const VkCopyBufferInfo2 shadowDataCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(SID("shadow_data")),
                .regionCount = shadowDataRegions.Size(),
                .pRegions = shadowDataRegions.Data()
            };
            vkCmdCopyBuffer2(cmd, &shadowDataCopyInfo);

            Core::Array<VkBufferCopy2, 1> lightDataRegions{};
            lightDataRegions[0].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            lightDataRegions[0].srcOffset = lightOffset;
            lightDataRegions[0].dstOffset = 0;
            lightDataRegions[0].size = sizeof(LightData);
            const VkCopyBufferInfo2 lightDataCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(SID("light_data")),
                .regionCount = lightDataRegions.Size(),
                .pRegions = lightDataRegions.Data()
            };
            vkCmdCopyBuffer2(cmd, &lightDataCopyInfo);
        });
}

void RenderThread::UploadModelUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    size_t totalInstanceCount = viewFamily.mainPassInstances.Size();

    for (const auto& pair : viewFamily.customShaderDraws) {
        pair.value.instanceBufferOffset = static_cast<uint32_t>(totalInstanceCount * sizeof(Instance));
        totalInstanceCount += pair.value.instances.Size();
    }

    UploadAllocation instanceUpload = renderGraph->AllocateTransient(totalInstanceCount * sizeof(Instance));
    auto* instanceBuffer = static_cast<Instance*>(instanceUpload.ptr);

    for (size_t i = 0; i < viewFamily.mainPassInstances.Size(); ++i) {
        auto& inst = viewFamily.mainPassInstances[i];
        instanceBuffer[i] = {
            .primitiveIndex = inst.primitiveIndex,
            .modelIndex = inst.modelIndex,
            .materialIndex = inst.gpuMaterialIndex,
            .stableId = inst.stableId,
        };
    }

    for (const auto& [key, customDraw] : viewFamily.customShaderDraws) {
        size_t startIndex = customDraw.instanceBufferOffset / sizeof(Instance);
        for (size_t i = 0; i < customDraw.instances.Size(); ++i) {
            auto& inst = customDraw.instances[i];
            instanceBuffer[startIndex + i] = {
                .primitiveIndex = inst.primitiveIndex,
                .modelIndex = inst.modelIndex,
                .materialIndex = inst.gpuMaterialIndex,
                .stableId = inst.stableId,
            };
        }
    }

    if (totalInstanceCount > 0) {
        renderGraph->CreateBuffer(GEOMETRY_INSTANCE_BUFFER, totalInstanceCount * sizeof(Instance), false);

        RenderPass& uploadPass = renderGraph->AddPass(SID("Upload Instances"), VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadPass.WriteTransferBuffer(GEOMETRY_INSTANCE_BUFFER);
        uploadPass.Execute([&,srcOffset = instanceUpload.offset,totalSize = totalInstanceCount * sizeof(Instance)](VkCommandBuffer cmd) {
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
        renderGraph->CreateBuffer(SID("model_buffer"), renderFamilyProperties.modelBufferSize, false);
        UploadAllocation modelUpload = renderGraph->AllocateTransient(viewFamily.modelMatrices.Size() * sizeof(Model));
        memcpy(modelUpload.ptr, viewFamily.modelMatrices.Data(), viewFamily.modelMatrices.Size() * sizeof(Model));

        RenderPass& uploadModelMatricesPass = renderGraph->AddPass(SID("Upload Model Matrices"), VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadModelMatricesPass.WriteTransferBuffer(SID("model_buffer"));
        uploadModelMatricesPass.Execute([&,
                modelOffset = modelUpload.offset,
                modelSize = viewFamily.modelMatrices.Size() * sizeof(Model)](VkCommandBuffer cmd) {
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

    if (!viewFamily.materials.IsEmpty()) {
        renderGraph->CreateBuffer(SID("material_buffer"), renderFamilyProperties.materialBufferSize, false);
        UploadAllocation materialUpload = renderGraph->AllocateTransient(viewFamily.materials.Size() * sizeof(MaterialProperties));
        memcpy(materialUpload.ptr, viewFamily.materials.Data(), viewFamily.materials.Size() * sizeof(MaterialProperties));

        RenderPass& uploadMaterialsPass = renderGraph->AddPass(SID("Upload Materials"), VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadMaterialsPass.WriteTransferBuffer(SID("material_buffer"));
        uploadMaterialsPass.Execute([&,
                materialOffset = materialUpload.offset,
                materialSize = viewFamily.materials.Size() * sizeof(MaterialProperties)](VkCommandBuffer cmd) {
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
    }
}

void RenderThread::SetupCascadedShadows(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties, uint32_t sceneIndex) const
{
    Core::ShadowConfiguration shadowConfig = viewFamily.shadowConfig;

    for (int32_t cascadeLevel = 0; cascadeLevel < SHADOW_CASCADE_COUNT; ++cascadeLevel) {
        Core::InlineString<32> shadowMapName;
        shadowMapName.len = snprintf(shadowMapName.buf, 32, "shadow_cascade_%d", cascadeLevel);
        Core::InlineString<48> shadowPassName;
        shadowPassName.len = snprintf(shadowPassName.buf, 48, "Shadow Cascade Pass %d", cascadeLevel);

        StringID shadowMapId = StringID(shadowMapName.c_str(), shadowMapName.Size());
        StringID shadowPassId = StringID(shadowPassName.c_str(), shadowPassName.Size());


        uint32_t cascadeWidth = shadowConfig.cascadePreset.extents[cascadeLevel][0];
        uint32_t cascadeHeight = shadowConfig.cascadePreset.extents[cascadeLevel][1];
        float linearBias = shadowConfig.cascadePreset.biases[cascadeLevel].linear;
        float slopedBias = shadowConfig.cascadePreset.biases[cascadeLevel].sloped;
        graph.CreateTexture(shadowMapId,
                            TextureInfo{
                                SHADOW_CASCADE_FORMAT,
                                static_cast<uint32_t>(shadowConfig.cascadePreset.extents[cascadeLevel][0]),
                                static_cast<uint32_t>(shadowConfig.cascadePreset.extents[cascadeLevel][1]),
                                1
                            },
                            CLEAR_COLOR_FULL, false);

        // Main Draw
        if (!viewFamily.mainPassInstances.IsEmpty()) {
            /*InstancedGeometryPassConfig mainConfig{
                .prefix = Core::InlineString("main_shadow"),
                .instanceCount = static_cast<uint32_t>(viewFamily.mainPassInstances.Size()),
                .instanceBufferOffset = 0,
                .visibleMeshletUpperBound = renderFamilyProperties.visibleMeshletUpperBound,

                // Buffer sizes from RenderFamilyProperties
                .instanceMeshletOffsetsBufferSize = renderFamilyProperties.instanceMeshletOffsetsBufferSize,
                .level1SumsBufferSize = renderFamilyProperties.level1SumsBufferSize,
                .level1BlockSumsBufferSize = renderFamilyProperties.level1BlockSumsBufferSize,
                .level2SumsBufferSize = renderFamilyProperties.level2SumsBufferSize,
                .level2BlockSumsBufferSize = renderFamilyProperties.level2BlockSumsBufferSize,
                .scannedLevel2BlockSumsBufferSize = renderFamilyProperties.scannedLevel2BlockSumsBufferSize,
                .intermediateMeshletBufferSize = renderFamilyProperties.intermediateMeshletBufferSize,
                .meshletLevel1SumsBufferSize = renderFamilyProperties.meshletLevel1SumsBufferSize,
                .meshletLevel1BlockSumsBufferSize = renderFamilyProperties.meshletLevel1BlockSumsBufferSize,
                .meshletLevel2SumsBufferSize = renderFamilyProperties.meshletLevel2SumsBufferSize,
                .meshletLevel2BlockSumsBufferSize = renderFamilyProperties.meshletLevel2BlockSumsBufferSize,
                .meshletScannedLevel2BlockSumsBufferSize = renderFamilyProperties.meshletScannedLevel2BlockSumsBufferSize,
                .visibleMeshletsBufferSize = renderFamilyProperties.visibleMeshletsBufferSize,

                .lodBias = LOD_BIAS,
            };

            InstancedGeometryPassOutputs mainOutputs = SetupInstancedGeometryShadowPass(graph, mainConfig, pipelineManager, sceneIndex, cascadeLevel, false);

            RenderPass& shadowPass = graph.AddPass(shadowPassId, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
            shadowPass.ReadWriteDepthAttachment(shadowMapId);
            shadowPass.ReadBuffer(SCENE_DATA_BUFFER);
            shadowPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
            shadowPass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
            shadowPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            shadowPass.ReadBuffer(mainOutputs.visibleMeshlets);
            shadowPass.ReadIndirectBuffer(mainOutputs.compactedDispatchArgs);
            shadowPass.Execute([&, mainOutputs, sceneIndex, cascadeLevel, cascadeWidth, cascadeHeight, linearBias, slopedBias, shadowMapId](VkCommandBuffer cmd) {
                VkViewport viewport = VkHelpers::GenerateViewport(cascadeWidth, cascadeHeight);
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor = VkHelpers::GenerateScissor(cascadeWidth, cascadeHeight);
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
                const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(shadowMapId), &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
                const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({cascadeWidth, cascadeHeight},
                                                                            nullptr, 0, &depthAttachment);
                vkCmdBeginRendering(cmd, &renderInfo);

                ShadowMeshShadingPushConstant pushConstants{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .shadowData = graph.GetBufferAddress(SHADOW_DATA_BUFFER),
                    .positionBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
                    .meshletVerticesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
                    .meshletTrianglesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
                    .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                    .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                    .visibleMeshlets = graph.GetBufferAddress(mainOutputs.visibleMeshlets),
                    .compactedDispatchBuffer = graph.GetBufferAddress(mainOutputs.compactedDispatchArgs),
                    .sceneDataIndex = sceneIndex,
                    .cascadeIndex = static_cast<uint32_t>(cascadeLevel)
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("shadow_cascade_instanced"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
                vkCmdSetDepthBias(cmd, -linearBias, 0.0f, -slopedBias);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_MESH_BIT_EXT,
                                   0, sizeof(BaseMeshShadingPushConstant), &pushConstants);

                vkCmdDrawMeshTasksIndirectEXT(
                    cmd,
                    graph.GetBufferHandle(mainOutputs.compactedDispatchArgs),
                    offsetof(InstancingCompactedMeshletDispatchIndirect, x),
                    1,
                    sizeof(InstancingCompactedMeshletDispatchIndirect));

                vkCmdEndRendering(cmd);
            });*/
        }

        // Custom draws need to also specify what shadow pipeline to use.
        /*for (const auto& customDraw : viewFamily.customShaderDraws) {
            InstancedGeometryPassConfig customConfig{
                .prefix = customDraw.first,
                .instanceCount = static_cast<uint32_t>(customDraw.second.instances.size()),
                .instanceBufferOffset = customDraw.second.instanceBufferOffset,
                .visibleMeshletUpperBound = renderFamilyProperties.visibleMeshletUpperBound,
                .bClearTargets = false,
                .instanceMeshletOffsetsBufferSize = renderFamilyProperties.instanceMeshletOffsetsBufferSize,
                .level1SumsBufferSize = renderFamilyProperties.level1SumsBufferSize,
                .level1BlockSumsBufferSize = renderFamilyProperties.level1BlockSumsBufferSize,
                .level2SumsBufferSize = renderFamilyProperties.level2SumsBufferSize,
                .level2BlockSumsBufferSize = renderFamilyProperties.level2BlockSumsBufferSize,
                .scannedLevel2BlockSumsBufferSize = renderFamilyProperties.scannedLevel2BlockSumsBufferSize,
                .intermediateMeshletBufferSize = renderFamilyProperties.intermediateMeshletBufferSize,
                .meshletLevel1SumsBufferSize = renderFamilyProperties.meshletLevel1SumsBufferSize,
                .meshletLevel1BlockSumsBufferSize = renderFamilyProperties.meshletLevel1BlockSumsBufferSize,
                .meshletLevel2SumsBufferSize = renderFamilyProperties.meshletLevel2SumsBufferSize,
                .meshletLevel2BlockSumsBufferSize = renderFamilyProperties.meshletLevel2BlockSumsBufferSize,
                .meshletScannedLevel2BlockSumsBufferSize = renderFamilyProperties.meshletScannedLevel2BlockSumsBufferSize,
                .visibleMeshletsBufferSize = renderFamilyProperties.visibleMeshletsBufferSize,
                .lodBias = LOD_BIAS,
            };

            InstancedGeometryPassOutputs customOutputs = SetupInstancedGeometryShadowPass(graph, customConfig, pipelineManager, sceneIndex, cascadeLevel, false);

            auto customDrawPassName = "Shadow Cascade Pass " + customDraw.first + std::to_string(cascadeLevel);
            RenderPass& customShadowPass = graph.AddPass(customDrawPassName, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
            customShadowPass.ReadWriteDepthAttachment(shadowMapName);
            customShadowPass.ReadBuffer("scene_data");
            customShadowPass.ReadBuffer("model_buffer");
            customShadowPass.ReadBuffer("material_buffer");
            customShadowPass.ReadBuffer(GEOMETRY_BUFFER_INSTANCE);
            customShadowPass.ReadBuffer(customOutputs.visibleMeshlets);
            customShadowPass.ReadIndirectBuffer(customOutputs.compactedDispatchArgs);
            customShadowPass.Execute(
                [&, customOutputs, customDraw, sceneIndex, cascadeLevel, cascadeWidth, cascadeHeight, instanceBufferOffset = customDraw.second.instanceBufferOffset](VkCommandBuffer cmd) {
                    VkViewport viewport = VkHelpers::GenerateViewport(cascadeWidth, cascadeHeight);
                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                    VkRect2D scissor = VkHelpers::GenerateScissor(cascadeWidth, cascadeHeight);
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(shadowMapName), nullptr, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
                    const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({cascadeWidth, cascadeHeight},
                                                                                nullptr, 0, &depthAttachment);


                    vkCmdBeginRendering(cmd, &renderInfo);

                    ShadowMeshShadingPushConstant pushConstants{
                        .sceneData = graph.GetBufferAddress("scene_data"),
                        .shadowData = graph.GetBufferAddress(GEOMETRY_BUFFER_SHADOW_DATA),
                        .positionBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_POSITION_BUFFER),
                        .meshletVerticesBuffer = graph.GetBufferAddress("meshlet_vertex_buffer"),
                        .meshletTrianglesBuffer = graph.GetBufferAddress("meshlet_triangle_buffer"),
                        .meshletBuffer = graph.GetBufferAddress("meshlet_buffer"),
                        .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                        .instanceBuffer = graph.GetBufferAddress(GEOMETRY_BUFFER_INSTANCE) + instanceBufferOffset,
                        .materialBuffer = graph.GetBufferAddress("material_buffer"),
                        .modelBuffer = graph.GetBufferAddress("model_buffer"),
                        .visibleMeshlets = graph.GetBufferAddress(customOutputs.visibleMeshlets),
                        .compactedDispatchBuffer = graph.GetBufferAddress(customOutputs.compactedDispatchArgs),
                        .sceneDataIndex = sceneIndex,
                        .cascadeIndex = static_cast<uint32_t>(cascadeLevel),
                    };
                    memcpy(pushConstants.customData, customDraw.second.pushConstantCustomData.data(), sizeof(pushConstants.customData));


                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(customDraw.second.pipelineName);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(pushConstants), &pushConstants);
                    if (customDraw.second.stencilValue != -1) {
                        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, customDraw.second.stencilValue);
                    }
                    vkCmdDrawMeshTasksIndirectEXT(
                        cmd,
                        graph.GetBufferHandle(customOutputs.compactedDispatchArgs),
                        offsetof(InstancingCompactedMeshletDispatchIndirect, x),
                        1,
                        sizeof(InstancingCompactedMeshletDispatchIndirect));

                    vkCmdEndRendering(cmd);
                });
        }*/
    }
}

void RenderThread::SetupPortalComposite(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const MainRenderTargets& targets,
                                        const MainRenderTargets& portalTargets) const
{
    RenderPass& portalCompositePass = graph.AddPass(SID("Portal Composite"), VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    portalCompositePass.ReadSampledImage(portalTargets.outputColor);
    portalCompositePass.ReadSampledImage(portalTargets.gbufferTwo);
    portalCompositePass.ReadSampledImage(portalTargets.depthStencil);
    portalCompositePass.WriteColorAttachment(targets.outputColor);
    portalCompositePass.WriteColorAttachment(targets.gbufferTwo);
    portalCompositePass.ReadWriteDepthAttachment(targets.depthStencil);
    portalCompositePass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
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
}

StringID RenderThread::SetupTemporalAntialiasing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const Core::Array<uint32_t, 2> renderExtent, const MainRenderTargets& ppTargets) const
{
    graph.CreateTexture(SID("taa_current"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);
    renderGraph->CarryTextureToNextFrame(SID("taa_current"), SID("taa_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    if (renderGraph->HasTexture(SID("gbuffer_two"))) {
        renderGraph->CarryTextureToNextFrame(SID("gbuffer_two"), SID("gbuffer_two_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    if (!graph.HasTexture(SID("taa_history")) || !graph.HasTexture(SID("velocity_history"))) {
        RenderPass& taaPass = graph.AddPass(SID("TAA Copy Deferred"), VK_PIPELINE_STAGE_2_COPY_BIT);
        taaPass.ReadCopyImage(ppTargets.outputColor);
        taaPass.WriteCopyImage(SID("taa_current"));
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            VkImage drawImage = graph.GetImageHandle(ppTargets.outputColor);
            VkImage taaImage = graph.GetImageHandle(SID("taa_current"));

            VkImageCopy2 copyRegion{};
            copyRegion.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent = {width, height, 1};

            VkCopyImageInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
            copyInfo.srcImage = drawImage;
            copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyInfo.dstImage = taaImage;
            copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &copyRegion;

            vkCmdCopyImage2(cmd, &copyInfo);
        });
        return ppTargets.outputColor;
    }

    RenderPass& taaPass = graph.AddPass(SID("TAA Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    taaPass.ReadBuffer(SID("scene_data"));
    taaPass.ReadSampledImage(ppTargets.outputColor);
    taaPass.ReadSampledImage(ppTargets.depthStencil);
    taaPass.ReadSampledImage(SID("taa_history"));
    taaPass.ReadSampledImage(ppTargets.gbufferTwo);
    taaPass.ReadSampledImage(SID("velocity_history"));
    taaPass.WriteStorageImage(SID("taa_current"));
    taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
        TemporalAntialiasingPushConstant pushData{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.outputColor),
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(ppTargets.depthStencil),
            .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex(SID("taa_history")),
            .velocityIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.gbufferTwo),
            .velocityHistoryIndex = graph.GetSampledImageViewDescriptorIndex(SID("velocity_history")),
            .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("taa_current")),
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("temporal_antialiasing"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TemporalAntialiasingPushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });


    graph.CreateTexture(SID("taa_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& finalCopyPass = graph.AddPass(SID("TAA Final Copy"), VK_PIPELINE_STAGE_2_BLIT_BIT);
    finalCopyPass.ReadBlitImage(SID("taa_current"));
    finalCopyPass.WriteBlitImage(SID("taa_output"));
    finalCopyPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        VkImage src = graph.GetImageHandle(SID("taa_current"));
        VkImage dst = graph.GetImageHandle(SID("taa_output"));

        VkOffset3D renderOffset = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};

        VkImageBlit2 blitRegion{};
        blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = renderOffset;
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = renderOffset;

        VkBlitImageInfo2 blitInfo{};
        blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blitInfo.srcImage = src;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.dstImage = dst;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;
        blitInfo.filter = VK_FILTER_LINEAR;

        vkCmdBlitImage2(cmd, &blitInfo);
    });

    return SID("taa_output");
}

void RenderThread::SetupDebugRender(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, StringID depthTarget, StringID targetImage,
                                    FrameResourceLimits& limits) const
{
#ifndef PACKAGED_BUILD
    // Worst-case segment counts for buffer allocation
    size_t totalSegments = 0;
    totalSegments += viewFamily.debugLines.Size(); // 1 segment per line
    totalSegments += viewFamily.debugBoxes.Size() * 12; // 12 edges per box
    totalSegments += viewFamily.debugSpheres.Size() * 96; // 32 segs * 3 circles (max LOD)

    if (totalSegments == 0) {
        return;
    }

    limits.highestDebugSegmentBuffer = std::max(limits.highestDebugSegmentBuffer, NextPowerOfTwo(totalSegments));

    graph.CreateBuffer(SID("debug_segment_buffer"), limits.highestDebugSegmentBuffer * sizeof(DebugLineSegment), false);

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


    if (segmentOffset == 0) {
        return;
    }

    const uint32_t totalLineSegments = segmentOffset;

    RenderPass& uploadDebugPass = graph.AddPass(SID("Upload Debug Geometry"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    uploadDebugPass.WriteTransferBuffer(SID("debug_segment_buffer"));

    VkBuffer srcBuffer = graph.GetTransientUploadBuffer();
    uploadDebugPass.Execute([&, srcBuffer,
            uploadOffset = segmentUpload.offset,
            uploadSize = totalLineSegments * sizeof(DebugLineSegment)](VkCommandBuffer cmd) {
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

    RenderPass& debugDrawPass = graph.AddPass(SID("Debug Draw"), VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    debugDrawPass.WriteColorAttachment(targetImage);
    bool bHasDepth = graph.HasTexture(depthTarget);
    if (bHasDepth) {
        debugDrawPass.ReadWriteDepthAttachment(depthTarget);
    }
    debugDrawPass.ReadBuffer(SID("scene_data"));
    debugDrawPass.ReadBuffer(SID("debug_segment_buffer"));
    debugDrawPass.Execute([&, width = renderExtent[0], height = renderExtent[1], totalLineSegments, bHasDepth, depthTarget, targetImage](VkCommandBuffer cmd) {
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
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
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
