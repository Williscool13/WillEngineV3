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
#include "render-graph/render_graph.h"
#include "render-graph/render_pass.h"
#include "shaders/constants_interop.h"
#include "shaders/push_constant_interop.h"
#include "tracy/Tracy.hpp"
#include "types/render_types.h"
#include "render/vulkan/vk_imgui_wrapper.h"
#include "backends/imgui_impl_vulkan.h"
#include "shadows/shadow_helpers.h"


namespace Render
{
RenderThread::RenderThread() = default;

RenderThread::RenderThread(Core::FrameSync* engineRenderSynchronization, enki::TaskScheduler* scheduler, SDL_Window* window, uint32_t width, uint32_t height)
    : window(window), engineRenderSynchronization(engineRenderSynchronization), scheduler(scheduler)
{
    context = std::make_unique<VulkanContext>(window);
    swapchain = std::make_unique<Swapchain>(context.get(), width, height);
    imgui = std::make_unique<ImguiWrapper>(context.get(), window, Core::FRAME_BUFFER_COUNT, COLOR_ATTACHMENT_FORMAT);
    renderExtents = std::make_unique<RenderExtents>(width, height, 1.0f);
    resourceManager = std::make_unique<ResourceManager>(context.get());
    renderGraph = std::make_unique<RenderGraph>(context.get(), resourceManager.get());

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
        bufferInfo.size = SCENE_DATA_BUFFER_SIZE;
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
        bufferInfo.size = SHADOW_CASCADE_BUFFER_SIZE;
        frameResources[i].shadowBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        frameResources[i].shadowBuffer.SetDebugName(("shadowBuffer_" + std::to_string(i)).c_str());
    }

    CreatePipelines();
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

void RenderThread::Join() const
{
    if (pinnedTask) {
        scheduler->WaitforTask(pinnedTask.get());
    }
}

void RenderThread::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("RenderThread");

    while (!bShouldExit.load()) {
        // Wait for frame
        {
            ZoneScopedN("WaitForFrame");
            if (!engineRenderSynchronization->renderFrames.try_acquire_for(std::chrono::milliseconds(100))) {
                continue;
            }
        }


        if (bShouldExit.load()) { break; }

        // Render Frame
        {
            ZoneScopedN("RenderFrame");
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

                renderGraph->InvalidateAll();
            }

            // Wait for the frame N - 3 to finish using resources
            RenderSynchronization& currentRenderSynchronization = frameSynchronization[currentFrameInFlight];
            FrameResources& currentFrameResources = frameResources[currentFrameInFlight];
            RenderResponse renderResponse = Render(currentFrameInFlight, currentRenderSynchronization, frameBuffer, currentFrameResources);
            if (renderResponse == SWAPCHAIN_OUTDATED) {
                bRenderRequestsRecreate = true;
            }

            frameNumber++;
        }

        FrameMark;
        engineRenderSynchronization->gameFrames.release();
    }

    vkDeviceWaitIdle(context->device);
}

RenderThread::RenderResponse RenderThread::Render(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer, FrameResources& frameResource)
{
    VK_CHECK(vkWaitForFences(context->device, 1, &renderSync.renderFence, true, UINT64_MAX));
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

    SetupFrameUniforms(frameResource, frameBuffer, renderExtent);

    renderGraph->Reset(frameNumber, RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD);

    renderGraph->ImportBufferNoBarrier("sceneData", frameResource.sceneDataBuffer.handle, frameResource.sceneDataBuffer.address,
                                        {frameResource.sceneDataBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("shadowData", frameResource.shadowBuffer.handle, frameResource.shadowBuffer.address,
                                       {frameResource.shadowBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});

    renderGraph->ImportBufferNoBarrier("vertexBuffer", resourceManager->megaVertexBuffer.handle, resourceManager->megaVertexBuffer.address,
                                       {resourceManager->megaVertexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("skinnedVertexBuffer", resourceManager->megaSkinnedVertexBuffer.handle, resourceManager->megaSkinnedVertexBuffer.address,
                                       {resourceManager->megaSkinnedVertexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("meshletVertexBuffer", resourceManager->megaMeshletVerticesBuffer.handle, resourceManager->megaMeshletVerticesBuffer.address,
                                       {resourceManager->megaMeshletVerticesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("meshletTriangleBuffer", resourceManager->megaMeshletTrianglesBuffer.handle, resourceManager->megaMeshletTrianglesBuffer.address,
                                       {resourceManager->megaMeshletTrianglesBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("meshletBuffer", resourceManager->megaMeshletBuffer.handle, resourceManager->megaMeshletBuffer.address,
                                       {resourceManager->megaMeshletBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("primitiveBuffer", resourceManager->primitiveBuffer.handle, resourceManager->primitiveBuffer.address,
                                       {resourceManager->primitiveBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("instanceBuffer", frameResource.instanceBuffer.handle, frameResource.instanceBuffer.address,
                                       {frameResource.instanceBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("modelBuffer", frameResource.modelBuffer.handle, frameResource.modelBuffer.address,
                                       {frameResource.modelBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("jointMatrixBuffer", frameResource.jointMatrixBuffer.handle, frameResource.jointMatrixBuffer.address,
                                       {frameResource.jointMatrixBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("materialBuffer", frameResource.materialBuffer.handle, frameResource.materialBuffer.address,
                                       {frameResource.materialBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});

    renderGraph->ImportBuffer("debugReadbackBuffer", resourceManager->debugReadbackBuffer.handle, resourceManager->debugReadbackBuffer.address,
                              {resourceManager->debugReadbackBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT}, resourceManager->debugReadbackLastKnownState);

    renderGraph->CreateBuffer("packedVisibilityBuffer", INSTANCING_PACKED_VISIBILITY_SIZE);
    renderGraph->CreateBuffer("instanceOffsetBuffer", INSTANCING_INSTANCE_OFFSET_SIZE);
    renderGraph->CreateBuffer("primitiveCountBuffer", INSTANCING_PRIMITIVE_COUNT_SIZE);
    renderGraph->CreateBuffer("compactedInstanceBuffer", INSTANCING_COMPACTED_INSTANCE_BUFFER_SIZE);
    renderGraph->CreateBuffer("indirectCountBuffer", INSTANCING_MESH_INDIRECT_COUNT);
    renderGraph->CreateBuffer("indirectBuffer", INSTANCING_MESH_INDIRECT_PARAMETERS);

    renderGraph->CreateTexture("albedoTarget", {GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1],});
    renderGraph->CreateTexture("normalTarget", {GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1],});
    renderGraph->CreateTexture("pbrTarget", {GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1],});
    renderGraph->CreateTexture("velocityTarget", {GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1],});
    renderGraph->CreateTexture("depthTarget", {VK_FORMAT_D32_SFLOAT, renderExtent[0], renderExtent[1]});

    if (frameBuffer.mainViewFamily.shadowConfig.enabled) {
        SetupCascadedShadows(*renderGraph, frameBuffer, frameResource);
    }
    SetupInstancingPipeline(*renderGraph, frameBuffer);
    SetupMainGeometryPass(*renderGraph);

    renderGraph->CreateTextureWithUsage("deferredResolve", {COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1],}, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    RenderPass& clearDeferredImagePass = renderGraph->AddPass("ClearDeferredImage", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearDeferredImagePass.WriteClearImage("deferredResolve");
    clearDeferredImagePass.Execute([&](VkCommandBuffer cmd) {
        VkImage img = renderGraph->GetImage("deferredResolve");
        constexpr VkClearColorValue clearColor = {0.0f, 0.1f, 0.2f, 1.0f};
        VkImageSubresourceRange colorSubresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
        vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &colorSubresource);
    });

    SetupDeferredLighting(*renderGraph, frameBuffer, renderExtent);
    renderGraph->CreateTextureWithUsage("taaCurrent",
                                        {COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1]},
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
    );

    SetupTemporalAntialiasing(*renderGraph, renderExtent);
    renderGraph->CarryToNextFrame("taaCurrent", "taaHistory");
    renderGraph->CreateTexture("finalImage", {COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1]});

    RenderPass& finalCopyPass = renderGraph->AddPass("finalCopy", VK_PIPELINE_STAGE_2_BLIT_BIT);
    finalCopyPass.ReadBlitImage("taaCurrent");
    finalCopyPass.WriteBlitImage("finalImage");
    finalCopyPass.Execute([&](VkCommandBuffer cmd) {
        VkImage src = renderGraph->GetImage("taaCurrent");
        VkImage dst = renderGraph->GetImage("finalImage");

        VkOffset3D renderOffset = {static_cast<int32_t>(renderExtent[0]), static_cast<int32_t>(renderExtent[1]), 1};

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

#if WILL_EDITOR
    /*RenderPass& readbackPass = renderGraph->AddPass("DebugReadback");
    readbackPass.ReadTransferBuffer("indirectBuffer", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    readbackPass.ReadTransferBuffer("indirectCountBuffer", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    readbackPass.WriteTransferBuffer("debugReadbackBuffer", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    readbackPass.Execute([&](VkCommandBuffer cmd) {
        VkBufferCopy countCopy{};
        countCopy.srcOffset = 0;
        countCopy.dstOffset = 0;
        countCopy.size = sizeof(uint32_t);
        vkCmdCopyBuffer(cmd, renderGraph->GetBuffer("indirectCountBuffer"), renderGraph->GetBuffer("debugReadbackBuffer"), 1, &countCopy);

        VkBufferCopy indirectCopy{};
        indirectCopy.srcOffset = 0;
        indirectCopy.dstOffset = sizeof(uint32_t);
        indirectCopy.size = 10 * sizeof(InstancedMeshIndirectDrawParameters);
        vkCmdCopyBuffer(cmd, renderGraph->GetBuffer("indirectBuffer"), renderGraph->GetBuffer("debugReadbackBuffer"), 1, &indirectCopy);
    });*/
#endif

    if (frameBuffer.mainViewFamily.mainView.debug != 0) {
        static constexpr const char* debugTargets[] = {
            "depthTarget",
            "depthTarget",
            "albedoTarget",
            "normalTarget",
            "pbrTarget",
            "velocityTarget",
            "shadowCascade_0",
            "shadowCascade_1",
            "shadowCascade_2",
            "shadowCascade_3",
        };

        uint32_t debugIndex = frameBuffer.mainViewFamily.mainView.debug;
        if (debugIndex >= std::size(debugTargets)) {
            debugIndex = 1;
        }
        const char* debugTargetName = debugTargets[debugIndex];

        auto& debugVisPass = renderGraph->AddPass("DebugVisualize", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        debugVisPass.ReadSampledImage(debugTargetName);
        debugVisPass.WriteStorageImage("finalImage");
        debugVisPass.Execute([&, debugTargetName, debugIndex](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, debugVisualizePipeline.pipeline.handle);

            const ResourceDimensions& dims = renderGraph->GetImageDimensions(debugTargetName);
            DebugVisualizePushConstant pushData{
                .srcExtent = {dims.width, dims.height},
                .dstExtent = {renderExtent[0], renderExtent[1]},
                .nearPlane = frameBuffer.mainViewFamily.mainView.currentViewData.nearPlane,
                .farPlane = frameBuffer.mainViewFamily.mainView.currentViewData.farPlane,
                .textureIndex = renderGraph->GetDescriptorIndex(debugTargetName),
                .samplerIndex = resourceManager->linearSamplerIndex,
                .outputImageIndex = renderGraph->GetDescriptorIndex("finalImage"),
                .debugType = debugIndex
            };

            vkCmdPushConstants(cmd, debugVisualizePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DebugVisualizePushConstant), &pushData);

            VkDescriptorBufferBindingInfoEXT bindingInfo = resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo();
            vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
            uint32_t bufferIndex = 0;
            VkDeviceSize offset = 0;
            vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, debugVisualizePipeline.pipelineLayout.handle, 0, 1, &bufferIndex, &offset);

            uint32_t xDispatch = (renderExtent[0] + 15) / 16;
            uint32_t yDispatch = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    if (frameBuffer.bDrawImgui) {
        auto& imguiEditorPass = renderGraph->AddPass("ImguiEditor", 0);
        imguiEditorPass.WriteColorAttachment("finalImage");
        imguiEditorPass.Execute([&](VkCommandBuffer cmd) {
            const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(renderGraph->GetImageView("finalImage"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const ResourceDimensions& dims = renderGraph->GetImageDimensions("finalImage");
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &imguiAttachment, nullptr);
            vkCmdBeginRendering(cmd, &renderInfo);
            ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[currentFrameIndex];
            ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, cmd);

            vkCmdEndRendering(cmd);
        });
    }

    renderGraph->ImportTexture("swapchainImage", currentSwapchainImage, currentSwapchainImageView, {swapchain->format, swapchain->extent.width, swapchain->extent.height}, swapchain->usages,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED);

    auto& blitPass = renderGraph->AddPass("BlitToSwapchain", VK_PIPELINE_STAGE_2_BLIT_BIT);
    blitPass.ReadBlitImage("finalImage");
    blitPass.WriteBlitImage("swapchainImage");
    blitPass.Execute([&](VkCommandBuffer cmd) {
        VkImage drawImage = renderGraph->GetImage("finalImage");

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

    renderGraph->SetDebugLogging(frameBuffer.bLogRDG);
    renderGraph->Compile(frameNumber);
    renderGraph->Execute(renderSync.commandBuffer);
    renderGraph->PrepareSwapchain(renderSync.commandBuffer, "swapchainImage");

    resourceManager->debugReadbackLastKnownState = renderGraph->GetBufferState("debugReadbackBuffer");

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
    meshShadingInstancedPipeline = MeshShadingInstancedPipeline(context.get(), resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout);
    shadowMeshShadingInstancedPipeline = ShadowMeshShadingInstancedPipeline(context.get());
    //
    {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(DebugVisualizePushConstant);
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkPipelineLayoutCreateInfo piplineLayoutCreateInfo{};
        piplineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        piplineLayoutCreateInfo.pSetLayouts = &resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle;
        piplineLayoutCreateInfo.setLayoutCount = 1;
        piplineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
        piplineLayoutCreateInfo.pushConstantRangeCount = 1;

        debugVisualizePipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "debugVisualize_compute.spv");

        pushConstantRange.size = sizeof(DeferredResolvePushConstant);
        deferredResolve = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "deferredResolve_compute.spv");

        pushConstantRange.size = sizeof(TemporalAntialiasingPushConstant);
        temporalAntialiasing = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "temporalAntialiasing_compute.spv");
    }

    //
    {
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

        pushConstant.size = sizeof(VisibilityShadowsPushConstant);
        instancingShadowsVisibility = ComputePipeline(context.get(), computePipelineLayoutCreateInfo, Platform::GetShaderPath() / "instancingShadowsVisibility_compute.spv");

        pushConstant.size = sizeof(PrefixSumPushConstant);
        instancingPrefixSum = ComputePipeline(context.get(), computePipelineLayoutCreateInfo, Platform::GetShaderPath() / "instancingPrefixSum_compute.spv");

        pushConstant.size = sizeof(IndirectWritePushConstant);
        instancingIndirectConstruction = ComputePipeline(context.get(), computePipelineLayoutCreateInfo, Platform::GetShaderPath() / "instancingCompactAndGenerateIndirect_compute.spv");
    }
}

void RenderThread::SetupFrameUniforms(FrameResources& frameResource, Core::FrameBuffer& frameBuffer, const std::array<uint32_t, 2>& renderExtent)
{
    //
    {
        auto* modelBuffer = static_cast<Model*>(frameResource.modelBuffer.allocationInfo.pMappedData);
        for (size_t i = 0; i < frameBuffer.mainViewFamily.modelMatrices.size(); ++i) {
            modelBuffer[i] = frameBuffer.mainViewFamily.modelMatrices[i];
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
            };
        }

        const Core::RenderView& view = frameBuffer.mainViewFamily.mainView;

        const glm::mat4 viewMatrix = glm::lookAt(view.currentViewData.cameraPos, view.currentViewData.cameraLookAt, view.currentViewData.cameraUp);
        const glm::mat4 projMatrix = glm::perspective(view.currentViewData.fovRadians, view.currentViewData.aspectRatio, view.currentViewData.farPlane, view.currentViewData.nearPlane);

        const glm::mat4 prevViewMatrix = glm::lookAt(view.previousViewData.cameraPos, view.previousViewData.cameraLookAt, view.previousViewData.cameraUp);
        const glm::mat4 prevProjMatrix = glm::perspective(view.previousViewData.fovRadians, view.previousViewData.aspectRatio, view.previousViewData.farPlane, view.previousViewData.nearPlane);

        HaltonSample jitter = HALTON_SEQUENCE[frameNumber % HALTON_SEQUENCE_COUNT];
        float jitterX = (jitter.x - 0.5f) * (2.0f / renderExtent[0]);
        float jitterY = (jitter.y - 0.5f) * (2.0f / renderExtent[1]);

        glm::mat4 jitteredProj = projMatrix;
        jitteredProj[2][0] += jitterX;
        jitteredProj[2][1] += jitterY;

        HaltonSample prevJitter = HALTON_SEQUENCE[(frameNumber - 1) % HALTON_SEQUENCE_COUNT];
        float prevJitterX = (prevJitter.x - 0.5f) * (2.0f / renderExtent[0]);
        float prevJitterY = (prevJitter.y - 0.5f) * (2.0f / renderExtent[1]);
        glm::mat4 jitteredPrevProj = prevProjMatrix;
        jitteredPrevProj[2][0] += prevJitterX;
        jitteredPrevProj[2][1] += prevJitterY;

        sceneData.jitter = {jitterX, jitterY};
        sceneData.prevJitter = {prevJitterX, prevJitterY};

        sceneData.view = viewMatrix;
        sceneData.proj = jitteredProj;
        sceneData.viewProj = jitteredProj * viewMatrix;
        sceneData.invView = glm::inverse(viewMatrix);
        sceneData.invProj = glm::inverse(jitteredProj);
        sceneData.invViewProj = glm::inverse(sceneData.viewProj);

        // Debug shadow view project
        /*{
            Core::ShadowConfiguration shadowConfig = frameBuffer.mainViewFamily.shadowConfiguration;
            ShadowCascadePreset shadowPreset = SHADOW_PRESETS[static_cast<uint32_t>(shadowConfig.quality)];
            Core::DirectionalLight directionalLight = frameBuffer.mainViewFamily.directionalLight;

            ShadowData shadowData{};

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

            shadowData.mainLightDirection = glm::vec4(directionalLight.direction, 0.0f);
            ViewProjMatrix pairs[SHADOW_CASCADE_COUNT]{};
            for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i) {
                ViewProjMatrix viewProj = GenerateLightSpaceMatrix(
                    static_cast<float>(shadowPreset.extents[i].width),
                    shadowData.nearSplits[i],
                    shadowData.farSplits[i],
                    frameBuffer.mainViewFamily.directionalLight.direction,
                    frameBuffer.mainViewFamily.mainView.currentViewData
                );
                shadowData.lightSpaceMatrices[i] = viewProj.proj * viewProj.view;
                pairs[i] = viewProj;;
                shadowData.lightFrustums[i] = CreateFrustum(shadowData.lightSpaceMatrices[i]);
            }

            // todo: tweak shadow intensity
            shadowData.shadowIntensity = 1.0f;

            glm::mat4 lightViewProj = shadowData.lightSpaceMatrices[0];
            sceneData.view = pairs[0].view;
            sceneData.proj = pairs[0].proj;
            sceneData.viewProj = lightViewProj;
            sceneData.invView = glm::inverse(sceneData.view);
            sceneData.invProj = glm::inverse(sceneData.proj);
            sceneData.invViewProj = glm::inverse(lightViewProj);
        }*/

        sceneData.prevViewProj = jitteredPrevProj * prevViewMatrix;

        sceneData.cameraWorldPos = glm::vec4(view.currentViewData.cameraPos, 1.0f);

        sceneData.texelSize = glm::vec2(1.0f, 1.0f) / glm::vec2(renderExtent[0], renderExtent[1]);
        sceneData.mainRenderTargetSize = glm::vec2(renderExtent[0], renderExtent[1]);

        sceneData.frustum = CreateFrustum(sceneData.viewProj);
        sceneData.deltaTime = 0.1f;

        AllocatedBuffer& currentSceneDataBuffer = frameResource.sceneDataBuffer;
        auto currentSceneData = static_cast<SceneData*>(currentSceneDataBuffer.allocationInfo.pMappedData);
        memcpy(currentSceneData, &sceneData, sizeof(SceneData));
    }
}

void RenderThread::SetupCascadedShadows(RenderGraph& graph, Core::FrameBuffer& frameBuffer, FrameResources& frameResource)
{
    Core::ShadowConfiguration shadowConfig = frameBuffer.mainViewFamily.shadowConfig;
    Core::DirectionalLight directionalLight = frameBuffer.mainViewFamily.directionalLight;

    ShadowData shadowData{};

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

    shadowData.mainLightDirection = glm::vec4(directionalLight.direction, 0.0f);
    for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i) {
        ViewProjMatrix viewProj = GenerateLightSpaceMatrix(
            static_cast<float>(shadowConfig.cascadePreset.extents[i].width),
            shadowData.nearSplits[i],
            shadowData.farSplits[i],
            frameBuffer.mainViewFamily.directionalLight.direction,
            frameBuffer.mainViewFamily.mainView.currentViewData
        );
        shadowData.lightSpaceMatrices[i] = viewProj.proj * viewProj.view;
        shadowData.lightFrustums[i] = CreateFrustum(shadowData.lightSpaceMatrices[i]);
    }

    // todo: tweak shadow intensity
    shadowData.shadowIntensity = 1.0f;

    AllocatedBuffer& shadowBuffer = frameResource.shadowBuffer;
    auto currentShadowData = static_cast<ShadowData*>(shadowBuffer.allocationInfo.pMappedData);
    memcpy(currentShadowData, &shadowData, sizeof(ShadowData));

    for (int32_t cascadeLevel = 0; cascadeLevel < SHADOW_CASCADE_COUNT; ++cascadeLevel) {
        std::string shadowMapName = "shadowCascade_" + std::to_string(cascadeLevel);
        std::string clearPassName = "ClearShadowBuffers_" + std::to_string(cascadeLevel);
        std::string visPassName = "ShadowVisibility_" + std::to_string(cascadeLevel);
        std::string prefixPassName = "ShadowPrefixSum_" + std::to_string(cascadeLevel);
        std::string indirectPassName = "ShadowIndirectConstruction_" + std::to_string(cascadeLevel);
        std::string shadowPassName = "ShadowCascadePass_" + std::to_string(cascadeLevel);

        graph.CreateTexture(shadowMapName, {SHADOW_CASCADE_FORMAT, shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height});

        RenderPass& clearPass = graph.AddPass(clearPassName, VK_PIPELINE_STAGE_2_CLEAR_BIT);
        clearPass.WriteTransferBuffer("packedVisibilityBuffer");
        clearPass.WriteTransferBuffer("primitiveCountBuffer");
        clearPass.WriteTransferBuffer("indirectCountBuffer");
        clearPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBuffer("packedVisibilityBuffer"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBuffer("primitiveCountBuffer"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBuffer("indirectCountBuffer"), 0, VK_WHOLE_SIZE, 0);
        });

        if (!frameBuffer.mainViewFamily.instances.empty()) {
            RenderPass& visibilityPass = graph.AddPass(visPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            visibilityPass.ReadBuffer("primitiveBuffer");
            visibilityPass.ReadBuffer("modelBuffer");
            visibilityPass.ReadBuffer("instanceBuffer");
            visibilityPass.ReadBuffer("sceneData");
            visibilityPass.ReadBuffer("shadowData");
            visibilityPass.WriteBuffer("packedVisibilityBuffer");
            visibilityPass.WriteBuffer("instanceOffsetBuffer");
            visibilityPass.WriteBuffer("primitiveCountBuffer");
            visibilityPass.Execute([&, cascadeLevel](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingShadowsVisibility.pipeline.handle);

                VisibilityShadowsPushConstant pushData{
                    .sceneData = graph.GetBufferAddress("sceneData"),
                    .shadowData = graph.GetBufferAddress("shadowData"),
                    .primitiveBuffer = graph.GetBufferAddress("primitiveBuffer"),
                    .modelBuffer = graph.GetBufferAddress("modelBuffer"),
                    .instanceBuffer = graph.GetBufferAddress("instanceBuffer"),
                    .packedVisibilityBuffer = graph.GetBufferAddress("packedVisibilityBuffer"),
                    .instanceOffsetBuffer = graph.GetBufferAddress("instanceOffsetBuffer"),
                    .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                    .instanceCount = static_cast<uint32_t>(frameBuffer.mainViewFamily.instances.size()),
                    .cascadeLevel = static_cast<uint32_t>(cascadeLevel)
                };

                vkCmdPushConstants(cmd, instancingShadowsVisibility.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityShadowsPushConstant), &pushData);
                uint32_t xDispatch = (frameBuffer.mainViewFamily.instances.size() + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });

            RenderPass& prefixSumPass = graph.AddPass(prefixPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            prefixSumPass.ReadBuffer("primitiveCountBuffer");
            prefixSumPass.Execute([&](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingPrefixSum.pipeline.handle);

                PrefixSumPushConstant pushConstant{
                    .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                    .highestPrimitiveIndex = 200,
                };

                vkCmdPushConstants(cmd, instancingPrefixSum.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefixSumPushConstant), &pushConstant);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& indirectPass = graph.AddPass(indirectPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            indirectPass.ReadBuffer("sceneData");
            indirectPass.ReadBuffer("primitiveBuffer");
            indirectPass.ReadBuffer("modelBuffer");
            indirectPass.ReadBuffer("instanceBuffer");
            indirectPass.ReadBuffer("packedVisibilityBuffer");
            indirectPass.ReadBuffer("instanceOffsetBuffer");
            indirectPass.ReadBuffer("primitiveCountBuffer");
            indirectPass.WriteBuffer("compactedInstanceBuffer");
            indirectPass.WriteBuffer("indirectCountBuffer");
            indirectPass.WriteBuffer("indirectBuffer");
            indirectPass.Execute([&](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingIndirectConstruction.pipeline.handle);

                IndirectWritePushConstant pushConstant{
                    .primitiveBuffer = graph.GetBufferAddress("primitiveBuffer"),
                    .modelBuffer = graph.GetBufferAddress("modelBuffer"),
                    .instanceBuffer = graph.GetBufferAddress("instanceBuffer"),
                    .packedVisibilityBuffer = graph.GetBufferAddress("packedVisibilityBuffer"),
                    .instanceOffsetBuffer = graph.GetBufferAddress("instanceOffsetBuffer"),
                    .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                    .compactedInstanceBuffer = graph.GetBufferAddress("compactedInstanceBuffer"),
                    .indirectCountBuffer = graph.GetBufferAddress("indirectCountBuffer"),
                    .indirectBuffer = graph.GetBufferAddress("indirectBuffer"),
                };

                vkCmdPushConstants(cmd, instancingIndirectConstruction.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IndirectWritePushConstant), &pushConstant);
                uint32_t xDispatch = (frameBuffer.mainViewFamily.instances.size() + (INSTANCING_CONSTRUCTION_DISPATCH_X - 1)) / INSTANCING_CONSTRUCTION_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });
        }

        RenderPass& shadowPass = graph.AddPass(shadowPassName, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
        shadowPass.WriteDepthAttachment(shadowMapName);
        shadowPass.Execute([&, shadowConfig, cascadeLevel, shadowMapName](VkCommandBuffer cmd) {
            VkViewport viewport = VkHelpers::GenerateViewport(shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor = VkHelpers::GenerateScissor(shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageView(shadowMapName), &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height}, nullptr, 0, &depthAttachment);

            vkCmdBeginRendering(cmd, &renderInfo);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMeshShadingInstancedPipeline.pipeline.handle);
            vkCmdSetDepthBias(cmd, -shadowConfig.cascadePreset.biases[cascadeLevel].linear, 0.0f, -shadowConfig.cascadePreset.biases[cascadeLevel].sloped);

            ShadowMeshShadingPushConstant pushConstants{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .shadowData = graph.GetBufferAddress("shadowData"),
                .vertexBuffer = graph.GetBufferAddress("vertexBuffer"),
                .meshletVerticesBuffer = graph.GetBufferAddress("meshletVertexBuffer"),
                .meshletTrianglesBuffer = graph.GetBufferAddress("meshletTriangleBuffer"),
                .meshletBuffer = graph.GetBufferAddress("meshletBuffer"),
                .indirectBuffer = graph.GetBufferAddress("indirectBuffer"),
                .compactedInstanceBuffer = graph.GetBufferAddress("compactedInstanceBuffer"),
                .modelBuffer = graph.GetBufferAddress("modelBuffer"),
                .cascadeIndex = static_cast<uint32_t>(cascadeLevel),
            };

            vkCmdPushConstants(cmd, shadowMeshShadingInstancedPipeline.pipelineLayout.handle, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT,
                               0, sizeof(ShadowMeshShadingPushConstant), &pushConstants);

            vkCmdDrawMeshTasksIndirectCountEXT(cmd,
                                               graph.GetBuffer("indirectBuffer"), 0,
                                               graph.GetBuffer("indirectCountBuffer"), 0,
                                               MEGA_PRIMITIVE_BUFFER_COUNT,
                                               sizeof(InstancedMeshIndirectDrawParameters));

            vkCmdEndRendering(cmd);
        });
#if WILL_EDITOR
        if (cascadeLevel == 3) {
            RenderPass& readbackPass = graph.AddPass("ShadowDebugReadback", VK_PIPELINE_STAGE_2_COPY_BIT);
            readbackPass.ReadTransferBuffer("indirectBuffer");
            readbackPass.ReadTransferBuffer("indirectCountBuffer");
            readbackPass.WriteTransferBuffer("debugReadbackBuffer");
            readbackPass.Execute([&](VkCommandBuffer cmd) {
                VkBufferCopy countCopy{};
                countCopy.srcOffset = 0;
                countCopy.dstOffset = 0;
                countCopy.size = sizeof(uint32_t);
                vkCmdCopyBuffer(cmd, graph.GetBuffer("indirectCountBuffer"), graph.GetBuffer("debugReadbackBuffer"), 1, &countCopy);

                VkBufferCopy indirectCopy{};
                indirectCopy.srcOffset = 0;
                indirectCopy.dstOffset = sizeof(uint32_t);
                indirectCopy.size = 10 * sizeof(InstancedMeshIndirectDrawParameters);
                vkCmdCopyBuffer(cmd, graph.GetBuffer("indirectBuffer"), graph.GetBuffer("debugReadbackBuffer"), 1, &indirectCopy);
            });
        }
#endif
    }
}

void RenderThread::SetupInstancingPipeline(RenderGraph& graph, Core::FrameBuffer& frameBuffer)
{
    if (!frameBuffer.bFreezeVisibility) {
        RenderPass& clearPass = graph.AddPass("ClearInstancingBuffers", VK_PIPELINE_STAGE_2_CLEAR_BIT);
        clearPass.WriteTransferBuffer("packedVisibilityBuffer");
        clearPass.WriteTransferBuffer("primitiveCountBuffer");
        clearPass.WriteTransferBuffer("indirectCountBuffer");
        clearPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBuffer("packedVisibilityBuffer"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBuffer("primitiveCountBuffer"), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBuffer("indirectCountBuffer"), 0, VK_WHOLE_SIZE, 0);
        });

        if (!frameBuffer.mainViewFamily.instances.empty()) {
            RenderPass& visibilityPass = graph.AddPass("ComputeVisibility", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            visibilityPass.ReadBuffer("primitiveBuffer");
            visibilityPass.ReadBuffer("modelBuffer");
            visibilityPass.ReadBuffer("instanceBuffer");
            visibilityPass.ReadBuffer("sceneData");
            visibilityPass.WriteBuffer("packedVisibilityBuffer");
            visibilityPass.WriteBuffer("instanceOffsetBuffer");
            visibilityPass.WriteBuffer("primitiveCountBuffer");
            visibilityPass.Execute([&](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingVisibility.pipeline.handle);

                // todo: profile; a lot of instances, 100k. Try first all of the same primitive. Then try again with a few different primitives (but total remains around the same)
                VisibilityPushConstant visibilityPushData{
                    .sceneData = graph.GetBufferAddress("sceneData"),
                    .primitiveBuffer = graph.GetBufferAddress("primitiveBuffer"),
                    .modelBuffer = graph.GetBufferAddress("modelBuffer"),
                    .instanceBuffer = graph.GetBufferAddress("instanceBuffer"),
                    .packedVisibilityBuffer = graph.GetBufferAddress("packedVisibilityBuffer"),
                    .instanceOffsetBuffer = graph.GetBufferAddress("instanceOffsetBuffer"),
                    .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                    .instanceCount = static_cast<uint32_t>(frameBuffer.mainViewFamily.instances.size())
                };

                vkCmdPushConstants(cmd, instancingVisibility.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityPushConstant), &visibilityPushData);
                uint32_t xDispatch = (frameBuffer.mainViewFamily.instances.size() + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });

            RenderPass& prefixSumPass = graph.AddPass("PrefixSum", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            prefixSumPass.ReadBuffer("primitiveCountBuffer");
            prefixSumPass.Execute([&](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingPrefixSum.pipeline.handle);

                // todo: optimize the F* out of this. Use multiple passes if necessary
                PrefixSumPushConstant prefixSumPushConstant{
                    .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                    .highestPrimitiveIndex = 200,
                };

                vkCmdPushConstants(cmd, instancingPrefixSum.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefixSumPushConstant), &prefixSumPushConstant);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& indirectConstructionPass = graph.AddPass("IndirectConstruction", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            indirectConstructionPass.ReadBuffer("sceneData");
            indirectConstructionPass.ReadBuffer("primitiveBuffer");
            indirectConstructionPass.ReadBuffer("modelBuffer");
            indirectConstructionPass.ReadBuffer("instanceBuffer");
            indirectConstructionPass.ReadBuffer("packedVisibilityBuffer");
            indirectConstructionPass.ReadBuffer("instanceOffsetBuffer");
            indirectConstructionPass.ReadBuffer("primitiveCountBuffer");
            indirectConstructionPass.WriteBuffer("compactedInstanceBuffer");
            indirectConstructionPass.WriteBuffer("indirectCountBuffer");
            indirectConstructionPass.WriteBuffer("indirectBuffer");
            indirectConstructionPass.Execute([&](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingIndirectConstruction.pipeline.handle);

                IndirectWritePushConstant indirectWritePushConstant{
                    .primitiveBuffer = graph.GetBufferAddress("primitiveBuffer"),
                    .modelBuffer = graph.GetBufferAddress("modelBuffer"),
                    .instanceBuffer = graph.GetBufferAddress("instanceBuffer"),
                    .packedVisibilityBuffer = graph.GetBufferAddress("packedVisibilityBuffer"),
                    .instanceOffsetBuffer = graph.GetBufferAddress("instanceOffsetBuffer"),
                    .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                    .compactedInstanceBuffer = graph.GetBufferAddress("compactedInstanceBuffer"),
                    .indirectCountBuffer = graph.GetBufferAddress("indirectCountBuffer"),
                    .indirectBuffer = graph.GetBufferAddress("indirectBuffer"),
                };

                vkCmdPushConstants(cmd, instancingIndirectConstruction.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IndirectWritePushConstant), &indirectWritePushConstant);
                uint32_t xDispatch = (frameBuffer.mainViewFamily.instances.size() + (INSTANCING_CONSTRUCTION_DISPATCH_X - 1)) / INSTANCING_CONSTRUCTION_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });
        }
    }
}

void RenderThread::SetupMainGeometryPass(RenderGraph& graph)
{
    RenderPass& instancedMeshShading = graph.AddPass("InstancedMeshShading", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    instancedMeshShading.WriteColorAttachment("albedoTarget");
    instancedMeshShading.WriteColorAttachment("normalTarget");
    instancedMeshShading.WriteColorAttachment("pbrTarget");
    instancedMeshShading.WriteColorAttachment("velocityTarget");
    instancedMeshShading.WriteDepthAttachment("depthTarget");
    instancedMeshShading.ReadBuffer("compactedInstanceBuffer");
    instancedMeshShading.ReadIndirectBuffer("indirectBuffer");
    instancedMeshShading.ReadIndirectCountBuffer("indirectCountBuffer");
    instancedMeshShading.Execute([&](VkCommandBuffer cmd) {
        const ResourceDimensions& dims = graph.GetImageDimensions("albedoTarget");
        VkViewport viewport = VkHelpers::GenerateViewport(dims.width, dims.height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(dims.width, dims.height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        const VkRenderingAttachmentInfo albedoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageView("albedoTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo normalAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageView("normalTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo pbrAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageView("pbrTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageView("velocityTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageView("depthTarget"), &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        const VkRenderingAttachmentInfo colorAttachments[] = {albedoAttachment, normalAttachment, pbrAttachment, velocityAttachment};
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, colorAttachments, 4, &depthAttachment);

        vkCmdBeginRendering(cmd, &renderInfo);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshShadingInstancedPipeline.pipeline.handle);
        VkDescriptorBufferBindingInfoEXT bindingInfo = graph.GetResourceManager()->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo();
        vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
        uint32_t bufferIndexImage = 0;
        VkDeviceSize bufferOffset = 0;
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshShadingInstancedPipeline.pipelineLayout.handle, 0, 1, &bufferIndexImage, &bufferOffset);

        InstancedMeshShadingPushConstant pushConstants{
            .sceneData = graph.GetBufferAddress("sceneData"),
            .vertexBuffer = graph.GetBufferAddress("vertexBuffer"),
            .meshletVerticesBuffer = graph.GetBufferAddress("meshletVertexBuffer"),
            .meshletTrianglesBuffer = graph.GetBufferAddress("meshletTriangleBuffer"),
            .meshletBuffer = graph.GetBufferAddress("meshletBuffer"),
            .indirectBuffer = graph.GetBufferAddress("indirectBuffer"),
            .compactedInstanceBuffer = graph.GetBufferAddress("compactedInstanceBuffer"),
            .materialBuffer = graph.GetBufferAddress("materialBuffer"),
            .modelBuffer = graph.GetBufferAddress("modelBuffer"),
        };

        vkCmdPushConstants(cmd, meshShadingInstancedPipeline.pipelineLayout.handle, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(InstancedMeshShadingPushConstant), &pushConstants);

        vkCmdDrawMeshTasksIndirectCountEXT(cmd,
                                           graph.GetBuffer("indirectBuffer"), 0,
                                           graph.GetBuffer("indirectCountBuffer"), 0,
                                           MEGA_PRIMITIVE_BUFFER_COUNT,
                                           sizeof(InstancedMeshIndirectDrawParameters));

        vkCmdEndRendering(cmd);
    });
}

void RenderThread::SetupDeferredLighting(RenderGraph& graph, const Core::FrameBuffer& frameBuffer, const std::array<uint32_t, 2>& renderExtent)
{
    bool bShadowsEnabled = frameBuffer.mainViewFamily.shadowConfig.enabled;

    RenderPass& deferredResolvePass = graph.AddPass("DeferredResolve", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    deferredResolvePass.ReadSampledImage("albedoTarget");
    deferredResolvePass.ReadSampledImage("normalTarget");
    deferredResolvePass.ReadSampledImage("pbrTarget");
    deferredResolvePass.ReadSampledImage("depthTarget");
    if (bShadowsEnabled) {
        deferredResolvePass.ReadSampledImage("shadowCascade_0");
        deferredResolvePass.ReadSampledImage("shadowCascade_1");
        deferredResolvePass.ReadSampledImage("shadowCascade_2");
        deferredResolvePass.ReadSampledImage("shadowCascade_3");
    }

    deferredResolvePass.WriteStorageImage("deferredResolve");
    deferredResolvePass.Execute([&, bShadowsEnabled](VkCommandBuffer cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, deferredResolve.pipeline.handle);

        uint32_t packedShadowMapIndices = bShadowsEnabled
                                              ? PackCascadeIndices(
                                                  graph.GetDescriptorIndex("shadowCascade_0"),
                                                  graph.GetDescriptorIndex("shadowCascade_1"),
                                                  graph.GetDescriptorIndex("shadowCascade_2"),
                                                  graph.GetDescriptorIndex("shadowCascade_3")
                                              )
                                              : 0xFFFFFFFF;

        uint32_t packedGBufferIndices = VkHelpers::PackGBufferIndices(
            graph.GetDescriptorIndex("albedoTarget"),
            graph.GetDescriptorIndex("normalTarget"),
            graph.GetDescriptorIndex("pbrTarget"),
            graph.GetDescriptorIndex("depthTarget")
        );

        DeferredResolvePushConstant pushData{
            .sceneData = graph.GetBufferAddress("sceneData"),
            .shadowData = graph.GetBufferAddress("shadowData"),
            .extent = {renderExtent[0], renderExtent[1]},
            .packedGBufferIndices = packedGBufferIndices,
            .packedCSMIndices = packedShadowMapIndices,
            .pointSamplerIndex = resourceManager->pointSamplerIndex,
            .depthCompareSamplerIndex = resourceManager->depthCompareSamplerIndex,
            .outputImageIndex = graph.GetDescriptorIndex("deferredResolve"),
        };

        vkCmdPushConstants(cmd, deferredResolve.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DeferredResolvePushConstant), &pushData);

        VkDescriptorBufferBindingInfoEXT bindingInfo = resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo();
        vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
        uint32_t bufferIndex = 0;
        VkDeviceSize offset = 0;
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, deferredResolve.pipelineLayout.handle, 0, 1, &bufferIndex, &offset);

        uint32_t xDispatch = (renderExtent[0] + 15) / 16;
        uint32_t yDispatch = (renderExtent[1] + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}

void RenderThread::SetupTemporalAntialiasing(RenderGraph& graph, const std::array<uint32_t, 2>& renderExtent)
{
    if (!graph.HasTexture("taaHistory")) {
        RenderPass& taaPass = graph.AddPass("TemporalAntialiasingFirstFrame", VK_PIPELINE_STAGE_2_COPY_BIT);
        taaPass.ReadCopyImage("deferredResolve");
        taaPass.WriteCopyImage("taaCurrent");
        taaPass.Execute([&](VkCommandBuffer cmd) {
            VkImage drawImage = graph.GetImage("deferredResolve");
            VkImage taaImage = graph.GetImage("taaCurrent");

            VkImageCopy2 copyRegion{};
            copyRegion.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent = {renderExtent[0], renderExtent[1], 1};

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
    }
    else {
        RenderPass& taaPass = graph.AddPass("TemporalAntialiasing", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        taaPass.ReadSampledImage("deferredResolve");
        taaPass.ReadSampledImage("depthTarget");
        taaPass.ReadSampledImage("taaHistory");
        taaPass.ReadSampledImage("velocityTarget");
        taaPass.WriteStorageImage("taaCurrent");
        taaPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporalAntialiasing.pipeline.handle);
            TemporalAntialiasingPushConstant pushData{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .pointSamplerIndex = resourceManager->pointSamplerIndex,
                .linearSamplerIndex = resourceManager->linearSamplerIndex,
                .colorResolvedIndex = graph.GetDescriptorIndex("deferredResolve"),
                .depthIndex = graph.GetDescriptorIndex("depthTarget"),
                .colorHistoryIndex = graph.GetDescriptorIndex("taaHistory"),
                .velocityIndex = graph.GetDescriptorIndex("velocityTarget"),
                .outputImageIndex = graph.GetDescriptorIndex("taaCurrent"),
            };

            vkCmdPushConstants(cmd, temporalAntialiasing.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TemporalAntialiasingPushConstant), &pushData);

            VkDescriptorBufferBindingInfoEXT bindingInfo = resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo();
            vkCmdBindDescriptorBuffersEXT(cmd, 1, &bindingInfo);
            uint32_t bufferIndex = 0;
            VkDeviceSize offset = 0;
            vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporalAntialiasing.pipelineLayout.handle, 0, 1, &bufferIndex, &offset);

            uint32_t xDispatch = (renderExtent[0] + 15) / 16;
            uint32_t yDispatch = (renderExtent[1] + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }
}
} // Render
