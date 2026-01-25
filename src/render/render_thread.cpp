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

void RenderThread::InitializePipelineManager(AssetLoad::AssetLoadThread* _assetLoadThread)
{
    pipelineManager->SetAssetLoadThread(_assetLoadThread);
    CreatePipelines();
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
        pipelineManager->Update(frameNumber);
        // Wait for frame
        {
            ZoneScopedN("WaitForFrame");
            if (!engineRenderSynchronization->renderFrames.try_acquire_for(std::chrono::milliseconds(10))) {
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
            RenderResponse renderResponse = Render(currentFrameInFlight, currentRenderSynchronization, frameBuffer);
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

RenderThread::RenderResponse RenderThread::Render(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer)
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


    renderGraph->Reset(currentFrameIndex, frameNumber, RDG_PHYSICAL_RESOURCE_UNUSED_THRESHOLD);

    std::array bindings{resourceManager->bindlessSamplerTextureDescriptorBuffer.GetBindingInfo(), resourceManager->bindlessRDGTransientDescriptorBuffer.GetBindingInfo()};
    std::array indices{0u, 1u};
    std::array<VkDeviceSize, 2> offsets{0, 0};
    vkCmdBindDescriptorBuffersEXT(renderSync.commandBuffer, bindings.size(), bindings.data());
    vkCmdSetDescriptorBufferOffsetsEXT(renderSync.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, globalPipelineLayout.handle, 0, bindings.size(), indices.data(), offsets.data());
    vkCmdSetDescriptorBufferOffsetsEXT(renderSync.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, globalPipelineLayout.handle, 0, bindings.size(), indices.data(), offsets.data());

    SetupFrameUniforms(viewFamily, renderExtent, frameBuffer.timeFrame.renderDeltaTime);
    SetupModelUniforms(viewFamily);


    renderGraph->ImportBufferNoBarrier("vertex_buffer", resourceManager->megaVertexBuffer.handle, resourceManager->megaVertexBuffer.address,
                                       {resourceManager->megaVertexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("skinned_vertex_buffer", resourceManager->megaSkinnedVertexBuffer.handle, resourceManager->megaSkinnedVertexBuffer.address,
                                       {resourceManager->megaSkinnedVertexBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
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

    // Main view G-buffer
    GBufferTargets targets{"albedo_target", "normal_target", "pbr_target", "emissive_target", "velocity_target", "depth_target", "deferred_resolve_target"};
    renderGraph->CreateTexture("deferred_resolve_target", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
    GBufferTargets portalTargets{"portal_albedo", "portal_normal", "portal_pbr", "portal_emissive", "portal_velocity", "portal_depth", "portal_deferred_resolve"};
    renderGraph->CreateTexture("portal_deferred_resolve", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    RenderPass& clearDeferredImagePass = renderGraph->AddPass("Clear Deferred Images", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearDeferredImagePass.WriteClearImage(targets.outFinalColor);
    clearDeferredImagePass.WriteClearImage(portalTargets.outFinalColor);
    clearDeferredImagePass.Execute([&](VkCommandBuffer cmd) {
        constexpr VkClearColorValue clearColor = {0.0f, 0.1f, 0.2f, 1.0f};
        VkImageSubresourceRange colorSubresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

        VkImage mainImg = renderGraph->GetImageHandle(targets.outFinalColor);
        vkCmdClearColorImage(cmd, mainImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &colorSubresource);

        VkImage portalImg = renderGraph->GetImageHandle(portalTargets.outFinalColor);
        vkCmdClearColorImage(cmd, portalImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &colorSubresource);
    });


    bool bHasMainGeometry = !viewFamily.mainInstances.empty() && pipelineManager->IsCategoryReady(PipelineCategory::Geometry | PipelineCategory::Instancing);
    bool bHasDirectGeometry = !viewFamily.customStencilDraws.empty() && pipelineManager->IsCategoryReady(PipelineCategory::CustomStencilPass);
    bool bHasAnyGeometry = bHasMainGeometry || bHasDirectGeometry;
    bool bHasGTAO = viewFamily.gtaoConfig.bEnabled && pipelineManager->IsCategoryReady(PipelineCategory::GTAO);
    bool bHasShadows = viewFamily.shadowConfig.enabled && pipelineManager->IsCategoryReady(PipelineCategory::ShadowPass);
    bool bHasDeferred = pipelineManager->IsCategoryReady(PipelineCategory::DeferredShading);

    if (bHasAnyGeometry) {
        if (bHasShadows) {
            SetupCascadedShadows(*renderGraph, viewFamily);
        }

        renderGraph->CreateTexture(targets.albedo, TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(targets.normal, TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(targets.pbr, TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(targets.emissive, TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(targets.velocity, TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(targets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

        if (bHasMainGeometry) {
            SetupMainGeometryPass(*renderGraph, viewFamily, renderExtent, targets, 0, true);
        }

        if (bHasDirectGeometry) {
            SetupDirectGeometryPass(*renderGraph, viewFamily, renderExtent, targets, 0, !bHasMainGeometry);
        }

        if (bHasGTAO) {
            SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent, targets, 0);
        }

        if (bHasShadows || bHasGTAO) {
            SetupShadowsResolve(*renderGraph, viewFamily, renderExtent, targets, 0);
        }

        if (bHasDeferred) {
            SetupDeferredLighting(*renderGraph, viewFamily, renderExtent, targets, 0);
        }
    }

    bool bHasPortalView = bHasAnyGeometry && !viewFamily.portalViews.empty();
    if (bHasPortalView) {
        renderGraph->CreateTexture(portalTargets.albedo, TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(portalTargets.normal, TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(portalTargets.pbr, TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(portalTargets.emissive, TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(portalTargets.velocity, TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1});
        renderGraph->CreateTexture(portalTargets.depthStencil, TextureInfo{DEPTH_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

        if (bHasMainGeometry) {
            SetupMainGeometryPass(*renderGraph, viewFamily, renderExtent, portalTargets, 1, true);
        }

        if (bHasDirectGeometry) {
            SetupDirectGeometryPass(*renderGraph, viewFamily, renderExtent, portalTargets, 1, !bHasMainGeometry);
        }

        if (bHasGTAO) {
            SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
        }

        if (bHasShadows || bHasGTAO) {
            SetupShadowsResolve(*renderGraph, viewFamily, renderExtent, portalTargets, 1);
        }

        if (bHasDeferred) {
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
    if (bHasAnyGeometry) {
        bool bHasTAAPass = pipelineManager->IsCategoryReady(PipelineCategory::TAA) && viewFamily.postProcessConfig.bEnableTemporalAntialiasing;
        if (bHasTAAPass) {
            taaTargets.finalColor = SetupTemporalAntialiasing(*renderGraph, viewFamily, renderExtent, ppTargets);
        }

        bool bHasPostProcess = pipelineManager->IsCategoryReady(PipelineCategory::PostProcess);
        if (bHasPostProcess) {
            finalOutput = SetupPostProcessing(*renderGraph, viewFamily, renderExtent, taaTargets, frameBuffer.timeFrame.renderDeltaTime);
        }
    }


#if WILL_EDITOR
    RenderPass& readbackPass = renderGraph->AddPass("Debug Readback", VK_PIPELINE_STAGE_2_COPY_BIT);
    if (renderGraph->HasBuffer("indirect_buffer") && renderGraph->HasBuffer("indirect_count_buffer")
        && renderGraph->HasBuffer("luminance_histogram") && renderGraph->HasBuffer("luminance_buffer")) {
        readbackPass.ReadTransferBuffer("indirect_buffer");
        readbackPass.ReadTransferBuffer("indirect_count_buffer");
        readbackPass.ReadTransferBuffer("luminance_histogram");
        readbackPass.ReadTransferBuffer("luminance_buffer");
        readbackPass.WriteTransferBuffer("debug_readback_buffer");
        readbackPass.Execute([&](VkCommandBuffer cmd) {
            size_t offsetSoFar = 0;
            VkBufferCopy countCopy{};
            countCopy.srcOffset = 0;
            countCopy.dstOffset = 0;
            countCopy.size = sizeof(uint32_t);
            vkCmdCopyBuffer(cmd, renderGraph->GetBufferHandle("indirect_count_buffer"), renderGraph->GetBufferHandle("debug_readback_buffer"), 1, &countCopy);
            offsetSoFar += sizeof(uint32_t);

            VkBufferCopy indirectCopy{};
            indirectCopy.srcOffset = 0;
            indirectCopy.dstOffset = offsetSoFar;
            indirectCopy.size = 10 * sizeof(InstancedMeshIndirectDrawParameters);
            vkCmdCopyBuffer(cmd, renderGraph->GetBufferHandle("indirect_buffer"), renderGraph->GetBufferHandle("debug_readback_buffer"), 1, &indirectCopy);
            offsetSoFar += 10 * sizeof(InstancedMeshIndirectDrawParameters);

            VkBufferCopy histogramCopy{};
            histogramCopy.srcOffset = 0;
            histogramCopy.dstOffset = offsetSoFar;
            histogramCopy.size = 256 * sizeof(uint32_t);
            vkCmdCopyBuffer(cmd, renderGraph->GetBufferHandle("luminance_histogram"), renderGraph->GetBufferHandle("debug_readback_buffer"), 1, &histogramCopy);
            offsetSoFar += 256 * sizeof(uint32_t);

            VkBufferCopy averageExposureCopy{};
            averageExposureCopy.srcOffset = 0;
            averageExposureCopy.dstOffset = offsetSoFar;
            averageExposureCopy.size = sizeof(uint32_t);
            vkCmdCopyBuffer(cmd, renderGraph->GetBufferHandle("luminance_buffer"), renderGraph->GetBufferHandle("debug_readback_buffer"), 1, &averageExposureCopy);
            offsetSoFar += sizeof(uint32_t);
        });
    }
#endif


    bool bHasDebugPass = !viewFamily.debugResourceName.empty() && pipelineManager->IsCategoryReady(PipelineCategory::Debug);
    if (bHasDebugPass) {
        const char* debugTargetName = viewFamily.debugResourceName.c_str();

        if (renderGraph->HasTexture(debugTargetName)) {
            auto& debugVisPass = renderGraph->AddPass("Debug Visualize", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            debugVisPass.ReadSampledImage(debugTargetName);
            debugVisPass.WriteStorageImage(finalOutput);
            debugVisPass.Execute([&, debugTargetName](VkCommandBuffer cmd) {
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
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                uint32_t xDispatch = (renderExtent[0] + 15) / 16;
                uint32_t yDispatch = (renderExtent[1] + 15) / 16;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
        }
    }

    renderGraph->ImportTexture("swapchain_image", currentSwapchainImage, currentSwapchainImageView, TextureInfo{swapchain->format, swapchain->extent.width, swapchain->extent.height, 1},
                               swapchain->usages,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED);

    auto& blitPass = renderGraph->AddPass("Blit To Swapchain", VK_PIPELINE_STAGE_2_BLIT_BIT);
    blitPass.ReadBlitImage(finalOutput);
    blitPass.WriteBlitImage("swapchain_image");
    blitPass.Execute([&](VkCommandBuffer cmd) {
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

        vkCmdBlitImage2(cmd, &blitInfo);
    });

    if (frameBuffer.bDrawImgui) {
        auto& imguiEditorPass = renderGraph->AddPass("Imgui Draw", VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
        imguiEditorPass.WriteColorAttachment("swapchain_image");
        imguiEditorPass.Execute([&](VkCommandBuffer cmd) {
            const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(renderGraph->GetImageViewHandle("swapchain_image"), nullptr,
                                                                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const ResourceDimensions& dims = renderGraph->GetImageDimensions("swapchain_image");
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &imguiAttachment, nullptr);
            vkCmdBeginRendering(cmd, &renderInfo);
            ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[currentFrameIndex];
            ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, cmd);

            vkCmdEndRendering(cmd);
        });
    }

    renderGraph->SetDebugLogging(frameBuffer.bLogRDG);
    renderGraph->Compile(frameNumber);
    renderGraph->Execute(renderSync.commandBuffer);
    renderGraph->PrepareSwapchain(renderSync.commandBuffer, "swapchain_image");

    resourceManager->debugReadbackLastKnownState = renderGraph->GetBufferState("debug_readback_buffer");

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

    pipelineManager->RegisterComputePipeline("instancing_visibility", Platform::GetShaderPath() / "instancing_visibility_compute.spv",
                                             sizeof(VisibilityPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_prefix_sum", Platform::GetShaderPath() / "instancing_prefix_sum_compute.spv",
                                             sizeof(PrefixSumPushConstant), PipelineCategory::Instancing);
    pipelineManager->RegisterComputePipeline("instancing_indirect_construction", Platform::GetShaderPath() / "instancing_indirect_construction_compute.spv",
                                             sizeof(IndirectWritePushConstant), PipelineCategory::Instancing);

    pipelineManager->RegisterComputePipeline("direct_mesh_shading_build_indirect", Platform::GetShaderPath() / "mesh_shading_direct_build_indirect_compute.spv",
                                             sizeof(BuildDirectIndirectPushConstant), PipelineCategory::CustomStencilPass);

    pipelineManager->RegisterComputePipeline("instancing_shadows_visibility", Platform::GetShaderPath() / "instancing_shadows_visibility_compute.spv",
                                             sizeof(VisibilityShadowsPushConstant), PipelineCategory::Instancing | PipelineCategory::Shadow);

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
        builder.AddShaderStage("shaders/mesh_shading_instanced_task.spv", VK_SHADER_STAGE_TASK_BIT_EXT);
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
            VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::Geometry
        );
        builder.Clear();
    }

    // Direct mesh shading pipeline
    {
        builder.AddShaderStage("shaders/mesh_shading_direct_task.spv", VK_SHADER_STAGE_TASK_BIT_EXT);
        builder.AddShaderStage("shaders/mesh_shading_direct_mesh.spv", VK_SHADER_STAGE_MESH_BIT_EXT);
        builder.AddShaderStage("shaders/mesh_shading_direct_fragment.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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
            "mesh_shading_direct",
            builder,
            sizeof(DirectMeshShadingPushConstant),
            VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            PipelineCategory::CustomStencilPass
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
            PipelineCategory::PortalRendering
        );
        builder.Clear();
    }
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
        SceneData portalSceneData = GenerateSceneData(viewFamily.portalViews[0], viewFamily.postProcessConfig, renderExtent, frameNumber, renderDeltaTime);
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
    VkBuffer srcBuffer = renderGraph->GetTransientUploadBuffer();
    uploadUniformsPass.Execute([&, srcBuffer,
            sceneOffset = sceneDataUploadAllocation.offset,
            portalOffset = portalSceneDataUploadAllocation.offset, bHasPortal,
            shadowOffset = shadowDataUploadAllocation.offset,
            lightOffset = lightDataUploadAllocation.offset](VkCommandBuffer cmd) {
            std::array<VkBufferCopy2, 2> sceneDataRegions{};
            uint32_t sceneDataCount{1};
            sceneDataRegions[0].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            sceneDataRegions[0].srcOffset = sceneOffset;
            sceneDataRegions[0].dstOffset = 0;
            sceneDataRegions[0].size = sizeof(SceneData);
            if (bHasPortal) {
                sceneDataRegions[1].sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
                sceneDataRegions[1].srcOffset = portalOffset;
                sceneDataRegions[1].dstOffset = sizeof(SceneData);
                sceneDataRegions[1].size = sizeof(SceneData);
                sceneDataCount++;
            }

            const VkCopyBufferInfo2 sceneDataCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = srcBuffer,
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
                .srcBuffer = srcBuffer,
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
                .srcBuffer = srcBuffer,
                .dstBuffer = renderGraph->GetBufferHandle("light_data"),
                .regionCount = lightDataRegions.size(),
                .pRegions = lightDataRegions.data()
            };
            vkCmdCopyBuffer2(cmd, &lightDataCopyInfo);
        });
}

void RenderThread::SetupModelUniforms(const Core::ViewFamily& viewFamily)
{
    bool bHasMainInstances = !viewFamily.mainInstances.empty();
    bool bHasCustomInstances = false;

    size_t totalCustomInstances = 0;
    for (const auto& customDraw : viewFamily.customStencilDraws) {
        totalCustomInstances += customDraw.instances.size();
    }
    bHasCustomInstances = totalCustomInstances > 0;

    if (!bHasMainInstances && !bHasCustomInstances) {
        return;
    }

    if (viewFamily.modelMatrices.empty() || viewFamily.materials.empty()) {
        return;
    }

    if (bHasMainInstances) {
        frameResourceLimits.highestInstanceBuffer = std::max(frameResourceLimits.highestInstanceBuffer, NextPowerOfTwo(viewFamily.mainInstances.size()));
    }
    if (bHasCustomInstances) {
        frameResourceLimits.highestDirectInstanceBuffer = std::max(frameResourceLimits.highestDirectInstanceBuffer, NextPowerOfTwo(totalCustomInstances));
    }
    frameResourceLimits.highestModelBuffer = std::max(frameResourceLimits.highestModelBuffer, NextPowerOfTwo(viewFamily.modelMatrices.size()));
    frameResourceLimits.highestMaterialBuffer = std::max(frameResourceLimits.highestMaterialBuffer, NextPowerOfTwo(viewFamily.materials.size()));

    if (bHasMainInstances) {
        size_t instanceBufferSize = frameResourceLimits.highestInstanceBuffer * sizeof(Instance);
        renderGraph->CreateBuffer("instance_buffer", instanceBufferSize);
    }
    if (bHasCustomInstances) {
        size_t directInstanceBufferSize = frameResourceLimits.highestDirectInstanceBuffer * sizeof(Instance);
        renderGraph->CreateBuffer("direct_instance_buffer", directInstanceBufferSize);
    }

    size_t modelBufferSize = frameResourceLimits.highestModelBuffer * sizeof(Model);
    size_t jointMatrixBufferSize = frameResourceLimits.highestModelBuffer * sizeof(glm::mat4);
    size_t materialBufferSize = frameResourceLimits.highestMaterialBuffer * sizeof(MaterialProperties);

    renderGraph->CreateBuffer("model_buffer", modelBufferSize);
    renderGraph->CreateBuffer("joint_matrix_buffer", jointMatrixBufferSize);
    renderGraph->CreateBuffer("material_buffer", materialBufferSize);

    UploadAllocation instanceUpload{};
    if (bHasMainInstances) {
        instanceUpload = renderGraph->AllocateTransient(viewFamily.mainInstances.size() * sizeof(Instance));
        auto* instanceBuffer = static_cast<Instance*>(instanceUpload.ptr);
        for (size_t i = 0; i < viewFamily.mainInstances.size(); ++i) {
            auto& inst = viewFamily.mainInstances[i];
            instanceBuffer[i] = {
                .primitiveIndex = inst.primitiveIndex,
                .modelIndex = inst.modelIndex,
                .materialIndex = inst.gpuMaterialIndex,
                .jointMatrixOffset = 0,
            };
        }
    }

    UploadAllocation directInstanceUpload{};
    if (bHasCustomInstances) {
        directInstanceUpload = renderGraph->AllocateTransient(totalCustomInstances * sizeof(Instance));
        auto* directInstanceBuffer = static_cast<Instance*>(directInstanceUpload.ptr);
        size_t directInstanceOffset = 0;
        for (const auto& customDraw : viewFamily.customStencilDraws) {
            for (const auto& inst : customDraw.instances) {
                directInstanceBuffer[directInstanceOffset++] = {
                    .primitiveIndex = inst.primitiveIndex,
                    .modelIndex = inst.modelIndex,
                    .materialIndex = inst.gpuMaterialIndex,
                    .jointMatrixOffset = 0,
                };
            }
        }
    }

    UploadAllocation modelUpload = renderGraph->AllocateTransient(viewFamily.modelMatrices.size() * sizeof(Model));
    auto* modelBuffer = static_cast<Model*>(modelUpload.ptr);
    for (size_t i = 0; i < viewFamily.modelMatrices.size(); ++i) {
        modelBuffer[i] = viewFamily.modelMatrices[i];
    }

    UploadAllocation materialUpload = renderGraph->AllocateTransient(viewFamily.materials.size() * sizeof(MaterialProperties));
    memcpy(materialUpload.ptr, viewFamily.materials.data(), viewFamily.materials.size() * sizeof(MaterialProperties));

    RenderPass& uploadModelsPass = renderGraph->AddPass("Upload Model Uniforms", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    if (bHasMainInstances) {
        uploadModelsPass.WriteTransferBuffer("instance_buffer");
    }
    if (bHasCustomInstances) {
        uploadModelsPass.WriteTransferBuffer("direct_instance_buffer");
    }
    uploadModelsPass.WriteTransferBuffer("model_buffer");
    uploadModelsPass.WriteTransferBuffer("material_buffer");

    VkBuffer srcBuffer = renderGraph->GetTransientUploadBuffer();
    uploadModelsPass.Execute([&, srcBuffer, bHasMainInstances, bHasCustomInstances,
            instanceOffset = instanceUpload.offset,
            instanceSize = bHasMainInstances ? viewFamily.mainInstances.size() * sizeof(Instance) : 0,
            directInstanceOffset = directInstanceUpload.offset,
            directInstanceSize = bHasCustomInstances ? totalCustomInstances * sizeof(Instance) : 0,
            modelOffset = modelUpload.offset,
            modelSize = viewFamily.modelMatrices.size() * sizeof(Model),
            materialOffset = materialUpload.offset,
            materialSize = viewFamily.materials.size() * sizeof(MaterialProperties)](VkCommandBuffer cmd) {
            if (bHasMainInstances) {
                VkBufferCopy2 instanceCopy{};
                instanceCopy.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
                instanceCopy.srcOffset = instanceOffset;
                instanceCopy.dstOffset = 0;
                instanceCopy.size = instanceSize;
                VkCopyBufferInfo2 instanceCopyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = srcBuffer,
                    .dstBuffer = renderGraph->GetBufferHandle("instance_buffer"),
                    .regionCount = 1,
                    .pRegions = &instanceCopy
                };
                vkCmdCopyBuffer2(cmd, &instanceCopyInfo);
            }

            if (bHasCustomInstances) {
                VkBufferCopy2 directInstanceCopy{};
                directInstanceCopy.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
                directInstanceCopy.srcOffset = directInstanceOffset;
                directInstanceCopy.dstOffset = 0;
                directInstanceCopy.size = directInstanceSize;
                VkCopyBufferInfo2 directInstanceCopyInfo{
                    .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                    .srcBuffer = srcBuffer,
                    .dstBuffer = renderGraph->GetBufferHandle("direct_instance_buffer"),
                    .regionCount = 1,
                    .pRegions = &directInstanceCopy
                };
                vkCmdCopyBuffer2(cmd, &directInstanceCopyInfo);
            }

            VkBufferCopy2 modelCopy{};
            modelCopy.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            modelCopy.srcOffset = modelOffset;
            modelCopy.dstOffset = 0;
            modelCopy.size = modelSize;
            VkCopyBufferInfo2 modelCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = srcBuffer,
                .dstBuffer = renderGraph->GetBufferHandle("model_buffer"),
                .regionCount = 1,
                .pRegions = &modelCopy
            };
            vkCmdCopyBuffer2(cmd, &modelCopyInfo);

            VkBufferCopy2 materialCopy{};
            materialCopy.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            materialCopy.srcOffset = materialOffset;
            materialCopy.dstOffset = 0;
            materialCopy.size = materialSize;
            VkCopyBufferInfo2 materialCopyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = srcBuffer,
                .dstBuffer = renderGraph->GetBufferHandle("material_buffer"),
                .regionCount = 1,
                .pRegions = &materialCopy
            };
            vkCmdCopyBuffer2(cmd, &materialCopyInfo);
        });

    if (bHasCustomInstances) {
        frameResourceLimits.highestDirectIndirectCommandBuffer = std::max(frameResourceLimits.highestDirectIndirectCommandBuffer, NextPowerOfTwo(totalCustomInstances));
        size_t indirectCommandBufferSize = frameResourceLimits.highestDirectIndirectCommandBuffer * sizeof(DrawMeshTasksIndirectCommand);
        renderGraph->CreateBuffer("direct_indirect_command_buffer", indirectCommandBufferSize);
    }

    const size_t instanceCount = viewFamily.mainInstances.size();
    const size_t instanceCountPow2 = NextPowerOfTwo(instanceCount);

    frameResourceLimits.highestPackedVisibilityBuffer = std::max(frameResourceLimits.highestPackedVisibilityBuffer, instanceCountPow2);
    frameResourceLimits.highestInstanceOffsetBuffer = std::max(frameResourceLimits.highestInstanceOffsetBuffer, instanceCountPow2);
    frameResourceLimits.highestCompactedInstanceBuffer = std::max(frameResourceLimits.highestCompactedInstanceBuffer, instanceCountPow2);

    renderGraph->CreateBuffer("packed_visibility_buffer", sizeof(uint32_t) * (frameResourceLimits.highestPackedVisibilityBuffer + 31) / 32);
    renderGraph->CreateBuffer("instance_offset_buffer", frameResourceLimits.highestInstanceOffsetBuffer * sizeof(uint32_t));
    renderGraph->CreateBuffer("compacted_instance_buffer", (frameResourceLimits.highestCompactedInstanceBuffer) * sizeof(Instance));

    // todo: somehow cap primitives so its not unbounded
    renderGraph->CreateBuffer("indirect_count_buffer", INSTANCING_MESH_INDIRECT_COUNT_SIZE);
    renderGraph->CreateBuffer("primitive_count_buffer", INSTANCING_PRIMITIVE_COUNT_SIZE);
    renderGraph->CreateBuffer("indirect_buffer", INSTANCING_MESH_INDIRECT_PARAMETERS);
}

void RenderThread::SetupCascadedShadows(RenderGraph& graph, const Core::ViewFamily& viewFamily) const
{
    Core::ShadowConfiguration shadowConfig = viewFamily.shadowConfig;

    for (int32_t cascadeLevel = 0; cascadeLevel < SHADOW_CASCADE_COUNT; ++cascadeLevel) {
        std::string shadowMapName = "shadow_cascade_" + std::to_string(cascadeLevel);
        std::string shadowPassName = "Shadow Cascade Pass " + std::to_string(cascadeLevel);

        graph.CreateTexture(shadowMapName, TextureInfo{SHADOW_CASCADE_FORMAT, shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height, 1});


        std::string clearPassName = "Clear Shadow Buffers " + std::to_string(cascadeLevel);
        std::string visPassName = "Shadow Visibility " + std::to_string(cascadeLevel);
        std::string prefixPassName = "Shadow Prefix Sum " + std::to_string(cascadeLevel);
        std::string indirectPassName = "Shadow Indirect Construction " + std::to_string(cascadeLevel);

        std::string packedVisName = "shadow_packed_visibility_" + std::to_string(cascadeLevel);
        std::string instanceOffsetName = "shadow_instance_offset_" + std::to_string(cascadeLevel);
        std::string primitiveCountName = "shadow_primitive_count_" + std::to_string(cascadeLevel);
        std::string indirectCountName = "shadow_indirect_count_" + std::to_string(cascadeLevel);
        std::string compactedInstanceName = "shadow_compacted_instance_" + std::to_string(cascadeLevel);
        std::string indirectName = "shadow_indirect_" + std::to_string(cascadeLevel);


        renderGraph->CreateBuffer(packedVisName, sizeof(uint32_t) * (frameResourceLimits.highestPackedVisibilityBuffer + 31) / 32);
        renderGraph->CreateBuffer(instanceOffsetName, frameResourceLimits.highestInstanceOffsetBuffer * sizeof(uint32_t));
        renderGraph->CreateBuffer(compactedInstanceName, (frameResourceLimits.highestCompactedInstanceBuffer) * sizeof(Instance));
        // todo: limit these as well
        renderGraph->CreateBuffer(primitiveCountName, INSTANCING_PRIMITIVE_COUNT_SIZE);
        renderGraph->CreateBuffer(indirectCountName, INSTANCING_MESH_INDIRECT_COUNT_SIZE);
        renderGraph->CreateBuffer(indirectName, INSTANCING_MESH_INDIRECT_PARAMETERS);

        RenderPass& clearPass = graph.AddPass(clearPassName, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
        clearPass.WriteTransferBuffer(packedVisName);
        clearPass.WriteTransferBuffer(primitiveCountName);
        clearPass.WriteTransferBuffer(indirectCountName);
        clearPass.Execute([&, packedVisName, primitiveCountName, indirectCountName](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(packedVisName), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(primitiveCountName), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(cmd, graph.GetBufferHandle(indirectCountName), 0, VK_WHOLE_SIZE, 0);
        });

        RenderPass& visibilityPass = graph.AddPass(visPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        visibilityPass.ReadBuffer("scene_data");
        visibilityPass.ReadBuffer("shadow_data");
        visibilityPass.ReadBuffer("primitive_buffer");
        visibilityPass.ReadBuffer("model_buffer");
        visibilityPass.ReadBuffer("instance_buffer");
        visibilityPass.WriteBuffer(packedVisName);
        visibilityPass.WriteBuffer(instanceOffsetName);
        visibilityPass.WriteBuffer(primitiveCountName);
        visibilityPass.Execute([&, cascadeLevel, packedVisName, instanceOffsetName, primitiveCountName](VkCommandBuffer cmd) {
            VisibilityShadowsPushConstant pushData{
                .shadowData = graph.GetBufferAddress("shadow_data"),
                .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                .packedVisibilityBuffer = graph.GetBufferAddress(packedVisName),
                .instanceOffsetBuffer = graph.GetBufferAddress(instanceOffsetName),
                .primitiveCountBuffer = graph.GetBufferAddress(primitiveCountName),
                .instanceCount = static_cast<uint32_t>(viewFamily.mainInstances.size()),
                .cascadeLevel = static_cast<uint32_t>(cascadeLevel),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_shadows_visibility");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityShadowsPushConstant), &pushData);
            uint32_t xDispatch = (viewFamily.mainInstances.size() + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
            vkCmdDispatch(cmd, xDispatch, 1, 1);
        });

        RenderPass& prefixSumPass = graph.AddPass(prefixPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        prefixSumPass.ReadBuffer(primitiveCountName);
        prefixSumPass.Execute([&, primitiveCountName](VkCommandBuffer cmd) {
            PrefixSumPushConstant pushConstant{
                .primitiveCountBuffer = graph.GetBufferAddress(primitiveCountName),
                .highestPrimitiveIndex = 200,
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefixSumPushConstant), &pushConstant);
            vkCmdDispatch(cmd, 1, 1, 1);
        });

        // todo: Shadow needs to also draw direct draw instances
        // todo: This needs to not draw if there are no instanced draws.
        RenderPass& indirectPass = graph.AddPass(indirectPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        indirectPass.ReadBuffer("primitive_buffer");
        indirectPass.ReadBuffer("model_buffer");
        indirectPass.ReadBuffer("instance_buffer");
        indirectPass.ReadBuffer(packedVisName);
        indirectPass.ReadBuffer(instanceOffsetName);
        indirectPass.ReadBuffer(primitiveCountName);
        indirectPass.WriteBuffer(compactedInstanceName);
        indirectPass.WriteBuffer(indirectCountName);
        indirectPass.WriteBuffer(indirectName);
        indirectPass.Execute([&, packedVisName, instanceOffsetName, primitiveCountName, compactedInstanceName, indirectCountName, indirectName](VkCommandBuffer cmd) {
            IndirectWritePushConstant pushConstant{
                .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                .packedVisibilityBuffer = graph.GetBufferAddress(packedVisName),
                .instanceOffsetBuffer = graph.GetBufferAddress(instanceOffsetName),
                .primitiveCountBuffer = graph.GetBufferAddress(primitiveCountName),
                .compactedInstanceBuffer = graph.GetBufferAddress(compactedInstanceName),
                .indirectCountBuffer = graph.GetBufferAddress(indirectCountName),
                .indirectBuffer = graph.GetBufferAddress(indirectName),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_indirect_construction");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IndirectWritePushConstant), &pushConstant);
            uint32_t xDispatch = (viewFamily.mainInstances.size() + (INSTANCING_CONSTRUCTION_DISPATCH_X - 1)) / INSTANCING_CONSTRUCTION_DISPATCH_X;
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
        shadowPass.ReadBuffer(compactedInstanceName);
        shadowPass.ReadIndirectBuffer(indirectName);
        shadowPass.ReadIndirectCountBuffer(indirectCountName);
        shadowPass.Execute([&, shadowConfig, cascadeLevel, shadowMapName, compactedInstanceName, indirectName, indirectCountName](VkCommandBuffer cmd) {
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
                .indirectBuffer = graph.GetBufferAddress(indirectName),
                .compactedInstanceBuffer = graph.GetBufferAddress(compactedInstanceName),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .cascadeIndex = static_cast<uint32_t>(cascadeLevel),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("shadow_cascade_instanced");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
            vkCmdSetDepthBias(cmd, -shadowConfig.cascadePreset.biases[cascadeLevel].linear, 0.0f, -shadowConfig.cascadePreset.biases[cascadeLevel].sloped);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT,
                               0, sizeof(ShadowMeshShadingPushConstant), &pushConstants);

            vkCmdDrawMeshTasksIndirectCountEXT(cmd,
                                               graph.GetBufferHandle(indirectName), 0,
                                               graph.GetBufferHandle(indirectCountName), 0,
                                               MEGA_PRIMITIVE_BUFFER_COUNT,
                                               sizeof(InstancedMeshIndirectDrawParameters));

            vkCmdEndRendering(cmd);
        });
    }
}

void RenderThread::SetupMainGeometryPass(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneIndex,
                                         bool bClearTargets) const
{
    RenderPass& clearPass = graph.AddPass("Clear Instancing Buffers", VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    clearPass.WriteTransferBuffer("packed_visibility_buffer");
    clearPass.WriteTransferBuffer("primitive_count_buffer");
    clearPass.WriteTransferBuffer("indirect_count_buffer");
    clearPass.Execute([&](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle("packed_visibility_buffer"), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle("primitive_count_buffer"), 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, graph.GetBufferHandle("indirect_count_buffer"), 0, VK_WHOLE_SIZE, 0);
    });
    RenderPass& visibilityPass = graph.AddPass("Compute Visibility", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    visibilityPass.ReadBuffer("scene_data");
    visibilityPass.ReadBuffer("primitive_buffer");
    visibilityPass.ReadBuffer("model_buffer");
    visibilityPass.ReadBuffer("instance_buffer");
    visibilityPass.WriteBuffer("packed_visibility_buffer");
    visibilityPass.WriteBuffer("instance_offset_buffer");
    visibilityPass.WriteBuffer("primitive_count_buffer");
    visibilityPass.Execute([&, sceneIndex](VkCommandBuffer cmd) {
        // todo: profile; a lot of instances, 100k. Try first all of the same primitive. Then try again with a few different primitives (but total remains around the same)
        VisibilityPushConstant visibilityPushData{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
            .modelBuffer = graph.GetBufferAddress("model_buffer"),
            .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
            .packedVisibilityBuffer = graph.GetBufferAddress("packed_visibility_buffer"),
            .instanceOffsetBuffer = graph.GetBufferAddress("instance_offset_buffer"),
            .primitiveCountBuffer = graph.GetBufferAddress("primitive_count_buffer"),
            .instanceCount = static_cast<uint32_t>(viewFamily.mainInstances.size()),
            .sceneDataIndex = sceneIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_visibility");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityPushConstant), &visibilityPushData);
        uint32_t xDispatch = (viewFamily.mainInstances.size() + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
        vkCmdDispatch(cmd, xDispatch, 1, 1);
    });

    RenderPass& prefixSumPass = graph.AddPass("Prefix Sum", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    prefixSumPass.ReadBuffer("primitive_count_buffer");
    prefixSumPass.Execute([&](VkCommandBuffer cmd) {
        // todo: optimize the F* out of this. Use multiple passes if necessary
        PrefixSumPushConstant prefixSumPushConstant{
            .primitiveCountBuffer = graph.GetBufferAddress("primitive_count_buffer"),
            .highestPrimitiveIndex = 200,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefixSumPushConstant), &prefixSumPushConstant);
        vkCmdDispatch(cmd, 1, 1, 1);
    });

    RenderPass& indirectConstructionPass = graph.AddPass("Indirect Construction", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    indirectConstructionPass.ReadBuffer("primitive_buffer");
    indirectConstructionPass.ReadBuffer("model_buffer");
    indirectConstructionPass.ReadBuffer("instance_buffer");
    indirectConstructionPass.ReadBuffer("packed_visibility_buffer");
    indirectConstructionPass.ReadBuffer("instance_offset_buffer");
    indirectConstructionPass.ReadBuffer("primitive_count_buffer");
    indirectConstructionPass.WriteBuffer("compacted_instance_buffer");
    indirectConstructionPass.WriteBuffer("indirect_count_buffer");
    indirectConstructionPass.WriteBuffer("indirect_buffer");
    indirectConstructionPass.Execute([&](VkCommandBuffer cmd) {
        IndirectWritePushConstant indirectWritePushConstant{
            .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
            .modelBuffer = graph.GetBufferAddress("model_buffer"),
            .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
            .packedVisibilityBuffer = graph.GetBufferAddress("packed_visibility_buffer"),
            .instanceOffsetBuffer = graph.GetBufferAddress("instance_offset_buffer"),
            .primitiveCountBuffer = graph.GetBufferAddress("primitive_count_buffer"),
            .compactedInstanceBuffer = graph.GetBufferAddress("compacted_instance_buffer"),
            .indirectCountBuffer = graph.GetBufferAddress("indirect_count_buffer"),
            .indirectBuffer = graph.GetBufferAddress("indirect_buffer"),
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_indirect_construction");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IndirectWritePushConstant), &indirectWritePushConstant);
        uint32_t xDispatch = (viewFamily.mainInstances.size() + (INSTANCING_CONSTRUCTION_DISPATCH_X - 1)) / INSTANCING_CONSTRUCTION_DISPATCH_X;
        vkCmdDispatch(cmd, xDispatch, 1, 1);
    });

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
    instancedMeshShading.ReadBuffer("compacted_instance_buffer");
    instancedMeshShading.ReadIndirectBuffer("indirect_buffer");
    instancedMeshShading.ReadIndirectCountBuffer("indirect_count_buffer");
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
            .indirectBuffer = graph.GetBufferAddress("indirect_buffer"),
            .compactedInstanceBuffer = graph.GetBufferAddress("compacted_instance_buffer"),
            .materialBuffer = graph.GetBufferAddress("material_buffer"),
            .modelBuffer = graph.GetBufferAddress("model_buffer"),
            .sceneDataIndex = sceneIndex,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("mesh_shading_instanced");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(InstancedMeshShadingPushConstant), &pushConstants);

        vkCmdDrawMeshTasksIndirectCountEXT(cmd,
                                           graph.GetBufferHandle("indirect_buffer"), 0,
                                           graph.GetBufferHandle("indirect_count_buffer"), 0,
                                           MEGA_PRIMITIVE_BUFFER_COUNT,
                                           sizeof(InstancedMeshIndirectDrawParameters));

        vkCmdEndRendering(cmd);
    });
}

void RenderThread::SetupDirectGeometryPass(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneIndex,
                                           bool bClearTargets) const
{
    if (viewFamily.customStencilDraws.empty()) { return; }

    size_t totalInstances = 0;
    for (const auto& customDraw : viewFamily.customStencilDraws) {
        totalInstances += customDraw.instances.size();
    }
    if (totalInstances == 0) { return; }

    RenderPass& buildIndirectPass = graph.AddPass("Build Direct Indirect Commands", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    buildIndirectPass.ReadBuffer("primitive_buffer");
    buildIndirectPass.ReadBuffer("direct_instance_buffer");
    buildIndirectPass.WriteBuffer("direct_indirect_command_buffer");
    buildIndirectPass.Execute([&, totalInstances](VkCommandBuffer cmd) {
        BuildDirectIndirectPushConstant pushConstant{
            .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
            .instanceBuffer = graph.GetBufferAddress("direct_instance_buffer"),
            .indirectCommandBuffer = graph.GetBufferAddress("direct_indirect_command_buffer"),
            .instanceCount = static_cast<uint32_t>(totalInstances),
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

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("mesh_shading_direct");
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
    });
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
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
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
    taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
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
    renderGraph->CreateTexture("post_process_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
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
        histogramPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
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
            constexpr float oneOverLogLuminanceRange = 1.0 / logLuminanceRange;
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
        thresholdPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
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
        sharpeningPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
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
        motionBlurTiledMaxPass.Execute([&, width = renderExtent[0], height = renderExtent[1], blurTiledX, blurTiledY](VkCommandBuffer cmd) {
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
        motionBlurReconstructionPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
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
} // Render
