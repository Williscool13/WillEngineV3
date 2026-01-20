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
#include "pipelines/pipeline_manager.h"
#include "shadows/shadow_helpers.h"


namespace Render
{
RenderThread::RenderThread() = default;

RenderThread::RenderThread(Core::FrameSync* engineRenderSynchronization, enki::TaskScheduler* scheduler, SDL_Window* window, uint32_t width,
                           uint32_t height)
    : window(window), engineRenderSynchronization(engineRenderSynchronization), scheduler(scheduler), assetLoadThread(assetLoadThread)
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
}

RenderThread::~RenderThread() = default;

void RenderThread::InitializePipelineManager(AssetLoad::AssetLoadThread* _assetLoadThread)
{
    std::array layouts{
        resourceManager->bindlessSamplerTextureDescriptorBuffer.descriptorSetLayout.handle,
        resourceManager->bindlessRDGTransientDescriptorBuffer.descriptorSetLayout.handle
    };
    pipelineManager = std::make_unique<PipelineManager>(context.get(), _assetLoadThread, layouts);
    assetLoadThread = _assetLoadThread;

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

    renderGraph->ImportBufferNoBarrier("scene_data", frameResource.sceneDataBuffer.handle, frameResource.sceneDataBuffer.address,
                                       {frameResource.sceneDataBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("shadow_data", frameResource.shadowBuffer.handle, frameResource.shadowBuffer.address,
                                       {frameResource.shadowBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("light_data", frameResource.lightBuffer.handle, frameResource.lightBuffer.address,
                                       {frameResource.lightBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});

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
    renderGraph->ImportBufferNoBarrier("instance_buffer", frameResource.instanceBuffer.handle, frameResource.instanceBuffer.address,
                                       {frameResource.instanceBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("model_buffer", frameResource.modelBuffer.handle, frameResource.modelBuffer.address,
                                       {frameResource.modelBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("joint_matrix_buffer", frameResource.jointMatrixBuffer.handle, frameResource.jointMatrixBuffer.address,
                                       {frameResource.jointMatrixBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    renderGraph->ImportBufferNoBarrier("material_buffer", frameResource.materialBuffer.handle, frameResource.materialBuffer.address,
                                       {frameResource.materialBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});

    renderGraph->ImportBuffer("debug_readback_buffer", resourceManager->debugReadbackBuffer.handle, resourceManager->debugReadbackBuffer.address,
                              {resourceManager->debugReadbackBuffer.allocationInfo.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT}, resourceManager->debugReadbackLastKnownState);

    renderGraph->CreateBuffer("packed_visibility_buffer", INSTANCING_PACKED_VISIBILITY_SIZE);
    renderGraph->CreateBuffer("instance_offset_buffer", INSTANCING_INSTANCE_OFFSET_SIZE);
    renderGraph->CreateBuffer("primitive_count_buffer", INSTANCING_PRIMITIVE_COUNT_SIZE);
    renderGraph->CreateBuffer("compacted_instance_buffer", INSTANCING_COMPACTED_INSTANCE_BUFFER_SIZE);
    renderGraph->CreateBuffer("indirect_count_buffer", INSTANCING_MESH_INDIRECT_COUNT);
    renderGraph->CreateBuffer("indirect_buffer", INSTANCING_MESH_INDIRECT_PARAMETERS);

    renderGraph->CreateTexture("albedo_target", TextureInfo{GBUFFER_ALBEDO_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("normal_target", TextureInfo{GBUFFER_NORMAL_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("pbr_target", TextureInfo{GBUFFER_PBR_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("emissive_target", TextureInfo{GBUFFER_EMISSIVE_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("velocity_target", TextureInfo{GBUFFER_MOTION_FORMAT, renderExtent[0], renderExtent[1], 1});
    renderGraph->CreateTexture("depth_target", TextureInfo{VK_FORMAT_D32_SFLOAT, renderExtent[0], renderExtent[1], 1});

    RenderPass& clearGBufferPass = renderGraph->AddPass("Clear GBuffer", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearGBufferPass.WriteClearImage("albedo_target");
    clearGBufferPass.WriteClearImage("normal_target");
    clearGBufferPass.WriteClearImage("pbr_target");
    clearGBufferPass.WriteClearImage("emissive_target");
    clearGBufferPass.WriteClearImage("velocity_target");
    clearGBufferPass.Execute([&](VkCommandBuffer cmd) {
        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdClearColorImage(cmd, renderGraph->GetImageHandle("albedo_target"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
        vkCmdClearColorImage(cmd, renderGraph->GetImageHandle("normal_target"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
        vkCmdClearColorImage(cmd, renderGraph->GetImageHandle("pbr_target"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
        vkCmdClearColorImage(cmd, renderGraph->GetImageHandle("emissive_target"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
        vkCmdClearColorImage(cmd, renderGraph->GetImageHandle("velocity_target"), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
    });


    bool bHasShadows = frameBuffer.mainViewFamily.shadowConfig.enabled && !viewFamily.instances.empty() && pipelineManager->IsCategoryReady(PipelineCategory::ShadowPass);
    if (bHasShadows) {
        SetupCascadedShadows(*renderGraph, viewFamily);
    }

    bool bHasInstancing = pipelineManager->IsCategoryReady(PipelineCategory::Instancing);
    if (bHasInstancing) {
        if (frameBuffer.bFreezeVisibility) {
            if (!bFrozenVisibility) {
                // Note that freezing visibility will freeze to the main camera's frustum.
                //   Shadows will render main camera's visible instances, so some shadows may be missing.
                SetupInstancing(*renderGraph, viewFamily);
                bFrozenVisibility = true;
            }

            renderGraph->CarryBufferToNextFrame("compacted_instance_buffer", "compacted_instance_buffer", 0);
            renderGraph->CarryBufferToNextFrame("indirect_buffer", "indirect_buffer", 0);
            renderGraph->CarryBufferToNextFrame("indirect_count_buffer", "indirect_count_buffer", 0);
        }
        else {
            SetupInstancing(*renderGraph, viewFamily);
            bFrozenVisibility = false;
        }
    }

    bool bHasMainGeometry = pipelineManager->IsCategoryReady(PipelineCategory::Geometry);
    if (bHasMainGeometry) {
        SetupMainGeometryPass(*renderGraph, viewFamily);
    }

    bool bHasGTAO = viewFamily.gtaoConfig.bEnabled && pipelineManager->IsCategoryReady(PipelineCategory::GTAO);
    if (bHasGTAO) {
        SetupGroundTruthAmbientOcclusion(*renderGraph, viewFamily, renderExtent);
    }

    // IN : albedoTarget, normalTarget, pbrTarget, emissiveTarget, depth_target, shadow_cascade_0, shadow_cascade_1, shadow_cascade_2, shadow_cascade_3
    // OUT: deferred_resolve_target
    SetupDeferredLighting(*renderGraph, viewFamily, renderExtent);

    bool bHasDebugPass = viewFamily.mainView.debug != -1 && pipelineManager->IsCategoryReady(PipelineCategory::Debug);
    if (bHasDebugPass) {
        int32_t debugIndex = viewFamily.mainView.debug;
        if (viewFamily.mainView.debug == 0) {
            debugIndex = 10;
        }
        static constexpr const char* debugTargets[] = {
            "dummy",
            "depth_target", // 1
            "albedo_target", // 2
            "normal_target", // 3
            "pbr_target", // 4
            "velocity_target", // 5
            "gtao_depth", // 6
            "gtao_depth", // 7
            "gtao_depth", // 8
            "gtao_depth", // 9
            "gtao_depth", // 0
        };

        if (debugIndex >= std::size(debugTargets)) {
            debugIndex = 1;
        }
        const char* debugTargetName = debugTargets[debugIndex];
        if (renderGraph->HasTexture(debugTargetName)) {
            auto& debugVisPass = renderGraph->AddPass("Debug Visualize", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            debugVisPass.ReadSampledImage(debugTargetName);
            debugVisPass.WriteStorageImage("deferred_resolve_target");
            debugVisPass.Execute([&, debugTargetName, debugIndex](VkCommandBuffer cmd) {
                const ResourceDimensions& dims = renderGraph->GetImageDimensions(debugTargetName);
                DebugVisualizePushConstant pushData{
                    .srcExtent = {dims.width, dims.height},
                    .dstExtent = {renderExtent[0], renderExtent[1]},
                    .nearPlane = viewFamily.mainView.currentViewData.nearPlane,
                    .farPlane = viewFamily.mainView.currentViewData.farPlane,
                    .textureIndex = renderGraph->GetSampledImageViewDescriptorIndex(debugTargetName),
                    .outputImageIndex = renderGraph->GetStorageImageViewDescriptorIndex("deferred_resolve_target"),
                    .debugType = static_cast<uint32_t>(debugIndex)
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("debug_visualize");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DebugVisualizePushConstant), &pushData);
                uint32_t xDispatch = (renderExtent[0] + 15) / 16;
                uint32_t yDispatch = (renderExtent[1] + 15) / 16;
                vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
            });
        }
    }


    // IN : deferred_resolve_target
    // OUT: taaCurrent, taaOutput
    bool bHasTAAPass = pipelineManager->IsCategoryReady(PipelineCategory::TAA);
    if (bHasTAAPass) {
        SetupTemporalAntialiasing(*renderGraph, viewFamily, renderExtent);
    } else {
        renderGraph->AliasTexture("taa_output", "deferred_resolve_target");
    }
    renderGraph->CarryTextureToNextFrame("taa_current", "taa_history", VK_IMAGE_USAGE_SAMPLED_BIT);
    renderGraph->CarryTextureToNextFrame("velocity_target", "velocity_history", VK_IMAGE_USAGE_SAMPLED_BIT);

    bool bHasPostProcess = pipelineManager->IsCategoryReady(PipelineCategory::PostProcess);
    if (bHasPostProcess) {
        SetupPostProcessing(*renderGraph, viewFamily, renderExtent, frameBuffer.timeFrame.renderDeltaTime);
    } else {
        renderGraph->AliasTexture("post_process_output", "taa_output");
    }


#if WILL_EDITOR
    RenderPass& readbackPass = renderGraph->AddPass("Debug Readback", VK_PIPELINE_STAGE_2_COPY_BIT);
    if (renderGraph->HasBuffer("indirect_buffer") && renderGraph->HasBuffer("indirect_count_buffer") && renderGraph->HasBuffer("luminance_histogram") && renderGraph->HasBuffer("luminance_buffer")) {
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

    if (frameBuffer.bDrawImgui) {
        auto& imguiEditorPass = renderGraph->AddPass("Imgui Draw", VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
        imguiEditorPass.WriteColorAttachment("post_process_output");
        imguiEditorPass.Execute([&](VkCommandBuffer cmd) {
            const VkRenderingAttachmentInfo imguiAttachment = VkHelpers::RenderingAttachmentInfo(renderGraph->GetImageViewHandle("post_process_output"), nullptr,
                                                                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const ResourceDimensions& dims = renderGraph->GetImageDimensions("post_process_output");
            const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, &imguiAttachment, nullptr);
            vkCmdBeginRendering(cmd, &renderInfo);
            ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[currentFrameIndex];
            ImGui_ImplVulkan_RenderDrawData(&imguiSnapshot.DrawData, cmd);

            vkCmdEndRendering(cmd);
        });
    }

    renderGraph->ImportTexture("swapchain_image", currentSwapchainImage, currentSwapchainImageView, TextureInfo{swapchain->format, swapchain->extent.width, swapchain->extent.height, 1},
                               swapchain->usages,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_UNDEFINED);

    auto& blitPass = renderGraph->AddPass("Blit To Swapchain", VK_PIPELINE_STAGE_2_BLIT_BIT);
    blitPass.ReadBlitImage("post_process_output");
    blitPass.WriteBlitImage("swapchain_image");
    blitPass.Execute([&](VkCommandBuffer cmd) {
        VkImage drawImage = renderGraph->GetImageHandle("post_process_output");

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

    pipelineManager->RegisterComputePipeline("instancing_shadows_visibility", Platform::GetShaderPath() / "instancing_shadows_visibility_compute.spv",
                                             sizeof(VisibilityShadowsPushConstant), PipelineCategory::Instancing | PipelineCategory::Shadow);
    shadowMeshShadingInstancedPipeline = ShadowMeshShadingInstancedPipeline(context.get());


    meshShadingInstancedPipeline = MeshShadingInstancedPipeline(context.get(), layouts);

    pipelineManager->RegisterComputePipeline("deferred_resolve", Platform::GetShaderPath() / "deferred_resolve_compute.spv",
                                             sizeof(DeferredResolvePushConstant), PipelineCategory::Geometry);

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

        sceneData.depthLinearizeMult = -sceneData.proj[3][2];
        sceneData.depthLinearizeAdd = sceneData.proj[2][2];
        if (sceneData.depthLinearizeMult * sceneData.depthLinearizeAdd < 0) {
            sceneData.depthLinearizeAdd = -sceneData.depthLinearizeAdd;
        }
        float tanHalfFOVY = 1.0f / sceneData.proj[1][1];
        float tanHalfFOVX = 1.0F / sceneData.proj[0][0];
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
        std::string shadowMapName = "shadow_cascade_" + std::to_string(cascadeLevel);
        std::string shadowPassName = "Shadow Cascade Pass " + std::to_string(cascadeLevel);

        graph.CreateTexture(shadowMapName, TextureInfo{SHADOW_CASCADE_FORMAT, shadowConfig.cascadePreset.extents[cascadeLevel].width, shadowConfig.cascadePreset.extents[cascadeLevel].height, 1});

        if (!bFrozenVisibility) {
            std::string clearPassName = "Clear Shadow Buffers " + std::to_string(cascadeLevel);
            std::string visPassName = "Shadow Visibility " + std::to_string(cascadeLevel);
            std::string prefixPassName = "Shadow Prefix Sum " + std::to_string(cascadeLevel);
            std::string indirectPassName = "Shadow Indirect Construction " + std::to_string(cascadeLevel);


            RenderPass& clearPass = graph.AddPass(clearPassName, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
            clearPass.WriteTransferBuffer("packed_visibility_buffer");
            clearPass.WriteTransferBuffer("primitive_count_buffer");
            clearPass.WriteTransferBuffer("indirect_count_buffer");
            clearPass.Execute([&](VkCommandBuffer cmd) {
                vkCmdFillBuffer(cmd, graph.GetBufferHandle("packed_visibility_buffer"), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle("primitive_count_buffer"), 0, VK_WHOLE_SIZE, 0);
                vkCmdFillBuffer(cmd, graph.GetBufferHandle("indirect_count_buffer"), 0, VK_WHOLE_SIZE, 0);
            });

            RenderPass& visibilityPass = graph.AddPass(visPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            visibilityPass.ReadBuffer("primitive_buffer");
            visibilityPass.ReadBuffer("model_buffer");
            visibilityPass.ReadBuffer("instance_buffer");
            visibilityPass.ReadBuffer("scene_data");
            visibilityPass.ReadBuffer("shadow_data");
            visibilityPass.WriteBuffer("packed_visibility_buffer");
            visibilityPass.WriteBuffer("instance_offset_buffer");
            visibilityPass.WriteBuffer("primitive_count_buffer");
            visibilityPass.Execute([&, cascadeLevel](VkCommandBuffer cmd) {
                VisibilityShadowsPushConstant pushData{
                    .sceneData = graph.GetBufferAddress("scene_data"),
                    .shadowData = graph.GetBufferAddress("shadow_data"),
                    .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                    .modelBuffer = graph.GetBufferAddress("model_buffer"),
                    .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                    .packedVisibilityBuffer = graph.GetBufferAddress("packed_visibility_buffer"),
                    .instanceOffsetBuffer = graph.GetBufferAddress("instance_offset_buffer"),
                    .primitiveCountBuffer = graph.GetBufferAddress("primitive_count_buffer"),
                    .instanceCount = static_cast<uint32_t>(viewFamily.instances.size()),
                    .cascadeLevel = static_cast<uint32_t>(cascadeLevel)
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_shadows_visibility");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityShadowsPushConstant), &pushData);
                uint32_t xDispatch = (viewFamily.instances.size() + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
                vkCmdDispatch(cmd, xDispatch, 1, 1);
            });

            RenderPass& prefixSumPass = graph.AddPass(prefixPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            prefixSumPass.ReadBuffer("primitive_count_buffer");
            prefixSumPass.Execute([&](VkCommandBuffer cmd) {
                PrefixSumPushConstant pushConstant{
                    .primitiveCountBuffer = graph.GetBufferAddress("primitive_count_buffer"),
                    .highestPrimitiveIndex = 200,
                };

                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_prefix_sum");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefixSumPushConstant), &pushConstant);
                vkCmdDispatch(cmd, 1, 1, 1);
            });

            RenderPass& indirectPass = graph.AddPass(indirectPassName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
            indirectPass.ReadBuffer("scene_data");
            indirectPass.ReadBuffer("primitive_buffer");
            indirectPass.ReadBuffer("model_buffer");
            indirectPass.ReadBuffer("instance_buffer");
            indirectPass.ReadBuffer("packed_visibility_buffer");
            indirectPass.ReadBuffer("instance_offset_buffer");
            indirectPass.ReadBuffer("primitive_count_buffer");
            indirectPass.WriteBuffer("compacted_instance_buffer");
            indirectPass.WriteBuffer("indirect_count_buffer");
            indirectPass.WriteBuffer("indirect_buffer");
            indirectPass.Execute([&](VkCommandBuffer cmd) {
                IndirectWritePushConstant pushConstant{
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
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IndirectWritePushConstant), &pushConstant);
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
            ShadowMeshShadingPushConstant pushConstants{
                .sceneData = graph.GetBufferAddress("scene_data"),
                .shadowData = graph.GetBufferAddress("shadow_data"),
                .vertexBuffer = graph.GetBufferAddress("vertex_buffer"),
                .meshletVerticesBuffer = graph.GetBufferAddress("meshlet_vertex_buffer"),
                .meshletTrianglesBuffer = graph.GetBufferAddress("meshlet_triangle_buffer"),
                .meshletBuffer = graph.GetBufferAddress("meshlet_buffer"),
                .indirectBuffer = graph.GetBufferAddress("indirect_buffer"),
                .compactedInstanceBuffer = graph.GetBufferAddress("compacted_instance_buffer"),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .cascadeIndex = static_cast<uint32_t>(cascadeLevel),
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMeshShadingInstancedPipeline.pipeline.handle);
            vkCmdSetDepthBias(cmd, -shadowConfig.cascadePreset.biases[cascadeLevel].linear, 0.0f, -shadowConfig.cascadePreset.biases[cascadeLevel].sloped);
            vkCmdPushConstants(cmd, shadowMeshShadingInstancedPipeline.pipelineLayout.handle, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT,
                               0, sizeof(ShadowMeshShadingPushConstant), &pushConstants);

            vkCmdDrawMeshTasksIndirectCountEXT(cmd,
                                               graph.GetBufferHandle("indirect_buffer"), 0,
                                               graph.GetBufferHandle("indirect_count_buffer"), 0,
                                               MEGA_PRIMITIVE_BUFFER_COUNT,
                                               sizeof(InstancedMeshIndirectDrawParameters));

            vkCmdEndRendering(cmd);
        });
    }
}

void RenderThread::SetupInstancing(RenderGraph& graph, const Core::ViewFamily& viewFamily) const
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

    if (!viewFamily.instances.empty()) {
        RenderPass& visibilityPass = graph.AddPass("Compute Visibility", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        visibilityPass.ReadBuffer("primitive_buffer");
        visibilityPass.ReadBuffer("model_buffer");
        visibilityPass.ReadBuffer("instance_buffer");
        visibilityPass.ReadBuffer("scene_data");
        visibilityPass.WriteBuffer("packed_visibility_buffer");
        visibilityPass.WriteBuffer("instance_offset_buffer");
        visibilityPass.WriteBuffer("primitive_count_buffer");
        visibilityPass.Execute([&](VkCommandBuffer cmd) {
            // todo: profile; a lot of instances, 100k. Try first all of the same primitive. Then try again with a few different primitives (but total remains around the same)
            VisibilityPushConstant visibilityPushData{
                .sceneData = graph.GetBufferAddress("scene_data"),
                .primitiveBuffer = graph.GetBufferAddress("primitive_buffer"),
                .modelBuffer = graph.GetBufferAddress("model_buffer"),
                .instanceBuffer = graph.GetBufferAddress("instance_buffer"),
                .packedVisibilityBuffer = graph.GetBufferAddress("packed_visibility_buffer"),
                .instanceOffsetBuffer = graph.GetBufferAddress("instance_offset_buffer"),
                .primitiveCountBuffer = graph.GetBufferAddress("primitive_count_buffer"),
                .instanceCount = static_cast<uint32_t>(viewFamily.instances.size())
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("instancing_visibility");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VisibilityPushConstant), &visibilityPushData);
            uint32_t xDispatch = (viewFamily.instances.size() + (INSTANCING_VISIBILITY_DISPATCH_X - 1)) / INSTANCING_VISIBILITY_DISPATCH_X;
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
        indirectConstructionPass.ReadBuffer("scene_data");
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
            uint32_t xDispatch = (viewFamily.instances.size() + (INSTANCING_CONSTRUCTION_DISPATCH_X - 1)) / INSTANCING_CONSTRUCTION_DISPATCH_X;
            vkCmdDispatch(cmd, xDispatch, 1, 1);
        });
    }
}

void RenderThread::SetupMainGeometryPass(RenderGraph& graph, const Core::ViewFamily& viewFamily) const
{
    RenderPass& instancedMeshShading = graph.AddPass("Instanced Mesh Shading", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    instancedMeshShading.WriteColorAttachment("albedo_target");
    instancedMeshShading.WriteColorAttachment("normal_target");
    instancedMeshShading.WriteColorAttachment("pbr_target");
    instancedMeshShading.WriteColorAttachment("emissive_target");
    instancedMeshShading.WriteColorAttachment("velocity_target");
    instancedMeshShading.WriteDepthAttachment("depth_target");
    instancedMeshShading.ReadBuffer("compacted_instance_buffer");
    instancedMeshShading.ReadIndirectBuffer("indirect_buffer");
    instancedMeshShading.ReadIndirectCountBuffer("indirect_count_buffer");
    instancedMeshShading.Execute([&](VkCommandBuffer cmd) {
        const ResourceDimensions& dims = graph.GetImageDimensions("albedo_target");
        VkViewport viewport = VkHelpers::GenerateViewport(dims.width, dims.height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(dims.width, dims.height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        const VkRenderingAttachmentInfo albedoAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("albedo_target"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo normalAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("normal_target"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo pbrAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("pbr_target"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo emissiveTarget = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("emissive_target"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo velocityAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("velocity_target"), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        constexpr VkClearValue depthClear = {.depthStencil = {0.0f, 0u}};
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle("depth_target"), &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        const VkRenderingAttachmentInfo colorAttachments[] = {albedoAttachment, normalAttachment, pbrAttachment, emissiveTarget, velocityAttachment};
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({dims.width, dims.height}, colorAttachments, 5, &depthAttachment);

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
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshShadingInstancedPipeline.pipeline.handle);
        vkCmdPushConstants(cmd, meshShadingInstancedPipeline.pipelineLayout.handle, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(InstancedMeshShadingPushConstant), &pushConstants);

        vkCmdDrawMeshTasksIndirectCountEXT(cmd,
                                           graph.GetBufferHandle("indirect_buffer"), 0,
                                           graph.GetBufferHandle("indirect_count_buffer"), 0,
                                           MEGA_PRIMITIVE_BUFFER_COUNT,
                                           sizeof(InstancedMeshIndirectDrawParameters));

        vkCmdEndRendering(cmd);
    });
}

void RenderThread::SetupDeferredLighting(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent) const
{
    graph.CreateTexture("deferred_resolve_target", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
    RenderPass& clearDeferredImagePass = graph.AddPass("Clear Deferred Image", VK_PIPELINE_STAGE_2_CLEAR_BIT);
    clearDeferredImagePass.WriteClearImage("deferred_resolve_target");
    clearDeferredImagePass.Execute([&](VkCommandBuffer cmd) {
        VkImage img = graph.GetImageHandle("deferred_resolve_target");
        constexpr VkClearColorValue clearColor = {0.0f, 0.1f, 0.2f, 1.0f};
        VkImageSubresourceRange colorSubresource = VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
        vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &colorSubresource);
    });

    RenderPass& deferredResolvePass = graph.AddPass("Deferred Resolve", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    deferredResolvePass.ReadSampledImage("albedo_target");
    deferredResolvePass.ReadSampledImage("normal_target");
    deferredResolvePass.ReadSampledImage("pbr_target");
    deferredResolvePass.ReadSampledImage("emissive_target");
    deferredResolvePass.ReadSampledImage("depth_target");
    bool bHasGTAO = graph.HasTexture("gtao_filtered");
    if (bHasGTAO) {
        deferredResolvePass.ReadSampledImage("gtao_filtered");
    }

    bool bHasShadows = graph.HasTexture("shadow_cascade_0");
    if (bHasShadows) {
        deferredResolvePass.ReadSampledImage("shadow_cascade_0");
        deferredResolvePass.ReadSampledImage("shadow_cascade_1");
        deferredResolvePass.ReadSampledImage("shadow_cascade_2");
        deferredResolvePass.ReadSampledImage("shadow_cascade_3");
    }

    deferredResolvePass.WriteStorageImage("deferred_resolve_target");
    deferredResolvePass.Execute([&, bHasShadows, bHasGTAO, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("deferred_resolve");
        glm::ivec4 csmIndices{-1, -1, -1, -1};
        if (bHasShadows) {
            csmIndices.x = graph.GetSampledImageViewDescriptorIndex("shadow_cascade_0");
            csmIndices.y = graph.GetSampledImageViewDescriptorIndex("shadow_cascade_1");
            csmIndices.z = graph.GetSampledImageViewDescriptorIndex("shadow_cascade_2");
            csmIndices.w = graph.GetSampledImageViewDescriptorIndex("shadow_cascade_3");
        }

        int32_t gtaoIndex = bHasGTAO ? graph.GetSampledImageViewDescriptorIndex("gtao_filtered") : -1;

        DeferredResolvePushConstant pushData{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .shadowData = graph.GetBufferAddress("shadow_data"),
            .lightData = graph.GetBufferAddress("light_data"),
            .extent = {width, height},
            .csmIndices = csmIndices,
            .albedoIndex = graph.GetSampledImageViewDescriptorIndex("albedo_target"),
            .normalIndex = graph.GetSampledImageViewDescriptorIndex("normal_target"),
            .pbrIndex = graph.GetSampledImageViewDescriptorIndex("pbr_target"),
            .emissiveIndex = graph.GetSampledImageViewDescriptorIndex("emissive_target"),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex("depth_target"),
            .gtaoFilteredIndex = gtaoIndex,
            .outputImageIndex = graph.GetStorageImageViewDescriptorIndex("deferred_resolve_target"),
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DeferredResolvePushConstant), &pushData);

        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}

void RenderThread::SetupGroundTruthAmbientOcclusion(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent) const
{
    const Core::GTAOConfiguration& gtaoConfig = viewFamily.gtaoConfig;

    graph.CreateTexture("gtao_depth", TextureInfo{VK_FORMAT_R16_SFLOAT, renderExtent[0], renderExtent[1], 5});

    graph.CreateTexture("gtao_ao", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});
    graph.CreateTexture("gtao_edges", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});

    RenderPass& depthPrepass = graph.AddPass("GTAO Depth Prepass", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    depthPrepass.ReadSampledImage("depth_target");
    depthPrepass.WriteStorageImage("gtao_depth");
    depthPrepass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        GTAODepthPrepassPushConstant pc{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .inputDepth = graph.GetSampledImageViewDescriptorIndex("depth_target"),
            .outputDepth0 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 0),
            .outputDepth1 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 1),
            .outputDepth2 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 2),
            .outputDepth3 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 3),
            .outputDepth4 = graph.GetStorageImageViewDescriptorIndex("gtao_depth", 4),
            .effectRadius = gtaoConfig.effectRadius,
            .effectFalloffRange = gtaoConfig.effectFalloffRange,
            .radiusMultiplier = gtaoConfig.radiusMultiplier,
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
    gtaoMainPass.ReadSampledImage("normal_target");
    gtaoMainPass.WriteStorageImage("gtao_ao");
    gtaoMainPass.WriteStorageImage("gtao_edges");
    gtaoMainPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        GTAOMainPushConstant pc{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .prefilteredDepthIndex = graph.GetSampledImageViewDescriptorIndex("gtao_depth"),
            .normalBufferIndex = graph.GetSampledImageViewDescriptorIndex("normal_target"),
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
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_main");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width + GTAO_MAIN_PASS_DISPATCH_X - 1) / GTAO_MAIN_PASS_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_MAIN_PASS_DISPATCH_Y - 1) / GTAO_MAIN_PASS_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Denoise pass(es) - typically run 2-3 times for better quality
    graph.CreateTexture("gtao_filtered", TextureInfo{VK_FORMAT_R8_UNORM, renderExtent[0], renderExtent[1], 1});

    RenderPass& denoisePass = graph.AddPass("GTAO Denoise", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    denoisePass.ReadSampledImage("gtao_ao");
    denoisePass.ReadSampledImage("gtao_edges");
    denoisePass.WriteStorageImage("gtao_filtered");
    denoisePass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        GTAODenoisePushConstant pc{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .rawAOIndex = graph.GetSampledImageViewDescriptorIndex("gtao_ao"),
            .edgeDataIndex = graph.GetSampledImageViewDescriptorIndex("gtao_edges"),
            .filteredAOIndex = graph.GetStorageImageViewDescriptorIndex("gtao_filtered"),
            .denoiseBlurBeta = gtaoConfig.denoiseBlurBeta,
            .isFinalDenoisePass = 1,
        };

        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gtao_denoise");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t xDispatch = (width / 2 + GTAO_DENOISE_DISPATCH_X - 1) / GTAO_DENOISE_DISPATCH_X;
        uint32_t yDispatch = (height + GTAO_DENOISE_DISPATCH_Y - 1) / GTAO_DENOISE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
}

void RenderThread::SetupTemporalAntialiasing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent) const
{
    graph.CreateTexture("taa_current", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    if (!graph.HasTexture("taa_history") || !graph.HasTexture("velocity_history")) {
        renderGraph->AliasTexture("taa_output", "deferred_resolve_target");

        RenderPass& taaPass = graph.AddPass("TAA First Frame", VK_PIPELINE_STAGE_2_COPY_BIT);
        taaPass.ReadCopyImage("deferred_resolve_target");
        taaPass.WriteCopyImage("taa_current");
        taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            VkImage drawImage = graph.GetImageHandle("deferred_resolve_target");
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
        return;
    }

    RenderPass& taaPass = graph.AddPass("TAA Main", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    taaPass.ReadSampledImage("deferred_resolve_target");
    taaPass.ReadSampledImage("depth_target");
    taaPass.ReadSampledImage("taa_history");
    taaPass.ReadSampledImage("velocity_target");
    taaPass.ReadSampledImage("velocity_history");
    taaPass.WriteStorageImage("taa_current");
    taaPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
        TemporalAntialiasingPushConstant pushData{
            .sceneData = graph.GetBufferAddress("scene_data"),
            .colorResolvedIndex = graph.GetSampledImageViewDescriptorIndex("deferred_resolve_target"),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex("depth_target"),
            .colorHistoryIndex = graph.GetSampledImageViewDescriptorIndex("taa_history"),
            .velocityIndex = graph.GetSampledImageViewDescriptorIndex("velocity_target"),
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
}

void RenderThread::SetupPostProcessing(RenderGraph& graph, const Core::ViewFamily& viewFamily, const std::array<uint32_t, 2> renderExtent, float deltaTime) const
{
    const Core::PostProcessConfiguration& ppConfig = viewFamily.postProcessConfig;
    graph.CreateTexture("post_process_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});

    // Exposure
    {
        renderGraph->CreateBuffer("luminance_histogram", POST_PROCESS_LUMINANCE_BUFFER_SIZE);

        if (!graph.HasBuffer("luminance_buffer")) {
            renderGraph->CreateBuffer("luminance_buffer", sizeof(float));
        }
        renderGraph->CarryBufferToNextFrame("luminance_buffer", "luminance_buffer", 0);

        auto& clearPass = graph.AddPass("Clear Histogram", VK_PIPELINE_STAGE_TRANSFER_BIT);
        clearPass.WriteTransferBuffer("luminance_histogram");
        clearPass.Execute([&](VkCommandBuffer cmd) {
            vkCmdFillBuffer(cmd, graph.GetBufferHandle("luminance_histogram"), 0, VK_WHOLE_SIZE, 0);
        });

        auto& histogramPass = graph.AddPass("Build Histogram", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        histogramPass.ReadSampledImage("taa_output");
        histogramPass.WriteBuffer("luminance_histogram");
        histogramPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            constexpr float minLogLuminance = -10.0;
            constexpr float maxLogLuminance = 2.0;
            constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
            constexpr float oneOverLogLuminanceRange = 1.0 / logLuminanceRange;
            HistogramBuildPushConstant pc{
                .hdrImageIndex = graph.GetSampledImageViewDescriptorIndex("taa_output"),
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
        thresholdPass.ReadSampledImage("taa_output");
        thresholdPass.ReadWriteImage("bloom_chain");
        thresholdPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            BloomThresholdPushConstant pc{
                .inputColorIndex = graph.GetSampledImageViewDescriptorIndex("taa_output"),
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
        sharpeningPass.ReadSampledImage("taa_output");
        sharpeningPass.WriteStorageImage("sharpening_output");
        sharpeningPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            SharpeningPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"),
                .inputIndex = graph.GetSampledImageViewDescriptorIndex("taa_output"),
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
        graph.CreateTexture("motion_blur_tiled_max", TextureInfo{GBUFFER_MOTION_FORMAT, blurTiledX, blurTiledY, 1});
        RenderPass& motionBlurTiledMaxPass = graph.AddPass("Motion Blur Tiled Max", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurTiledMaxPass.ReadSampledImage("velocity_target");
        motionBlurTiledMaxPass.WriteStorageImage("motion_blur_tiled_max");
        motionBlurTiledMaxPass.Execute([&, width = renderExtent[0], height = renderExtent[1], blurTiledX, blurTiledY](VkCommandBuffer cmd) {
            MotionBlurTileVelocityPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"),
                .velocityBufferSize = {width, height},
                .tileBufferSize = {blurTiledX, blurTiledY},
                .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex("velocity_target"),
                .depthBufferIndex = graph.GetSampledImageViewDescriptorIndex("depth_target"),
                .tileMaxIndex = graph.GetStorageImageViewDescriptorIndex("motion_blur_tiled_max"),
            };

            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("motion_blur_tile_max");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X;
            uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });


        graph.CreateTexture("motion_blur_tiled_neighbor_max", TextureInfo{GBUFFER_MOTION_FORMAT, blurTiledX, blurTiledY, 1});
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

        graph.CreateTexture("motion_blur_output", TextureInfo{COLOR_ATTACHMENT_FORMAT, renderExtent[0], renderExtent[1], 1});
        RenderPass& motionBlurReconstructionPass = graph.AddPass("Motion Blur Reconstruction", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        motionBlurReconstructionPass.ReadSampledImage("tonemap_output");
        motionBlurReconstructionPass.ReadSampledImage("velocity_target");
        motionBlurReconstructionPass.ReadSampledImage("depth_target");
        motionBlurReconstructionPass.ReadSampledImage("motion_blur_tiled_neighbor_max");
        motionBlurReconstructionPass.WriteStorageImage("motion_blur_output");
        motionBlurReconstructionPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            MotionBlurReconstructionPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"),
                .sceneColorIndex = graph.GetSampledImageViewDescriptorIndex("tonemap_output"),
                .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex("velocity_target"),
                .depthBufferIndex = graph.GetSampledImageViewDescriptorIndex("depth_target"),
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
        colorGradingPass.ReadSampledImage("motion_blur_output");
        colorGradingPass.WriteStorageImage("color_grading_output");
        colorGradingPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            ColorGradingPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"),
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
        vignetteAberrationPass.ReadSampledImage("color_grading_output");
        vignetteAberrationPass.WriteStorageImage("vignette_aberration_output");
        vignetteAberrationPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            VignetteChromaticAberrationPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"),
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
        filmGrainPass.ReadSampledImage("vignette_aberration_output");
        filmGrainPass.WriteStorageImage("post_process_output");
        filmGrainPass.Execute([&, width = renderExtent[0], height = renderExtent[1]](VkCommandBuffer cmd) {
            FilmGrainPushConstant pc{
                .sceneData = graph.GetBufferAddress("scene_data"),
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
}
} // Render
