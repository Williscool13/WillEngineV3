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
        bufferInfo.size = SHADOW_DATA_BUFFER_SIZE;
        frameResources[i].shadowBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        frameResources[i].shadowBuffer.SetDebugName(("shadowBuffer_" + std::to_string(i)).c_str());
        bufferInfo.size = LIGHT_DATA_BUFFER_SIZE;
        frameResources[i].lightBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        frameResources[i].lightBuffer.SetDebugName(("lightBuffer_" + std::to_string(i)).c_str());
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

    ProcessAcquisitions(renderSync.commandBuffer, frameBuffer.bufferAcquireOperations, frameBuffer.imageAcquireOperations);
    frameBuffer.bufferAcquireOperations.clear();
    frameBuffer.imageAcquireOperations.clear();

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
    Core::ViewFamily& viewFamily = frameBuffer.mainViewFamily;

    SetupFrameUniforms(renderSync.commandBuffer, viewFamily, frameResource, renderExtent);

    renderGraph->Reset(frameNumber, RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD);

    renderGraph->ImportBufferNoBarrier("sceneData", frameResource.sceneDataBuffer.handle, frameResource.sceneDataBuffer.address,
                                       {frameResource.sceneDataBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("shadowData", frameResource.shadowBuffer.handle, frameResource.shadowBuffer.address,
                                       {frameResource.shadowBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("lightData", frameResource.lightBuffer.handle, frameResource.lightBuffer.address,
                                       {frameResource.lightBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});

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

    renderGraph->CreateTexture("albedoTarget", TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("normalTarget", TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("pbrTarget", TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("emissiveTarget", TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("velocityTarget", TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("depthTarget", TextureInfo{VK_FORMAT_D32_SFLOAT, renderExtent[0], renderExtent[1], 1});

    bool bHasShadows = frameBuffer.mainViewFamily.shadowConfig.enabled && !viewFamily.instances.empty();
    if (bHasShadows) {
        SetupCascadedShadows(*renderGraph, viewFamily);
    }

    if (frameBuffer.bFreezeVisibility) {
        if (!bFrozenVisibility) {
            // Note that freezing visibility will freeze to the main camera's frustum.
            //   Shadows will render main camera's visible instances, so some shadows may be missing.
            SetupInstancing(*renderGraph, viewFamily);
            bFrozenVisibility = true;
        }

        renderGraph->CarryBufferToNextFrame("compactedInstanceBuffer", "compactedInstanceBuffer", 0);
        renderGraph->CarryBufferToNextFrame("indirectBuffer", "indirectBuffer", 0);
        renderGraph->CarryBufferToNextFrame("indirectCountBuffer", "indirectCountBuffer", 0);
    }
    else {
        SetupInstancing(*renderGraph, viewFamily);
        bFrozenVisibility = false;
    }
    SetupMainGeometryPass(*renderGraph, viewFamily);

    if (viewFamily.gtaoConfig.bEnabled) {
        SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent);
    }

    // IN : albedoTarget, normalTarget, pbrTarget, emissiveTarget, depthTarget, shadowCascade_0, shadowCascade_1, shadowCascade_2, shadowCascade_3
    // OUT: deferredResolve
    SetupDeferredLighting(*renderGraph, viewFamily, renderExtent, bHasShadows);

    if (viewFamily.mainView.debug != 0) {
        static constexpr const char* debugTargets[] = {
            "depthTarget",
            "depthTarget", // 1
            "albedoTarget", // 2
            "normalTarget", // 3
            "pbrTarget", // 4
            "gtaoAO", // 5
            "gtaoEdges", // 6
            "gtaoFiltered", // 7
            "gtaoDepth", // 8
            "gtaoDepth", // 9
        };

        uint32_t debugIndex = viewFamily.mainView.debug;
        if (debugIndex >= std::size(debugTargets)) {
            debugIndex = 1;
        }
        const char* debugTargetName = debugTargets[debugIndex];
        if (renderGraph->HasTexture(debugTargetName)) {
            auto& debugVisPass = renderGraph->AddPass("DebugVisualize", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            debugVisPass.ReadSampledImage(debugTargetName);
            debugVisPass.WriteStorageImage("deferredResolve");
            debugVisPass.Execute([&, debugTargetName, debugIndex](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, debugVisualizePipeline.pipeline.handle);

                const ResourceDimensions& dims = renderGraph->GetImageDimensions(debugTargetName);
                DebugVisualizePushConstant pushData{
                    .srcExtent = {dims.width, dims.height},
                    .dstExtent = {renderExtent[0], renderExtent[1]},
                    .nearPlane = viewFamily.mainView.currentViewData.nearPlane,
                    .farPlane = viewFamily.mainView.currentViewData.farPlane,
                    .textureIndex = renderGraph->GetSampledImageViewDescriptorIndex(debugTargetName),
                    .outputImageIndex = renderGraph->GetStorageImageViewDescriptorIndex("deferredResolve"),
                    .debugType = debugIndex
                };

                vkCmdPushConstants(cmd, debugVisualizePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DebugVisualizePushConstant), &pushData);

                uint32_t xDispatch = (renderExtent[0] + 15) / 16;
                uint32_t yDispatch = (renderExtent[1] + 15) / 16;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
        }
    }


    // IN : deferredResolve
    // OUT: taaCurrent, taaOutput
    SetupTemporalAntialiasing(*renderGraph, viewFamily, renderExtent);
    renderGraph->CarryTextureToNextFrame("taaCurrent", "taaHistory", VK_IMAGE_USAGE_SAMPLED_BIT);

    SetupPostProcessing(*renderGraph, viewFamily, renderExtent, frameBuffer.timeFrame.renderDeltaTime);

#if WILL_EDITOR
    RenderPass& readbackPass = renderGraph->AddPass("DebugReadback", VK_PIPELINE_STAGE_2_COPY_BIT);
    readbackPass.ReadTransferBuffer("indirectBuffer");
    readbackPass.ReadTransferBuffer("indirectCountBuffer");
    readbackPass.ReadTransferBuffer("luminanceHistogram");
    readbackPass.ReadTransferBuffer("luminanceBuffer");
    readbackPass.WriteTransferBuffer("debugReadbackBuffer");
    readbackPass.Execute([&](VkCommandBuffer cmd) {
        size_t offsetSoFar = 0;
        VkBufferCopy countCopy{};
        countCopy.srcOffset = 0;
        countCopy.dstOffset = 0;
        countCopy.size = sizeof(uint32_t);
        vkCmdCopyBuffer(cmd, renderGraph->GetBufferHandle("indirectCountBuffer"), renderGraph->GetBufferHandle("debugReadbackBuffer"), 1, &countCopy);
        offsetSoFar += sizeof(uint32_t);

        VkBufferCopy indirectCopy{};
        indirectCopy.srcOffset = 0;
        indirectCopy.dstOffset = offsetSoFar;
        indirectCopy.size = 10 * sizeof(InstancedMeshIndirectDrawParameters);
        vkCmdCopyBuffer(cmd, renderGraph->GetBufferHandle("indirectBuffer"), renderGraph->GetBufferHandle("debugReadbackBuffer"), 1, &indirectCopy);
        offsetSoFar += 10 * sizeof(InstancedMeshIndirectDrawParameters);

        VkBufferCopy histogramCopy{};
        histogramCopy.srcOffset = 0;
        histogramCopy.dstOffset = offsetSoFar;
        histogramCopy.size = 256 * sizeof(uint32_t);
        vkCmdCopyBuffer(cmd, renderGraph->GetBufferHandle("luminanceHistogram"), renderGraph->GetBufferHandle("debugReadbackBuffer"), 1, &histogramCopy);
        offsetSoFar += 256 * sizeof(uint32_t);

        VkBufferCopy averageExposureCopy{};
        averageExposureCopy.srcOffset = 0;
        averageExposureCopy.dstOffset = offsetSoFar;
        averageExposureCopy.size = sizeof(uint32_t);
        vkCmdCopyBuffer(cmd, renderGraph->GetBufferHandle("luminanceBuffer"), renderGraph->GetBufferHandle("debugReadbackBuffer"), 1, &averageExposureCopy);
        offsetSoFar += sizeof(uint32_t);
    });
#endif

    if (frameBuffer.bDrawImgui) {
        auto& imguiEditorPass = renderGraph->AddPass("ImguiEditor", VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
        imguiEditorPass.WriteColorAttachment("postProcessOutput");
        imguiEditorPass.Execute([&](VkCommandBuffer cmd) {
            const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(renderGraph->GetImageViewHandle("postProcessOutput"), nullptr,
                                                                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const ResourceDimensions& dims = renderGraph->GetImageDimensions("postProcessOutput");
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &imguiAttachment, nullptr);
            vkCmdBeginRendering(cmd, &renderInfo);
            ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[currentFrameIndex];
            ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, cmd);

            vkCmdEndRendering(cmd);
        });
    }

    renderGraph->ImportTexture("swapchainImage", currentSwapchainImage, currentSwapchainImageView, TextureInfo{swapchain->format, swapchain->extent.width, swapchain->extent.height, 1},
                               swapchain->usages,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED);

    auto& blitPass = renderGraph->AddPass("BlitToSwapchain", VK_PIPELINE_STAGE_2_BLIT_BIT);
    blitPass.ReadBlitImage("postProcessOutput");
    blitPass.WriteBlitImage("swapchainImage");
    blitPass.Execute([&](VkCommandBuffer cmd) {
        VkImage drawImage = renderGraph->GetImageHandle("postProcessOutput");

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

void RenderThread::ProcessAcquisitions(VkCommandBuffer cmd,
                                       const std::vector<Core::BufferAcquireOperation>& bufferAcquireOperations,
                                       const std::vector<Core::ImageAcquireOperation>& imageAcquireOperations)
{
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

    meshShadingInstancedPipeline = MeshShadingInstancedPipeline(context.get(), layouts);
    shadowMeshShadingInstancedPipeline = ShadowMeshShadingInstancedPipeline(context.get());
    //
    {
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkPipelineLayoutCreateInfo piplineLayoutCreateInfo{};
        piplineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        piplineLayoutCreateInfo.pSetLayouts = layouts.data();
        piplineLayoutCreateInfo.setLayoutCount = layouts.size();
        piplineLayoutCreateInfo.pPushConstantRanges = &pushConstant;
        piplineLayoutCreateInfo.pushConstantRangeCount = 1;

        pushConstant.size = sizeof(DebugVisualizePushConstant);
        debugVisualizePipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "debugVisualize_compute.spv");

        pushConstant.size = sizeof(DeferredResolvePushConstant);
        deferredResolvePipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "deferredResolve_compute.spv");

        pushConstant.size = sizeof(TemporalAntialiasingPushConstant);
        temporalAntialiasingPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "temporalAntialiasing_compute.spv");

        pushConstant.size = sizeof(VisibilityPushConstant);
        instancingVisibilityPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "instancingVisibility_compute.spv");

        pushConstant.size = sizeof(VisibilityShadowsPushConstant);
        instancingShadowsVisibilityPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "instancingShadowsVisibility_compute.spv");

        pushConstant.size = sizeof(PrefixSumPushConstant);
        instancingPrefixSumPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "instancingPrefixSum_compute.spv");

        pushConstant.size = sizeof(IndirectWritePushConstant);
        instancingIndirectConstructionPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "instancingCompactAndGenerateIndirect_compute.spv");

        pushConstant.size = sizeof(GTAODepthPrepassPushConstant);
        gtaoDepthPrepassPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "gtaoDepthPrepass_compute.spv");

        pushConstant.size = sizeof(GTAOMainPushConstant);
        gtaoMainPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "gtaoMain_compute.spv");

        pushConstant.size = sizeof(GTAODenoisePushConstant);
        gtaoDenoisePipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "gtaoDenoise_compute.spv");

        pushConstant.size = sizeof(HistogramBuildPushConstant);
        exposureBuildHistogramPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "exposureBuildHistogram_compute.spv");

        pushConstant.size = sizeof(ExposureCalculatePushConstant);
        exposureCalculateAveragePipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "exposureCalculateAverage_compute.spv");

        pushConstant.size = sizeof(TonemapSDRPushConstant);
        tonemapSDRPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "tonemapSDR_compute.spv");

        pushConstant.size = sizeof(MotionBlurTileVelocityPushConstant);
        motionBlurTileMaxPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "motionBlurTileMax_compute.spv");

        pushConstant.size = sizeof(MotionBlurNeighborMaxPushConstant);
        motionBlurNeighborMaxPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "motionBlurBlurNeighborMax_compute.spv");

        pushConstant.size = sizeof(MotionBlurReconstructionPushConstant);
        motionBlurReconstructionPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "motionBlurReconstruction_compute.spv");

        pushConstant.size = sizeof(BloomThresholdPushConstant);
        bloomThresholdPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "bloomThreshold_compute.spv");

        pushConstant.size = sizeof(BloomDownsamplePushConstant);
        bloomDownsamplePipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "bloomDownsample_compute.spv");

        pushConstant.size = sizeof(BloomUpsamplePushConstant);
        bloomUpsamplePipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "bloomUpsample_compute.spv");

        pushConstant.size = sizeof(VignetteChromaticAberrationPushConstant);
        vignetteAberrationPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "vignetteAberration_compute.spv");

        pushConstant.size = sizeof(FilmGrainPushConstant);
        filmGrainPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "filmGrain_compute.spv");

        pushConstant.size = sizeof(SharpeningPushConstant);
        sharpeningPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "sharpening_compute.spv");

        pushConstant.size = sizeof(ColorGradingPushConstant);
        colorGradingPipeline = ComputePipeline(context.get(), piplineLayoutCreateInfo, Platform::GetShaderPath() / "colorGrading_compute.spv");
    }
}

void RenderThread::SetupFrameUniforms(VkCommandBuffer cmd, const Core::ViewFamily& viewFamily, FrameResources& frameResource, const std::array<uint32_t, 2> renderExtent) const
{
    std::array bindings{resourceManager->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo(), resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo()};
    std::array indices{0u, 1u};
    std::array<VkDeviceSize, 2> offsets{0, 0};
    vkCmdBindDescriptorBuffersEXT(cmd, bindings.size(), bindings.data());
    vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, globalPipelineLayout.handle, 0, bindings.size(), indices.data(), offsets.data());
    vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, globalPipelineLayout.handle, 0, bindings.size(), indices.data(), offsets.data());

    // Scene Data
    {
        auto* modelBuffer = static_cast<Model*>(frameResource.modelBuffer.allocationInfo.pMappedData);
        for (size_t i = 0; i < viewFamily.modelMatrices.size(); ++i) {
            modelBuffer[i] = viewFamily.modelMatrices[i];
        }

        auto* materialBuffer = static_cast<MaterialProperties*>(frameResource.materialBuffer.allocationInfo.pMappedData);
        memcpy(materialBuffer, viewFamily.materials.data(), viewFamily.materials.size() * sizeof(MaterialProperties));


        auto* instanceBuffer = static_cast<Instance*>(frameResource.instanceBuffer.allocationInfo.pMappedData);
        for (size_t i = 0; i < viewFamily.instances.size(); ++i) {
            auto& inst = viewFamily.instances[i];
            instanceBuffer[i] = {
                .primitiveIndex = inst.primitiveIndex,
                .modelIndex = inst.modelIndex,
                .materialIndex = inst.gpuMaterialIndex,
                .jointMatrixOffset = 0,
            };
        }

        const Core::RenderView& view = viewFamily.mainView;

        const glm::mat4 viewMatrix = glm::lookAt(view.currentViewData.cameraPos, view.currentViewData.cameraLookAt, view.currentViewData.cameraUp);
        const glm::mat4 projMatrix = glm::perspective(view.currentViewData.fovRadians, view.currentViewData.aspectRatio, view.currentViewData.farPlane, view.currentViewData.nearPlane);

        const glm::mat4 prevViewMatrix = glm::lookAt(view.previousViewData.cameraPos, view.previousViewData.cameraLookAt, view.previousViewData.cameraUp);
        const glm::mat4 prevProjMatrix = glm::perspective(view.previousViewData.fovRadians, view.previousViewData.aspectRatio, view.previousViewData.farPlane, view.previousViewData.nearPlane);

        SceneData sceneData{};
        sceneData.view = viewMatrix;

        if (viewFamily.postProcessConfig.bEnableTemporalAntialiasing) {
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
            sceneData.proj = jitteredProj;
            sceneData.prevViewProj = jitteredPrevProj * prevViewMatrix;
        }
        else {
            sceneData.jitter = {0.0f, 0.0f};
            sceneData.prevJitter = {0.0f, 0.0f};
            sceneData.proj = projMatrix;
            sceneData.prevViewProj = prevProjMatrix * prevViewMatrix;
        }

        sceneData.viewProj = sceneData.proj * sceneData.view;
        sceneData.invView = glm::inverse(sceneData.view);
        sceneData.invProj = glm::inverse(sceneData.proj);
        sceneData.invViewProj = glm::inverse(sceneData.viewProj);

        sceneData.unjitteredViewProj = projMatrix * viewMatrix;
        sceneData.unjitteredPrevViewProj = prevProjMatrix * prevViewMatrix;

        sceneData.cameraWorldPos = glm::vec4(view.currentViewData.cameraPos, 1.0f);

        sceneData.texelSize = glm::vec2(1.0f, 1.0f) / glm::vec2(renderExtent[0], renderExtent[1]);
        sceneData.mainRenderTargetSize = glm::vec2(renderExtent[0], renderExtent[1]);

        sceneData.depthLinearizeMult = -projMatrix[3][2];
        sceneData.depthLinearizeAdd = projMatrix[2][2];
        if (sceneData.depthLinearizeMult * sceneData.depthLinearizeAdd < 0) {
            sceneData.depthLinearizeAdd = -sceneData.depthLinearizeAdd;
        }
        float tanHalfFOVY = 1.0f / projMatrix[1][1];
        float tanHalfFOVX = 1.0F / projMatrix[0][0];
        glm::vec2 cameraTanHalfFOV{tanHalfFOVX, tanHalfFOVY};
        sceneData.ndcToViewMul = {cameraTanHalfFOV.x * 2.0f, cameraTanHalfFOV.y * -2.0f};
        sceneData.ndcToViewAdd = {cameraTanHalfFOV.x * -1.0f, cameraTanHalfFOV.y * 1.0f};
        const glm::vec2 texelSize = {1.0f / static_cast<float>(renderExtent[0]), 1.0f / static_cast<float>(renderExtent[1])};
        sceneData.ndcToViewMulXPixelSize = {sceneData.ndcToViewMul.x * texelSize.x, sceneData.ndcToViewMul.y * texelSize.y};

        sceneData.frustum = CreateFrustum(sceneData.viewProj);
        sceneData.deltaTime = 0.1f;

        AllocatedBuffer& currentSceneDataBuffer = frameResource.sceneDataBuffer;
        auto currentSceneData = static_cast<SceneData*>(currentSceneDataBuffer.allocationInfo.pMappedData);
        memcpy(currentSceneData, &sceneData, sizeof(SceneData));
    }

    // Shadows
    {
        Core::ShadowConfiguration shadowConfig = viewFamily.shadowConfig;
        Core::DirectionalLight directionalLight = viewFamily.directionalLight;
        directionalLight.direction = normalize(directionalLight.direction);

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

        for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i) {
            ViewProjMatrix viewProj = GenerateLightSpaceMatrix(
                static_cast<float>(shadowConfig.cascadePreset.extents[i].width),
                shadowData.nearSplits[i],
                shadowData.farSplits[i],
                directionalLight.direction,
                viewFamily.mainView.currentViewData
            );
            shadowData.lightSpaceMatrices[i] = viewProj.proj * viewProj.view;
            shadowData.lightFrustums[i] = CreateFrustum(shadowData.lightSpaceMatrices[i]);
            shadowData.lightSizes[i] = shadowConfig.cascadePreset.lightSizes[i];
            shadowData.blockerSearchSamples[i] = shadowConfig.cascadePreset.pcssSamples[i].blockerSearchSamples;
            shadowData.pcfSamples[i] = shadowConfig.cascadePreset.pcssSamples[i].pcfSamples;
        }

        shadowData.shadowIntensity = shadowConfig.shadowIntensity;

        AllocatedBuffer& shadowBuffer = frameResource.shadowBuffer;
        auto currentShadowData = static_cast<ShadowData*>(shadowBuffer.allocationInfo.pMappedData);
        memcpy(currentShadowData, &shadowData, sizeof(ShadowData));
    }

    // Lights
    {
        LightData lightData{};
        lightData.mainLightDirection = {viewFamily.directionalLight.direction, viewFamily.directionalLight.intensity};
        lightData.mainLightColor = {viewFamily.directionalLight.color, 0.0f};
        AllocatedBuffer& currentLightBuffer = frameResource.lightBuffer;
        auto currentLightData = static_cast<LightData*>(currentLightBuffer.allocationInfo.pMappedData);
        memcpy(currentLightData, &lightData, sizeof(LightData));
    }
}

void RenderThread::SetupCascadedShadows(RenderGraph& graph, const Core::ViewFamily& viewFamily) const
{
    Core::ShadowConfiguration shadowConfig = viewFamily.shadowConfig;

    for (int32_t cascadeLevel = 0; cascadeLevel < SHADOW_CASCADE_COUNT; ++cascadeLevel) {
        std::string shadowMapName = "shadowCascade_" + std::to_string(cascadeLevel);
        std::string shadowPassName = "ShadowCascadePass_" + std::to_string(cascadeLevel);

        graph.CreateTexture(shadowMapName, TextureInfo{SHADOW_CASCADE_FORMAT, shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height, 1});

        if (!bFrozenVisibility) {
            std::string clearPassName = "ClearShadowBuffers_" + std::to_string(cascadeLevel);
            std::string visPassName = "ShadowVisibility_" + std::to_string(cascadeLevel);
            std::string prefixPassName = "ShadowPrefixSum_" + std::to_string(cascadeLevel);
            std::string indirectPassName = "ShadowIndirectConstruction_" + std::to_string(cascadeLevel);


            RenderPass& clearPass = graph.AddPass(clearPassName, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
            clearPass.WriteTransferBuffer("packedVisibilityBuffer");
            clearPass.WriteTransferBuffer("primitiveCountBuffer");
            clearPass.WriteTransferBuffer("indirectCountBuffer");
            clearPass.Execute([&](VkCommandBuffer cmd) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle("packedVisibilityBuffer"), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle("primitiveCountBuffer"), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle("indirectCountBuffer"), 0, VK_WHOLE_SIZE, 0);
            });

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
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingShadowsVisibilityPipeline.pipeline.handle);

                VisibilityShadowsPushConstant pushData{
                    .sceneData = graph.GetBufferAddress("sceneData"),
                    .shadowData = graph.GetBufferAddress("shadowData"),
                    .primitiveBuffer = graph.GetBufferAddress("primitiveBuffer"),
                    .modelBuffer = graph.GetBufferAddress("modelBuffer"),
                    .instanceBuffer = graph.GetBufferAddress("instanceBuffer"),
                    .packedVisibilityBuffer = graph.GetBufferAddress("packedVisibilityBuffer"),
                    .instanceOffsetBuffer = graph.GetBufferAddress("instanceOffsetBuffer"),
                    .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                    .instanceCount = static_cast<uint32_t>(viewFamily.instances.size()),
                    .cascadeLevel = static_cast<uint32_t>(cascadeLevel)
                };

                vkCmdPushConstants(cmd, instancingShadowsVisibilityPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityShadowsPushConstant), &pushData);
                uint32_t xDispatch = (viewFamily.instances.size() + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });

            RenderPass& prefixSumPass = graph.AddPass(prefixPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            prefixSumPass.ReadBuffer("primitiveCountBuffer");
            prefixSumPass.Execute([&](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingPrefixSumPipeline.pipeline.handle);

                PrefixSumPushConstant pushConstant{
                    .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                    .highestPrimitiveIndex = 200,
                };

                vkCmdPushConstants(cmd, instancingPrefixSumPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefixSumPushConstant), &pushConstant);
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
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingIndirectConstructionPipeline.pipeline.handle);

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

                vkCmdPushConstants(cmd, instancingIndirectConstructionPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IndirectWritePushConstant), &pushConstant);
                uint32_t xDispatch = (viewFamily.instances.size() + (INSTANCING_CONSTRUCTION_DISPATCH_X - 1)) / INSTANCING_CONSTRUCTION_DISPATCH_X;
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
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(shadowMapName), &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height}, nullptr, 0,
                                                                        &depthAttachment);

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
                                               graph.GetBufferHandle("indirectBuffer"), 0,
                                               graph.GetBufferHandle("indirectCountBuffer"), 0,
                                               MEGA_PRIMITIVE_BUFFER_COUNT,
                                               sizeof(InstancedMeshIndirectDrawParameters));

            vkCmdEndRendering(cmd);
        });
    }
}

void RenderThread::SetupInstancing(RenderGraph& graph, const Core::ViewFamily& viewFamily) const
{
    RenderPass& clearPass = graph.AddPass("ClearInstancingBuffers", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    clearPass.WriteTransferBuffer("packedVisibilityBuffer");
    clearPass.WriteTransferBuffer("primitiveCountBuffer");
    clearPass.WriteTransferBuffer("indirectCountBuffer");
    clearPass.Execute([&](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle("packedVisibilityBuffer"), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle("primitiveCountBuffer"), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle("indirectCountBuffer"), 0, VK_WHOLE_SIZE, 0);
    });

    if (!viewFamily.instances.empty()) {
        RenderPass& visibilityPass = graph.AddPass("ComputeVisibility", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        visibilityPass.ReadBuffer("primitiveBuffer");
        visibilityPass.ReadBuffer("modelBuffer");
        visibilityPass.ReadBuffer("instanceBuffer");
        visibilityPass.ReadBuffer("sceneData");
        visibilityPass.WriteBuffer("packedVisibilityBuffer");
        visibilityPass.WriteBuffer("instanceOffsetBuffer");
        visibilityPass.WriteBuffer("primitiveCountBuffer");
        visibilityPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingVisibilityPipeline.pipeline.handle);

            // todo: profile; a lot of instances, 100k. Try first all of the same primitive. Then try again with a few different primitives (but total remains around the same)
            VisibilityPushConstant visibilityPushData{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .primitiveBuffer = graph.GetBufferAddress("primitiveBuffer"),
                .modelBuffer = graph.GetBufferAddress("modelBuffer"),
                .instanceBuffer = graph.GetBufferAddress("instanceBuffer"),
                .packedVisibilityBuffer = graph.GetBufferAddress("packedVisibilityBuffer"),
                .instanceOffsetBuffer = graph.GetBufferAddress("instanceOffsetBuffer"),
                .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                .instanceCount = static_cast<uint32_t>(viewFamily.instances.size())
            };

            vkCmdPushConstants(cmd, instancingVisibilityPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityPushConstant), &visibilityPushData);
            uint32_t xDispatch = (viewFamily.instances.size() + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
            vkCmdDispatch(cmd, xDispatch, 1, 1);
        });

        RenderPass& prefixSumPass = graph.AddPass("PrefixSum", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        prefixSumPass.ReadBuffer("primitiveCountBuffer");
        prefixSumPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingPrefixSumPipeline.pipeline.handle);

            // todo: optimize the F* out of this. Use multiple passes if necessary
            PrefixSumPushConstant prefixSumPushConstant{
                .primitiveCountBuffer = graph.GetBufferAddress("primitiveCountBuffer"),
                .highestPrimitiveIndex = 200,
            };

            vkCmdPushConstants(cmd, instancingPrefixSumPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefixSumPushConstant), &prefixSumPushConstant);
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
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, instancingIndirectConstructionPipeline.pipeline.handle);

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

            vkCmdPushConstants(cmd, instancingIndirectConstructionPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IndirectWritePushConstant), &indirectWritePushConstant);
            uint32_t xDispatch = (viewFamily.instances.size() + (INSTANCING_CONSTRUCTION_DISPATCH_X - 1)) / INSTANCING_CONSTRUCTION_DISPATCH_X;
            vkCmdDispatch(cmd, xDispatch, 1, 1);
        });
    }
}

void RenderThread::SetupMainGeometryPass(RenderGraph& graph, const Core::ViewFamily& viewFamily) const
{
    RenderPass& clearGBufferPass = graph.AddPass("ClearGBuffer", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearGBufferPass.WriteClearImage("albedoTarget");
    clearGBufferPass.WriteClearImage("normalTarget");
    clearGBufferPass.WriteClearImage("pbrTarget");
    clearGBufferPass.WriteClearImage("emissiveTarget");
    clearGBufferPass.WriteClearImage("velocityTarget");
    clearGBufferPass.Execute([&](VkCommandBuffer cmd) {
        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdClearColorImage(cmd, graph.GetImageHandle("albedoTarget"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
        vkCmdClearColorImage(cmd, graph.GetImageHandle("normalTarget"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
        vkCmdClearColorImage(cmd, graph.GetImageHandle("pbrTarget"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
        vkCmdClearColorImage(cmd, graph.GetImageHandle("emissiveTarget"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
        vkCmdClearColorImage(cmd, graph.GetImageHandle("velocityTarget"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
    });

    RenderPass& instancedMeshShading = graph.AddPass("InstancedMeshShading", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    instancedMeshShading.WriteColorAttachment("albedoTarget");
    instancedMeshShading.WriteColorAttachment("normalTarget");
    instancedMeshShading.WriteColorAttachment("pbrTarget");
    instancedMeshShading.WriteColorAttachment("emissiveTarget");
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
        const VkRenderingAttachmentInfo albedoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("albedoTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo normalAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("normalTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo pbrAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("pbrTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo emissiveTarget = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("emissiveTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("velocityTarget"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("depthTarget"), &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        const VkRenderingAttachmentInfo colorAttachments[] = {albedoAttachment, normalAttachment, pbrAttachment, emissiveTarget, velocityAttachment};
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, colorAttachments, 5, &depthAttachment);

        vkCmdBeginRendering(cmd, &renderInfo);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshShadingInstancedPipeline.pipeline.handle);

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
                                           graph.GetBufferHandle("indirectBuffer"), 0,
                                           graph.GetBufferHandle("indirectCountBuffer"), 0,
                                           MEGA_PRIMITIVE_BUFFER_COUNT,
                                           sizeof(InstancedMeshIndirectDrawParameters));

        vkCmdEndRendering(cmd);
    });
}

void RenderThread::SetupDeferredLighting(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, bool bEnableShadows) const
{
    graph.CreateTexture("deferredResolve", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
    RenderPass& clearDeferredImagePass = graph.AddPass("ClearDeferredImage", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearDeferredImagePass.WriteClearImage("deferredResolve");
    clearDeferredImagePass.Execute([&](VkCommandBuffer cmd) {
        VkImage img = graph.GetImageHandle("deferredResolve");
        constexpr VkClearColorValue clearColor = {0.0f, 0.1f, 0.2f, 1.0f};
        VkImageSubresourceRange colorSubresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
        vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &colorSubresource);
    });

    RenderPass& deferredResolvePass = graph.AddPass("DeferredResolve", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    deferredResolvePass.ReadSampledImage("albedoTarget");
    deferredResolvePass.ReadSampledImage("normalTarget");
    deferredResolvePass.ReadSampledImage("pbrTarget");
    deferredResolvePass.ReadSampledImage("emissiveTarget");
    deferredResolvePass.ReadSampledImage("depthTarget");
    if (viewFamily.gtaoConfig.bEnabled) {
        deferredResolvePass.ReadSampledImage("gtaoFiltered");
    }

    if (bEnableShadows) {
        deferredResolvePass.ReadSampledImage("shadowCascade_0");
        deferredResolvePass.ReadSampledImage("shadowCascade_1");
        deferredResolvePass.ReadSampledImage("shadowCascade_2");
        deferredResolvePass.ReadSampledImage("shadowCascade_3");
    }

    deferredResolvePass.WriteStorageImage("deferredResolve");
    deferredResolvePass.Execute([&, bEnableShadows, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, deferredResolvePipeline.pipeline.handle);


        glm::ivec4 csmIndices{-1, -1, -1, -1};
        if (bEnableShadows) {
            csmIndices.x = graph.GetSampledImageViewDescriptorIndex("shadowCascade_0");
            csmIndices.y = graph.GetSampledImageViewDescriptorIndex("shadowCascade_1");
            csmIndices.z = graph.GetSampledImageViewDescriptorIndex("shadowCascade_2");
            csmIndices.w = graph.GetSampledImageViewDescriptorIndex("shadowCascade_3");
        }

        int32_t gtaoIndex = viewFamily.gtaoConfig.bEnabled ? graph.GetSampledImageViewDescriptorIndex("gtaoFiltered") : -1;

        DeferredResolvePushConstant pushData{
            .sceneData = graph.GetBufferAddress("sceneData"),
            .shadowData = graph.GetBufferAddress("shadowData"),
            .lightData = graph.GetBufferAddress("lightData"),
            .extent = {width, height},
            .csmIndices = csmIndices,
            .albedoIndex = graph.GetSampledImageViewDescriptorIndex("albedoTarget"),
            .normalIndex = graph.GetSampledImageViewDescriptorIndex("normalTarget"),
            .pbrIndex = graph.GetSampledImageViewDescriptorIndex("pbrTarget"),
            .emissiveIndex = graph.GetSampledImageViewDescriptorIndex("emissiveTarget"),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex("depthTarget"),
            .gtaoFilteredIndex = gtaoIndex,
            .outputImageIndex = graph.GetStorageImageViewDescriptorIndex("deferredResolve"),
        };

        vkCmdPushConstants(cmd, deferredResolvePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DeferredResolvePushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}

void RenderThread::SetupGroundTruthAmbientOcclusion(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent) const
{
    const Core::GTAOConfiguration& gtaoConfig = viewFamily.gtaoConfig;

    graph.CreateTexture("gtaoDepth", TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 5});

    graph.CreateTexture("gtaoAO", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});
    graph.CreateTexture("gtaoEdges", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});

    RenderPass& depthPrepass = graph.AddPass("GTAODepthPrepass", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    depthPrepass.ReadSampledImage("depthTarget");
    depthPrepass.WriteStorageImage("gtaoDepth");
    depthPrepass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        GTAODepthPrepassPushConstant pc{
            .sceneData = graph.GetBufferAddress("sceneData"),
            .inputDepth = graph.GetSampledImageViewDescriptorIndex("depthTarget"),
            .outputDepth0 = graph.GetStorageImageViewDescriptorIndex("gtaoDepth", 0),
            .outputDepth1 = graph.GetStorageImageViewDescriptorIndex("gtaoDepth", 1),
            .outputDepth2 = graph.GetStorageImageViewDescriptorIndex("gtaoDepth", 2),
            .outputDepth3 = graph.GetStorageImageViewDescriptorIndex("gtaoDepth", 3),
            .outputDepth4 = graph.GetStorageImageViewDescriptorIndex("gtaoDepth", 4),
            .effectRadius = gtaoConfig.effectRadius,
            .effectFalloffRange = gtaoConfig.effectFalloffRange,
            .radiusMultiplier = gtaoConfig.radiusMultiplier,
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gtaoDepthPrepassPipeline.pipeline.handle);
        vkCmdPushConstants(cmd, gtaoDepthPrepassPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);


        uint32_t xDispatch = (width / 2 + GTAO_DEPTH_PREPASS_DISPATCH_X - 1) / GTAO_DEPTH_PREPASS_DISPATCH_X;
        uint32_t yDispatch = (height / 2 + GTAO_DEPTH_PREPASS_DISPATCH_Y - 1) / GTAO_DEPTH_PREPASS_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& gtaoMainPass = graph.AddPass("GTAOMain", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    gtaoMainPass.ReadSampledImage("gtaoDepth");
    gtaoMainPass.ReadSampledImage("normalTarget");
    gtaoMainPass.WriteStorageImage("gtaoAO");
    gtaoMainPass.WriteStorageImage("gtaoEdges");
    gtaoMainPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        GTAOMainPushConstant pc{
            .sceneData = graph.GetBufferAddress("sceneData"),
            .prefilteredDepthIndex = graph.GetSampledImageViewDescriptorIndex("gtaoDepth"),
            .normalBufferIndex = graph.GetSampledImageViewDescriptorIndex("normalTarget"),
            .aoOutputIndex = graph.GetStorageImageViewDescriptorIndex("gtaoAO"),
            .edgeDataIndex = graph.GetStorageImageViewDescriptorIndex("gtaoEdges"),

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
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gtaoMainPipeline.pipeline.handle);
        vkCmdPushConstants(cmd, gtaoMainPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Denoise pass(es) - typically run 2-3 times for better quality
    graph.CreateTexture("gtaoFiltered", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});

    RenderPass& denoisePass = graph.AddPass("GTAODenoise", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    denoisePass.ReadSampledImage("gtaoAO");
    denoisePass.ReadSampledImage("gtaoEdges");
    denoisePass.WriteStorageImage("gtaoFiltered");
    denoisePass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        GTAODenoisePushConstant pc{
            .sceneData = graph.GetBufferAddress("sceneData"),
            .rawAOIndex = graph.GetSampledImageViewDescriptorIndex("gtaoAO"),
            .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex("gtaoEdges"),
            .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex("gtaoFiltered"),
            .denoiseBlurBeta = gtaoConfig.denoiseBlurBeta,
            .isFinalDenoisePass = 1,
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gtaoDenoisePipeline.pipeline.handle);
        vkCmdPushConstants(cmd, gtaoDenoisePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}

void RenderThread::SetupTemporalAntialiasing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent) const
{
    graph.CreateTexture("taaCurrent", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    if (!graph.HasTexture("taaHistory") || !viewFamily.postProcessConfig.bEnableTemporalAntialiasing) {
        RenderPass& taaPass = graph.AddPass("TemporalAntialiasingFirstFrame", VK_PIPELINE_STAGE_2_COPY_BIT);
        taaPass.ReadCopyImage("deferredResolve");
        taaPass.WriteCopyImage("taaCurrent");
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            VkImage drawImage = graph.GetImageHandle("deferredResolve");
            VkImage taaImage = graph.GetImageHandle("taaCurrent");

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
    }
    else {
        RenderPass& taaPass = graph.AddPass("TemporalAntialiasing", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        taaPass.ReadSampledImage("deferredResolve");
        taaPass.ReadSampledImage("depthTarget");
        taaPass.ReadSampledImage("taaHistory");
        taaPass.ReadSampledImage("velocityTarget");
        taaPass.WriteStorageImage("taaCurrent");
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporalAntialiasingPipeline.pipeline.handle);
            TemporalAntialiasingPushConstant pushData{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex("deferredResolve"),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex("depthTarget"),
                .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex("taaHistory"),
                .velocityIndex = graph.GetSampledImageViewDescriptorIndex("velocityTarget"),
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex("taaCurrent"),
            };

            vkCmdPushConstants(cmd, temporalAntialiasingPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TemporalAntialiasingPushConstant), &pushData);

            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    graph.CreateTexture("taaOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    RenderPass& finalCopyPass = graph.AddPass("finalCopy", VK_PIPELINE_STAGE_2_BLIT_BIT);
    finalCopyPass.ReadBlitImage("taaCurrent");
    finalCopyPass.WriteBlitImage("taaOutput");
    finalCopyPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        VkImage src = graph.GetImageHandle("taaCurrent");
        VkImage dst = graph.GetImageHandle("taaOutput");

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
}

void RenderThread::SetupPostProcessing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, float deltaTime) const
{
    const Core::PostProcessConfiguration& ppConfig = viewFamily.postProcessConfig;
    graph.CreateTexture("postProcessOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    // Exposure
    {
        renderGraph->CreateBuffer("luminanceHistogram", POST_PROCESS_LUMINANCE_BUFFER_SIZE);

        if (!graph.HasBuffer("luminanceBuffer")) {
            renderGraph->CreateBuffer("luminanceBuffer", sizeof(float));
        }
        renderGraph->CarryBufferToNextFrame("luminanceBuffer", "luminanceBuffer", 0);

        auto& clearPass = graph.AddPass("Clear Histogram", VK_PIPELINE_STAGE_TRANSFER_BIT);
        clearPass.WriteTransferBuffer("luminanceHistogram");
        clearPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("luminanceHistogram"), 0, VK_WHOLE_SIZE, 0);
        });

        auto& histogramPass = graph.AddPass("Build Histogram", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        histogramPass.ReadSampledImage("taaOutput");
        histogramPass.WriteBuffer("luminanceHistogram");
        histogramPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            constexpr float minLogLuminance = -10.0;
            constexpr float maxLogLuminance = 2.0;
            constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
            constexpr float oneOverLogLuminanceRange = 1.0 / logLuminanceRange;
            HistogramBuildPushConstant pc{
                .hdrImageIndex = graph.GetSampledImageViewDescriptorIndex("taaOutput"),
                .histogramBufferAddress = graph.GetBufferAddress("luminanceHistogram"),
                .width = width,
                .height = height,
                .minLogLuminance = minLogLuminance,
                .oneOverLogLuminanceRange = oneOverLogLuminanceRange,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, exposureBuildHistogramPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, exposureBuildHistogramPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HistogramBuildPushConstant), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_LUMINANCE_DISPATCH_X - 1) / POST_PROCESS_LUMINANCE_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_LUMINANCE_DISPATCH_Y - 1) / POST_PROCESS_LUMINANCE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

        auto& exposurePass = graph.AddPass("Calculate Exposure", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        exposurePass.ReadBuffer("luminanceHistogram");
        exposurePass.ReadWriteBuffer("luminanceBuffer");
        exposurePass.Execute([&, deltaTime, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            constexpr float minLogLuminance = -10.0;
            constexpr float maxLogLuminance = 2.0;
            constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
            constexpr float oneOverLogLuminanceRange = 1.0 / logLuminanceRange;
            ExposureCalculatePushConstant pc{
                .histogramBufferAddress = graph.GetBufferAddress("luminanceHistogram"),
                .luminanceBufferAddress = graph.GetBufferAddress("luminanceBuffer"),
                .minLogLuminance = minLogLuminance,
                .logLuminanceRange = logLuminanceRange,
                .adaptationSpeed = ppConfig.exposureAdaptationRate * deltaTime,
                .totalPixels = width * height,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, exposureCalculateAveragePipeline.pipeline.handle);
            vkCmdPushConstants(cmd, exposureCalculateAveragePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    // Bloom
    {
        const uint32_t numDownsamples = (renderExtent[0] >= 3840) ? 6 : 5;

        // Create mipmapped bloom chain
        uint32_t numMips = numDownsamples + 1;
        graph.CreateTexture("bloomChain", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], numMips});

        // Threshold pass - write directly to mip 0
        RenderPass& thresholdPass = graph.AddPass("BloomThreshold", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        thresholdPass.ReadSampledImage("taaOutput");
        thresholdPass.ReadWriteImage("bloomChain");
        thresholdPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            BloomThresholdPushConstant pc{
                .inputColorIndex = graph.GetSampledImageViewDescriptorIndex("taaOutput"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("bloomChain", 0),
                .threshold = ppConfig.bloomThreshold,
                .softThreshold = ppConfig.bloomSoftThreshold,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bloomThresholdPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, bloomThresholdPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

        // Downsample chain
        for (uint32_t i = 0; i < numDownsamples; ++i) {
            uint32_t mipWidth = std::max(1u, renderExtent[0] >> (i + 1));
            uint32_t mipHeight = std::max(1u, renderExtent[1] >> (i + 1));

            RenderPass& downsamplePass = graph.AddPass(std::format("BloomDownsample{}", i), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            downsamplePass.ReadWriteImage("bloomChain");
            downsamplePass.Execute([&, mipWidth, mipHeight, srcMip = i, dstMip = i + 1](VkCommandBuffer cmd) {
                BloomDownsamplePushConstant pc{
                    .inputIndex = graph.GetSampledImageViewDescriptorIndex("bloomChain"),
                    .outputIndex = graph.GetStorageImageViewDescriptorIndex("bloomChain", dstMip),
                    .srcMipLevel = srcMip,
                };

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bloomDownsamplePipeline.pipeline.handle);
                vkCmdPushConstants(cmd, bloomDownsamplePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                uint32_t xDispatch = (mipWidth + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
                uint32_t yDispatch = (mipHeight + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
        }

        // Upsample chain
        for (int i = numDownsamples - 1; i >= 0; --i) {
            uint32_t mipWidth = std::max(1u, renderExtent[0] >> i);
            uint32_t mipHeight = std::max(1u, renderExtent[1] >> i);

            RenderPass& upsamplePass = graph.AddPass(std::format("BloomUpsample{}", i), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            upsamplePass.ReadWriteImage("bloomChain");
            upsamplePass.Execute([&, mipWidth, mipHeight, dstMip = i, lowerMip = i + 1](VkCommandBuffer cmd) {
                BloomUpsamplePushConstant pc{
                    .inputIndex = graph.GetSampledImageViewDescriptorIndex("bloomChain"),
                    .outputIndex = graph.GetStorageImageViewDescriptorIndex("bloomChain", dstMip),
                    .lowerMipLevel = static_cast<uint32_t>(lowerMip),
                    .higherMipLevel = static_cast<uint32_t>(dstMip),
                    .radius = ppConfig.bloomRadius,
                };

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bloomUpsamplePipeline.pipeline.handle);
                vkCmdPushConstants(cmd, bloomUpsamplePipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                uint32_t xDispatch = (mipWidth + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
                uint32_t yDispatch = (mipHeight + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
        }
    }

    // Sharpening
    {
        graph.CreateTexture("sharpeningOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& sharpeningPass = graph.AddPass("Sharpening", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        sharpeningPass.ReadSampledImage("taaOutput");
        sharpeningPass.WriteStorageImage("sharpeningOutput");
        sharpeningPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            SharpeningPushConstant pc{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("taaOutput"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("sharpeningOutput"),
                .sharpness = ppConfig.sharpeningStrength,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sharpeningPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, sharpeningPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_SHARPENING_DISPATCH_X - 1) / POST_PROCESS_SHARPENING_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_SHARPENING_DISPATCH_Y - 1) / POST_PROCESS_SHARPENING_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Tonemap
    {
        // todo: add support for HDR swapchain
        graph.CreateTexture("tonemapOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& tonemapPass = graph.AddPass("TonemapSDR", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        tonemapPass.ReadSampledImage("sharpeningOutput");
        tonemapPass.ReadSampledImage("bloomChain");
        tonemapPass.WriteStorageImage("tonemapOutput");
        tonemapPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            TonemapSDRPushConstant pushData{
                .tonemapOperator = ppConfig.tonemapOperator,
                .targetLuminance = ppConfig.exposureTargetLuminance,
                .luminanceBufferAddress = graph.GetBufferAddress("luminanceBuffer"),
                .bloomImageIndex = graph.GetSampledImageViewDescriptorIndex("bloomChain"),
                .bloomIntensity = ppConfig.bloomIntensity,
                .outputWidth = width,
                .outputHeight = height,
                .srcImageIndex = graph.GetSampledImageViewDescriptorIndex("sharpeningOutput"),
                .dstImageIndex = graph.GetStorageImageViewDescriptorIndex("tonemapOutput"),
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tonemapSDRPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, tonemapSDRPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TonemapSDRPushConstant), &pushData);
            uint32_t xDispatch = (width + 15) / 16;
            uint32_t yDispatch = (height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Motion Blur
    {
        uint32_t blurTiledX = (renderExtent[0] + POST_PROCESS_MOTION_BLUR_TILE_SIZE - 1) / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
        uint32_t blurTiledY = (renderExtent[1] + POST_PROCESS_MOTION_BLUR_TILE_SIZE - 1) / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
        graph.CreateTexture("motionBlurTiledMax", TextureInfo{GBUFFER_MOTION_FORMAT, blurTiledX, blurTiledY, 1});
        RenderPass& motionBlurTiledMaxPass = graph.AddPass("MotionBlurTiledMax", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurTiledMaxPass.ReadSampledImage("velocityTarget");
        motionBlurTiledMaxPass.WriteStorageImage("motionBlurTiledMax");
        motionBlurTiledMaxPass.Execute([&, width = renderExtent[0], height = renderExtent[1], blurTiledX, blurTiledY](VkCommandBuffer cmd) {
            MotionBlurTileVelocityPushConstant pc{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .velocityBufferSize = {width, height},
                .tileBufferSize = {blurTiledX, blurTiledY},
                .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex("velocityTarget"),
                .depthBufferIndex = graph.GetSampledImageViewDescriptorIndex("depthTarget"),
                .tileMaxIndex = graph.GetStorageImageViewDescriptorIndex("motionBlurTiledMax"),
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, motionBlurTileMaxPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, motionBlurTileMaxPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X;
            uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });


        graph.CreateTexture("motionBlurTiledNeighborMax", TextureInfo{GBUFFER_MOTION_FORMAT, blurTiledX, blurTiledY, 1});
        RenderPass& motionBlurNeighborMax = graph.AddPass("MotionBlurNeighborMax", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurNeighborMax.ReadSampledImage("motionBlurTiledMax");
        motionBlurNeighborMax.WriteStorageImage("motionBlurTiledNeighborMax");
        motionBlurNeighborMax.Execute([&, blurTiledX, blurTiledY](VkCommandBuffer cmd) {
            MotionBlurNeighborMaxPushConstant pc{
                .tileBufferSize = {blurTiledX, blurTiledY},
                .tileMaxIndex = graph.GetSampledImageViewDescriptorIndex("motionBlurTiledMax"),
                .neighborMaxIndex = graph.GetStorageImageViewDescriptorIndex("motionBlurTiledNeighborMax"),
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, motionBlurNeighborMaxPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, motionBlurNeighborMaxPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X;
            uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });

        graph.CreateTexture("motionBlurOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& motionBlurReconstructionPass = graph.AddPass("MotionBlurReconstruction", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurReconstructionPass.ReadSampledImage("tonemapOutput");
        motionBlurReconstructionPass.ReadSampledImage("velocityTarget");
        motionBlurReconstructionPass.ReadSampledImage("depthTarget");
        motionBlurReconstructionPass.ReadSampledImage("motionBlurTiledNeighborMax");
        motionBlurReconstructionPass.WriteStorageImage("motionBlurOutput");
        motionBlurReconstructionPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            MotionBlurReconstructionPushConstant pc{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .sceneColorIndex = graph.GetSampledImageViewDescriptorIndex("tonemapOutput"),
                .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex("velocityTarget"),
                .depthBufferIndex = graph.GetSampledImageViewDescriptorIndex("depthTarget"),
                .tileNeighborMaxIndex = graph.GetSampledImageViewDescriptorIndex("motionBlurTiledNeighborMax"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("motionBlurOutput"),
                .velocityScale = ppConfig.motionBlurVelocityScale,
                .depthScale = ppConfig.motionBlurDepthScale,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, motionBlurReconstructionPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, motionBlurReconstructionPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_MOTION_BLUR_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_MOTION_BLUR_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Color Grading
    {
        graph.CreateTexture("colorGradingOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& colorGradingPass = graph.AddPass("ColorGrading", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        colorGradingPass.ReadSampledImage("motionBlurOutput");
        colorGradingPass.WriteStorageImage("colorGradingOutput");
        colorGradingPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            ColorGradingPushConstant pc{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("motionBlurOutput"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("colorGradingOutput"),
                .exposure = ppConfig.colorGradingExposure,
                .contrast = ppConfig.colorGradingContrast,
                .saturation = ppConfig.colorGradingSaturation,
                .temperature = ppConfig.colorGradingTemperature,
                .tint = ppConfig.colorGradingTint,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, colorGradingPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, colorGradingPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_COLOR_GRADING_DISPATCH_X - 1) / POST_PROCESS_COLOR_GRADING_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_COLOR_GRADING_DISPATCH_Y - 1) / POST_PROCESS_COLOR_GRADING_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Vignette + Chromatic Aberration
    {
        graph.CreateTexture("vignetteAberrationOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& vignetteAberrationPass = graph.AddPass("Vignette+Aberration", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        vignetteAberrationPass.ReadSampledImage("colorGradingOutput");
        vignetteAberrationPass.WriteStorageImage("vignetteAberrationOutput");
        vignetteAberrationPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            VignetteChromaticAberrationPushConstant pc{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("colorGradingOutput"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("vignetteAberrationOutput"),
                .chromaticAberrationStrength = ppConfig.chromaticAberrationStrength,
                .vignetteStrength = ppConfig.vignetteStrength,
                .vignetteRadius = ppConfig.vignetteRadius,
                .vignetteSmoothness = ppConfig.vignetteSmoothness,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vignetteAberrationPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, vignetteAberrationPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_X - 1) / POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_Y - 1) / POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    // Film Grain
    {
        // graph.CreateTexture("filmGrainOutput", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& filmGrainPass = graph.AddPass("FilmGrain", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        filmGrainPass.ReadSampledImage("vignetteAberrationOutput");
        filmGrainPass.WriteStorageImage("postProcessOutput");
        filmGrainPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            FilmGrainPushConstant pc{
                .sceneData = graph.GetBufferAddress("sceneData"),
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("vignetteAberrationOutput"),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex("postProcessOutput"),
                .grainStrength = ppConfig.grainStrength,
                .grainSize = ppConfig.grainSize,
                .frameIndex = static_cast<uint32_t>(frameNumber),
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, filmGrainPipeline.pipeline.handle);
            vkCmdPushConstants(cmd, filmGrainPipeline.pipelineLayout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (width + POST_PROCESS_FILM_GRAIN_DISPATCH_X - 1) / POST_PROCESS_FILM_GRAIN_DISPATCH_X;
            uint32_t yDispatch = (height + POST_PROCESS_FILM_GRAIN_DISPATCH_Y - 1) / POST_PROCESS_FILM_GRAIN_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }
}
} // Render
