//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

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
#include "core/math/math_helpers.h"
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

        if (bShouldExit.load()) { break; }

        // Render Frame
        {
            currentFrameInFlight = frameNumber % Core::FRAME_BUFFER_COUNT;
            Core::FrameBuffer& frameBuffer = engineRenderSynchronization->frameBuffers[currentFrameInFlight];
            assert(frameBuffer.currentFrameBuffer == currentFrameInFlight);


            bEngineRequestsRecreate |= frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate;
            if (!frameBuffer.swapchainRecreateCommand.bIsMinimized && bEngineRequestsRecreate) {
                ZoneScopedN("SwapchainRecreate");
                SPDLOG_INFO("[RenderThread::ThreadMain] Swapchain Recreated");
                vkDeviceWaitIdle(context->device);

                swapchain->Recreate(frameBuffer.swapchainRecreateCommand.width, frameBuffer.swapchainRecreateCommand.height);
                renderExtents->ApplyResize(frameBuffer.swapchainRecreateCommand.width, frameBuffer.swapchainRecreateCommand.height);

                bRenderRequestsRecreate = false;
                bEngineRequestsRecreate = false;

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
        SetupFrameUniforms(viewFamily, renderExtent, frameBuffer.timeFrame.renderDeltaTime);
        SetupModelUniforms(viewFamily, renderFamilyProperties);
    } {
        ZoneScopedN("ImportBuffers");
        renderGraph->ImportBufferNoBarrier("vertex_buffer", resourceManager->megaVertexBuffer.handle, resourceManager->megaVertexBuffer.address,
                                           {resourceManager->megaVertexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier("meshlet_vertex_buffer", resourceManager->megaMeshletVerticesBuffer.handle, resourceManager->megaMeshletVerticesBuffer.address,
                                           {resourceManager->megaMeshletVerticesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier("meshlet_triangle_buffer", resourceManager->megaMeshletTrianglesBuffer.handle, resourceManager->megaMeshletTrianglesBuffer.address,
                                           {resourceManager->megaMeshletTrianglesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier("meshlet_buffer", resourceManager->megaMeshletBuffer.handle, resourceManager->megaMeshletBuffer.address,
                                           {resourceManager->megaMeshletBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBufferNoBarrier("primitive_buffer", resourceManager->primitiveBuffer.handle, resourceManager->primitiveBuffer.address,
                                           {resourceManager->primitiveBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        renderGraph->ImportBuffer("debug_readback_buffer", resourceManager->debugReadbackBuffer.handle, resourceManager->debugReadbackBuffer.address,
                                  {resourceManager->debugReadbackBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT}, resourceManager->debugReadbackLastKnownState);
    }

    // Main view G-buffer
    GBufferTargets targets{"albedo_target", "normal_target", "pbr_target", "emissive_target", "velocity_target", "depth_target", "deferred_resolve_target"};
    renderGraph->CreateTexture("deferred_resolve_target", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
    GBufferTargets portalTargets{"portal_albedo", "portal_normal", "portal_pbr", "portal_emissive", "portal_velocity", "portal_depth", "portal_deferred_resolve"};
    renderGraph->CreateTexture("portal_deferred_resolve", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    //
    {
        ZoneScopedN("SetupRenderGraph");

        RenderPass& clearDeferredImagePass = renderGraph->AddPass("Clear Deferred Images", VK_PIPELINE_STAGE_2_CLEAR_BIT);
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

        if (renderFamilyProperties.bHasAnyGeometry) {
            if (renderFamilyProperties.bHasShadows) {
                SetupCascadedShadows(*renderGraph, viewFamily, renderFamilyProperties);
            }

            renderGraph->CreateTexture(targets.albedo, TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.normal, TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.pbr, TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.emissive, TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.velocity, TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(targets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

            if (renderFamilyProperties.bHasMainGeometry) {
                SetupMainGeometryPass(*renderGraph, viewFamily, renderFamilyProperties, renderExtent, targets, 0, true);
            }

            if (renderFamilyProperties.bHasDirectGeometry) {
                SetupDirectGeometryPass(*renderGraph, viewFamily, renderFamilyProperties, renderExtent, targets, 0, !renderFamilyProperties.bHasMainGeometry);
            }

            if (renderFamilyProperties.bHasGTAO) {
                SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent, targets, 0);
            }

            if (renderFamilyProperties.bHasShadows || renderFamilyProperties.bHasGTAO) {
                SetupShadowsResolve(*renderGraph, viewFamily, renderExtent, targets, 0);
            }

            if (renderFamilyProperties.bHasDeferred) {
                SetupDeferredLighting(*renderGraph, viewFamily, renderExtent, targets, 0);
            }
        }

        bool bHasPortalView = (renderFamilyProperties.bHasMainGeometry || renderFamilyProperties.bHasDirectGeometry) && !viewFamily.portalViews.empty();
        if (bHasPortalView) {
            renderGraph->CreateTexture(portalTargets.albedo, TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(portalTargets.normal, TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(portalTargets.pbr, TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(portalTargets.emissive, TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(portalTargets.velocity, TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1});
            renderGraph->CreateTexture(portalTargets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

            if (renderFamilyProperties.bHasMainGeometry) {
                SetupMainGeometryPass(*renderGraph, viewFamily, renderFamilyProperties, renderExtent, portalTargets, 1, true);
            }

            if (renderFamilyProperties.bHasDirectGeometry) {
                SetupDirectGeometryPass(*renderGraph, viewFamily, renderFamilyProperties, renderExtent, portalTargets, 1, !renderFamilyProperties.bHasMainGeometry);
            }

            if (renderFamilyProperties.bHasGTAO) {
                SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
            }

            if (renderFamilyProperties.bHasShadows || renderFamilyProperties.bHasGTAO) {
                SetupShadowsResolve(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
            }

            if (renderFamilyProperties.bHasDeferred) {
                SetupDeferredLighting(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
            }
        }


        // Portal Composite
        if (bHasPortalView) {
            SetupPortalComposite(*renderGraph, viewFamily, renderExtent, targets, portalTargets);
        }

        PostProcessTargets ppTargets{targets.outFinalColor, targets.velocity, targets.depthStencil};
        PostProcessTargets taaTargets{targets.outFinalColor, targets.velocity, targets.depthStencil};
        std::string finalOutput = targets.outFinalColor;
        if (renderFamilyProperties.bHasMainGeometry || renderFamilyProperties.bHasDirectGeometry) {
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
        if (renderFamilyProperties.bHasAnyGeometry) {
            TemporaryRenderTests(*renderGraph, viewFamily, renderFamilyProperties, renderExtent);
        }

        size_t offset = 0;
        if (viewFamily.mainPassInstances.size() >= 640 && renderGraph->HasBuffer("instance_meshlet_offsets")) {
            RenderPass& debugReadbackPass = renderGraph->AddPass(
                "Debug Readback Meshlet Instancing",
                VK_PIPELINE_STAGE_2_COPY_BIT
            );

            debugReadbackPass.ReadTransferBuffer("instance_meshlet_offsets");
            debugReadbackPass.WriteTransferBuffer("debug_readback_buffer");

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                // Instance meshlet offsets
                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = 640 * sizeof(InstanceMeshletOffsetPrefixSum);

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle("instance_meshlet_offsets"),
                    renderGraph->GetBufferHandle("debug_readback_buffer"),
                    1,
                    &copies[0]
                );
            });
        }
        offset += 640 * sizeof(InstanceMeshletOffsetPrefixSum);

        if (renderGraph->HasBuffer("meshlet_count_dispatch_args")) {
            RenderPass& debugReadbackPass = renderGraph->AddPass("Debug Readback meshlet dispatch args", VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer("meshlet_count_dispatch_args");
            debugReadbackPass.WriteTransferBuffer("debug_readback_buffer");

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(InstancingMeshletDispatchIndirect);

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle("meshlet_count_dispatch_args"),
                    renderGraph->GetBufferHandle("debug_readback_buffer"),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(InstancingMeshletDispatchIndirect);

        if (renderGraph->HasBuffer("intermediate_meshlets")) {
            RenderPass& debugReadbackPass = renderGraph->AddPass("Debug Readback intermediate meshlets", VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer("intermediate_meshlets");
            debugReadbackPass.WriteTransferBuffer("debug_readback_buffer");

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(IntermediateMeshlet) * 128;

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle("intermediate_meshlets"),
                    renderGraph->GetBufferHandle("debug_readback_buffer"),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(IntermediateMeshlet) * 128;

        if (renderGraph->HasBuffer("visible_meshlets")) {
            RenderPass& debugReadbackPass = renderGraph->AddPass("Debug Readback visible meshlets", VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer("visible_meshlets");
            debugReadbackPass.WriteTransferBuffer("debug_readback_buffer");

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(CompactedMeshlet) * 128;

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle("visible_meshlets"),
                    renderGraph->GetBufferHandle("debug_readback_buffer"),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(CompactedMeshlet) * 128;

        if (renderGraph->HasBuffer("meshlet_scanned_level2_block_sums")) {
            RenderPass& debugReadbackPass = renderGraph->AddPass("Debug Readback meshlet scanned level2 block sums", VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer("meshlet_scanned_level2_block_sums");
            debugReadbackPass.WriteTransferBuffer("debug_readback_buffer");

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(uint32_t) * 256;

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle("meshlet_scanned_level2_block_sums"),
                    renderGraph->GetBufferHandle("debug_readback_buffer"),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(uint32_t) * 256;

        if (renderGraph->HasBuffer("compacted_meshlet_dispatch_args")) {
            RenderPass& debugReadbackPass = renderGraph->AddPass("Debug Readback compacted dispatch args", VK_PIPELINE_STAGE_2_COPY_BIT);

            debugReadbackPass.ReadTransferBuffer("compacted_meshlet_dispatch_args");
            debugReadbackPass.WriteTransferBuffer("debug_readback_buffer");

            debugReadbackPass.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copies[1];

                copies[0].srcOffset = 0;
                copies[0].dstOffset = offset;
                copies[0].size = sizeof(InstancingCompactedMeshletDispatchIndirect);

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle("compacted_meshlet_dispatch_args"),
                    renderGraph->GetBufferHandle("debug_readback_buffer"),
                    1,
                    &copies[0]
                );
            });
        }
        offset += sizeof(InstancingCompactedMeshletDispatchIndirect);

        if (renderGraph->HasBuffer("visible_meshlets")) {
            RenderPass& readbackVisibleMeshlets = renderGraph->AddPass("Readback Visible Meshlets", VK_PIPELINE_STAGE_2_COPY_BIT);
            readbackVisibleMeshlets.ReadTransferBuffer("visible_meshlets");
            readbackVisibleMeshlets.Execute([&, offset](VkCommandBuffer cmd) {
                VkBufferCopy copy;
                copy.srcOffset = 0;
                copy.dstOffset = offset;
                copy.size = sizeof(CompactedMeshlet) * 128;

                vkCmdCopyBuffer(
                    cmd,
                    renderGraph->GetBufferHandle("visible_meshlets"),
                    renderGraph->GetBufferHandle("debug_readback_buffer"),
                    1,
                    &copy
                );
            });
        }
        offset += sizeof(CompactedMeshlet) * 128;
#endif


        bool bHasDebugPass = !viewFamily.debugResourceName.empty() && pipelineManager->IsCategoryReady(PipelineCategory::Debug);
        if (bHasDebugPass) {
            const char* debugTargetName = viewFamily.debugResourceName.c_str();

            if (renderGraph->HasTexture(debugTargetName)) {
                auto& debugVisPass = renderGraph->AddPass("Debug Visualize", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
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
                        .sceneData = renderGraph->GetBufferAddress("scene_data"),
                        .srcExtent = {dims.width, dims.height},
                        .dstExtent = {renderExtent[0], renderExtent[1]},
                        .nearPlane = viewFamily.mainView.currentViewData.nearPlane,
                        .farPlane = viewFamily.mainView.currentViewData.farPlane,
                        .textureArrayIndex = textureArrayIndex,
                        .textureIndexInArray = textureIndexInArray,
                        .valueTransformationType = static_cast<uint32_t>(viewFamily.debugTransformationType),
                        .outputImageIndex = outputIndexIndex,
                    };
                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("debug_visualize");
                    vkCmdBindPipeline(_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                    vkCmdPushConstants(_cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    uint32_t xDispatch = (renderExtent[0] + 15) / 16;
                    uint32_t yDispatch = (renderExtent[1] + 15) / 16;
                    vkCmdDispatch(_cmd, xDispatch, yDispatch, 1);
                });
            }
        }

        renderGraph->ImportTexture("swapchain_image", currentSwapchainImage, currentSwapchainImageView, TextureInfo{swapchain->format, swapchain->extent.width, swapchain->extent.height, 1},
                                   swapchain->usages,
                                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED);

        auto& blitPass = renderGraph->AddPass("Blit To Swapchain", VK_PIPELINE_STAGE_2_BLIT_BIT);
        blitPass.ReadBlitImage(finalOutput);
        blitPass.WriteBlitImage("swapchain_image");
        blitPass.Execute([&, finalOutput](VkCommandBuffer _cmd) {
            VkImage drawImage = renderGraph->GetImageHandle(finalOutput);

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
            blitRegion.dstOffsets[0] = {0, swapchainOffset.y, 0};
            blitRegion.dstOffsets[1] = {swapchainOffset.x, 0, swapchainOffset.z};

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
            auto& imguiEditorPass = renderGraph->AddPass("Imgui Draw", VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
            imguiEditorPass.WriteColorAttachment("swapchain_image");
            imguiEditorPass.Execute([&](VkCommandBuffer _cmd) {
                const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(renderGraph->GetImageViewHandle("swapchain_image"), nullptr,
                                                                                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                const ResourceDimensions& dims = renderGraph->GetImageDimensions("swapchain_image");
                const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &imguiAttachment, nullptr);
                vkCmdBeginRendering(_cmd, &renderInfo);
                ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[frameIndex];
                ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, _cmd);

                vkCmdEndRendering(_cmd);
            });
        }
    } {
        ZoneScopedN("RenderGraphCompile");
        renderGraph->SetDebugLogging(frameBuffer.bLogRDG);
        renderGraph->Compile(frameNumber);
    } {
        ZoneScopedN("RenderGraphExecute");
        renderGraph->Execute(cmd);
        renderGraph->PrepareSwapchain(cmd, "swapchain_image");
    }

    resourceManager->debugReadbackLastKnownState = renderGraph->GetBufferState("debug_readback_buffer");
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

    pipelineManager->RegisterComputePipeline("instancing_instance_lod", Platform::GetShaderPath() / "instancing_instance_lod_compute.spv",
                                             sizeof(InstanceLODPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_prefix_sum_up_1", Platform::GetShaderPath() / "instancing_prefix_sum_up_1_compute.spv",
                                             sizeof(PrefixSumUpsweep1PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_prefix_sum_up_2", Platform::GetShaderPath() / "instancing_prefix_sum_up_2_compute.spv",
                                             sizeof(PrefixSumUpsweep2PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_scan_blocks", Platform::GetShaderPath() / "instancing_scan_blocks_compute.spv",
                                             sizeof(PrefixSumScanBlocksPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_prefix_sum_down_1", Platform::GetShaderPath() / "instancing_prefix_sum_down_1_compute.spv",
                                             sizeof(PrefixSumDownsweep1PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_prefix_sum_down_2", Platform::GetShaderPath() / "instancing_prefix_sum_down_2_compute.spv",
                                             sizeof(PrefixSumDownsweep2PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_total_meshlet_count", Platform::GetShaderPath() / "instancing_total_meshlet_count_compute.spv",
                                             sizeof(TotalMeshletCountPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_expand_instance_to_meshlet", Platform::GetShaderPath() / "instancing_expand_instance_to_meshlet_compute.spv",
                                             sizeof(ExpandMeshletsPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_meshlet_visibility_prefix_sum_up_1", Platform::GetShaderPath() / "instancing_meshlet_visibility_prefix_sum_up_1_compute.spv",
                                             sizeof(MeshletVisibilityPrefixSumUpsweep1PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_meshlet_visibility_prefix_sum_down_2", Platform::GetShaderPath() / "instancing_meshlet_visibility_prefix_sum_down_2_compute.spv",
                                             sizeof(MeshletVisibilityPrefixSumDownsweep2PushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_compacted_meshlet_dispatch", Platform::GetShaderPath() / "instancing_compacted_meshlet_dispatch_compute.spv",
                                             sizeof(MeshletVisibilityPrefixSumDownsweep2PushConstant), PipelineCategory::Instancing);

    pipelineManager->RegisterComputePipeline("direct_mesh_shading_build_indirect", Platform::GetShaderPath() / "mesh_shading_direct_build_indirect_compute.spv",
                                             sizeof(BuildDirectIndirectPushConstant), PipelineCategory::CustomRendering);

    pipelineManager->RegisterComputePipeline("shadows_resolve", Platform::GetShaderPath() / "shadows_resolve_compute.spv",
                                             sizeof(ShadowsResolvePushConstant), PipelineCategory::ShadowCombine);
    pipelineManager->RegisterComputePipeline("deferred_resolve", Platform::GetShaderPath() / "deferred_resolve_compute.spv",
                                             sizeof(DeferredResolvePushConstant), PipelineCategory::DeferredShading);

    pipelineManager->RegisterComputePipeline("temporal_antialiasing", Platform::GetShaderPath() / "temporal_antialiasing_compute.spv",
                                             sizeof(TemporalAntialiasingPushConstant), PipelineCategory::TAA);

    pipelineManager->RegisterComputePipeline("gtao_depth_prepass", Platform::GetShaderPath() / "gtao_depth_prepass_compute.spv",
                                             sizeof(GTAODepthPrepassPushConstant), PipelineCategory::GTAO);
    pipelineManager->RegisterComputePipeline("gtao_main", Platform::GetShaderPath() / "gtao_main_compute.spv",
                                             sizeof(GTAOMainPushConstant), PipelineCategory::GTAO);
    pipelineManager->RegisterComputePipeline("gtao_denoise", Platform::GetShaderPath() / "gtao_denoise_compute.spv",
                                             sizeof(GTAODenoisePushConstant), PipelineCategory::GTAO);


    pipelineManager->RegisterComputePipeline("exposure_build_histogram", Platform::GetShaderPath() / "exposure_build_histogram_compute.spv",
                                             sizeof(HistogramBuildPushConstant), PipelineCategory::Exposure);
    pipelineManager->RegisterComputePipeline("exposure_calculate_average", Platform::GetShaderPath() / "exposure_calculate_average_compute.spv",
                                             sizeof(ExposureCalculatePushConstant), PipelineCategory::Exposure);
    pipelineManager->RegisterComputePipeline("tonemap_sdr", Platform::GetShaderPath() / "tonemap_sdr_compute.spv",
                                             sizeof(TonemapSDRPushConstant), PipelineCategory::Tonemap);
    pipelineManager->RegisterComputePipeline("motion_blur_tile_max", Platform::GetShaderPath() / "motion_blur_tile_max_compute.spv",
                                             sizeof(MotionBlurTileVelocityPushConstant), PipelineCategory::MotionBlur);
    pipelineManager->RegisterComputePipeline("motion_blur_neighbor_max", Platform::GetShaderPath() / "motion_blur_neighbor_max_compute.spv",
                                             sizeof(MotionBlurNeighborMaxPushConstant), PipelineCategory::MotionBlur);
    pipelineManager->RegisterComputePipeline("motion_blur_reconstruction", Platform::GetShaderPath() / "motion_blur_reconstruction_compute.spv",
                                             sizeof(MotionBlurReconstructionPushConstant), PipelineCategory::MotionBlur);
    pipelineManager->RegisterComputePipeline("bloom_threshold", Platform::GetShaderPath() / "bloom_threshold_compute.spv",
                                             sizeof(BloomThresholdPushConstant), PipelineCategory::Bloom);
    pipelineManager->RegisterComputePipeline("bloom_downsample", Platform::GetShaderPath() / "bloom_downsample_compute.spv",
                                             sizeof(BloomDownsamplePushConstant), PipelineCategory::Bloom);
    pipelineManager->RegisterComputePipeline("bloom_upsample", Platform::GetShaderPath() / "bloom_upsample_compute.spv",
                                             sizeof(BloomUpsamplePushConstant), PipelineCategory::Bloom);
    pipelineManager->RegisterComputePipeline("vignette_aberration", Platform::GetShaderPath() / "vignette_aberration_compute.spv",
                                             sizeof(VignetteChromaticAberrationPushConstant), PipelineCategory::Vignette);
    pipelineManager->RegisterComputePipeline("film_grain", Platform::GetShaderPath() / "film_grain_compute.spv",
                                             sizeof(FilmGrainPushConstant), PipelineCategory::FilmGrain);
    pipelineManager->RegisterComputePipeline("sharpening", Platform::GetShaderPath() / "sharpening_compute.spv",
                                             sizeof(SharpeningPushConstant), PipelineCategory::Sharpening);
    pipelineManager->RegisterComputePipeline("color_grading", Platform::GetShaderPath() / "color_grading_compute.spv",
                                             sizeof(ColorGradingPushConstant), PipelineCategory::ColorGrade);

    pipelineManager->RegisterComputePipeline("debug_visualize", Platform::GetShaderPath() / "debug_visualize_compute.spv",
                                             sizeof(DebugVisualizePushConstant), PipelineCategory::Debug);

#if WILL_EDITOR
    std::vector emapLayout{
        resourceManager->environmentMapGenerateResources.descriptorSetLayout.handle,
    };
    pipelineManager->RegisterComputePipelineCustomLayout("ibl_equirect_to_cubemap", Platform::GetShaderPath() / "ibl_equirect_to_cubemap_compute.spv",
                                                         sizeof(EquirectToCubemapPushConstant), PipelineCategory::AssetGeneration, emapLayout);

    pipelineManager->RegisterComputePipelineCustomLayout("ibl_convolve_diffuse", Platform::GetShaderPath() / "ibl_convolve_diffuse_compute.spv",
                                                         sizeof(ConvolveDiffusePushConstant), PipelineCategory::AssetGeneration, emapLayout);

    pipelineManager->RegisterComputePipelineCustomLayout("ibl_prefilter_specular", Platform::GetShaderPath() / "ibl_prefilter_specular_compute.spv",
                                                         sizeof(PrefilterSpecularPushConstant), PipelineCategory::AssetGeneration, emapLayout);
#endif

    GraphicsPipelineBuilder builder;

    // Shadow cascade pipeline
    {
        builder.AddShaderStage("shaders/shadow_mesh_shading_instanced_task.spv", VK_SHADER_STAGE_TASK_BIT_EXT);
        builder.AddShaderStage("shaders/shadow_mesh_shading_instanced_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.SetupInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetupRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
        builder.SetupDepthState(VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL);
        builder.EnableDepthBias();
        builder.SetupRenderer(nullptr, 0, SHADOW_CASCADE_FORMAT);
        builder.AddDynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS);

        pipelineManager->RegisterGraphicsPipeline(
            "shadow_cascade_instanced",
            builder,
            sizeof(ShadowMeshShadingPushConstant),
            VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT,
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
            "mesh_shading_instanced",
            builder,
            sizeof(InstancedMeshShadingPushConstant),
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Geometry
        );
        builder.Clear();
    }

    // Portal Graphics Pipeline
    {
        builder.AddShaderStage("shaders/portal_rendering_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage("shaders/portal_rendering_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
            "portal_rendering",
            builder,
            sizeof(PortalRenderingMeshShadingPushConstant),
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
            "portal_composite",
            builder,
            sizeof(PortalCompositePushConstant),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::CustomRendering
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
            "debug_render",
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
    renderFamilyProperties.primitiveIndexToRangeBufferMap.resize(MEGA_PRIMITIVE_BUFFER_COUNT);
    renderFamilyProperties.viewFamily = &viewFamily;
    renderFamilyProperties.bHasMainGeometry = !viewFamily.mainPassInstances.empty() && _pipelineManager->IsCategoryReady(PipelineCategory::Geometry | PipelineCategory::Instancing);
    renderFamilyProperties.bHasDirectGeometry = !viewFamily.customShaderDraws.empty() && _pipelineManager->IsCategoryReady(PipelineCategory::CustomRendering);
    renderFamilyProperties.bHasAnyGeometry = renderFamilyProperties.bHasMainGeometry || renderFamilyProperties.bHasDirectGeometry;
    renderFamilyProperties.bHasGTAO = viewFamily.gtaoConfig.bEnabled && _pipelineManager->IsCategoryReady(PipelineCategory::GTAO);
    renderFamilyProperties.bHasShadows = viewFamily.shadowConfig.enabled && _pipelineManager->IsCategoryReady(PipelineCategory::ShadowPass);
    renderFamilyProperties.bHasShadows = false;
    renderFamilyProperties.bHasDeferred = _pipelineManager->IsCategoryReady(PipelineCategory::DeferredShading);


    uint32_t filteredPrimtiveCount = 0;
    if (!viewFamily.mainPassInstances.empty()) {
        std::ranges::sort(viewFamily.mainPassInstances, [](const Core::InstanceData& a, const Core::InstanceData& b) {
            return a.primitiveIndex < b.primitiveIndex;
        });

        uint32_t currentPrimitive = viewFamily.mainPassInstances[0].primitiveIndex;
        uint32_t rangeIdx = 0;
        renderFamilyProperties.primitiveIndexToRangeBufferMap[currentPrimitive] = rangeIdx;

        for (size_t i = 1; i < viewFamily.mainPassInstances.size(); ++i) {
            uint32_t primIndex = viewFamily.mainPassInstances[i].primitiveIndex;
            if (primIndex != currentPrimitive) {
                currentPrimitive = primIndex;
                ++rangeIdx;
                renderFamilyProperties.primitiveIndexToRangeBufferMap[currentPrimitive] = rangeIdx;
            }
        }
        filteredPrimtiveCount = rangeIdx + 1;

        _limits.highestModelBuffer = std::max(_limits.highestModelBuffer, NextPowerOfTwo(viewFamily.modelMatrices.size()));
        _limits.highestMaterialBuffer = std::max(_limits.highestMaterialBuffer, NextPowerOfTwo(viewFamily.materials.size()));
        _limits.highestInstanceBuffer = std::max(_limits.highestInstanceBuffer, NextPowerOfTwo(viewFamily.mainPassInstances.size()));
        _limits.highestFilteredPrimitiveCount = std::max(_limits.highestFilteredPrimitiveCount, NextPowerOfTwo(filteredPrimtiveCount));
        _limits.highestMeshletCount = std::max(_limits.highestMeshletCount, NextPowerOfTwo(readbackData->meshletCount));
    }

    /*if (!viewFamily. customStencilDraws.empty()) {
        size_t totalCustomInstances = 0;
        for (const auto& customDraw : viewFamily.customStencilDraws) {
            totalCustomInstances += customDraw.instances.size();
        }

        _limits.highestDirectInstanceBuffer = std::max(_limits.highestDirectInstanceBuffer, NextPowerOfTwo(totalCustomInstances));
        _limits.highestDirectIndirectCommandBuffer = std::max(_limits.highestDirectIndirectCommandBuffer, NextPowerOfTwo(totalCustomInstances));
    }*/


    renderFamilyProperties.modelBufferSize = _limits.highestModelBuffer * sizeof(Model);
    renderFamilyProperties.materialBufferSize = _limits.highestMaterialBuffer * sizeof(MaterialProperties);
    renderFamilyProperties.instanceBufferSize = _limits.highestInstanceBuffer * sizeof(Instance);

    renderFamilyProperties.instanceIndirectionBufferSize = _limits.highestInstanceBuffer * sizeof(uint32_t);
    renderFamilyProperties.primitivePrefixSumBufferSize = _limits.highestFilteredPrimitiveCount * sizeof(PrimitiveOffsets);
    constexpr uint32_t blockSumCount = MEGA_PRIMITIVE_BUFFER_COUNT / INSTANCING_PREFIX_SUM_LOCAL_DISPATCH_X;
    static_assert(blockSumCount == 256);
    renderFamilyProperties.primitivePrefixBlockSumBufferSize = blockSumCount * sizeof(PrimitiveOffsets);
    renderFamilyProperties.primitiveCountersBufferSize = _limits.highestFilteredPrimitiveCount * sizeof(PrimitiveCounters);
    renderFamilyProperties.mainCommandBufferSize = _limits.highestFilteredPrimitiveCount * LOD_COUNT * sizeof(InstancedMeshIndirectDrawParameters);

    renderFamilyProperties.directInstanceBufferSize = _limits.highestDirectInstanceBuffer * sizeof(Instance);
    renderFamilyProperties.directIndirectCommandBufferSize = _limits.highestDirectIndirectCommandBuffer * sizeof(InstancedMeshIndirectDrawParameters);


    renderFamilyProperties.instanceMeshletOffsetsBufferSize = _limits.highestInstanceBuffer * sizeof(InstanceMeshletOffsetPrefixSum);

    uint32_t level1BlockCount = (_limits.highestInstanceBuffer + 255) / 256;
    uint32_t level2BlockCount = (level1BlockCount + 255) / 256;

    renderFamilyProperties.level1SumsBufferSize = _limits.highestInstanceBuffer * sizeof(uint32_t);
    renderFamilyProperties.level1BlockSumsBufferSize = level1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.level2SumsBufferSize = level1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.level2BlockSumsBufferSize = level2BlockCount * sizeof(uint32_t);
    renderFamilyProperties.scannedLevel2BlockSumsBufferSize = glm::max(level2BlockCount, 256u) * sizeof(uint32_t);

    renderFamilyProperties.intermediateMeshletBufferSize = _limits.highestMeshletCount * sizeof(IntermediateMeshlet);
    uint32_t meshletLevel1BlockCount = (_limits.highestMeshletCount + 255) / 256;
    uint32_t meshletLevel2BlockCount = (meshletLevel1BlockCount + 255) / 256;

    renderFamilyProperties.meshletLevel1SumsBufferSize = _limits.highestMeshletCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel1BlockSumsBufferSize = meshletLevel1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel2SumsBufferSize = meshletLevel1BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletLevel2BlockSumsBufferSize = meshletLevel2BlockCount * sizeof(uint32_t);
    renderFamilyProperties.meshletScannedLevel2BlockSumsBufferSize = glm::max(meshletLevel2BlockCount, 256u) * sizeof(uint32_t);

    renderFamilyProperties.visibleMeshletsBufferSize = _limits.highestMeshletCount * sizeof(CompactedMeshlet);


    renderFamilyProperties.instanceCount = viewFamily.mainPassInstances.size();
    renderFamilyProperties.visibleMeshletUpperBound = _limits.highestMeshletCount;
    renderFamilyProperties.filteredPrimitiveCount = filteredPrimtiveCount;
}

void RenderThread::SetupFrameUniforms(const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, float renderDeltaTime) const
{
    renderGraph->CreateBuffer("scene_data", SCENE_DATA_BUFFER_SIZE);
    renderGraph->CreateBuffer("shadow_data", SHADOW_DATA_BUFFER_SIZE);
    renderGraph->CreateBuffer("light_data", LIGHT_DATA_BUFFER_SIZE);

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

    auto& uploadUniformsPass = renderGraph->AddPass("Upload Uniforms", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    uploadUniformsPass.WriteTransferBuffer("scene_data");
    uploadUniformsPass.WriteTransferBuffer("shadow_data");
    uploadUniformsPass.WriteTransferBuffer("light_data");
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
                .dstBuffer = renderGraph->GetBufferHandle("scene_data"),
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
                .dstBuffer = renderGraph->GetBufferHandle("shadow_data"),
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
                .dstBuffer = renderGraph->GetBufferHandle("light_data"),
                .regionCount = lightDataRegions.size(),
                .pRegions = lightDataRegions.data()
            };
            vkCmdCopyBuffer2(cmd, &lightDataCopyInfo);
        });
}

void RenderThread::SetupModelUniforms(const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    if (!viewFamily.modelMatrices.empty()) {
        renderGraph->CreateBuffer("model_buffer", renderFamilyProperties.modelBufferSize);
        UploadAllocation modelUpload = renderGraph->AllocateTransient(viewFamily.modelMatrices.size() * sizeof(Model));
        memcpy(modelUpload.ptr, viewFamily.modelMatrices.data(), viewFamily.modelMatrices.size() * sizeof(Model));

        RenderPass& uploadModelMatricesPass = renderGraph->AddPass("Upload Model Matrices", VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadModelMatricesPass.WriteTransferBuffer("model_buffer");
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
                    .dstBuffer = renderGraph->GetBufferHandle("model_buffer"),
                    .regionCount = 1,
                    .pRegions = &copy
                };
                vkCmdCopyBuffer2(cmd, &copyInfo);
            });
    }

    if (!viewFamily.materials.empty()) {
        renderGraph->CreateBuffer("material_buffer", renderFamilyProperties.materialBufferSize);
        UploadAllocation materialUpload = renderGraph->AllocateTransient(viewFamily.materials.size() * sizeof(MaterialProperties));
        memcpy(materialUpload.ptr, viewFamily.materials.data(), viewFamily.materials.size() * sizeof(MaterialProperties));

        RenderPass& uploadMaterialsPass = renderGraph->AddPass("Upload Materials", VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadMaterialsPass.WriteTransferBuffer("material_buffer");
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
                    .dstBuffer = renderGraph->GetBufferHandle("material_buffer"),
                    .regionCount = 1,
                    .pRegions = &copy
                };
                vkCmdCopyBuffer2(cmd, &copyInfo);
            });
    }

    if (!viewFamily.mainPassInstances.empty()) {
        assert(!viewFamily.modelMatrices.empty() && !viewFamily.materials.empty());

        renderGraph->CreateBuffer("instance_buffer", renderFamilyProperties.instanceBufferSize);
        UploadAllocation instanceUpload = renderGraph->AllocateTransient(viewFamily.mainPassInstances.size() * sizeof(Instance));
        auto* instanceBuffer = static_cast<Instance*>(instanceUpload.ptr);
        for (size_t i = 0; i < viewFamily.mainPassInstances.size(); ++i) {
            auto& inst = viewFamily.mainPassInstances[i];
            instanceBuffer[i] = {
                .primitiveIndex = inst.primitiveIndex,
                .modelIndex = inst.modelIndex,
                .materialIndex = inst.gpuMaterialIndex,
                .bIsVisible = 0,
                .lod = 0,
            };
        }

        RenderPass& uploadModelsPass = renderGraph->AddPass("Upload Instances", VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadModelsPass.WriteTransferBuffer("instance_buffer");
        uploadModelsPass.Execute([&,
                instanceOffset = instanceUpload.offset,
                instanceSize = viewFamily.mainPassInstances.size() * sizeof(Instance)](VkCommandBuffer cmd) {
                VkBufferCopy2 copy{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                    .srcOffset = instanceOffset,
                    .dstOffset = 0,
                    .size = instanceSize,
                };

                VkCopyBufferInfo2 instanceCopyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                    .dstBuffer = renderGraph->GetBufferHandle("instance_buffer"),
                    .regionCount = 1,
                    .pRegions = &copy
                };
                vkCmdCopyBuffer2(cmd, &instanceCopyInfo);
            });
    }


    renderGraph->CreateBuffer("primitive_to_range_map_buffer", renderFamilyProperties.primitiveIndexToPrimitiveCounterBufferSize);

    UploadAllocation primitiveIndexToRangeUpload = renderGraph->AllocateTransient(MEGA_PRIMITIVE_BUFFER_COUNT * sizeof(uint32_t));
    auto* primitiveIndexToRangeBuffer = static_cast<uint32_t*>(primitiveIndexToRangeUpload.ptr);
    memcpy(primitiveIndexToRangeBuffer, renderFamilyProperties.primitiveIndexToRangeBufferMap.data(), renderFamilyProperties.primitiveIndexToRangeBufferMap.size() * sizeof(uint32_t));

    RenderPass& uploadModelsPass = renderGraph->AddPass("Upload Model Uniforms", VK_PIPELINE_STAGE_2_COPY_BIT);
    uploadModelsPass.WriteTransferBuffer("primitive_to_range_map_buffer");
    uploadModelsPass.Execute([&,
            primitiveMapOffset = primitiveIndexToRangeUpload.offset,
            primitiveMapSize = MEGA_PRIMITIVE_BUFFER_COUNT * sizeof(uint32_t)](VkCommandBuffer cmd) {
            VkBufferCopy2 copy{
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .srcOffset = primitiveMapOffset,
                .dstOffset = 0,
                .size = primitiveMapSize,
            };

            VkCopyBufferInfo2 primitiveMapCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                .dstBuffer = renderGraph->GetBufferHandle("primitive_to_range_map_buffer"),
                .regionCount = 1,
                .pRegions = &copy
            };
            vkCmdCopyBuffer2(cmd, &primitiveMapCopyInfo);
        });

    /*if (!viewFamily.customStencilDraws.empty()) {
        size_t totalCustomInstances = 0;
        for (const auto& customDraw : viewFamily.customStencilDraws) {
            totalCustomInstances += customDraw.instances.size();
        }

        renderGraph->CreateBuffer("direct_instance_buffer", renderFamilyProperties.directInstanceBufferSize);
        renderGraph->CreateBuffer("direct_indirect_command_buffer", renderFamilyProperties.directIndirectCommandBufferSize);

        UploadAllocation directInstanceUpload = renderGraph->AllocateTransient(totalCustomInstances * sizeof(Instance));
        auto* directInstanceBuffer = static_cast<Instance*>(directInstanceUpload.ptr);
        size_t directInstanceOffset = 0;
        for (const auto& customDraw : viewFamily.customStencilDraws) {
            for (const auto& inst : customDraw.instances) {
                directInstanceBuffer[directInstanceOffset++] = {
                    .primitiveIndex = inst.primitiveIndex,
                    .modelIndex = inst.modelIndex,
                    .materialIndex = inst.gpuMaterialIndex,
                    .bIsVisible = 0,
                    .lod = 0,
                };
            }
        }

        RenderPass& uploadDirectInstancesPass = renderGraph->AddPass("Upload Direct Instances", VK_PIPELINE_STAGE_2_COPY_BIT);
        uploadDirectInstancesPass.WriteTransferBuffer("direct_instance_buffer");
        uploadDirectInstancesPass.Execute([&,
                uploadOffset = directInstanceUpload.offset,
                uploadSize = totalCustomInstances * sizeof(Instance)](VkCommandBuffer cmd) {
                VkBufferCopy2 copy{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                    .srcOffset = uploadOffset,
                    .dstOffset = 0,
                    .size = uploadSize
                };

                VkCopyBufferInfo2 copyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = renderGraph->GetTransientUploadBuffer(),
                    .dstBuffer = renderGraph->GetBufferHandle("direct_instance_buffer"),
                    .regionCount = 1,
                    .pRegions = &copy
                };
                vkCmdCopyBuffer2(cmd, &copyInfo);
            });
    }*/
}

void RenderThread::SetupCascadedShadows(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const
{
    /*Core::ShadowConfiguration shadowConfig = viewFamily.shadowConfig;

    for (int32_t cascadeLevel = 0; cascadeLevel < SHADOW_CASCADE_COUNT; ++cascadeLevel) {
        std::string shadowMapName = "shadow_cascade_" + std::to_string(cascadeLevel);
        std::string shadowPassName = "Shadow Cascade Pass " + std::to_string(cascadeLevel);

        graph.CreateTexture(shadowMapName, TextureInfo{SHADOW_CASCADE_FORMAT, shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height, 1});

        std::string clearPassName = "Clear Shadow Buffers " + std::to_string(cascadeLevel);
        std::string visPassName = "Shadow Visibility " + std::to_string(cascadeLevel);
        std::string prefixLocalPassName = "Shadow Prefix Sum Local " + std::to_string(cascadeLevel);
        std::string prefixBlocksPassName = "Shadow Prefix Sum Blocks " + std::to_string(cascadeLevel);
        std::string prefixScatterPassName = "Shadow Prefix Sum Scatter " + std::to_string(cascadeLevel);
        std::string indirectPassName = "Shadow Compact and Indirect " + std::to_string(cascadeLevel);

        std::string instanceIndirectionName = "shadow_instance_indirection_" + std::to_string(cascadeLevel);
        std::string blockSumsName = "shadow_block_sums_" + std::to_string(cascadeLevel);
        std::string scannedBlockOffsetsName = "shadow_scanned_block_offsets_" + std::to_string(cascadeLevel);
        std::string primitiveRangePrefixSumName = "shadow_primitive_offset_prefix_sum_" + std::to_string(cascadeLevel);
        std::string primitiveCountersName = "shadow_primitive_counters_buffer_" + std::to_string(cascadeLevel);
        std::string indirectCommandName = "shadow_indirect_command_" + std::to_string(cascadeLevel);
        std::string indirectCountName = "shadow_indirect_count_" + std::to_string(cascadeLevel);

        renderGraph->CreateBuffer(instanceIndirectionName, renderFamilyProperties.instanceIndirectionBufferSize);
        renderGraph->CreateBuffer(primitiveRangePrefixSumName, renderFamilyProperties.primitivePrefixSumBufferSize);
        renderGraph->CreateBuffer(blockSumsName, renderFamilyProperties.primitivePrefixBlockSumBufferSize);
        renderGraph->CreateBuffer(scannedBlockOffsetsName, renderFamilyProperties.primitivePrefixBlockSumBufferSize);
        renderGraph->CreateBuffer(primitiveCountersName, renderFamilyProperties.primitiveCountersBufferSize);
        renderGraph->CreateBuffer(indirectCommandName, renderFamilyProperties.mainCommandBufferSize);
        renderGraph->CreateBuffer(indirectCountName, sizeof(InstancedMeshIndirectCountBuffer));

        RenderPass& clearPass = graph.AddPass(clearPassName, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        clearPass.WriteTransferBuffer(instanceIndirectionName);
        clearPass.WriteTransferBuffer(primitiveCountersName);
        clearPass.WriteTransferBuffer(indirectCommandName);
        clearPass.WriteTransferBuffer(indirectCountName);
        clearPass.Execute([&, instanceIndirectionName, primitiveCountersName, indirectCommandName, indirectCountName](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(instanceIndirectionName), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(primitiveCountersName), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(indirectCommandName), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(indirectCountName), 0, VK_WHOLE_SIZE, 0);
        });

        RenderPass& visibilityPass = graph.AddPass(visPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        visibilityPass.ReadBuffer("shadow_data");
        visibilityPass.ReadBuffer("primitive_buffer");
        visibilityPass.ReadBuffer("model_buffer");
        visibilityPass.ReadBuffer("primitive_to_range_map_buffer");
        visibilityPass.ReadWriteBuffer("instance_buffer");
        visibilityPass.ReadWriteBuffer(primitiveCountersName);
        visibilityPass.Execute([&, cascadeLevel, primitiveCountersName](VkCommandBuffer cmd) {
            VisibilityShadowsPushConstant pushData{
                .shadowData = graph.GetBufferAddress("shadow_data"),
                .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                .primitiveToPrimitiveRangeMapBuffer = graph.GetBufferAddress("primitive_to_range_map_buffer"),
                .primitiveCountersBuffer = graph.GetBufferAddress(primitiveCountersName),
                .instanceCount = renderFamilyProperties.instanceCount,
                .cascadeLevel = static_cast<uint32_t>(cascadeLevel),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_shadows_visibility");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityShadowsPushConstant), &pushData);
            uint32_t xDispatch = (renderFamilyProperties.instanceCount + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
            vkCmdDispatch(cmd, xDispatch, 1, 1);
        });

        RenderPass& prefixSumLocalPass = graph.AddPass(prefixLocalPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        prefixSumLocalPass.ReadBuffer(primitiveCountersName);
        prefixSumLocalPass.WriteBuffer(primitiveRangePrefixSumName);
        prefixSumLocalPass.WriteBuffer(blockSumsName);
        prefixSumLocalPass.WriteBuffer(indirectCountName);
        prefixSumLocalPass.Execute([&, primitiveCountersName, primitiveRangePrefixSumName, blockSumsName, indirectCountName](VkCommandBuffer cmd) {
            PrefixSumLocalPushConstant pc{
                .primitiveCountersBuffer = graph.GetBufferAddress(primitiveCountersName),
                .prefixSums = graph.GetBufferAddress(primitiveRangePrefixSumName),
                .blockSums = graph.GetBufferAddress(blockSumsName),
                .indirectCountBuffer = graph.GetBufferAddress(indirectCountName),
                .primitiveRangeCount = renderFamilyProperties.filteredPrimitiveCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_local");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t numWorkgroups = (renderFamilyProperties.filteredPrimitiveCount + INSTANCING_PREFIX_SUM_LOCAL_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_LOCAL_DISPATCH_X;
            vkCmdDispatch(cmd, numWorkgroups, 1, 1);
        });

        RenderPass& prefixSumBlocksPass = graph.AddPass(prefixBlocksPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        prefixSumBlocksPass.ReadBuffer(blockSumsName);
        prefixSumBlocksPass.WriteBuffer(scannedBlockOffsetsName);
        prefixSumBlocksPass.Execute([&, blockSumsName, scannedBlockOffsetsName](VkCommandBuffer cmd) {
            PrefixSumBlocksPushConstant pc{
                .blockSums = graph.GetBufferAddress(blockSumsName),
                .scannedBlockOffsets = graph.GetBufferAddress(scannedBlockOffsetsName),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_blocks");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });

        RenderPass& prefixSumScatterPass = graph.AddPass(prefixScatterPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        prefixSumScatterPass.ReadBuffer(scannedBlockOffsetsName);
        prefixSumScatterPass.ReadWriteBuffer(primitiveRangePrefixSumName);
        prefixSumScatterPass.Execute([&, primitiveRangePrefixSumName, scannedBlockOffsetsName](VkCommandBuffer cmd) {
            PrefixSumScatterPushConstant pc{
                .prefixSums = graph.GetBufferAddress(primitiveRangePrefixSumName),
                .scannedBlockOffsets = graph.GetBufferAddress(scannedBlockOffsetsName),
                .primitiveRangeCount = renderFamilyProperties.filteredPrimitiveCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_scatter");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t numWorkgroups = (renderFamilyProperties.filteredPrimitiveCount + INSTANCING_PREFIX_SUM_SCATTER_DISPATCH_X - 1) / INSTANCING_PREFIX_SUM_SCATTER_DISPATCH_X;
            vkCmdDispatch(cmd, numWorkgroups, 1, 1);
        });

        RenderPass& indirectPass = graph.AddPass(indirectPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        indirectPass.ReadBuffer("primitive_buffer");
        indirectPass.ReadBuffer("instance_buffer");
        indirectPass.ReadBuffer("primitive_to_range_map_buffer");
        indirectPass.ReadBuffer(primitiveRangePrefixSumName);
        indirectPass.WriteBuffer(instanceIndirectionName);
        indirectPass.WriteBuffer(indirectCommandName);
        indirectPass.ReadWriteBuffer(primitiveCountersName);
        indirectPass.Execute([&, primitiveRangePrefixSumName, instanceIndirectionName, indirectCommandName, primitiveCountersName](VkCommandBuffer cmd) {
            CompactAndIndirectPushConstant pc{
                .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                .primitiveToPrimitiveRangeMapBuffer = graph.GetBufferAddress("primitive_to_range_map_buffer"),
                .prefixSums = graph.GetBufferAddress(primitiveRangePrefixSumName),
                .instanceCount = renderFamilyProperties.instanceCount,

                .instanceIndirectionBuffer = graph.GetBufferAddress(instanceIndirectionName),
                .indirectBuffer = graph.GetBufferAddress(indirectCommandName),
                .primitiveCountersBuffer = graph.GetBufferAddress(primitiveCountersName),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_compact_and_indirect");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (renderFamilyProperties.instanceCount + (INSTANCING_CONSTRUCTION_DISPATCH_X - 1)) / INSTANCING_CONSTRUCTION_DISPATCH_X;
            vkCmdDispatch(cmd, xDispatch, 1, 1);
        });

        RenderPass& shadowPass = graph.AddPass(shadowPassName, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        shadowPass.ReadWriteDepthAttachment(shadowMapName);
        shadowPass.ReadBuffer("scene_data");
        shadowPass.ReadBuffer("shadow_data");
        shadowPass.ReadBuffer("model_buffer");
        shadowPass.ReadBuffer("vertex_buffer");
        shadowPass.ReadBuffer("meshlet_vertex_buffer");
        shadowPass.ReadBuffer("meshlet_triangle_buffer");
        shadowPass.ReadBuffer("meshlet_buffer");
        shadowPass.ReadBuffer("instance_buffer");
        shadowPass.ReadBuffer(instanceIndirectionName);
        shadowPass.ReadIndirectBuffer(indirectCommandName);
        shadowPass.ReadIndirectCountBuffer(indirectCountName);
        shadowPass.Execute([&, shadowConfig, cascadeLevel, shadowMapName, instanceIndirectionName, indirectCommandName, indirectCountName](VkCommandBuffer cmd) {
            VkViewport viewport = VkHelpers::GenerateViewport(shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor = VkHelpers::GenerateScissor(shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(shadowMapName), &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height}, nullptr, 0,
                                                                        &depthAttachment);

            vkCmdBeginRendering(cmd, &renderInfo);

            ShadowMeshShadingPushConstant pushConstants{
                .shadowData = graph.GetBufferAddress("shadow_data"),
                .vertexBuffer = graph.GetBufferAddress("vertex_buffer"),
                .meshletVerticesBuffer = graph.GetBufferAddress("meshlet_vertex_buffer"),
                .meshletTrianglesBuffer = graph.GetBufferAddress("meshlet_triangle_buffer"),
                .meshletBuffer = graph.GetBufferAddress("meshlet_buffer"),
                .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                .instanceIndirectionBuffer = graph.GetBufferAddress(instanceIndirectionName),
                .indirectBuffer = graph.GetBufferAddress(indirectCommandName),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .cascadeIndex = static_cast<uint32_t>(cascadeLevel),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("shadow_cascade_instanced");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
            vkCmdSetDepthBias(cmd, -shadowConfig.cascadePreset.biases[cascadeLevel].linear, 0.0f, -shadowConfig.cascadePreset.biases[cascadeLevel].sloped);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(ShadowMeshShadingPushConstant), &pushConstants);

            vkCmdDrawMeshTasksIndirectCountEXT(cmd, graph.GetBufferHandle(indirectCommandName), 0,
                                               graph.GetBufferHandle(indirectCountName), offsetof(InstancedMeshIndirectCountBuffer, indirectCount),
                                               renderFamilyProperties.filteredPrimitiveCount * LOD_COUNT,
                                               sizeof(InstancedMeshIndirectDrawParameters));

            vkCmdEndRendering(cmd);
        });
    }*/
}

void RenderThread::SetupMainGeometryPass(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties,
                                         std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneIndex, bool bClearTargets) const
{
    // Create and Clear
    {
        graph.CreateBuffer("instance_meshlet_offsets", renderFamilyProperties.instanceMeshletOffsetsBufferSize);
        graph.CreateBuffer("level1_sums", renderFamilyProperties.level1SumsBufferSize);
        graph.CreateBuffer("level1_block_sums", renderFamilyProperties.level1BlockSumsBufferSize);
        graph.CreateBuffer("level2_sums", renderFamilyProperties.level2SumsBufferSize);
        graph.CreateBuffer("level2_block_sums", renderFamilyProperties.level2BlockSumsBufferSize);
        graph.CreateBuffer("scanned_level2_block_sums", renderFamilyProperties.scannedLevel2BlockSumsBufferSize);
        graph.CreateBuffer("intermediate_meshlets", renderFamilyProperties.intermediateMeshletBufferSize);
        graph.CreateBuffer("meshlet_level1_sums", renderFamilyProperties.meshletLevel1SumsBufferSize);
        graph.CreateBuffer("meshlet_level1_block_sums", renderFamilyProperties.meshletLevel1BlockSumsBufferSize);
        graph.CreateBuffer("meshlet_level2_sums", renderFamilyProperties.meshletLevel2SumsBufferSize);
        graph.CreateBuffer("meshlet_level2_block_sums", renderFamilyProperties.meshletLevel2BlockSumsBufferSize);
        graph.CreateBuffer("meshlet_scanned_level2_block_sums", renderFamilyProperties.meshletScannedLevel2BlockSumsBufferSize);
        graph.CreateBuffer("visible_meshlets", renderFamilyProperties.visibleMeshletsBufferSize);
        graph.CreateBuffer("meshlet_count_dispatch_args", sizeof(InstancingMeshletDispatchIndirect));
        graph.CreateBuffer("compacted_meshlet_dispatch_args", sizeof(InstancingCompactedMeshletDispatchIndirect));

        RenderPass& clearPass = graph.AddPass("Clear Temp Instancing Buffers", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        clearPass.WriteTransferBuffer("instance_meshlet_offsets");
        clearPass.WriteTransferBuffer("level1_sums");
        clearPass.WriteTransferBuffer("level1_block_sums");
        clearPass.WriteTransferBuffer("level2_sums");
        clearPass.WriteTransferBuffer("level2_block_sums");
        clearPass.WriteTransferBuffer("scanned_level2_block_sums");
        clearPass.WriteTransferBuffer("intermediate_meshlets");
        clearPass.WriteTransferBuffer("meshlet_level1_sums");
        clearPass.WriteTransferBuffer("meshlet_level1_block_sums");
        clearPass.WriteTransferBuffer("meshlet_level2_sums");
        clearPass.WriteTransferBuffer("meshlet_level2_block_sums");
        clearPass.WriteTransferBuffer("meshlet_scanned_level2_block_sums");
        clearPass.WriteTransferBuffer("visible_meshlets");
        clearPass.WriteTransferBuffer("meshlet_count_dispatch_args");
        clearPass.WriteTransferBuffer("compacted_meshlet_dispatch_args");
        clearPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("instance_meshlet_offsets"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("level1_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("level1_block_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("level2_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("level2_block_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("scanned_level2_block_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("intermediate_meshlets"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("meshlet_level1_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("meshlet_level1_block_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("meshlet_level2_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("meshlet_level2_block_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("meshlet_scanned_level2_block_sums"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("visible_meshlets"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("meshlet_count_dispatch_args"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("compacted_meshlet_dispatch_args"), 0, VK_WHOLE_SIZE, 0);
        });
    }

    // Instance LOD
    {
        RenderPass& instanceLODPass = graph.AddPass("Instance LOD Selection", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        instanceLODPass.ReadBuffer("scene_data");
        instanceLODPass.ReadBuffer("primitive_buffer");
        instanceLODPass.ReadBuffer("model_buffer");
        instanceLODPass.ReadBuffer("instance_buffer");
        instanceLODPass.WriteBuffer("instance_meshlet_offsets");
        instanceLODPass.Execute([&](VkCommandBuffer cmd) {
            InstanceLODPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"),
                .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                .instanceMeshletOffsets = graph.GetBufferAddress("instance_meshlet_offsets"),
                .instanceCount = renderFamilyProperties.instanceCount,
                .sceneDataIndex = 0,
                .lodBias = LOD_BIAS,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_instance_lod");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (renderFamilyProperties.instanceCount + 255) / 256;
            vkCmdDispatch(cmd, xDispatch, 1, 1);
        });
    }

    uint32_t instanceCount = renderFamilyProperties.instanceCount;

    // Prefix Sum for Expansion
    {
        uint32_t level1BlockCount = (instanceCount + 255) / 256;

        RenderPass& upsweep1Pass = graph.AddPass("Prefix Sum Upsweep 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        upsweep1Pass.ReadBuffer("instance_meshlet_offsets");
        upsweep1Pass.WriteBuffer("level1_sums");
        upsweep1Pass.WriteBuffer("level1_block_sums");
        upsweep1Pass.Execute([&, instanceCount, level1BlockCount](VkCommandBuffer cmd) {
            PrefixSumUpsweep1PushConstant pc{
                .instanceMeshletOffsets = graph.GetBufferAddress("instance_meshlet_offsets"),
                .level1Sums = graph.GetBufferAddress("level1_sums"),
                .level1BlockSums = graph.GetBufferAddress("level1_block_sums"),
                .elementCount = instanceCount,
                .blockCount = level1BlockCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_up_1");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, level1BlockCount, 1, 1);
        });

        uint32_t level2BlockCount = (level1BlockCount + 255) / 256;


        if (level2BlockCount > 1) {
            RenderPass& upsweep2Pass = graph.AddPass("Prefix Sum Upsweep 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            upsweep2Pass.ReadBuffer("level1_block_sums");
            upsweep2Pass.WriteBuffer("level2_sums");
            upsweep2Pass.WriteBuffer("level2_block_sums");
            upsweep2Pass.Execute([&, level1BlockCount, level2BlockCount](VkCommandBuffer cmd) {
                PrefixSumUpsweep2PushConstant pc{
                    .level1BlockSums = graph.GetBufferAddress("level1_block_sums"),
                    .level2Sums = graph.GetBufferAddress("level2_sums"),
                    .level2BlockSums = graph.GetBufferAddress("level2_block_sums"),
                    .elementCount = level1BlockCount,
                    .blockCount = level2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_up_2");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level2BlockCount, 1, 1);
            });

            RenderPass& scanBlocksPass = graph.AddPass("Prefix Sum Scan Blocks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            scanBlocksPass.ReadBuffer("level2_block_sums");
            scanBlocksPass.WriteBuffer("scanned_level2_block_sums");
            scanBlocksPass.Execute([&, level2BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress("level2_block_sums"),
                    .scannedLevel2BlockSums = graph.GetBufferAddress("scanned_level2_block_sums"),
                    .blockCount = level2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& downsweep1Pass = graph.AddPass("Prefix Sum Downsweep 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            downsweep1Pass.ReadBuffer("scanned_level2_block_sums");
            downsweep1Pass.ReadWriteBuffer("level2_sums");
            downsweep1Pass.Execute([&, level1BlockCount, level2BlockCount](VkCommandBuffer cmd) {
                PrefixSumDownsweep1PushConstant pc{
                    .scannedLevel2BlockSums = graph.GetBufferAddress("scanned_level2_block_sums"),
                    .level2Sums = graph.GetBufferAddress("level2_sums"),
                    .elementCount = level1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_down_1");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, level2BlockCount, 1, 1);
            });
        }
        else {
            RenderPass& scanBlocksPass = graph.AddPass("Prefix Sum Scan Blocks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            scanBlocksPass.ReadBuffer("level1_block_sums");
            scanBlocksPass.WriteBuffer("scanned_level2_block_sums");
            scanBlocksPass.Execute([&, level1BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress("level1_block_sums"),
                    .scannedLevel2BlockSums = graph.GetBufferAddress("scanned_level2_block_sums"),
                    .blockCount = level1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        RenderPass& downsweep2Pass = graph.AddPass("Prefix Sum Downsweep 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        downsweep2Pass.ReadBuffer("level1_sums");
        if (level2BlockCount > 1) {
            downsweep2Pass.ReadBuffer("level2_sums");
        }
        else {
            downsweep2Pass.ReadBuffer("scanned_level2_block_sums");
        }
        downsweep2Pass.WriteBuffer("instance_meshlet_offsets");
        downsweep2Pass.Execute([&, level2BlockCount, instanceCount, level1BlockCount](VkCommandBuffer cmd) {
            PrefixSumDownsweep2PushConstant pc{
                .level1Sums = graph.GetBufferAddress("level1_sums"),
                .level2Sums = level2BlockCount > 1 ? graph.GetBufferAddress("level2_sums") : graph.GetBufferAddress("scanned_level2_block_sums"),
                .instanceMeshletOffsets = graph.GetBufferAddress("instance_meshlet_offsets"),
                .elementCount = instanceCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_down_2");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            vkCmdDispatch(cmd, level1BlockCount, 1, 1);
        });

        RenderPass& totalMeshletCalculator = graph.AddPass("Total Meshlet Count", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        totalMeshletCalculator.ReadBuffer("instance_meshlet_offsets");
        totalMeshletCalculator.WriteBuffer("meshlet_count_dispatch_args");
        totalMeshletCalculator.Execute([&, instanceCount](VkCommandBuffer cmd) {
            TotalMeshletCountPushConstant pc{
                .indirectDispatchBuffer = graph.GetBufferAddress("meshlet_count_dispatch_args"),
                .instanceMeshletOffsets = graph.GetBufferAddress("instance_meshlet_offsets"),
                .instanceCount = instanceCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_total_meshlet_count");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            vkCmdDispatch(cmd, 1, 1, 1);
        });


        RenderPass& readbackMeshletCount = graph.AddPass("Readback Meshlet Count", VK_PIPELINE_STAGE_2_COPY_BIT);
        readbackMeshletCount.ReadTransferBuffer("meshlet_count_dispatch_args");
        readbackMeshletCount.Execute([&](VkCommandBuffer cmd) {
            VkBufferCopy copy;
            copy.srcOffset = offsetof(InstancingMeshletDispatchIndirect, totalMeshlets);
            copy.dstOffset = offsetof(ReadbackStruct, meshletCount);
            copy.size = sizeof(uint32_t);

            vkCmdCopyBuffer(
                cmd,
                renderGraph->GetBufferHandle("meshlet_count_dispatch_args"),
                graph.GetReadback(),
                1,
                &copy
            );
        });
    }

    uint32_t highestMeshletCount = renderFamilyProperties.visibleMeshletUpperBound;

    // Expand Instance to Meshlet
    {
        RenderPass& expandInstancesToMeshlets = graph.AddPass("Expand Instance To Meshlet", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        expandInstancesToMeshlets.ReadBuffer("instance_meshlet_offsets");
        expandInstancesToMeshlets.ReadIndirectBuffer("meshlet_count_dispatch_args");
        expandInstancesToMeshlets.WriteBuffer("intermediate_meshlets");
        expandInstancesToMeshlets.Execute([&, instanceCount, highestMeshletCount](VkCommandBuffer cmd) {
            ExpandMeshletsPushConstant pc{
                .indirectDispatchBuffer = graph.GetBufferAddress("meshlet_count_dispatch_args"),
                .instanceMeshletOffsets = graph.GetBufferAddress("instance_meshlet_offsets"),
                .intermediateMeshlets = graph.GetBufferAddress("intermediate_meshlets"),
                .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .meshletBuffer = graph.GetBufferAddress("meshlet_buffer"),
                .sceneData = graph.GetBufferAddress("scene_data"),
                .sceneDataIndex = 0,
                .instanceCount = instanceCount,
                .currentFrameBufferMeshletLimit = highestMeshletCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_expand_instance_to_meshlet");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatchIndirect(cmd, graph.GetBufferHandle("meshlet_count_dispatch_args"), offsetof(InstancingMeshletDispatchIndirect, x));
        });
    }

    // Prefix Sum for Compaction
    {
        uint32_t meshletLevel1BlockCount = (highestMeshletCount + 255) / 256;
        uint32_t meshletLevel2BlockCount = (meshletLevel1BlockCount + 255) / 256;

        RenderPass& meshletUpsweep1Pass = graph.AddPass("Meshlet Visibility Prefix Sum Upsweep 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        meshletUpsweep1Pass.ReadBuffer("intermediate_meshlets");
        meshletUpsweep1Pass.WriteBuffer("meshlet_level1_sums");
        meshletUpsweep1Pass.WriteBuffer("meshlet_level1_block_sums");
        meshletUpsweep1Pass.ReadIndirectBuffer("meshlet_count_dispatch_args");
        meshletUpsweep1Pass.Execute([&, meshletLevel1BlockCount](VkCommandBuffer cmd) {
            MeshletVisibilityPrefixSumUpsweep1PushConstant pc{
                .intermediateMeshlets = graph.GetBufferAddress("intermediate_meshlets"),

                .indirectDispatchBuffer = graph.GetBufferAddress("meshlet_count_dispatch_args"),
                .meshletLevel1Sums = graph.GetBufferAddress("meshlet_level1_sums"),
                .meshletLevel1BlockSums = graph.GetBufferAddress("meshlet_level1_block_sums"),
                .blockCount = meshletLevel1BlockCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_meshlet_visibility_prefix_sum_up_1");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatchIndirect(cmd, graph.GetBufferHandle("meshlet_count_dispatch_args"), offsetof(InstancingMeshletDispatchIndirect, x));
        });

        if (meshletLevel2BlockCount > 1) {
            RenderPass& meshletUpsweep2Pass = graph.AddPass("Meshlet Visibility Prefix Sum Upsweep 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletUpsweep2Pass.ReadBuffer("meshlet_level1_block_sums");
            meshletUpsweep2Pass.WriteBuffer("meshlet_level2_sums");
            meshletUpsweep2Pass.WriteBuffer("meshlet_level2_block_sums");
            meshletUpsweep2Pass.Execute([&, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                PrefixSumUpsweep2PushConstant pc{
                    .level1BlockSums = graph.GetBufferAddress("meshlet_level1_block_sums"),
                    .level2Sums = graph.GetBufferAddress("meshlet_level2_sums"),
                    .level2BlockSums = graph.GetBufferAddress("meshlet_level2_block_sums"),
                    .elementCount = meshletLevel1BlockCount,
                    .blockCount = meshletLevel2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_up_2");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
            });

            RenderPass& meshletScanBlocksPass = graph.AddPass("Meshlet Visibility Prefix Sum Scan Blocks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletScanBlocksPass.ReadBuffer("meshlet_level2_block_sums");
            meshletScanBlocksPass.WriteBuffer("meshlet_scanned_level2_block_sums");
            meshletScanBlocksPass.Execute([&, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress("meshlet_level2_block_sums"),
                    .scannedLevel2BlockSums = graph.GetBufferAddress("meshlet_scanned_level2_block_sums"),
                    .blockCount = meshletLevel2BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& meshletDownsweep1Pass = graph.AddPass("Meshlet Visibility Prefix Sum Downsweep 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletDownsweep1Pass.ReadBuffer("meshlet_scanned_level2_block_sums");
            meshletDownsweep1Pass.ReadWriteBuffer("meshlet_level2_sums");
            meshletDownsweep1Pass.Execute([&, meshletLevel1BlockCount, meshletLevel2BlockCount](VkCommandBuffer cmd) {
                PrefixSumDownsweep1PushConstant pc{
                    .scannedLevel2BlockSums = graph.GetBufferAddress("meshlet_scanned_level2_block_sums"),
                    .level2Sums = graph.GetBufferAddress("meshlet_level2_sums"),
                    .elementCount = meshletLevel1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum_down_1");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, meshletLevel2BlockCount, 1, 1);
            });
        }
        else {
            RenderPass& meshletScanBlocksPass = graph.AddPass("Meshlet Visibility Prefix Sum Scan Blocks", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            meshletScanBlocksPass.ReadBuffer("meshlet_level1_block_sums");
            meshletScanBlocksPass.WriteBuffer("meshlet_scanned_level2_block_sums");
            meshletScanBlocksPass.Execute([&, meshletLevel1BlockCount](VkCommandBuffer cmd) {
                PrefixSumScanBlocksPushConstant pc{
                    .level2BlockSums = graph.GetBufferAddress("meshlet_level1_block_sums"),
                    .scannedLevel2BlockSums = graph.GetBufferAddress("meshlet_scanned_level2_block_sums"),
                    .blockCount = meshletLevel1BlockCount,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_scan_blocks");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, 1, 1, 1);
            });
        }

        RenderPass& meshletDownsweep2Pass = graph.AddPass("Meshlet Visibility Prefix Sum Downsweep 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        meshletDownsweep2Pass.ReadBuffer("meshlet_level1_sums");
        meshletDownsweep2Pass.ReadBuffer("intermediate_meshlets");
        if (meshletLevel2BlockCount > 1) {
            meshletDownsweep2Pass.ReadBuffer("meshlet_level2_sums");
        }
        else {
            meshletDownsweep2Pass.ReadBuffer("meshlet_scanned_level2_block_sums");
        }
        meshletDownsweep2Pass.WriteBuffer("visible_meshlets");
        meshletDownsweep2Pass.WriteBuffer("compacted_meshlet_dispatch_args");
        meshletDownsweep2Pass.ReadIndirectBuffer("meshlet_count_dispatch_args");
        meshletDownsweep2Pass.Execute([&, meshletLevel2BlockCount, highestMeshletCount](VkCommandBuffer cmd) {
            MeshletVisibilityPrefixSumDownsweep2PushConstant pc{
                .meshletLevel1Sums = graph.GetBufferAddress("meshlet_level1_sums"),
                .meshletLevel2Sums = meshletLevel2BlockCount > 1 ? graph.GetBufferAddress("meshlet_level2_sums") : graph.GetBufferAddress("meshlet_scanned_level2_block_sums"),
                .intermediateMeshlets = graph.GetBufferAddress("intermediate_meshlets"),
                .indirectDispatchBuffer = graph.GetBufferAddress("meshlet_count_dispatch_args"),

                .visibleMeshlets = graph.GetBufferAddress("visible_meshlets"),
                .compactedDispatchBuffer = graph.GetBufferAddress("compacted_meshlet_dispatch_args"),
                .currentFrameBufferMeshletLimit = highestMeshletCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_meshlet_visibility_prefix_sum_down_2");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatchIndirect(cmd, graph.GetBufferHandle("meshlet_count_dispatch_args"), offsetof(InstancingMeshletDispatchIndirect, x));
        });


        RenderPass& compactedDispatchCalc = graph.AddPass("Compacted Meshlet Dispatch Calculation", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        compactedDispatchCalc.ReadWriteBuffer("compacted_meshlet_dispatch_args");
        compactedDispatchCalc.Execute([&, highestMeshletCount](VkCommandBuffer cmd) {
            CompactedMeshletDispatchPushConstant pc{
                .compactedDispatchBuffer = graph.GetBufferAddress("compacted_meshlet_dispatch_args"),
                .currentFrameBufferMeshletLimit = highestMeshletCount,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_compacted_meshlet_dispatch");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    RenderPass& instancedMeshShading = graph.AddPass("Instanced Mesh Shading", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    instancedMeshShading.WriteColorAttachment(targets.albedo);
    instancedMeshShading.WriteColorAttachment(targets.normal);
    instancedMeshShading.WriteColorAttachment(targets.pbr);
    instancedMeshShading.WriteColorAttachment(targets.emissive);
    instancedMeshShading.WriteColorAttachment(targets.velocity);
    instancedMeshShading.ReadWriteDepthAttachment(targets.depthStencil);
    instancedMeshShading.ReadBuffer("scene_data");
    instancedMeshShading.ReadBuffer("model_buffer");
    instancedMeshShading.ReadBuffer("material_buffer");
    instancedMeshShading.ReadBuffer("instance_buffer");
    instancedMeshShading.ReadBuffer("visible_meshlets");
    instancedMeshShading.ReadIndirectBuffer("compacted_meshlet_dispatch_args");
    instancedMeshShading.Execute([&, sceneIndex, width = renderExtent[0], height = renderExtent[1], bClearTargets](VkCommandBuffer cmd) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        constexpr VkClearValue colorClear = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}};
        constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
        const VkClearValue* _colorClear = bClearTargets ? &colorClear : nullptr;
        const VkClearValue* _depthClear = bClearTargets ? &depthClear : nullptr;

        const VkRenderingAttachmentInfo albedoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.albedo), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo normalAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.normal), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo pbrAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.pbr), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo emissiveTarget = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.emissive), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.velocity), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), _depthClear,
                                                                                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), _depthClear,
                                                                                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        const VkRenderingAttachmentInfo colorAttachments[] = {albedoAttachment, normalAttachment, pbrAttachment, emissiveTarget, velocityAttachment};
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 5, &depthAttachment, &stencilAttachment);

        vkCmdBeginRendering(cmd, &renderInfo);

        InstancedMeshShadingPushConstant pushConstants{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .vertexBuffer = graph.GetBufferAddress("vertex_buffer"),
            .meshletVerticesBuffer = graph.GetBufferAddress("meshlet_vertex_buffer"),
            .meshletTrianglesBuffer = graph.GetBufferAddress("meshlet_triangle_buffer"),
            .meshletBuffer = graph.GetBufferAddress("meshlet_buffer"),
            .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
            .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
            .materialBuffer = graph.GetBufferAddress("material_buffer"),
            .modelBuffer = graph.GetBufferAddress("model_buffer"),
            .visibleMeshlets = graph.GetBufferAddress("visible_meshlets"),
            .compactedDispatchBuffer = graph.GetBufferAddress("compacted_meshlet_dispatch_args"),
            .sceneDataIndex = sceneIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("mesh_shading_instanced");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(InstancedMeshShadingPushConstant), &pushConstants);

        vkCmdDrawMeshTasksIndirectEXT(
            cmd,
            graph.GetBufferHandle("compacted_meshlet_dispatch_args"),
            offsetof(InstancingCompactedMeshletDispatchIndirect, x),
            1,
            sizeof(InstancingCompactedMeshletDispatchIndirect));

        vkCmdEndRendering(cmd);
    });
}

void RenderThread::SetupDirectGeometryPass(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties, std::array<uint32_t, 2> renderExtent,
                                           const GBufferTargets& targets,
                                           uint32_t sceneIndex, bool bClearTargets) const
{
    /*if (viewFamily.customStencilDraws.empty()) { return; }

    size_t totalInstances = 0;
    for (const auto& customDraw : viewFamily.customStencilDraws) {
        totalInstances += customDraw.instances.size();
    }
    if (totalInstances == 0) { return; }

    RenderPass& buildIndirectPass = graph.AddPass("Build Direct Indirect Commands", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    buildIndirectPass.ReadBuffer("primitive_buffer");
    buildIndirectPass.ReadWriteBuffer("direct_instance_buffer");
    buildIndirectPass.WriteBuffer("direct_indirect_command_buffer");
    buildIndirectPass.Execute([&, totalInstances](VkCommandBuffer cmd) {
        BuildDirectIndirectPushConstant pushConstant{
            .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
            .instanceBuffer = graph.GetBufferAddress("direct_instance_buffer"),
            .indirectCommandBuffer = graph.GetBufferAddress("direct_indirect_command_buffer"),
            .modelBuffer = graph.GetBufferAddress("model_buffer"),
            .sceneData = graph.GetBufferAddress("scene_data"),
            .sceneDataIndex = sceneIndex,
            .instanceCount = static_cast<uint32_t>(totalInstances),
            .lodBias = LOD_BIAS,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("direct_mesh_shading_build_indirect");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BuildDirectIndirectPushConstant), &pushConstant);
        uint32_t xDispatch = (totalInstances + 63) / 64;
        vkCmdDispatch(cmd, xDispatch, 1, 1);
    });

    RenderPass& directMeshShading = graph.AddPass("Direct Mesh Shading", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    directMeshShading.WriteColorAttachment(targets.albedo);
    directMeshShading.WriteColorAttachment(targets.normal);
    directMeshShading.WriteColorAttachment(targets.pbr);
    directMeshShading.WriteColorAttachment(targets.emissive);
    directMeshShading.WriteColorAttachment(targets.velocity);
    directMeshShading.ReadWriteDepthAttachment(targets.depthStencil);
    directMeshShading.ReadBuffer("scene_data");
    directMeshShading.ReadBuffer("model_buffer");
    directMeshShading.ReadBuffer("material_buffer");
    directMeshShading.ReadBuffer("direct_instance_buffer");
    directMeshShading.ReadIndirectBuffer("direct_indirect_command_buffer");
    directMeshShading.Execute([&, sceneIndex, width = renderExtent[0], height = renderExtent[1], bClearTargets](VkCommandBuffer cmd) {
        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        constexpr VkClearValue colorClear = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}};
        constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
        const VkClearValue* _colorClear = bClearTargets ? &colorClear : nullptr;
        const VkClearValue* _depthClear = bClearTargets ? &depthClear : nullptr;
        const VkRenderingAttachmentInfo albedoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.albedo), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo normalAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.normal), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo pbrAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.pbr), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo emissiveTarget = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.emissive), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.velocity), _colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), _depthClear,
                                                                                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo stencilAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targets.depthStencil), _depthClear,
                                                                                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        const VkRenderingAttachmentInfo colorAttachments[] = {albedoAttachment, normalAttachment, pbrAttachment, emissiveTarget, velocityAttachment};
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, colorAttachments, 5, &depthAttachment, &stencilAttachment);

        vkCmdBeginRendering(cmd, &renderInfo);

        DirectMeshShadingPushConstant pushConstants{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .vertexBuffer = graph.GetBufferAddress("vertex_buffer"),
            .meshletVerticesBuffer = graph.GetBufferAddress("meshlet_vertex_buffer"),
            .meshletTrianglesBuffer = graph.GetBufferAddress("meshlet_triangle_buffer"),
            .meshletBuffer = graph.GetBufferAddress("meshlet_buffer"),
            .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
            .instanceBuffer = graph.GetBufferAddress("direct_instance_buffer"),
            .materialBuffer = graph.GetBufferAddress("material_buffer"),
            .modelBuffer = graph.GetBufferAddress("model_buffer"),
            .sceneDataIndex = sceneIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("portal_rendering");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout,
                           VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DirectMeshShadingPushConstant), &pushConstants);

        uint32_t instanceOffset = 0;
        for (const auto& customDraw : viewFamily.customStencilDraws) {
            if (customDraw.instances.empty()) continue;

            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, customDraw.stencilValue);
            vkCmdDrawMeshTasksIndirectEXT(cmd, graph.GetBufferHandle("direct_indirect_command_buffer"),
                                          instanceOffset * sizeof(DrawMeshTasksIndirectCommand),
                                          static_cast<uint32_t>(customDraw.instances.size()),
                                          sizeof(DrawMeshTasksIndirectCommand));

            instanceOffset += customDraw.instances.size();
        }

        vkCmdEndRendering(cmd);
    });*/
}

void RenderThread::SetupGroundTruthAmbientOcclusion(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets,
                                                    uint32_t sceneDataIndex) const
{
    const Core::GTAOConfiguration& gtaoConfig = viewFamily.gtaoConfig;

    graph.CreateTexture("gtao_depth", TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 5});
    graph.CreateTexture("gtao_ao", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});
    graph.CreateTexture("gtao_edges", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});
    // Denoise pass(es) - typically run 2-3 times for better quality
    graph.CreateTexture("gtao_temp", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});
    graph.CreateTexture("gtao_filtered", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});

    RenderPass& depthPrepass = graph.AddPass("GTAO Depth Prepass", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    depthPrepass.ReadBuffer("scene_data");
    depthPrepass.ReadSampledImage(targets.depthStencil);
    depthPrepass.WriteStorageImage("gtao_depth");
    depthPrepass.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        GTAODepthPrepassPushConstant pc{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .inputDepth = graph.GetDepthOnlySampledImageViewDescriptorIndex(targets.depthStencil),
            .outputDepth0 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 0),
            .outputDepth1 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 1),
            .outputDepth2 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 2),
            .outputDepth3 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 3),
            .outputDepth4 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 4),
            .effectRadius = gtaoConfig.effectRadius,
            .effectFalloffRange = gtaoConfig.effectFalloffRange,
            .radiusMultiplier = gtaoConfig.radiusMultiplier,
            .sceneDataIndex = sceneDataIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_depth_prepass");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);


        uint32_t xDispatch = (width / 2 + GTAO_DEPTH_PREPASS_DISPATCH_X - 1) / GTAO_DEPTH_PREPASS_DISPATCH_X;
        uint32_t yDispatch = (height / 2 + GTAO_DEPTH_PREPASS_DISPATCH_Y - 1) / GTAO_DEPTH_PREPASS_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& gtaoMainPass = graph.AddPass("GTAO Main", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    gtaoMainPass.ReadSampledImage("gtao_depth");
    gtaoMainPass.ReadSampledImage(targets.normal);
    gtaoMainPass.WriteStorageImage("gtao_ao");
    gtaoMainPass.WriteStorageImage("gtao_edges");
    gtaoMainPass.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        GTAOMainPushConstant pc{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .prefilteredDepthIndex = graph.GetSampledImageViewDescriptorIndex("gtao_depth"),
            .normalBufferIndex = graph.GetSampledImageViewDescriptorIndex(targets.normal),
            .aoOutputIndex = graph.GetStorageImageViewDescriptorIndex("gtao_ao"),
            .edgeDataIndex = graph.GetStorageImageViewDescriptorIndex("gtao_edges"),

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

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_main");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& denoise1 = graph.AddPass("GTAO Denoise 1", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    denoise1.ReadSampledImage("gtao_ao");
    denoise1.ReadSampledImage("gtao_edges");
    denoise1.WriteStorageImage("gtao_temp");
    denoise1.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        GTAODenoisePushConstant pc{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .rawAOIndex = graph.GetSampledImageViewDescriptorIndex("gtao_ao"),
            .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex("gtao_edges"),
            .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex("gtao_temp"),
            .denoiseBlurBeta = 1e4f,
            .isFinalDenoisePass = 0,
            .sceneDataIndex = sceneDataIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_denoise");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& denoise2 = graph.AddPass("GTAO Denoise 2", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    denoise2.ReadSampledImage("gtao_temp");
    denoise2.ReadSampledImage("gtao_edges");
    denoise2.WriteStorageImage("gtao_filtered");
    denoise2.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        GTAODenoisePushConstant pc{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .rawAOIndex = graph.GetSampledImageViewDescriptorIndex("gtao_temp"),
            .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex("gtao_edges"),
            .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex("gtao_filtered"),
            .denoiseBlurBeta = gtaoConfig.denoiseBlurBeta,
            .isFinalDenoisePass = 1,
            .sceneDataIndex = sceneDataIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_denoise");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}


void RenderThread::SetupShadowsResolve(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const
{
    renderGraph->CreateTexture("shadows_resolve_target", TextureInfo{VK_FORMAT_R8G8_UNORM, renderExtent[0], renderExtent[1], 1});
    RenderPass& shadowsResolvePass = graph.AddPass("Shadows Resolve", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    shadowsResolvePass.ReadSampledImage(targets.normal);
    shadowsResolvePass.ReadSampledImage(targets.depthStencil);

    bool bHasGTAO = graph.HasTexture("gtao_filtered");
    if (bHasGTAO) {
        shadowsResolvePass.ReadSampledImage("gtao_filtered");
    }

    bool bHasShadows = graph.HasTexture("shadow_cascade_0");
    if (bHasShadows) {
        shadowsResolvePass.ReadSampledImage("shadow_cascade_0");
        shadowsResolvePass.ReadSampledImage("shadow_cascade_1");
        shadowsResolvePass.ReadSampledImage("shadow_cascade_2");
        shadowsResolvePass.ReadSampledImage("shadow_cascade_3");
    }

    shadowsResolvePass.ReadBuffer("scene_data");
    shadowsResolvePass.ReadBuffer("shadow_data");
    shadowsResolvePass.ReadBuffer("light_data");
    shadowsResolvePass.WriteStorageImage("shadows_resolve_target");
    shadowsResolvePass.Execute([&, bHasShadows, bHasGTAO, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("shadows_resolve");

        glm::ivec4 csmIndices{-1, -1, -1, -1};
        if (bHasShadows) {
            csmIndices.x = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex("shadow_cascade_0"));
            csmIndices.y = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex("shadow_cascade_1"));
            csmIndices.z = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex("shadow_cascade_2"));
            csmIndices.w = static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex("shadow_cascade_3"));
        }

        int32_t gtaoIndex = bHasGTAO ? static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex("gtao_filtered")) : -1;

        ShadowsResolvePushConstant pc{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .shadowData = graph.GetBufferAddress("shadow_data"),
            .lightData = graph.GetBufferAddress("light_data"),
            .gtaoFilteredIndex = gtaoIndex,
            .outputImageIndex = graph.GetStorageImageViewDescriptorIndex("shadows_resolve_target"),
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
    RenderPass& deferredResolvePass = graph.AddPass("Deferred Resolve", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    deferredResolvePass.ReadBuffer("scene_data");
    deferredResolvePass.ReadBuffer("light_data");
    deferredResolvePass.ReadSampledImage(targets.albedo);
    deferredResolvePass.ReadSampledImage(targets.normal);
    deferredResolvePass.ReadSampledImage(targets.pbr);
    deferredResolvePass.ReadSampledImage(targets.emissive);
    deferredResolvePass.ReadSampledImage(targets.depthStencil);
    deferredResolvePass.ReadSampledImage("shadows_resolve_target");
    deferredResolvePass.WriteStorageImage(targets.outFinalColor);
    deferredResolvePass.Execute([&, width = renderExtent[0], height = renderExtent[1], sceneDataIndex](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("deferred_resolve");

        DeferredResolvePushConstant pushData{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .lightData = graph.GetBufferAddress("light_data"),
            .albedoIndex = graph.GetSampledImageViewDescriptorIndex(targets.albedo),
            .normalIndex = graph.GetSampledImageViewDescriptorIndex(targets.normal),
            .pbrIndex = graph.GetSampledImageViewDescriptorIndex(targets.pbr),
            .emissiveIndex = graph.GetSampledImageViewDescriptorIndex(targets.emissive),
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(targets.depthStencil),
            .shadowsIndex = graph.GetSampledImageViewDescriptorIndex("shadows_resolve_target"),
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
    RenderPass& portalCompositePass = graph.AddPass("Portal Composite", VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    portalCompositePass.ReadSampledImage(portalTargets.outFinalColor);
    portalCompositePass.ReadSampledImage(portalTargets.velocity);
    portalCompositePass.ReadSampledImage(portalTargets.depthStencil);
    portalCompositePass.WriteColorAttachment(targets.outFinalColor);
    portalCompositePass.WriteColorAttachment(targets.velocity);
    portalCompositePass.ReadWriteDepthAttachment(targets.depthStencil);
    portalCompositePass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
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

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("portal_composite");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 1);

        // Because this writes to SV_Depth, apparently all future draw calls to this depth will not use early Z out lol. (stackoverflow 2018)
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);
    });
}


std::string RenderThread::SetupTemporalAntialiasing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, const PostProcessTargets& ppTargets) const
{
    graph.CreateTexture("taa_current", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CarryTextureToNextFrame("taa_current", "taa_history", VK_IMAGE_USAGE_SAMPLED_BIT);

    if (renderGraph->HasTexture("velocity_target")) {
        renderGraph->CarryTextureToNextFrame("velocity_target", "velocity_history", VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    if (!graph.HasTexture("taa_history") || !graph.HasTexture("velocity_history")) {
        RenderPass& taaPass = graph.AddPass("TAA Copy Deferred", VK_PIPELINE_STAGE_2_COPY_BIT);
        taaPass.ReadCopyImage(ppTargets.finalColor);
        taaPass.WriteCopyImage("taa_current");
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            VkImage drawImage = graph.GetImageHandle(ppTargets.finalColor);
            VkImage taaImage = graph.GetImageHandle("taa_current");

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

    RenderPass& taaPass = graph.AddPass("TAA Main", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    taaPass.ReadBuffer("scene_data");
    taaPass.ReadSampledImage(ppTargets.finalColor);
    taaPass.ReadSampledImage(ppTargets.depthStencil);
    taaPass.ReadSampledImage("taa_history");
    taaPass.ReadSampledImage(ppTargets.velocity);
    taaPass.ReadSampledImage("velocity_history");
    taaPass.WriteStorageImage("taa_current");
    taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
        TemporalAntialiasingPushConstant pushData{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.finalColor),
            .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(ppTargets.depthStencil),
            .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex("taa_history"),
            .velocityIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.velocity),
            .velocityHistoryIndex = graph.GetSampledImageViewDescriptorIndex("velocity_history"),
            .outputImageIndex = graph.GetStorageImageViewDescriptorIndex("taa_current"),
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("temporal_antialiasing");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TemporalAntialiasingPushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });


    graph.CreateTexture("taa_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    RenderPass& finalCopyPass = graph.AddPass("TAA Final Copy", VK_PIPELINE_STAGE_2_BLIT_BIT);
    finalCopyPass.ReadBlitImage("taa_current");
    finalCopyPass.WriteBlitImage("taa_output");
    finalCopyPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        VkImage src = graph.GetImageHandle("taa_current");
        VkImage dst = graph.GetImageHandle("taa_output");

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

    return "taa_output";
}

std::string RenderThread::SetupPostProcessing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, const PostProcessTargets& ppTargets,
                                              float deltaTime) const
{
    renderGraph->CreateTexture("post_process_output", TextureInfo{POST_PROCESS_OUTPUT_FORMAT, renderExtent[0], renderExtent[1], 1});
    const Core::PostProcessConfiguration& ppConfig = viewFamily.postProcessConfig;

    // Exposure
    {
        renderGraph->CreateBuffer("luminance_histogram", POST_PROCESS_LUMINANCE_BUFFER_SIZE);

        if (!graph.HasBuffer("luminance_buffer")) {
            renderGraph->CreateBuffer("luminance_buffer", sizeof(float));
        }
        renderGraph->CarryBufferToNextFrame("luminance_buffer", "luminance_buffer", VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

        auto& clearPass = graph.AddPass("Clear Histogram", VK_PIPELINE_STAGE_TRANSFER_BIT);
        clearPass.WriteTransferBuffer("luminance_histogram");
        clearPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("luminance_histogram"), 0, VK_WHOLE_SIZE, 0);
        });

        auto& histogramPass = graph.AddPass("Build Histogram", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        histogramPass.ReadSampledImage(ppTargets.finalColor);
        histogramPass.WriteBuffer("luminance_histogram");
        histogramPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            constexpr float minLogLuminance = -10.0;
            constexpr float maxLogLuminance = 2.0;
            constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
            constexpr float oneOverLogLuminanceRange = 1.0 / logLuminanceRange;
            HistogramBuildPushConstant pc{
                .hdrImageIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.finalColor),
                .histogramBufferAddress = graph.GetBufferAddress("luminance_histogram"),
                .width = width,
                .height = height,
                .minLogLuminance = minLogLuminance,
                .oneOverLogLuminanceRange = oneOverLogLuminanceRange,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("exposure_build_histogram");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HistogramBuildPushConstant), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_LUMINANCE_DISPATCH_X - 1) / POST_PROCESS_LUMINANCE_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_LUMINANCE_DISPATCH_Y - 1) / POST_PROCESS_LUMINANCE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

        auto& exposurePass = graph.AddPass("Calculate Exposure", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        exposurePass.ReadBuffer("luminance_histogram");
        exposurePass.ReadWriteBuffer("luminance_buffer");
        exposurePass.Execute([&, deltaTime, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            constexpr float minLogLuminance = -10.0;
            constexpr float maxLogLuminance = 2.0;
            constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
            // constexpr float oneOverLogLuminanceRange = 1.0 / logLuminanceRange;
            ExposureCalculatePushConstant pc{
                .histogramBufferAddress = graph.GetBufferAddress("luminance_histogram"),
                .luminanceBufferAddress = graph.GetBufferAddress("luminance_buffer"),
                .minLogLuminance = minLogLuminance,
                .logLuminanceRange = logLuminanceRange,
                .adaptationSpeed = ppConfig.exposureAdaptationRate * deltaTime,
                .totalPixels = width * height,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("exposure_calculate_average");
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
        graph.CreateTexture("bloom_chain", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], numMips});

        // Threshold pass - write directly to mip 0
        RenderPass& thresholdPass = graph.AddPass("Bloom Threshold", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        thresholdPass.ReadSampledImage(ppTargets.finalColor);
        thresholdPass.ReadWriteImage("bloom_chain");
        thresholdPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            BloomThresholdPushConstant pc{
                .outputExtent = {width, height},
                .inputColorIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.finalColor),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("bloom_chain", 0),
                .threshold = ppConfig.bloomThreshold,
                .softThreshold = ppConfig.bloomSoftThreshold,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("bloom_threshold");
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

            RenderPass& downsamplePass = graph.AddPass(std::format("Bloom Downsample {}", i), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            downsamplePass.ReadWriteImage("bloom_chain");
            downsamplePass.Execute([&, mipWidth, mipHeight, srcMip = i, dstMip = i + 1](VkCommandBuffer cmd) {
                BloomDownsamplePushConstant pc{
                    .outputExtent = {mipWidth, mipHeight},
                    .inputIndex = graph.GetSampledImageViewDescriptorIndex("bloom_chain"),
                    .outputIndex = graph.GetStorageImageViewDescriptorIndex("bloom_chain", dstMip),
                    .srcMipLevel = srcMip,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("bloom_downsample");
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

            RenderPass& upsamplePass = graph.AddPass(std::format("Bloom Upsample {}", i), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            upsamplePass.ReadWriteImage("bloom_chain");
            upsamplePass.Execute([&, mipWidth, mipHeight, dstMip = i, lowerMip = i + 1](VkCommandBuffer cmd) {
                BloomUpsamplePushConstant pc{
                    .inputIndex = graph.GetSampledImageViewDescriptorIndex("bloom_chain"),
                    .outputIndex = graph.GetStorageImageViewDescriptorIndex("bloom_chain", dstMip),
                    .lowerMipLevel = static_cast<uint32_t>(lowerMip),
                    .higherMipLevel = static_cast<uint32_t>(dstMip),
                    .radius = ppConfig.bloomRadius,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("bloom_upsample");
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
        graph.CreateTexture("sharpening_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& sharpeningPass = graph.AddPass("Sharpening", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        sharpeningPass.ReadBuffer("scene_data");
        sharpeningPass.ReadSampledImage(ppTargets.finalColor);
        sharpeningPass.WriteStorageImage("sharpening_output");
        sharpeningPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            SharpeningPushConstant pc{
                .outputExtent = {width, height},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.finalColor),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("sharpening_output"),
                .sharpness = ppConfig.sharpeningStrength,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("sharpening");
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
        graph.CreateTexture("tonemap_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& tonemapPass = graph.AddPass("Tonemap SDR", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        tonemapPass.ReadSampledImage("sharpening_output");
        tonemapPass.ReadSampledImage("bloom_chain");
        tonemapPass.WriteStorageImage("tonemap_output");
        tonemapPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            TonemapSDRPushConstant pushData{
                .tonemapOperator = ppConfig.tonemapOperator,
                .targetLuminance = ppConfig.exposureTargetLuminance,
                .luminanceBufferAddress = graph.GetBufferAddress("luminance_buffer"),
                .bloomImageIndex = graph.GetSampledImageViewDescriptorIndex("bloom_chain"),
                .bloomIntensity = ppConfig.bloomIntensity,
                .outputWidth = width,
                .outputHeight = height,
                .srcImageIndex = graph.GetSampledImageViewDescriptorIndex("sharpening_output"),
                .dstImageIndex = graph.GetStorageImageViewDescriptorIndex("tonemap_output"),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("tonemap_sdr");
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
        graph.CreateTexture("motion_blur_tiled_max", TextureInfo{VK_FORMAT_R16G16_SFLOAT, blurTiledX, blurTiledY, 1});
        graph.CreateTexture("motion_blur_tiled_neighbor_max", TextureInfo{VK_FORMAT_R16G16_SFLOAT, blurTiledX, blurTiledY, 1});
        graph.CreateTexture("motion_blur_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

        RenderPass& motionBlurTiledMaxPass = graph.AddPass("Motion Blur Tiled Max", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurTiledMaxPass.ReadBuffer("scene_data");
        motionBlurTiledMaxPass.ReadSampledImage(ppTargets.velocity);
        motionBlurTiledMaxPass.WriteStorageImage("motion_blur_tiled_max");
        motionBlurTiledMaxPass.Execute([&, width = renderExtent[0], height = renderExtent[1], blurTiledX, blurTiledY, ppTargets](VkCommandBuffer cmd) {
            MotionBlurTileVelocityPushConstant pc{
                .velocityBufferSize = {width, height},
                .tileBufferSize = {blurTiledX, blurTiledY},
                .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.velocity),
                .tileMaxIndex = graph.GetStorageImageViewDescriptorIndex("motion_blur_tiled_max"),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("motion_blur_tile_max");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X;
            uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });


        RenderPass& motionBlurNeighborMax = graph.AddPass("Motion Blur Neighbor Max", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurNeighborMax.ReadSampledImage("motion_blur_tiled_max");
        motionBlurNeighborMax.WriteStorageImage("motion_blur_tiled_neighbor_max");
        motionBlurNeighborMax.Execute([&, blurTiledX, blurTiledY](VkCommandBuffer cmd) {
            MotionBlurNeighborMaxPushConstant pc{
                .tileBufferSize = {blurTiledX, blurTiledY},
                .tileMaxIndex = graph.GetSampledImageViewDescriptorIndex("motion_blur_tiled_max"),
                .neighborMaxIndex = graph.GetStorageImageViewDescriptorIndex("motion_blur_tiled_neighbor_max"),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("motion_blur_neighbor_max");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X;
            uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });


        RenderPass& motionBlurReconstructionPass = graph.AddPass("Motion Blur Reconstruction", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurReconstructionPass.ReadBuffer("scene_data");
        motionBlurReconstructionPass.ReadSampledImage("tonemap_output");
        motionBlurReconstructionPass.ReadSampledImage(ppTargets.velocity);
        motionBlurReconstructionPass.ReadSampledImage(ppTargets.depthStencil);
        motionBlurReconstructionPass.ReadSampledImage("motion_blur_tiled_neighbor_max");
        motionBlurReconstructionPass.WriteStorageImage("motion_blur_output");
        motionBlurReconstructionPass.Execute([&, width = renderExtent[0], height = renderExtent[1], ppTargets](VkCommandBuffer cmd) {
            MotionBlurReconstructionPushConstant pc{
                .srcBufferSize = {width, height},
                .sceneColorIndex = graph.GetSampledImageViewDescriptorIndex("tonemap_output"),
                .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex(ppTargets.velocity),
                .depthBufferIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(ppTargets.depthStencil),
                .tileNeighborMaxIndex = graph.GetSampledImageViewDescriptorIndex("motion_blur_tiled_neighbor_max"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("motion_blur_output"),
                .velocityScale = ppConfig.motionBlurVelocityScale,
                .depthScale = ppConfig.motionBlurDepthScale,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("motion_blur_reconstruction");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_MOTION_BLUR_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_MOTION_BLUR_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Color Grading
    {
        graph.CreateTexture("color_grading_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& colorGradingPass = graph.AddPass("Color Grading", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        colorGradingPass.ReadBuffer("scene_data");
        colorGradingPass.ReadSampledImage("motion_blur_output");
        colorGradingPass.WriteStorageImage("color_grading_output");
        colorGradingPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            ColorGradingPushConstant pc{
                .outputExtent = {width, height},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("motion_blur_output"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("color_grading_output"),
                .exposure = ppConfig.colorGradingExposure,
                .contrast = ppConfig.colorGradingContrast,
                .saturation = ppConfig.colorGradingSaturation,
                .temperature = ppConfig.colorGradingTemperature,
                .tint = ppConfig.colorGradingTint,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("color_grading");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_COLOR_GRADING_DISPATCH_X - 1) / POST_PROCESS_COLOR_GRADING_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_COLOR_GRADING_DISPATCH_Y - 1) / POST_PROCESS_COLOR_GRADING_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Vignette + Chromatic Aberration
    {
        graph.CreateTexture("vignette_aberration_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& vignetteAberrationPass = graph.AddPass("Vignette and Aberration", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        vignetteAberrationPass.ReadBuffer("scene_data");
        vignetteAberrationPass.ReadSampledImage("color_grading_output");
        vignetteAberrationPass.WriteStorageImage("vignette_aberration_output");
        vignetteAberrationPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            VignetteChromaticAberrationPushConstant pc{
                .outputExtent = {width, height},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("color_grading_output"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("vignette_aberration_output"),
                .chromaticAberrationStrength = ppConfig.chromaticAberrationStrength,
                .vignetteStrength = ppConfig.vignetteStrength,
                .vignetteRadius = ppConfig.vignetteRadius,
                .vignetteSmoothness = ppConfig.vignetteSmoothness,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("vignette_aberration");
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
        RenderPass& filmGrainPass = graph.AddPass("Film Grain", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        filmGrainPass.ReadBuffer("scene_data");
        filmGrainPass.ReadSampledImage("vignette_aberration_output");
        filmGrainPass.WriteStorageImage("post_process_output");
        filmGrainPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            FilmGrainPushConstant pc{
                .outputExtent = {width, height},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("vignette_aberration_output"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("post_process_output"),
                .grainStrength = ppConfig.grainStrength,
                .grainSize = ppConfig.grainSize,
                .frameIndex = static_cast<uint32_t>(frameNumber),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("film_grain");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_FILM_GRAIN_DISPATCH_X - 1) / POST_PROCESS_FILM_GRAIN_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_FILM_GRAIN_DISPATCH_Y - 1) / POST_PROCESS_FILM_GRAIN_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    return "post_process_output";
}

void RenderThread::SetupDebugRender(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const std::string& depthTarget, const std::string& targetImage,
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

    graph.CreateBuffer("debug_vertex_buffer", debugVertexBufferSize);
    graph.CreateBuffer("debug_index_buffer", debugIndexBufferSize);

    UploadAllocation vertexUpload = graph.AllocateTransient(totalDebugVertices * sizeof(DebugVertex));
    UploadAllocation indexUpload = graph.AllocateTransient(totalDebugIndices * sizeof(uint32_t));

    auto* vertices = static_cast<DebugVertex*>(vertexUpload.ptr);
    auto* indices = static_cast<uint32_t*>(indexUpload.ptr);

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    const glm::mat4 viewMatrix = glm::lookAt(viewFamily.mainView.currentViewData.cameraPos, viewFamily.mainView.currentViewData.cameraLookAt, viewFamily.mainView.currentViewData.cameraUp);
    const glm::mat4 projMatrix = glm::perspective(viewFamily.mainView.currentViewData.fovRadians, viewFamily.mainView.currentViewData.aspectRatio, viewFamily.mainView.currentViewData.farPlane,
                                                  viewFamily.mainView.currentViewData.nearPlane);
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

    RenderPass& uploadDebugPass = graph.AddPass("Upload Debug Geometry", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    uploadDebugPass.WriteTransferBuffer("debug_vertex_buffer");
    uploadDebugPass.WriteTransferBuffer("debug_index_buffer");

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
                .dstBuffer = graph.GetBufferHandle("debug_vertex_buffer"),
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
                .dstBuffer = graph.GetBufferHandle("debug_index_buffer"),
                .regionCount = 1,
                .pRegions = &indexCopy
            };
            vkCmdCopyBuffer2(cmd, &indexCopyInfo);
        });


    totalDebugIndices = indexOffset;

    if (totalDebugIndices == 0) {
        return;
    }

    RenderPass& debugDrawPass = graph.AddPass("Debug Draw", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    debugDrawPass.WriteColorAttachment(targetImage);
    bool bHasDepth = graph.HasTexture(depthTarget);
    if (bHasDepth) {
        debugDrawPass.ReadWriteDepthAttachment(depthTarget);
    }
    debugDrawPass.ReadBuffer("scene_data");
    debugDrawPass.ReadBuffer("debug_vertex_buffer");
    debugDrawPass.ReadIndexBuffer("debug_index_buffer");
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
            .sceneData = graph.GetBufferAddress("scene_data"),
            .vertexBuffer = graph.GetBufferAddress("debug_vertex_buffer"),
            .sceneDataIndex = 0,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("debug_render");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DebugDrawPushConstant), &pushConstants);

        vkCmdBindIndexBuffer(cmd, graph.GetBufferHandle("debug_index_buffer"), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(totalDebugIndices), 1, 0, 0, 0);

        vkCmdEndRendering(cmd);
    });

    return;
#else
    return;
#endif
}

void RenderThread::TemporaryRenderTests(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties, std::array<uint32_t, 2> renderExtent)
{}
} // Render
