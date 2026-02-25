//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include "render-graph/passes/instanced_geometry_pass.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_render_extents.h"
#include "resource_manager.h"
#include "render/vulkan/vk_swapchain.h"
#include "render/vulkan/vk_utils.h"
#include "engine/will_engine.h"
#include "platform/paths.h"
#include "render-graph/render_graph.h"
#include "render-graph/render_pass.h"
#include "shaders/constants_interop.h"
#include "shaders/push_constant_interop.h"

#include "types/render_types.h"
#include "render/vulkan/vk_imgui_wrapper.h"
#include "backends/imgui_impl_vulkan.h"
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

RenderThread::RenderThread(Core::FrameSync* engineRenderSynchronization, enki::TaskScheduler* scheduler, SDL_Window* window, uint32_t width,
                           uint32_t height)
    : window(window), engineRenderSynchronization(engineRenderSynchronization), scheduler(scheduler)
{
    context = std::make_unique<VulkanContext>(window);
    swapchain = std::make_unique<Swapchain>(context.get(), width, height);
    imgui = std::make_unique<ImguiWrapper>(context.get(), window, Core::FRAME_BUFFER_COUNT, swapchain->format);
    renderExtents = std::make_unique<RenderExtents>(width, height, 1.0f);
    resourceManager = std::make_unique<ResourceManager>(context.get());
    renderGraph = std::make_unique<RenderGraph>(context.get(), resourceManager.get());
    std::array layouts{
        resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle
    };
    pipelineManager = std::make_unique<PipelineManager>(context.get(), layouts);

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
}

RenderThread::~RenderThread() = default;

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
                LOG_INFO(Renderer, "Swapchain Recreated");
                vkDeviceWaitIdle(context->device);

                swapchain->Recreate(frameBuffer.swapchainRecreateCommand.windowWidth, frameBuffer.swapchainRecreateCommand.windowHeight);
                renderExtents->ApplyResize(frameBuffer.swapchainRecreateCommand.windowWidth, frameBuffer.swapchainRecreateCommand.windowHeight);

                bRenderRequestsRecreate = false;
                bEngineRequestsRecreate = false;
                frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = false;

                renderGraph->InvalidateAll();
            }

            if (frameBuffer.viewportResizeCommand.bEngineCommandsResize) {
                vkDeviceWaitIdle(context->device);

                renderExtents->ApplyViewportResize(frameBuffer.viewportResizeCommand.offsetX, frameBuffer.viewportResizeCommand.offsetY, frameBuffer.viewportResizeCommand.sizeX,
                                                   frameBuffer.viewportResizeCommand.sizeY);
                frameBuffer.viewportResizeCommand.bEngineCommandsResize = false;
                renderGraph->InvalidateAll();
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

    VK_CHECK(vkResetCommandBuffer(renderSync.commandBuffer, 0));
    VkCommandBufferBeginInfo beginInfo = VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(renderSync.commandBuffer, &beginInfo));

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
    //
    {
        ZoneScopedN("RenderGraphReset");
        renderGraph->Reset(frameIndex, frameNumber, RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD);
    }


    std::array<uint32_t, 2> renderExtent = renderExtents->GetScaledExtent();
    VkImage currentSwapchainImage = swapchain->swapchainImages[swapchainImageIndex];
    VkImageView currentSwapchainImageView = swapchain->swapchainImageViews[swapchainImageIndex];

    Core::ViewFamily& viewFamily = frameBuffer.mainViewFamily;
    ReadbackStruct* readbackData = renderGraph->GetReadbackData();
    PrepareRenderFamilyProperties(viewFamily, readbackData, persistentRenderFamilyProperties, pipelineManager.get(), frameResourceLimits);
    RenderFamilyProperties& renderFamilyProperties = persistentRenderFamilyProperties;

    //
    {
        ZoneScopedN("BindDescriptorBuffers");
        std::array bindings{resourceManager->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo(), resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo()};
        std::array indices{0u, 1u};
        std::array<VkDeviceSize, 2> offsets{0, 0};
        vkCmdBindDescriptorBuffersEXT(cmd, bindings.size(), bindings.data());
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, globalPipelineLayout.handle, 0, bindings.size(), indices.data(), offsets.data());
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, globalPipelineLayout.handle, 0, bindings.size(), indices.data(), offsets.data());
    } {
        ZoneScopedN("SetupUniforms");
        UploadFrameUniforms(viewFamily, renderExtent, frameBuffer.timeFrame.renderDeltaTime);
        UploadModelUniforms(viewFamily, renderFamilyProperties);
    } {
        ZoneScopedN("ImportBuffers");
        renderGraph->ImportBufferNoBarrier(SID("vertex_buffer"), resourceManager->megaVertexBuffer.handle, resourceManager->megaVertexBuffer.address,
                                           {resourceManager->megaVertexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
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

    // Readback that will be copied into the FIF host memory at the end of the frame (to be read in N+3 frames)
    renderGraph->CreateBuffer(SID("readback_buffer"), sizeof(ReadbackStruct));
    RenderPass& clearReadbackBuffer = renderGraph->AddPass(SID("Clear Readback Buffer"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    clearReadbackBuffer.WriteTransferBuffer(SID("readback_buffer"));
    clearReadbackBuffer.Execute([&](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, renderGraph->GetBufferHandle(SID("readback_buffer")), 0, VK_WHOLE_SIZE, 0);
    });

    // Main view G-buffer
    GBufferTargets targets{
        SID("albedo_target"),
        SID("normal_target"),
        SID("pbr_target"),
        SID("emissive_target"),
        SID("velocity_target"),
        SID("depth_target"),
        SID("deferred_resolve_target")
    };
    renderGraph->CreateTexture(SID("deferred_resolve_target"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
    GBufferTargets portalTargets{
        SID("portal_albedo"),
        SID("portal_normal"),
        SID("portal_pbr"),
        SID("portal_emissive"),
        SID("portal_velocity"),
        SID("portal_depth"),
        SID("portal_deferred_resolve")
    };
    renderGraph->CreateTexture(SID("portal_deferred_resolve"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    //
    {
        ZoneScopedN("SetupRenderGraph");

        RenderPass& clearDeferredImagePass = renderGraph->AddPass(SID("Clear Deferred Images"), VK_PIPELINE_STAGE_2_CLEAR_BIT);
        clearDeferredImagePass.WriteClearImage(targets.outFinalColor);
        clearDeferredImagePass.WriteClearImage(portalTargets.outFinalColor);
        clearDeferredImagePass.Execute([&](VkCommandBuffer _cmd) {
            constexpr VkClearColorValue clearColor = {0.0f, 0.1f, 0.2f, 1.0f};
            VkImageSubresourceRange colorSubresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

            VkImage mainImg = renderGraph->GetImageHandle(targets.outFinalColor);
            vkCmdClearColorImage(_cmd, mainImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &colorSubresource);

            VkImage portalImg = renderGraph->GetImageHandle(portalTargets.outFinalColor);
            vkCmdClearColorImage(_cmd, portalImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &colorSubresource);
        });

        if (renderFamilyProperties.bHasGeometry) {
            if (renderFamilyProperties.bHasShadows) {
                SetupCascadedShadows(*renderGraph, viewFamily, renderFamilyProperties, 0);
            }

            renderGraph->CreateTexture(targets.albedo, TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.normal, TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.pbr, TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.emissive, TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.velocity, TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

            SetupGeometryPasses(*renderGraph, viewFamily, renderFamilyProperties, renderExtent, targets, 0, true);


            if (renderFamilyProperties.bHasGTAO) {
                SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent, targets, 0);
            }

            if (renderFamilyProperties.bHasShadows || renderFamilyProperties.bHasGTAO) {
                SetupShadowsResolve(*renderGraph, viewFamily, renderExtent, targets, 0);
            }

            if (renderFamilyProperties.bHasDeferred) {
                SetupDeferredLighting(*renderGraph, viewFamily, renderExtent, targets, 0);
            }

            if (renderFamilyProperties.bHasSkybox) {
                SetupSkyboxRendering(*renderGraph, viewFamily, renderExtent, targets, 0);
            }

            bool bHasPortalView = !viewFamily.portalViews.empty();
            if (bHasPortalView) {
                renderGraph->CreateTexture(portalTargets.albedo, TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1});
                renderGraph->CreateTexture(portalTargets.normal, TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1});
                renderGraph->CreateTexture(portalTargets.pbr, TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1});
                renderGraph->CreateTexture(portalTargets.emissive, TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1});
                renderGraph->CreateTexture(portalTargets.velocity, TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1});
                renderGraph->CreateTexture(portalTargets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

                SetupGeometryPasses(*renderGraph, viewFamily, renderFamilyProperties, renderExtent, portalTargets, 1, true);

                if (renderFamilyProperties.bHasGTAO) {
                    SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
                }

                if (renderFamilyProperties.bHasShadows || renderFamilyProperties.bHasGTAO) {
                    SetupShadowsResolve(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
                }

                if (renderFamilyProperties.bHasDeferred) {
                    SetupDeferredLighting(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
                }

                SetupPortalComposite(*renderGraph, viewFamily, renderExtent, targets, portalTargets);
            }
        }

        PostProcessTargets ppTargets{targets.outFinalColor, targets.velocity, targets.depthStencil};
        PostProcessTargets taaTargets{targets.outFinalColor, targets.velocity, targets.depthStencil};
        StringID finalOutput = targets.outFinalColor;
        if (renderFamilyProperties.bHasGeometry) {
            bool bHasTAAPass = pipelineManager->IsCategoryReady(PipelineCategory::TAA) && viewFamily.postProcessConfig.bEnableTemporalAntialiasing;
            if (bHasTAAPass) {
                taaTargets.finalColor = SetupTemporalAntialiasing(*renderGraph, viewFamily, renderExtent, ppTargets);
            }

            bool bHasPostProcess = pipelineManager->IsCategoryReady(PipelineCategory::PostProcess);
            if (bHasPostProcess) {
                finalOutput = SetupPostProcessing(*renderGraph, viewFamily, renderExtent, taaTargets, frameBuffer.timeFrame.renderDeltaTime);
            }
        }


        bool bHasDebugRender = pipelineManager->IsCategoryReady(PipelineCategory::DebugRendering);
        if (bHasDebugRender) {
            SetupDebugRender(*renderGraph, viewFamily, renderExtent, targets.depthStencil, finalOutput, frameResourceLimits);
        }


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


        bool bHasDebugPass = !viewFamily.debugResourceName.empty() && pipelineManager->IsCategoryReady(PipelineCategory::Debug);
        if (bHasDebugPass) {
            StringID debugTargetName = StringID(viewFamily.debugResourceName.c_str(), viewFamily.debugResourceName.size());

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

                    StorageImageType storageType = GetStorageImageType(dims.format, viewAspect);
                    uint32_t textureArrayIndex{3};
                    switch (storageType) {
                        case StorageImageType::Float4:
                            textureArrayIndex = 0;
                            break;
                        case StorageImageType::Float2:
                            textureArrayIndex = 1;
                            break;
                        case StorageImageType::Float:
                            textureArrayIndex = 2;
                            break;
                        case StorageImageType::UInt4:
                            textureArrayIndex = 0;
                            break;
                        case StorageImageType::UInt:
                            textureArrayIndex = 2;
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

        renderGraph->ImportTexture(SID("swapchain_image"), currentSwapchainImage, currentSwapchainImageView, TextureInfo{swapchain->format, swapchain->extent.width, swapchain->extent.height, 1},
                                   swapchain->usages,
                                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED);

        auto& blitPass = renderGraph->AddPass(SID("Blit To Swapchain"), VK_PIPELINE_STAGE_2_BLIT_BIT);
        blitPass.ReadBlitImage(finalOutput);
        blitPass.WriteBlitImage(SID("swapchain_image"));
        blitPass.Execute([&, finalOutput](VkCommandBuffer _cmd) {
            VkImage drawImage = renderGraph->GetImageHandle(finalOutput);

            std::array<uint32_t, 2> scaledExtent = renderExtents->GetScaledExtent();
            std::array<uint32_t, 2> vpOffset = renderExtents->GetViewportOffset();
            std::array<uint32_t, 2> vpExtent = renderExtents->GetViewportExtent();

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

void RenderThread::ProcessAcquisitions(VkCommandBuffer cmd,
                                       const std::vector<Core::BufferAcquireOperation>& bufferAcquireOperations,
                                       const std::vector<Core::ImageAcquireOperation>& imageAcquireOperations)
{
    ZoneScoped;
    if (bufferAcquireOperations.empty() && imageAcquireOperations.empty()) {
        return;
    }

    tempBufferBarriers.clear();
    tempBufferBarriers.reserve(bufferAcquireOperations.size());
    for (const auto& op : bufferAcquireOperations) {
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
    tempImageBarriers.reserve(imageAcquireOperations.size());
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
}

void RenderThread::CreatePipelines()
{
    std::array layouts{
        resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pSetLayouts = layouts.data();
    layoutInfo.setLayoutCount = layouts.size();
    globalPipelineLayout = PipelineLayout::CreatePipelineLayout(context.get(), layoutInfo);

    pipelineManager->RegisterComputePipeline(SID("instancing_instance_lod"), Platform::GetShaderPath() / "instancing_instance_lod_compute.spv",
                                             sizeof(InstanceLODPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_prefix_sum_up_1"), Platform::GetShaderPath() / "instancing_prefix_sum_up_1_compute.spv",
                                             sizeof(PrefixSumUpsweep1PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_prefix_sum_up_2"), Platform::GetShaderPath() / "instancing_prefix_sum_up_2_compute.spv",
                                             sizeof(PrefixSumUpsweep2PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_scan_blocks"), Platform::GetShaderPath() / "instancing_scan_blocks_compute.spv",
                                             sizeof(PrefixSumScanBlocksPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_prefix_sum_down_1"), Platform::GetShaderPath() / "instancing_prefix_sum_down_1_compute.spv",
                                             sizeof(PrefixSumDownsweep1PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_prefix_sum_down_2"), Platform::GetShaderPath() / "instancing_prefix_sum_down_2_compute.spv",
                                             sizeof(PrefixSumDownsweep2PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_total_meshlet_count"), Platform::GetShaderPath() / "instancing_total_meshlet_count_compute.spv",
                                             sizeof(TotalMeshletCountPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_expand_instance_to_meshlet"), Platform::GetShaderPath() / "instancing_expand_instance_to_meshlet_compute.spv",
                                             sizeof(ExpandMeshletsPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_meshlet_visibility_prefix_sum_up_1"), Platform::GetShaderPath() / "instancing_meshlet_visibility_prefix_sum_up_1_compute.spv",
                                             sizeof(MeshletVisibilityPrefixSumUpsweep1PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_meshlet_visibility_prefix_sum_down_2"), Platform::GetShaderPath() / "instancing_meshlet_visibility_prefix_sum_down_2_compute.spv",
                                             sizeof(MeshletVisibilityPrefixSumDownsweep2PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_compacted_meshlet_dispatch"), Platform::GetShaderPath() / "instancing_compacted_meshlet_dispatch_compute.spv",
                                             sizeof(CompactedMeshletDispatchPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_max_meshlet_count"), Platform::GetShaderPath() / "instancing_max_meshlet_count_compute.spv",
                                             sizeof(MaxMeshletCountPushConstant), PipelineCategory::Instancing);


    pipelineManager->RegisterComputePipeline(SID("instancing_instance_lod_shadows"), Platform::GetShaderPath() / "instancing_instance_lod_shadows_compute.spv",
                                             sizeof(InstanceLODShadowsPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline(SID("instancing_expand_instance_to_meshlet_shadows"), Platform::GetShaderPath() / "instancing_expand_instance_to_meshlet_shadows_compute.spv",
                                             sizeof(ExpandMeshletsShadowsPushConstant), PipelineCategory::Instancing);

    pipelineManager->RegisterComputePipeline(SID("shadows_resolve"), Platform::GetShaderPath() / "shadows_resolve_compute.spv",
                                             sizeof(ShadowsResolvePushConstant), PipelineCategory::ShadowCombine);
    pipelineManager->RegisterComputePipeline(SID("deferred_resolve"), Platform::GetShaderPath() / "deferred_resolve_compute.spv",
                                             sizeof(DeferredResolvePushConstant), PipelineCategory::DeferredShading);

    pipelineManager->RegisterComputePipeline(SID("temporal_antialiasing"), Platform::GetShaderPath() / "temporal_antialiasing_compute.spv",
                                             sizeof(TemporalAntialiasingPushConstant), PipelineCategory::TAA);

    pipelineManager->RegisterComputePipeline(SID("gtao_depth_prepass"), Platform::GetShaderPath() / "gtao_depth_prepass_compute.spv",
                                             sizeof(GTAODepthPrepassPushConstant), PipelineCategory::GTAO);
    pipelineManager->RegisterComputePipeline(SID("gtao_main"), Platform::GetShaderPath() / "gtao_main_compute.spv",
                                             sizeof(GTAOMainPushConstant), PipelineCategory::GTAO);
    pipelineManager->RegisterComputePipeline(SID("gtao_denoise"), Platform::GetShaderPath() / "gtao_denoise_compute.spv",
                                             sizeof(GTAODenoisePushConstant), PipelineCategory::GTAO);


    pipelineManager->RegisterComputePipeline(SID("exposure_build_histogram"), Platform::GetShaderPath() / "exposure_build_histogram_compute.spv",
                                             sizeof(HistogramBuildPushConstant), PipelineCategory::Exposure);
    pipelineManager->RegisterComputePipeline(SID("exposure_calculate_average"), Platform::GetShaderPath() / "exposure_calculate_average_compute.spv",
                                             sizeof(ExposureCalculatePushConstant), PipelineCategory::Exposure);
    pipelineManager->RegisterComputePipeline(SID("tonemap_sdr"), Platform::GetShaderPath() / "tonemap_sdr_compute.spv",
                                             sizeof(TonemapSDRPushConstant), PipelineCategory::Tonemap);
    pipelineManager->RegisterComputePipeline(SID("motion_blur_tile_max"), Platform::GetShaderPath() / "motion_blur_tile_max_compute.spv",
                                             sizeof(MotionBlurTileVelocityPushConstant), PipelineCategory::MotionBlur);
    pipelineManager->RegisterComputePipeline(SID("motion_blur_neighbor_max"), Platform::GetShaderPath() / "motion_blur_neighbor_max_compute.spv",
                                             sizeof(MotionBlurNeighborMaxPushConstant), PipelineCategory::MotionBlur);
    pipelineManager->RegisterComputePipeline(SID("motion_blur_reconstruction"), Platform::GetShaderPath() / "motion_blur_reconstruction_compute.spv",
                                             sizeof(MotionBlurReconstructionPushConstant), PipelineCategory::MotionBlur);
    pipelineManager->RegisterComputePipeline(SID("bloom_threshold"), Platform::GetShaderPath() / "bloom_threshold_compute.spv",
                                             sizeof(BloomThresholdPushConstant), PipelineCategory::Bloom);
    pipelineManager->RegisterComputePipeline(SID("bloom_downsample"), Platform::GetShaderPath() / "bloom_downsample_compute.spv",
                                             sizeof(BloomDownsamplePushConstant), PipelineCategory::Bloom);
    pipelineManager->RegisterComputePipeline(SID("bloom_upsample"), Platform::GetShaderPath() / "bloom_upsample_compute.spv",
                                             sizeof(BloomUpsamplePushConstant), PipelineCategory::Bloom);
    pipelineManager->RegisterComputePipeline(SID("vignette_aberration"), Platform::GetShaderPath() / "vignette_aberration_compute.spv",
                                             sizeof(VignetteChromaticAberrationPushConstant), PipelineCategory::Vignette);
    pipelineManager->RegisterComputePipeline(SID("film_grain"), Platform::GetShaderPath() / "film_grain_compute.spv",
                                             sizeof(FilmGrainPushConstant), PipelineCategory::FilmGrain);
    pipelineManager->RegisterComputePipeline(SID("sharpening"), Platform::GetShaderPath() / "sharpening_compute.spv",
                                             sizeof(SharpeningPushConstant), PipelineCategory::Sharpening);
    pipelineManager->RegisterComputePipeline(SID("color_grading"), Platform::GetShaderPath() / "color_grading_compute.spv",
                                             sizeof(ColorGradingPushConstant), PipelineCategory::ColorGrade);

    pipelineManager->RegisterComputePipeline(SID("debug_visualize"), Platform::GetShaderPath() / "debug_visualize_compute.spv",
                                             sizeof(DebugVisualizePushConstant), PipelineCategory::Debug);

#if WILL_EDITOR
    std::vector emapLayout{
        resourceManager->environmentMapGenerateResources.descriptorSetLayout.handle,
    };
    pipelineManager->RegisterComputePipelineCustomLayout(SID("ibl_equirect_to_cubemap"), Platform::GetShaderPath() / "ibl_equirect_to_cubemap_compute.spv",
                                                         sizeof(EquirectToCubemapPushConstant), PipelineCategory::AssetGeneration, emapLayout);

    pipelineManager->RegisterComputePipelineCustomLayout(SID("ibl_convolve_diffuse"), Platform::GetShaderPath() / "ibl_convolve_diffuse_compute.spv",
                                                         sizeof(ConvolveDiffusePushConstant), PipelineCategory::AssetGeneration, emapLayout);

    pipelineManager->RegisterComputePipelineCustomLayout(SID("ibl_prefilter_specular"), Platform::GetShaderPath() / "ibl_prefilter_specular_compute.spv",
                                                         sizeof(PrefilterSpecularPushConstant), PipelineCategory::AssetGeneration, emapLayout);

    std::vector brdfLutLayout{
        resourceManager->brdfLutGenerateResources.descriptorSetLayout.handle,
    };
    pipelineManager->RegisterComputePipelineCustomLayout(SID("ibl_brdf_lut"), Platform::GetShaderPath() / "brdf_lut_generate_compute.spv",
                                                         sizeof(BRDFLUTPushConstant), PipelineCategory::AssetGeneration, brdfLutLayout);
#endif

    GraphicsPipelineBuilder builder;

    // Shadow cascade pipeline
    {
        builder.AddShaderStage("shaders/shadow_mesh_shading_instanced_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
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
            PipelineCategory::Shadow
        );
        builder.Clear();
    }

    // Instanced mesh shading pipeline
    {
        builder.AddShaderStage("shaders/mesh_shading_instanced_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage("shaders/mesh_shading_instanced_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);

        VkFormat colorFormats[5] = {
            GBUFFER_ALBEDO_FORMAT,
            GBUFFER_NORMAL_FORMAT,
            GBUFFER_PBR_FORMAT,
            GBUFFER_EMISSIVE_FORMAT,
            GBUFFER_MOTION_FORMAT
        };
        builder.SetupRenderer(colorFormats, 5, DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);

        pipelineManager->RegisterGraphicsPipeline(
            SID("mesh_shading_instanced"),
            builder,
            sizeof(BaseMeshShadingPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Geometry
        );
        builder.Clear();
    }

    // Portal Graphics Pipeline
    {
        builder.AddShaderStage("shaders/mesh_shading_instanced_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage("shaders/mesh_shading_instanced_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        builder.SetupStencilState(VK_TRUE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);

        VkFormat colorFormats[5] = {
            GBUFFER_ALBEDO_FORMAT,
            GBUFFER_NORMAL_FORMAT,
            GBUFFER_PBR_FORMAT,
            GBUFFER_EMISSIVE_FORMAT,
            GBUFFER_MOTION_FORMAT
        };
        builder.SetupRenderer(colorFormats, 5, DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);
        builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

        pipelineManager->RegisterGraphicsPipeline(
            SID("portal_rendering"),
            builder,
            sizeof(BaseMeshShadingPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::CustomRendering
        );
        builder.Clear();
    }

    // Portal Composite
    {
        builder.AddShaderStage("shaders/fullscreen_pass_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage("shaders/portal_composite_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_ALWAYS);
        builder.SetupStencilState(VK_TRUE, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL);

        VkFormat colorFormats[2] = {
            COLOR_ATTACHMENT_FORMAT,
            GBUFFER_MOTION_FORMAT
        };
        builder.SetupRenderer(colorFormats, 2, DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);
        builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

        pipelineManager->RegisterGraphicsPipeline(
            SID("portal_composite"),
            builder,
            sizeof(PortalCompositePushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::CustomRendering
        );
        builder.Clear();
    }

    // Cubemap Visualizer
    {
        builder.AddShaderStage("shaders/cubemap_visualizer_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage("shaders/cubemap_visualizer_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);

        VkFormat colorFormats[5] = {
            GBUFFER_ALBEDO_FORMAT,
            GBUFFER_NORMAL_FORMAT,
            GBUFFER_PBR_FORMAT,
            GBUFFER_EMISSIVE_FORMAT,
            GBUFFER_MOTION_FORMAT
        };
        builder.SetupRenderer(colorFormats, 5, DEPTH_ATTACHMENT_FORMAT, DEPTH_ATTACHMENT_FORMAT);

        pipelineManager->RegisterGraphicsPipeline(
            SID("cubemap_visualize"),
            builder,
            sizeof(BaseMeshShadingPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::DebugRendering
        );
        builder.Clear();
    }


    // Skybox Rendering
    {
        builder.AddShaderStage("shaders/fullscreen_pass_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage("shaders/environment_map_skybox_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
            PipelineCategory::EnvironmentMap
        );
        builder.Clear();
    }

    // Debug Render
    {
        builder.AddShaderStage("shaders/debug_render_vertex.spv", VK_SHADER_STAGE_VERTEX_BIT);
        builder.AddShaderStage("shaders/debug_render_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
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
            VK_SHADER_STAGE_VERTEX_BIT,
            PipelineCategory::DebugRendering
        );
        builder.Clear();
    }
}

void RenderThread::PrepareRenderFamilyProperties(Core::ViewFamily& viewFamily, ReadbackStruct* readbackData, RenderFamilyProperties& renderFamilyProperties, PipelineManager* _pipelineManager,
                                                 FrameResourceLimits& _limits)
{
    renderFamilyProperties.Reset();
    renderFamilyProperties.viewFamily = &viewFamily;
    bool bHasGeometry = !viewFamily.mainPassInstances.empty();
    if (!bHasGeometry) {
        for (const auto& [key, customDraw] : viewFamily.customShaderDraws) {
            if (!customDraw.instances.empty()) {
                bHasGeometry = true;
                break;
            }
        }
    }
    renderFamilyProperties.bHasGeometry = bHasGeometry && _pipelineManager->IsCategoryReady(PipelineCategory::Geometry | PipelineCategory::Instancing | PipelineCategory::CustomRendering);
    renderFamilyProperties.bHasGTAO = viewFamily.gtaoConfig.bEnabled && _pipelineManager->IsCategoryReady(PipelineCategory::GTAO);
    renderFamilyProperties.bHasShadows = viewFamily.shadowConfig.enabled && _pipelineManager->IsCategoryReady(PipelineCategory::ShadowPass);
    renderFamilyProperties.bHasDeferred = _pipelineManager->IsCategoryReady(PipelineCategory::DeferredShading);
    renderFamilyProperties.bHasSkybox = viewFamily.skyboxIndex != -1 && _pipelineManager->IsCategoryReady(PipelineCategory::EnvironmentMap);

    if (!viewFamily.mainPassInstances.empty()) {
        std::ranges::sort(viewFamily.mainPassInstances, [](const Core::InstanceData& a, const Core::InstanceData& b) {
            return a.primitiveIndex < b.primitiveIndex;
        });
    }


    _limits.highestModelBuffer = std::max(_limits.highestModelBuffer, NextPowerOfTwo(viewFamily.modelMatrices.size()));
    _limits.highestMaterialBuffer = std::max(_limits.highestMaterialBuffer, NextPowerOfTwo(viewFamily.materials.size()));


    uint32_t totalInstanceCountThisFrame = viewFamily.mainPassInstances.size();
    for (const auto& [key, customDraw] : viewFamily.customShaderDraws) {
        totalInstanceCountThisFrame += customDraw.instances.size();
    }
    _limits.highestInstanceBuffer = std::max(_limits.highestInstanceBuffer, NextPowerOfTwo(totalInstanceCountThisFrame));
    _limits.highestMeshletCount = std::max(_limits.highestMeshletCount, NextPowerOfTwo(readbackData->meshletCount));


    renderFamilyProperties.modelBufferSize = _limits.highestModelBuffer * sizeof(Model);
    renderFamilyProperties.materialBufferSize = _limits.highestMaterialBuffer * sizeof(MaterialProperties);
    renderFamilyProperties.instanceBufferSize = _limits.highestInstanceBuffer * sizeof(Instance);


    renderFamilyProperties.instanceMeshletOffsetsBufferSize = _limits.highestInstanceBuffer * sizeof(InstanceMeshletOffsetPrefixSum);
    uint32_t level1BlockCount = (_limits.highestInstanceBuffer + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
    uint32_t level2BlockCount = (level1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
    renderFamilyProperties.level1SumsBufferSize = _limits.highestInstanceBuffer * sizeof(uint32_t);
    renderFamilyProperties.level1BlockSumsBufferSize = level1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.level2SumsBufferSize = level1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.level2BlockSumsBufferSize = level2BlockCount * sizeof(uint32_t);
    renderFamilyProperties.scannedLevel2BlockSumsBufferSize = glm::max(level2BlockCount, INSTANCING_PREFIX_SUM_DISPATCH_X) * sizeof(uint32_t);

    renderFamilyProperties.intermediateMeshletBufferSize = _limits.highestMeshletCount * sizeof(IntermediateMeshlet);
    uint32_t meshletLevel1BlockCount = (_limits.highestMeshletCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;
    uint32_t meshletLevel2BlockCount = (meshletLevel1BlockCount + INSTANCING_PREFIX_SUM_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_DISPATCH_X;

    renderFamilyProperties.meshletLevel1SumsBufferSize = _limits.highestMeshletCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel1BlockSumsBufferSize = meshletLevel1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel2SumsBufferSize = meshletLevel1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel2BlockSumsBufferSize = meshletLevel2BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletScannedLevel2BlockSumsBufferSize = glm::max(meshletLevel2BlockCount, INSTANCING_PREFIX_SUM_DISPATCH_X) * sizeof(uint32_t);

    renderFamilyProperties.visibleMeshletsBufferSize = _limits.highestMeshletCount * sizeof(CompactedMeshlet);


    renderFamilyProperties.visibleMeshletUpperBound = _limits.highestMeshletCount;
}

void RenderThread::UploadFrameUniforms(const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, float renderDeltaTime) const
{
    renderGraph->CreateBuffer(SID("scene_data"), SCENE_DATA_BUFFER_SIZE);
    renderGraph->CreateBuffer(SID("shadow_data"), SHADOW_DATA_BUFFER_SIZE);
    renderGraph->CreateBuffer(SID("light_data"), LIGHT_DATA_BUFFER_SIZE);

    // Scene Data
    SceneData sceneData = GenerateSceneData(viewFamily.mainView, viewFamily.postProcessConfig, renderExtent, frameNumber, renderDeltaTime);
    UploadAllocation sceneDataUploadAllocation = renderGraph->AllocateTransient(sizeof(SceneData));
    memcpy(sceneDataUploadAllocation.ptr, &sceneData, sizeof(SceneData));
    // Portal Scene Data
    UploadAllocation portalSceneDataUploadAllocation{};
    bool bHasPortal = !viewFamily.portalViews.empty();
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
                static_cast<float>(shadowConfig.cascadePreset.extents[i].width),
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
            std::array<VkBufferCopy2, 2> sceneDataRegions{};
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
                .pRegions = sceneDataRegions.data()
            };
            vkCmdCopyBuffer2(cmd, &sceneDataCopyInfo);

            std::array<VkBufferCopy2, 1> shadowDataRegions{};
            shadowDataRegions[0].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            shadowDataRegions[0].srcOffset = shadowOffset;
            shadowDataRegions[0].dstOffset = 0;
            shadowDataRegions[0].size = sizeof(ShadowData);
            const VkCopyBufferInfo2 shadowDataCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(SID("shadow_data")),
                .regionCount = shadowDataRegions.size(),
                .pRegions = shadowDataRegions.data()
            };
            vkCmdCopyBuffer2(cmd, &shadowDataCopyInfo);

            std::array<VkBufferCopy2, 1> lightDataRegions{};
            lightDataRegions[0].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            lightDataRegions[0].srcOffset = lightOffset;
            lightDataRegions[0].dstOffset = 0;
            lightDataRegions[0].size = sizeof(LightData);
            const VkCopyBufferInfo2 lightDataCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle(SID("light_data")),
                .regionCount = lightDataRegions.size(),
                .pRegions = lightDataRegions.data()
            };
            vkCmdCopyBuffer2(cmd, &lightDataCopyInfo);
        });
}

void RenderThread::UploadModelUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    size_t totalInstanceCount = viewFamily.mainPassInstances.size();

    for (auto& [key, customDraw] : viewFamily.customShaderDraws) {
        customDraw.instanceBufferOffset = static_cast<uint32_t>(totalInstanceCount * sizeof(Instance));
        totalInstanceCount += customDraw.instances.size();
    }

    UploadAllocation instanceUpload = renderGraph->AllocateTransient(totalInstanceCount * sizeof(Instance));
    auto* instanceBuffer = static_cast<Instance*>(instanceUpload.ptr);

    for (size_t i = 0; i < viewFamily.mainPassInstances.size(); ++i) {
        auto& inst = viewFamily.mainPassInstances[i];
        instanceBuffer[i] = {
            .primitiveIndex = inst.primitiveIndex,
            .modelIndex = inst.modelIndex,
            .materialIndex = inst.gpuMaterialIndex,
        };
    }

    for (auto& [key, customDraw] : viewFamily.customShaderDraws) {
        size_t startIndex = customDraw.instanceBufferOffset / sizeof(Instance);
        for (size_t i = 0; i < customDraw.instances.size(); ++i) {
            auto& inst = customDraw.instances[i];
            instanceBuffer[startIndex + i] = {
                .primitiveIndex = inst.primitiveIndex,
                .modelIndex = inst.modelIndex,
                .materialIndex = inst.gpuMaterialIndex,
            };
        }
    }

    if (totalInstanceCount > 0) {
        renderGraph->CreateBuffer(GEOMETRY_INSTANCE_BUFFER, totalInstanceCount * sizeof(Instance));

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


    if (!viewFamily.modelMatrices.empty()) {
        renderGraph->CreateBuffer(SID("model_buffer"), renderFamilyProperties.modelBufferSize);
        UploadAllocation modelUpload = renderGraph->AllocateTransient(viewFamily.modelMatrices.size() * sizeof(Model));
        memcpy(modelUpload.ptr, viewFamily.modelMatrices.data(), viewFamily.modelMatrices.size() * sizeof(Model));

        RenderPass& uploadModelMatricesPass = renderGraph->AddPass(SID("Upload Model Matrices"), VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadModelMatricesPass.WriteTransferBuffer(SID("model_buffer"));
        uploadModelMatricesPass.Execute([&,
                modelOffset = modelUpload.offset,
                modelSize = viewFamily.modelMatrices.size() * sizeof(Model)](VkCommandBuffer cmd) {
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

    if (!viewFamily.materials.empty()) {
        renderGraph->CreateBuffer(SID("material_buffer"), renderFamilyProperties.materialBufferSize);
        UploadAllocation materialUpload = renderGraph->AllocateTransient(viewFamily.materials.size() * sizeof(MaterialProperties));
        memcpy(materialUpload.ptr, viewFamily.materials.data(), viewFamily.materials.size() * sizeof(MaterialProperties));

        RenderPass& uploadMaterialsPass = renderGraph->AddPass(SID("Upload Materials"), VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadMaterialsPass.WriteTransferBuffer(SID("material_buffer"));
        uploadMaterialsPass.Execute([&,
                materialOffset = materialUpload.offset,
                materialSize = viewFamily.materials.size() * sizeof(MaterialProperties)](VkCommandBuffer cmd) {
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
        std::string shadowMapName = "shadow_cascade_" + std::to_string(cascadeLevel);
        std::string shadowPassName = "Shadow Cascade Pass " + std::to_string(cascadeLevel);

        StringID shadowMapId = StringID(shadowMapName.c_str(), shadowMapName.size());
        StringID shadowPassId = StringID(shadowPassName.c_str(), shadowPassName.size());


        uint32_t cascadeWidth = shadowConfig.cascadePreset.extents[cascadeLevel].width;
        uint32_t cascadeHeight = shadowConfig.cascadePreset.extents[cascadeLevel].height;
        float linearBias = shadowConfig.cascadePreset.biases[cascadeLevel].linear;
        float slopedBias = shadowConfig.cascadePreset.biases[cascadeLevel].sloped;
        graph.CreateTexture(shadowMapId, TextureInfo{SHADOW_CASCADE_FORMAT, shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height, 1});

        // Main Draw
        if (!viewFamily.mainPassInstances.empty()) {
            InstancedGeometryPassConfig mainConfig{
                .prefix = "main_shadow",
                .instanceCount = static_cast<uint32_t>(viewFamily.mainPassInstances.size()),
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

            InstancedGeometryPassOutputs mainOutputs = SetupInstancedGeometryShadowPass(graph, mainConfig, pipelineManager.get(), sceneIndex, cascadeLevel, false);

            RenderPass& shadowPass = graph.AddPass(shadowPassId, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
            shadowPass.ReadWriteDepthAttachment(shadowMapId);
            shadowPass.ReadBuffer(SCENE_DATA_BUFFER);
            shadowPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
            shadowPass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
            shadowPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
            shadowPass.ReadBuffer(mainOutputs.visibleMeshlets);
            shadowPass.ReadIndirectBuffer(mainOutputs.compactedDispatchArgs);
            shadowPass.Execute([&, mainOutputs, sceneIndex, shadowMapName, cascadeLevel, cascadeWidth, cascadeHeight, linearBias, slopedBias, shadowMapId](VkCommandBuffer cmd) {
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
                    .vertexBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_BUFFER),
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
            });
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

            InstancedGeometryPassOutputs customOutputs = SetupInstancedGeometryShadowPass(graph, customConfig, pipelineManager.get(), sceneIndex, cascadeLevel, false);

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
                        .vertexBuffer = graph.GetBufferAddress("vertex_buffer"),
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

void RenderThread::SetupGeometryPasses(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties,
                                       std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneIndex, bool bClearTargets) const
{
    if (bClearTargets) {
        RenderPass& clearPass = graph.AddPass(SID("Clear GBuffer"), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        clearPass.WriteColorAttachment(targets.albedo);
        clearPass.WriteColorAttachment(targets.normal);
        clearPass.WriteColorAttachment(targets.pbr);
        clearPass.WriteColorAttachment(targets.emissive);
        clearPass.WriteColorAttachment(targets.velocity);
        clearPass.WriteDepthAttachment(targets.depthStencil);
        clearPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            constexpr VkClearValue colorClear = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}};
            constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};

            const VkRenderingAttachmentInfo albedoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.albedo), &colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo normalAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.normal), &colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo pbrAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.pbr), &colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo emissiveTarget = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.emissive), &colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.velocity), &colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), &depthClear,
                                                                                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), &depthClear,
                                                                                                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            const VkRenderingAttachmentInfo colorAttachments[] = {albedoAttachment, normalAttachment, pbrAttachment, emissiveTarget, velocityAttachment};
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 5, &depthAttachment, &stencilAttachment);

            vkCmdBeginRendering(cmd, &renderInfo);
            vkCmdEndRendering(cmd);
        });
    }


    // Main Draw
    if (!viewFamily.mainPassInstances.empty()) {
        InstancedGeometryPassConfig mainConfig{
            .prefix = "main",
            .instanceCount = static_cast<uint32_t>(viewFamily.mainPassInstances.size()),
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

        InstancedGeometryPassOutputs mainOutputs = SetupInstancedGeometryPass(graph, mainConfig, pipelineManager.get(), sceneIndex);

        RenderPass& instancedMeshShading = graph.AddPass(SID("Instanced Mesh Shading"), VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        instancedMeshShading.WriteColorAttachment(targets.albedo);
        instancedMeshShading.WriteColorAttachment(targets.normal);
        instancedMeshShading.WriteColorAttachment(targets.pbr);
        instancedMeshShading.WriteColorAttachment(targets.emissive);
        instancedMeshShading.WriteColorAttachment(targets.velocity);
        instancedMeshShading.ReadWriteDepthAttachment(targets.depthStencil);
        instancedMeshShading.ReadBuffer(SCENE_DATA_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_MODEL_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
        instancedMeshShading.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        instancedMeshShading.ReadBuffer(mainOutputs.visibleMeshlets);
        instancedMeshShading.ReadIndirectBuffer(mainOutputs.compactedDispatchArgs);
        instancedMeshShading.Execute([&, mainOutputs, sceneIndex, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            VkViewport viewport = VkHelpers::GenerateViewport(width, height);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            const VkRenderingAttachmentInfo albedoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.albedo), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo normalAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.normal), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo pbrAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.pbr), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo emissiveTarget = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.emissive), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.velocity), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr,
                                                                                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr,
                                                                                                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            const VkRenderingAttachmentInfo colorAttachments[] = {albedoAttachment, normalAttachment, pbrAttachment, emissiveTarget, velocityAttachment};
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 5, &depthAttachment, &stencilAttachment);

            vkCmdBeginRendering(cmd, &renderInfo);

            BaseMeshShadingPushConstant pushConstants{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .vertexBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_BUFFER),
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
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("mesh_shading_instanced"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(BaseMeshShadingPushConstant), &pushConstants);

            vkCmdDrawMeshTasksIndirectEXT(
                cmd,
                graph.GetBufferHandle(mainOutputs.compactedDispatchArgs),
                offsetof(InstancingCompactedMeshletDispatchIndirect, x),
                1,
                sizeof(InstancingCompactedMeshletDispatchIndirect));

            vkCmdEndRendering(cmd);
        });
    }

    for (const auto& customDraw : viewFamily.customShaderDraws) {
        InstancedGeometryPassConfig customConfig{
            .prefix = customDraw.first,
            .instanceCount = static_cast<uint32_t>(customDraw.second.instances.size()),
            .instanceBufferOffset = customDraw.second.instanceBufferOffset,
            .visibleMeshletUpperBound = renderFamilyProperties.visibleMeshletUpperBound,
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

        InstancedGeometryPassOutputs customOutputs = SetupInstancedGeometryPass(graph, customConfig, pipelineManager.get(), sceneIndex);

        std::string customDrawName = "Custom Draw " + customDraw.first;
        StringID customDrawId = StringID(customDrawName.c_str(), customDrawName.size());
        RenderPass& customDrawPass = graph.AddPass(customDrawId, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        customDrawPass.WriteColorAttachment(targets.albedo);
        customDrawPass.WriteColorAttachment(targets.normal);
        customDrawPass.WriteColorAttachment(targets.pbr);
        customDrawPass.WriteColorAttachment(targets.emissive);
        customDrawPass.WriteColorAttachment(targets.velocity);
        customDrawPass.ReadWriteDepthAttachment(targets.depthStencil);
        customDrawPass.ReadBuffer(SCENE_DATA_BUFFER);
        customDrawPass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
        customDrawPass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
        customDrawPass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        customDrawPass.ReadBuffer(customOutputs.visibleMeshlets);
        customDrawPass.ReadIndirectBuffer(customOutputs.compactedDispatchArgs);
        customDrawPass.Execute(
            [&, customOutputs, customDraw, sceneIndex, width = renderExtent[0], height = renderExtent[1], instanceBufferOffset = customDraw.second.instanceBufferOffset](VkCommandBuffer cmd) {
                VkViewport viewport = VkHelpers::GenerateViewport(width, height);
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                const VkRenderingAttachmentInfo albedoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.albedo), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                const VkRenderingAttachmentInfo normalAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.normal), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                const VkRenderingAttachmentInfo pbrAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.pbr), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                const VkRenderingAttachmentInfo emissiveTarget = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.emissive), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                const VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.velocity), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr,
                                                                                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                const VkRenderingAttachmentInfo stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr,
                                                                                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

                const VkRenderingAttachmentInfo colorAttachments[] = {albedoAttachment, normalAttachment, pbrAttachment, emissiveTarget, velocityAttachment};
                const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 5, &depthAttachment, &stencilAttachment);

                vkCmdBeginRendering(cmd, &renderInfo);

                BaseMeshShadingPushConstant pushConstants{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .vertexBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_BUFFER),
                    .meshletVerticesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_VERTEX_BUFFER),
                    .meshletTrianglesBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_TRIANGLE_BUFFER),
                    .meshletBuffer = graph.GetBufferAddress(GEOMETRY_MESHLET_BUFFER),
                    .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                    .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER) + instanceBufferOffset,
                    .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                    .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                    .visibleMeshlets = graph.GetBufferAddress(customOutputs.visibleMeshlets),
                    .compactedDispatchBuffer = graph.GetBufferAddress(customOutputs.compactedDispatchArgs),
                    .sceneDataIndex = sceneIndex,
                };
                memcpy(pushConstants.customData, customDraw.second.pushConstantCustomData.data(), sizeof(pushConstants.customData));

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(customDraw.second.pipelineId);
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
    }
}

void RenderThread::SetupGroundTruthAmbientOcclusion(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets,
                                                    uint32_t sceneDataIndex) const
{
    const Core::GTAOConfiguration& gtaoConfig = viewFamily.gtaoConfig;

    graph.CreateTexture(SID("gtao_depth"), TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 5});
    graph.CreateTexture(SID("gtao_ao"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});
    graph.CreateTexture(SID("gtao_edges"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});
    // Denoise pass(es) - typically run 2-3 times for better quality
    graph.CreateTexture(SID("gtao_temp"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});
    graph.CreateTexture(SID("gtao_filtered"), TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});

    RenderPass& depthPrepass = graph.AddPass(SID("GTAO Depth Prepass"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    depthPrepass.ReadBuffer(SID("scene_data"));
    depthPrepass.ReadSampledImage(targets.depthStencil);
    depthPrepass.WriteStorageImage(SID("gtao_depth"));
    depthPrepass.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        GTAODepthPrepassPushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .inputDepth = graph.GetDepthOnlySampledImageViewDescriptorIndex(targets.depthStencil),
            .outputDepth0 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 0),
            .outputDepth1 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 1),
            .outputDepth2 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 2),
            .outputDepth3 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 3),
            .outputDepth4 = graph.GetStorageImageViewDescriptorIndex(SID("gtao_depth"), 4),
            .effectRadius = gtaoConfig.effectRadius,
            .effectFalloffRange = gtaoConfig.effectFalloffRange,
            .radiusMultiplier = gtaoConfig.radiusMultiplier,
            .sceneDataIndex = sceneDataIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_depth_prepass"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);


        uint32_t xDispatch = (width / 2 + GTAO_DEPTH_PREPASS_DISPATCH_X - 1) / GTAO_DEPTH_PREPASS_DISPATCH_X;
        uint32_t yDispatch = (height / 2 + GTAO_DEPTH_PREPASS_DISPATCH_Y - 1) / GTAO_DEPTH_PREPASS_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& gtaoMainPass = graph.AddPass(SID("GTAO Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    gtaoMainPass.ReadSampledImage(SID("gtao_depth"));
    gtaoMainPass.ReadSampledImage(targets.normal);
    gtaoMainPass.WriteStorageImage(SID("gtao_ao"));
    gtaoMainPass.WriteStorageImage(SID("gtao_edges"));
    gtaoMainPass.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        GTAOMainPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .prefilteredDepthIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_depth")),
            .normalBufferIndex = graph.GetSampledImageViewDescriptorIndex(targets.normal),
            .aoOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_ao")),
            .edgeDataIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_edges")),

            .effectRadius = gtaoConfig.effectRadius,
            .radiusMultiplier = gtaoConfig.radiusMultiplier,
            .effectFalloffRange = gtaoConfig.effectFalloffRange,
            .sampleDistributionPower = gtaoConfig.sampleDistributionPower,
            .thinOccluderCompensation = gtaoConfig.thinOccluderCompensation,
            .finalValuePower = gtaoConfig.finalValuePower,
            .depthMipSamplingOffset = gtaoConfig.depthMipSamplingOffset,
            .sliceCount = gtaoConfig.sliceCount,
            .stepsPerSlice = gtaoConfig.stepsPerSlice,
            .noiseIndex = static_cast<uint32_t>(frameNumber % 64),
            .sceneDataIndex = sceneDataIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_main"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& denoise1 = graph.AddPass(SID("GTAO Denoise 1"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    denoise1.ReadSampledImage(SID("gtao_ao"));
    denoise1.ReadSampledImage(SID("gtao_edges"));
    denoise1.WriteStorageImage(SID("gtao_temp"));
    denoise1.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        GTAODenoisePushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_ao")),
            .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_edges")),
            .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_temp")),
            .denoiseBlurBeta = 1e4f,
            .isFinalDenoisePass = 0,
            .sceneDataIndex = sceneDataIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_denoise"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& denoise2 = graph.AddPass(SID("GTAO Denoise 2"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    denoise2.ReadSampledImage(SID("gtao_temp"));
    denoise2.ReadSampledImage(SID("gtao_edges"));
    denoise2.WriteStorageImage(SID("gtao_filtered"));
    denoise2.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        GTAODenoisePushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .rawAOIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_temp")),
            .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex(SID("gtao_edges")),
            .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex(SID("gtao_filtered")),
            .denoiseBlurBeta = gtaoConfig.denoiseBlurBeta,
            .isFinalDenoisePass = 1,
            .sceneDataIndex = sceneDataIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gtao_denoise"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}


void RenderThread::SetupShadowsResolve(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const
{
    renderGraph->CreateTexture(SID("shadows_resolve_target"), TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1});
    RenderPass& shadowsResolvePass = graph.AddPass(SID("Shadows Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    shadowsResolvePass.ReadSampledImage(targets.normal);
    shadowsResolvePass.ReadSampledImage(targets.depthStencil);

    bool bHasGTAO = graph.HasTexture(SID("gtao_filtered"));
    if (bHasGTAO) {
        shadowsResolvePass.ReadSampledImage(SID("gtao_filtered"));
    }

    bool bHasShadows = graph.HasTexture(SID("shadow_cascade_0"));
    if (bHasShadows) {
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_0"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_1"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_2"));
        shadowsResolvePass.ReadSampledImage(SID("shadow_cascade_3"));
    }

    shadowsResolvePass.ReadBuffer(SID("scene_data"));
    shadowsResolvePass.ReadBuffer(SID("shadow_data"));
    shadowsResolvePass.ReadBuffer(SID("light_data"));
    shadowsResolvePass.WriteStorageImage(SID("shadows_resolve_target"));
    shadowsResolvePass.Execute([&, bHasShadows, bHasGTAO, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("shadows_resolve"));

        glm::ivec4 csmIndices{-1, -1, -1, -1};
        if (bHasShadows) {
            csmIndices.x = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_0")));
            csmIndices.y = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_1")));
            csmIndices.z = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_2")));
            csmIndices.w = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("shadow_cascade_3")));
        }

        int32_t gtaoIndex = bHasGTAO ? static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(SID("gtao_filtered"))) : -1;

        ShadowsResolvePushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .shadowData = graph.GetBufferAddress(SID("shadow_data")),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .gtaoFilteredIndex = gtaoIndex,
            .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("shadows_resolve_target")),
            .csmIndices = csmIndices,
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(targets.depthStencil),
            .normalIndex = graph.GetSampledImageViewDescriptorIndex(targets.normal),
            .sceneDataIndex = sceneDataIndex,
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}

void RenderThread::SetupDeferredLighting(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets,
                                         uint32_t sceneDataIndex) const
{
    RenderPass& deferredResolvePass = graph.AddPass(SID("Deferred Resolve"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    deferredResolvePass.ReadBuffer(SID("scene_data"));
    deferredResolvePass.ReadBuffer(SID("light_data"));
    deferredResolvePass.ReadSampledImage(targets.albedo);
    deferredResolvePass.ReadSampledImage(targets.normal);
    deferredResolvePass.ReadSampledImage(targets.pbr);
    deferredResolvePass.ReadSampledImage(targets.emissive);
    deferredResolvePass.ReadSampledImage(targets.depthStencil);
    deferredResolvePass.ReadSampledImage(SID("shadows_resolve_target"));
    deferredResolvePass.WriteStorageImage(targets.outFinalColor);
    deferredResolvePass.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("deferred_resolve"));

        DeferredResolvePushConstant pushData{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .lightData = graph.GetBufferAddress(SID("light_data")),
            .albedoIndex = graph.GetSampledImageViewDescriptorIndex(targets.albedo),
            .normalIndex = graph.GetSampledImageViewDescriptorIndex(targets.normal),
            .pbrIndex = graph.GetSampledImageViewDescriptorIndex(targets.pbr),
            .emissiveIndex = graph.GetSampledImageViewDescriptorIndex(targets.emissive),
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(targets.depthStencil),
            .shadowsIndex = graph.GetSampledImageViewDescriptorIndex(SID("shadows_resolve_target")),
            .skyboxIndex = viewFamily.skyboxIndex,
            .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(targets.outFinalColor),
            .sceneDataIndex = sceneDataIndex,
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DeferredResolvePushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}

void RenderThread::SetupPortalComposite(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets,
                                        const GBufferTargets& portalTargets) const
{
    RenderPass& portalCompositePass = graph.AddPass(SID("Portal Composite"), VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    portalCompositePass.ReadSampledImage(portalTargets.outFinalColor);
    portalCompositePass.ReadSampledImage(portalTargets.velocity);
    portalCompositePass.ReadSampledImage(portalTargets.depthStencil);
    portalCompositePass.WriteColorAttachment(targets.outFinalColor);
    portalCompositePass.WriteColorAttachment(targets.velocity);
    portalCompositePass.ReadWriteDepthAttachment(targets.depthStencil);
    portalCompositePass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.outFinalColor), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.velocity), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        std::array colorAttachments = {colorAttachment, velocityAttachment};
        VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments.data(), 2, &depthAttachment, &stencilAttachment);
        vkCmdBeginRendering(cmd, &renderInfo);

        PortalCompositePushConstant pc{
            .portalColorIndex = graph.GetSampledImageViewDescriptorIndex(portalTargets.outFinalColor),
            .portalVelocityIndex = graph.GetSampledImageViewDescriptorIndex(portalTargets.velocity),
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

void RenderThread::SetupSkyboxRendering(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const
{
    RenderPass& skyboxPass = graph.AddPass(SID("Skybox"), VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    skyboxPass.WriteColorAttachment(targets.outFinalColor);
    skyboxPass.ReadWriteDepthAttachment(targets.depthStencil);
    skyboxPass.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.outFinalColor), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        std::array colorAttachments = {colorAttachment};
        VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments.data(), 1, &depthAttachment, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);

        EnvironmentSkyboxPushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .sceneDataIndex = sceneDataIndex,
            .cubemapIndex = viewFamily.skyboxIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("environment_skybox"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);
    });
}

StringID RenderThread::SetupTemporalAntialiasing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, const PostProcessTargets& ppTargets) const
{
    graph.CreateTexture(SID("taa_current"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CarryTextureToNextFrame(SID("taa_current"), SID("taa_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    if (renderGraph->HasTexture(SID("velocity_target"))) {
        renderGraph->CarryTextureToNextFrame(SID("velocity_target"), SID("velocity_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    if (!graph.HasTexture(SID("taa_history")) || !graph.HasTexture(SID("velocity_history"))) {
        RenderPass& taaPass = graph.AddPass(SID("TAA Copy Deferred"), VK_PIPELINE_STAGE_2_COPY_BIT);
        taaPass.ReadCopyImage(ppTargets.finalColor);
        taaPass.WriteCopyImage(SID("taa_current"));
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            VkImage drawImage = graph.GetImageHandle(ppTargets.finalColor);
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
        return ppTargets.finalColor;
    }

    RenderPass& taaPass = graph.AddPass(SID("TAA Main"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    taaPass.ReadBuffer(SID("scene_data"));
    taaPass.ReadSampledImage(ppTargets.finalColor);
    taaPass.ReadSampledImage(ppTargets.depthStencil);
    taaPass.ReadSampledImage(SID("taa_history"));
    taaPass.ReadSampledImage(ppTargets.velocity);
    taaPass.ReadSampledImage(SID("velocity_history"));
    taaPass.WriteStorageImage(SID("taa_current"));
    taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
        TemporalAntialiasingPushConstant pushData{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.finalColor),
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(ppTargets.depthStencil),
            .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex(SID("taa_history")),
            .velocityIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.velocity),
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


    graph.CreateTexture(SID("taa_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

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

StringID RenderThread::SetupPostProcessing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, const PostProcessTargets& ppTargets,
                                           float deltaTime) const
{
    renderGraph->CreateTexture(SID("post_process_output"), TextureInfo{POST_PROCESS_OUTPUT_FORMAT, renderExtent[0], renderExtent[1], 1});
    const Core::PostProcessConfiguration& ppConfig = viewFamily.postProcessConfig;

    // Exposure
    {
        renderGraph->CreateBuffer(SID("luminance_histogram"), POST_PROCESS_LUMINANCE_BUFFER_SIZE);

        if (!graph.HasBuffer(SID("luminance_buffer"))) {
            renderGraph->CreateBuffer(SID("luminance_buffer"), sizeof(float));
        }
        renderGraph->CarryBufferToNextFrame(SID("luminance_buffer"), SID("luminance_buffer"), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

        auto& clearPass = graph.AddPass(SID("Clear Histogram"), VK_PIPELINE_STAGE_TRANSFER_BIT);
        clearPass.WriteTransferBuffer(SID("luminance_histogram"));
        clearPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("luminance_histogram")), 0, VK_WHOLE_SIZE, 0);
        });

        auto& histogramPass = graph.AddPass(SID("Build Histogram"), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        histogramPass.ReadSampledImage(ppTargets.finalColor);
        histogramPass.WriteBuffer(SID("luminance_histogram"));
        histogramPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            constexpr float minLogLuminance = -10.0;
            constexpr float maxLogLuminance = 2.0;
            constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
            constexpr float oneOverLogLuminanceRange = 1.0 / logLuminanceRange;
            HistogramBuildPushConstant pc{
                .hdrImageIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.finalColor),
                .histogramBufferAddress = graph.GetBufferAddress(SID("luminance_histogram")),
                .width = width,
                .height = height,
                .minLogLuminance = minLogLuminance,
                .oneOverLogLuminanceRange = oneOverLogLuminanceRange,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("exposure_build_histogram"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HistogramBuildPushConstant), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_LUMINANCE_DISPATCH_X - 1) / POST_PROCESS_LUMINANCE_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_LUMINANCE_DISPATCH_Y - 1) / POST_PROCESS_LUMINANCE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

        auto& exposurePass = graph.AddPass(SID("Calculate Exposure"), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        exposurePass.ReadBuffer(SID("luminance_histogram"));
        exposurePass.ReadWriteBuffer(SID("luminance_buffer"));
        exposurePass.Execute([&, deltaTime, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            constexpr float minLogLuminance = -10.0;
            constexpr float maxLogLuminance = 2.0;
            constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
            // constexpr float oneOverLogLuminanceRange = 1.0 / logLuminanceRange;
            ExposureCalculatePushConstant pc{
                .histogramBufferAddress = graph.GetBufferAddress(SID("luminance_histogram")),
                .luminanceBufferAddress = graph.GetBufferAddress(SID("luminance_buffer")),
                .minLogLuminance = minLogLuminance,
                .logLuminanceRange = logLuminanceRange,
                .adaptationSpeed = ppConfig.exposureAdaptationRate * deltaTime,
                .totalPixels = width * height,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("exposure_calculate_average"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    // Bloom
    {
        const uint32_t numDownsamples = (renderExtent[0] >= 3840) ? 6 : 5;

        // Create mipmapped bloom chain
        uint32_t numMips = numDownsamples + 1;
        graph.CreateTexture(SID("bloom_chain"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], numMips});

        // Threshold pass - write directly to mip 0
        RenderPass& thresholdPass = graph.AddPass(SID("Bloom Threshold"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        thresholdPass.ReadSampledImage(ppTargets.finalColor);
        thresholdPass.ReadWriteImage(SID("bloom_chain"));
        thresholdPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            BloomThresholdPushConstant pc{
                .outputExtent = {width, height},
                .inputColorIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.finalColor),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("bloom_chain"), 0),
                .threshold = ppConfig.bloomThreshold,
                .softThreshold = ppConfig.bloomSoftThreshold,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("bloom_threshold"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

        // Downsample chain
        for (uint32_t i = 0; i < numDownsamples; ++i) {
            uint32_t mipWidth = std::max(1u, renderExtent[0] >> (i + 1));
            uint32_t mipHeight = std::max(1u, renderExtent[1] >> (i + 1));

            std::string passName = std::format("Bloom Downsample {}", i);
            RenderPass& downsamplePass = graph.AddPass(StringID(passName.c_str(), passName.size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            downsamplePass.ReadWriteImage(SID("bloom_chain"));
            downsamplePass.Execute([&, mipWidth, mipHeight, srcMip = i, dstMip = i + 1](VkCommandBuffer cmd) {
                BloomDownsamplePushConstant pc{
                    .outputExtent = {mipWidth, mipHeight},
                    .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("bloom_chain")),
                    .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("bloom_chain"), dstMip),
                    .srcMipLevel = srcMip,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("bloom_downsample"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                uint32_t xDispatch = (mipWidth + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
                uint32_t yDispatch = (mipHeight + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
        }

        // Upsample chain
        for (int32_t i = static_cast<int32_t>(numDownsamples) - 1; i >= 0; --i) {
            uint32_t mipWidth = std::max(1u, renderExtent[0] >> i);
            uint32_t mipHeight = std::max(1u, renderExtent[1] >> i);

            std::string passName = std::format("Bloom Upsample {}", i);

            RenderPass& upsamplePass = graph.AddPass(StringID(passName.c_str(), passName.size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            upsamplePass.ReadWriteImage(SID("bloom_chain"));
            upsamplePass.Execute([&, mipWidth, mipHeight, dstMip = i, lowerMip = i + 1](VkCommandBuffer cmd) {
                BloomUpsamplePushConstant pc{
                    .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("bloom_chain")),
                    .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("bloom_chain"), dstMip),
                    .lowerMipLevel = static_cast<uint32_t>(lowerMip),
                    .higherMipLevel = static_cast<uint32_t>(dstMip),
                    .radius = ppConfig.bloomRadius,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("bloom_upsample"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                uint32_t xDispatch = (mipWidth + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
                uint32_t yDispatch = (mipHeight + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
        }
    }

    // Sharpening
    {
        graph.CreateTexture(SID("sharpening_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& sharpeningPass = graph.AddPass(SID("Sharpening"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        sharpeningPass.ReadBuffer(SID("scene_data"));
        sharpeningPass.ReadSampledImage(ppTargets.finalColor);
        sharpeningPass.WriteStorageImage(SID("sharpening_output"));
        sharpeningPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            SharpeningPushConstant pc{
                .outputExtent = {width, height},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.finalColor),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("sharpening_output")),
                .sharpness = ppConfig.sharpeningStrength,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("sharpening"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_SHARPENING_DISPATCH_X - 1) / POST_PROCESS_SHARPENING_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_SHARPENING_DISPATCH_Y - 1) / POST_PROCESS_SHARPENING_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Tonemap
    {
        // todo: add support for HDR swapchain
        graph.CreateTexture(SID("tonemap_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& tonemapPass = graph.AddPass(SID("Tonemap SDR"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        tonemapPass.ReadSampledImage(SID("sharpening_output"));
        tonemapPass.ReadSampledImage(SID("bloom_chain"));
        tonemapPass.WriteStorageImage(SID("tonemap_output"));
        tonemapPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            TonemapSDRPushConstant pushData{
                .tonemapOperator = ppConfig.tonemapOperator,
                .targetLuminance = ppConfig.exposureTargetLuminance,
                .luminanceBufferAddress = graph.GetBufferAddress(SID("luminance_buffer")),
                .bloomImageIndex = graph.GetSampledImageViewDescriptorIndex(SID("bloom_chain")),
                .bloomIntensity = ppConfig.bloomIntensity,
                .outputWidth = width,
                .outputHeight = height,
                .srcImageIndex = graph.GetSampledImageViewDescriptorIndex(SID("sharpening_output")),
                .dstImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("tonemap_output")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("tonemap_sdr"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TonemapSDRPushConstant), &pushData);
            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Motion Blur
    {
        uint32_t blurTiledX = (renderExtent[0] + POST_PROCESS_MOTION_BLUR_TILE_SIZE - 1) / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
        uint32_t blurTiledY = (renderExtent[1] + POST_PROCESS_MOTION_BLUR_TILE_SIZE - 1) / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
        graph.CreateTexture(SID("motion_blur_tiled_max"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, blurTiledX, blurTiledY, 1});
        graph.CreateTexture(SID("motion_blur_tiled_neighbor_max"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, blurTiledX, blurTiledY, 1});
        graph.CreateTexture(SID("motion_blur_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

        RenderPass& motionBlurTiledMaxPass = graph.AddPass(SID("Motion Blur Tiled Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurTiledMaxPass.ReadBuffer(SID("scene_data"));
        motionBlurTiledMaxPass.ReadSampledImage(ppTargets.velocity);
        motionBlurTiledMaxPass.WriteStorageImage(SID("motion_blur_tiled_max"));
        motionBlurTiledMaxPass.Execute([&, width = renderExtent[0], height = renderExtent[1], blurTiledX, blurTiledY, ppTargets](VkCommandBuffer cmd) {
            MotionBlurTileVelocityPushConstant pc{
                .velocityBufferSize = {width, height},
                .tileBufferSize = {blurTiledX, blurTiledY},
                .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.velocity),
                .tileMaxIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_tiled_max")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("motion_blur_tile_max"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X;
            uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });


        RenderPass& motionBlurNeighborMax = graph.AddPass(SID("Motion Blur Neighbor Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurNeighborMax.ReadSampledImage(SID("motion_blur_tiled_max"));
        motionBlurNeighborMax.WriteStorageImage(SID("motion_blur_tiled_neighbor_max"));
        motionBlurNeighborMax.Execute([&, blurTiledX, blurTiledY](VkCommandBuffer cmd) {
            MotionBlurNeighborMaxPushConstant pc{
                .tileBufferSize = {blurTiledX, blurTiledY},
                .tileMaxIndex = graph.GetSampledImageViewDescriptorIndex(SID("motion_blur_tiled_max")),
                .neighborMaxIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_tiled_neighbor_max")),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("motion_blur_neighbor_max"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X;
            uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });


        RenderPass& motionBlurReconstructionPass = graph.AddPass(SID("Motion Blur Reconstruction"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurReconstructionPass.ReadBuffer(SID("scene_data"));
        motionBlurReconstructionPass.ReadSampledImage(SID("tonemap_output"));
        motionBlurReconstructionPass.ReadSampledImage(ppTargets.velocity);
        motionBlurReconstructionPass.ReadSampledImage(ppTargets.depthStencil);
        motionBlurReconstructionPass.ReadSampledImage(SID("motion_blur_tiled_neighbor_max"));
        motionBlurReconstructionPass.WriteStorageImage(SID("motion_blur_output"));
        motionBlurReconstructionPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            MotionBlurReconstructionPushConstant pc{
                .srcBufferSize = {width, height},
                .sceneColorIndex = graph.GetSampledImageViewDescriptorIndex(SID("tonemap_output")),
                .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.velocity),
                .depthBufferIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(ppTargets.depthStencil),
                .tileNeighborMaxIndex = graph.GetSampledImageViewDescriptorIndex(SID("motion_blur_tiled_neighbor_max")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_output")),
                .velocityScale = ppConfig.motionBlurVelocityScale,
                .depthScale = ppConfig.motionBlurDepthScale,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("motion_blur_reconstruction"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_MOTION_BLUR_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_MOTION_BLUR_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Color Grading
    {
        graph.CreateTexture(SID("color_grading_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& colorGradingPass = graph.AddPass(SID("Color Grading"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        colorGradingPass.ReadBuffer(SID("scene_data"));
        colorGradingPass.ReadSampledImage(SID("motion_blur_output"));
        colorGradingPass.WriteStorageImage(SID("color_grading_output"));
        colorGradingPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            ColorGradingPushConstant pc{
                .outputExtent = {width, height},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("motion_blur_output")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("color_grading_output")),
                .exposure = ppConfig.colorGradingExposure,
                .contrast = ppConfig.colorGradingContrast,
                .saturation = ppConfig.colorGradingSaturation,
                .temperature = ppConfig.colorGradingTemperature,
                .tint = ppConfig.colorGradingTint,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("color_grading"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_COLOR_GRADING_DISPATCH_X - 1) / POST_PROCESS_COLOR_GRADING_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_COLOR_GRADING_DISPATCH_Y - 1) / POST_PROCESS_COLOR_GRADING_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Vignette + Chromatic Aberration
    {
        graph.CreateTexture(SID("vignette_aberration_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& vignetteAberrationPass = graph.AddPass(SID("Vignette and Aberration"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        vignetteAberrationPass.ReadBuffer(SID("scene_data"));
        vignetteAberrationPass.ReadSampledImage(SID("color_grading_output"));
        vignetteAberrationPass.WriteStorageImage(SID("vignette_aberration_output"));
        vignetteAberrationPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            VignetteChromaticAberrationPushConstant pc{
                .outputExtent = {width, height},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("color_grading_output")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("vignette_aberration_output")),
                .chromaticAberrationStrength = ppConfig.chromaticAberrationStrength,
                .vignetteStrength = ppConfig.vignetteStrength,
                .vignetteRadius = ppConfig.vignetteRadius,
                .vignetteSmoothness = ppConfig.vignetteSmoothness,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("vignette_aberration"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_X - 1) / POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_Y - 1) / POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Film Grain
    {
        // graph.CreateTexture("filmGrainOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& filmGrainPass = graph.AddPass(SID("Film Grain"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        filmGrainPass.ReadBuffer(SID("scene_data"));
        filmGrainPass.ReadSampledImage(SID("vignette_aberration_output"));
        filmGrainPass.WriteStorageImage(SID("post_process_output"));
        filmGrainPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            FilmGrainPushConstant pc{
                .outputExtent = {width, height},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("vignette_aberration_output")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("post_process_output")),
                .grainStrength = ppConfig.grainStrength,
                .grainSize = ppConfig.grainSize,
                .frameIndex = static_cast<uint32_t>(frameNumber),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("film_grain"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_FILM_GRAIN_DISPATCH_X - 1) / POST_PROCESS_FILM_GRAIN_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_FILM_GRAIN_DISPATCH_Y - 1) / POST_PROCESS_FILM_GRAIN_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    return SID("post_process_output");
}

void RenderThread::SetupDebugRender(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, StringID depthTarget, StringID targetImage,
                                    FrameResourceLimits& limits) const
{
#ifndef PACKAGED_BUILD
    size_t totalDebugVertices = 0;
    size_t totalDebugIndices = 0;

    // Lines: 2 vertices per line
    totalDebugVertices += viewFamily.debugLines.size() * 2;
    totalDebugIndices += viewFamily.debugLines.size() * 2;

    // Boxes: 8 vertices, 24 indices (12 lines * 2)
    totalDebugVertices += viewFamily.debugBoxes.size() * 8;
    totalDebugIndices += viewFamily.debugBoxes.size() * 24;

    // Spheres: approximate with circles on 3 axes, say 32 segments each
    // 3 circles * 32 segments = 96 vertices, 192 indices
    totalDebugVertices += viewFamily.debugSpheres.size() * 96;
    totalDebugIndices += viewFamily.debugSpheres.size() * 192;

    if (totalDebugVertices == 0) {
        return;
    }

    limits.highestDebugVertexBuffer = std::max(limits.highestDebugVertexBuffer, NextPowerOfTwo(totalDebugVertices));
    limits.highestDebugIndexBuffer = std::max(limits.highestDebugIndexBuffer, NextPowerOfTwo(totalDebugIndices));

    size_t debugVertexBufferSize = limits.highestDebugVertexBuffer * sizeof(DebugVertex);
    size_t debugIndexBufferSize = limits.highestDebugIndexBuffer * sizeof(uint32_t);

    graph.CreateBuffer(SID("debug_vertex_buffer"), debugVertexBufferSize);
    graph.CreateBuffer(SID("debug_index_buffer"), debugIndexBufferSize);

    UploadAllocation vertexUpload = graph.AllocateTransient(totalDebugVertices * sizeof(DebugVertex));
    UploadAllocation indexUpload = graph.AllocateTransient(totalDebugIndices * sizeof(uint32_t));

    auto* vertices = static_cast<DebugVertex*>(vertexUpload.ptr);
    auto* indices = static_cast<uint32_t*>(indexUpload.ptr);

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    const glm::mat4 viewMatrix = viewFamily.mainView.currentViewData.view;
    const glm::mat4 projMatrix = viewFamily.mainView.currentViewData.proj;

    Frustum mainViewFrustum = CreateFrustum(projMatrix * viewMatrix);

    for (const auto& line : viewFamily.debugLines) {
        vertices[vertexOffset++] = {.position = line.start, .color = line.color};
        vertices[vertexOffset++] = {.position = line.end, .color = line.color};
        indices[indexOffset++] = vertexOffset - 2;
        indices[indexOffset++] = vertexOffset - 1;
    }

    for (const auto& box : viewFamily.debugBoxes) {
        glm::mat3 rot = glm::mat3_cast(box.rotation);

        if (!IntersectsOBB(mainViewFrustum, box.center, box.extents, rot)) {
            continue;
        }

        glm::vec3 corners[8] = {
            glm::vec3(-1, -1, -1), glm::vec3(1, -1, -1),
            glm::vec3(1, 1, -1), glm::vec3(-1, 1, -1),
            glm::vec3(-1, -1, 1), glm::vec3(1, -1, 1),
            glm::vec3(1, 1, 1), glm::vec3(-1, 1, 1),
        };

        uint32_t baseVertex = vertexOffset;
        for (const auto& corner : corners) {
            glm::vec3 pos = box.center + rot * (corner * box.extents);
            vertices[vertexOffset++] = {.position = pos, .color = box.color};
        }

        // 12 lines for box edges
        uint32_t boxIndices[] = {
            0, 1, 1, 2, 2, 3, 3, 0, // bottom face
            4, 5, 5, 6, 6, 7, 7, 4, // top face
            0, 4, 1, 5, 2, 6, 3, 7 // vertical edges
        };
        for (uint32_t idx : boxIndices) {
            indices[indexOffset++] = baseVertex + idx;
        }
    }

    for (const auto& sphere : viewFamily.debugSpheres) {
        if (!IntersectsSphere(mainViewFrustum, sphere.center, sphere.radius)) {
            continue;
        }

        const int segments = GetSphereSegments(sphere.center, viewFamily.mainView.currentViewData.cameraPos, sphere.radius);
        uint32_t baseVertex = vertexOffset;

        // XY circle
        for (int i = 0; i < segments; ++i) {
            float angle = static_cast<float>(i) / segments * 2.0f * glm::pi<float>();
            glm::vec3 pos = sphere.center + glm::vec3(
                                glm::cos(angle) * sphere.radius,
                                glm::sin(angle) * sphere.radius,
                                0.0f
                            );
            vertices[vertexOffset++] = {.position = pos, .color = sphere.color};
        }
        // XZ circle
        for (int i = 0; i < segments; ++i) {
            float angle = static_cast<float>(i) / segments * 2.0f * glm::pi<float>();
            glm::vec3 pos = sphere.center + glm::vec3(
                                glm::cos(angle) * sphere.radius,
                                0.0f,
                                glm::sin(angle) * sphere.radius
                            );
            vertices[vertexOffset++] = {.position = pos, .color = sphere.color};
        }
        // YZ circle
        for (int i = 0; i < segments; ++i) {
            float angle = static_cast<float>(i) / segments * 2.0f * glm::pi<float>();
            glm::vec3 pos = sphere.center + glm::vec3(
                                0.0f,
                                glm::cos(angle) * sphere.radius,
                                glm::sin(angle) * sphere.radius
                            );
            vertices[vertexOffset++] = {.position = pos, .color = sphere.color};
        }

        // Indices for 3 circles
        for (int circle = 0; circle < 3; ++circle) {
            uint32_t circleBase = baseVertex + circle * segments;
            for (int i = 0; i < segments; ++i) {
                indices[indexOffset++] = circleBase + i;
                indices[indexOffset++] = circleBase + (i + 1) % segments;
            }
        }
    }

    RenderPass& uploadDebugPass = graph.AddPass(SID("Upload Debug Geometry"), VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    uploadDebugPass.WriteTransferBuffer(SID("debug_vertex_buffer"));
    uploadDebugPass.WriteTransferBuffer(SID("debug_index_buffer"));

    VkBuffer srcBuffer = graph.GetTransientUploadBuffer();
    uploadDebugPass.Execute([&, srcBuffer,
            vertexOffset = vertexUpload.offset,
            vertexSize = totalDebugVertices * sizeof(DebugVertex),
            indexOffset = indexUpload.offset,
            indexSize = totalDebugIndices * sizeof(uint32_t)](VkCommandBuffer cmd) {
            VkBufferCopy2 vertexCopy{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = vertexOffset,
                .dstOffset = 0,
                .size = vertexSize
            };
            VkCopyBufferInfo2 vertexCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = srcBuffer,
                .dstBuffer = graph.GetBufferHandle(SID("debug_vertex_buffer")),
                .regionCount = 1,
                .pRegions = &vertexCopy
            };
            vkCmdCopyBuffer2(cmd, &vertexCopyInfo);

            VkBufferCopy2 indexCopy{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = indexOffset,
                .dstOffset = 0,
                .size = indexSize
            };
            VkCopyBufferInfo2 indexCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = srcBuffer,
                .dstBuffer = graph.GetBufferHandle(SID("debug_index_buffer")),
                .regionCount = 1,
                .pRegions = &indexCopy
            };
            vkCmdCopyBuffer2(cmd, &indexCopyInfo);
        });


    totalDebugIndices = indexOffset;

    if (totalDebugIndices == 0) {
        return;
    }

    RenderPass& debugDrawPass = graph.AddPass(SID("Debug Draw"), VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    debugDrawPass.WriteColorAttachment(targetImage);
    bool bHasDepth = graph.HasTexture(depthTarget);
    if (bHasDepth) {
        debugDrawPass.ReadWriteDepthAttachment(depthTarget);
    }
    debugDrawPass.ReadBuffer(SID("scene_data"));
    debugDrawPass.ReadBuffer(SID("debug_vertex_buffer"));
    debugDrawPass.ReadIndexBuffer(SID("debug_index_buffer"));
    debugDrawPass.Execute([&, width = renderExtent[0], height = renderExtent[1], totalDebugIndices, bHasDepth, depthTarget, targetImage](VkCommandBuffer cmd) {
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
            .vertexBuffer = graph.GetBufferAddress(SID("debug_vertex_buffer")),
            .sceneDataIndex = 0,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("debug_render"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DebugDrawPushConstant), &pushConstants);

        vkCmdBindIndexBuffer(cmd, graph.GetBufferHandle(SID("debug_index_buffer")), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(totalDebugIndices), 1, 0, 0, 0);

        vkCmdEndRendering(cmd);
    });

    return;
#else
    return;
#endif
}
} // Render
