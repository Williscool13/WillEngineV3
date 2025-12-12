//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

#include <enkiTS/src/TaskScheduler.h>

#include "vk_context.h"
#include "vk_imgui_wrapper.h"
#include "vk_render_extents.h"
#include "vk_render_targets.h"
#include "vk_swapchain.h"
#include "engine/will_engine.h"
#include "spdlog/spdlog.h"


namespace Render
{
RenderThread::RenderThread() = default;

RenderThread::~RenderThread() = default;

void RenderThread::Initialize(Engine::WillEngine* engine_, enki::TaskScheduler* scheduler_, SDL_Window* window_, uint32_t width, uint32_t height)
{
    engine = engine_;
    scheduler = scheduler_;
    window = window_;

    context = std::make_unique<VulkanContext>(window);
    swapchain = std::make_unique<Swapchain>(context.get(), width, height);
    imgui = std::make_unique<ImguiWrapper>(context.get(), window, swapchain->imageCount, swapchain->format);
    renderTargets = std::make_unique<RenderTargets>(context.get(), width, height);
    renderExtents = std::make_unique<RenderExtents>(width, height, 1.0f);

    for (FrameSynchronization& frameSync : frameSynchronization) {
        frameSync = FrameSynchronization(context.get());
        frameSync.Initialize();
    }


    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.usage = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    for (FrameResources& frameResource : frameResources) {
        bufferInfo.size = sizeof(SceneData);
        frameResource.sceneDataBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));

        bufferInfo.size = BINDLESS_INSTANCE_BUFFER_SIZE;
        frameResource.modelBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResource.instanceBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
        bufferInfo.size = BINDLESS_MODEL_BUFFER_SIZE;
        frameResource.jointMatrixBuffer = std::move(AllocatedBuffer::CreateAllocatedBuffer(context.get(), bufferInfo, vmaAllocInfo));
    }
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

void RenderThread::Join()
{
    vkDeviceWaitIdle(context->device);
    if (pinnedTask) {
        scheduler->WaitforTask(pinnedTask.get());
    }
}

void RenderThread::ThreadMain()
{
    while (!bShouldExit.load()) {
        engine->AcquireRenderFrame();

        if (bShouldExit.load()) { break; }

        const uint32_t currentFrameInFlight = frameNumber % Core::FRAME_BUFFER_COUNT;
        Core::FrameBuffer& frameBuffer = engine->GetFrameBuffer(currentFrameInFlight);

        assert(frameBuffer.currentFrameBuffer == currentFrameInFlight);

        modelMatrixOperationRingBuffer.Enqueue(frameBuffer.modelMatrixOperations);
        frameBuffer.modelMatrixOperations.clear();
        instanceOperationRingBuffer.Enqueue(frameBuffer.instanceOperations);
        frameBuffer.instanceOperations.clear();
        jointMatrixOperationRingBuffer.Enqueue(frameBuffer.jointMatrixOperations);
        frameBuffer.jointMatrixOperations.clear();

        if (frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate) {
            SPDLOG_INFO("[RenderThread::ThreadMain] Swapchain Recreated");
            vkDeviceWaitIdle(context->device);

            int32_t w, h;
            SDL_GetWindowSize(window, &w, &h);

            swapchain->Recreate(w, h);
            for (FrameSynchronization& fs : frameSynchronization) {
                fs.RecreateSynchronization();
            }

            // Input::Input::Get().UpdateWindowExtent(swapchain->extent.width, swapchain->extent.height);
            renderExtents->RequestResize(w, h);
        }

        if (renderExtents->HasPendingResize()) {
            vkDeviceWaitIdle(context->device);
            renderExtents->ApplyResize();

            std::array<uint32_t, 2> newExtents = renderExtents->GetExtent();
            renderTargets->Recreate(newExtents[0], newExtents[1]);

            // Upload to descriptor buffer
            // VkDescriptorImageInfo drawDescriptorInfo;
            // drawDescriptorInfo.imageView = renderTargets->drawImageView.handle;
            // drawDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            // renderTargetDescriptors.UpdateDescriptor(drawDescriptorInfo, 0, 0, 0);
        }

        const AllocatedBuffer& currentModelBuffer = frameResources[currentFrameInFlight].modelBuffer;
        const AllocatedBuffer& currentInstanceBuffer = frameResources[currentFrameInFlight].instanceBuffer;
        const AllocatedBuffer& currentJointMatrixBuffers = frameResources[currentFrameInFlight].jointMatrixBuffer;
        FrameSynchronization& currentFrameSynchronization = frameSynchronization[currentFrameInFlight];

        // Wait for the GPU to finish the last frame that used this frame-in-flight's resources (N - imageCount).
        vkWaitForFences(context->device, 1, &currentFrameSynchronization.renderFence, true, UINT64_MAX);

        modelMatrixOperationRingBuffer.ProcessOperations(static_cast<char*>(currentModelBuffer.allocationInfo.pMappedData), Core::FRAME_BUFFER_COUNT + 1);
        instanceOperationRingBuffer.ProcessOperations(static_cast<char*>(currentInstanceBuffer.allocationInfo.pMappedData), Core::FRAME_BUFFER_COUNT, highestInstanceIndex);
        jointMatrixOperationRingBuffer.ProcessOperations(static_cast<char*>(currentJointMatrixBuffers.allocationInfo.pMappedData), Core::FRAME_BUFFER_COUNT + 1);

        if (swapchain->extent.width > 0 && swapchain->extent.height > 0) {
            RenderResponse renderResponse = Render(currentFrameInFlight, currentFrameSynchronization, frameBuffer);
            if (renderResponse == SWAPCHAIN_OUTDATED) {
                frameBuffer.swapchainRecreateCommand.bRenderRequestsRecreate = true;
            }
        }


        frameNumber++;
        engine->ReleaseGameFrame();
    }
}

RenderThread::RenderResponse RenderThread::Render(uint32_t frameInFlight, FrameSynchronization& frameSync, Core::FrameBuffer& frameBuffer)
{
    ImDrawDataSnapshot& currentImguiFrameBuffer = frameBuffer.imguiDataSnapshot;

    return SUCCESS;
}

} // Render
