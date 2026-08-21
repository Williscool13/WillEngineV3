//
// Created by William on 2026-08-13.
//

#include "gpu_dispatcher.h"

#include <enkiTS/src/TaskScheduler.h>
#include <tracy/Tracy.hpp>

#include "platform/thread_utils.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace Render
{
static VkPipelineStageFlags2 ChannelStageMask(DispatchChannel channel)
{
    switch (channel) {
        case DispatchChannel::Transfer:
            return VK_PIPELINE_STAGE_2_COPY_BIT;
        case DispatchChannel::Compute:
            return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
        case DispatchChannel::Graphics:
            return VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    }
    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

GPUDispatcher::GPUDispatcher(VulkanContext* context, enki::TaskScheduler* scheduler)
    : context(context), scheduler(scheduler)
{
    if (context->transferQueue != context->graphicsQueue) {
        transferWorker.queue = context->transferQueue;
        transferWorker.thread = std::jthread([this] { WorkerThreadMain(transferWorker, "GPUDispatchTransfer"); });
    }
    if (context->computeQueue != VK_NULL_HANDLE && context->computeQueue != context->graphicsQueue) {
        computeWorker.queue = context->computeQueue;
        computeWorker.thread = std::jthread([this] { WorkerThreadMain(computeWorker, "GPUDispatchCompute"); });
    }
}

GPUDispatcher::~GPUDispatcher()
{
    Shutdown();
}

void GPUDispatcher::Enqueue(DispatchChannel channel, VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* submittedSignal, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore)
{
    const GPUDispatchRequest request{cmd, fence, submittedSignal, signalSemaphore, waitSemaphore, ChannelStageMask(channel)};

    WorkerChannel* worker = nullptr;
    if (channel == DispatchChannel::Transfer) {
        worker = &transferWorker;
    }
    else if (channel == DispatchChannel::Compute) {
        worker = &computeWorker;
    }

    if (worker && worker->queue != VK_NULL_HANDLE) {
        worker->requests.enqueue(request);
        worker->workCounter.fetch_add(1);
        worker->wakeCV.notify_one();
    }
    else {
        graphicsRequests.enqueue(request);
    }
}

void GPUDispatcher::DrainGraphics()
{
    GPUDispatchRequest req{};
    while (graphicsRequests.try_dequeue(req)) {
        VkCommandBufferSubmitInfo cmdSubmitInfo = VkHelpers::CommandBufferSubmitInfo(req.cmd);
        VkSemaphoreSubmitInfo waitInfo = VkHelpers::SemaphoreSubmitInfo(req.waitSemaphore, req.stageMask);
        VkSemaphoreSubmitInfo signalInfo = VkHelpers::SemaphoreSubmitInfo(req.signalSemaphore, req.stageMask);
        const VkSemaphoreSubmitInfo* pWaitInfo = req.waitSemaphore != VK_NULL_HANDLE ? &waitInfo : nullptr;
        const VkSemaphoreSubmitInfo* pSignalInfo = req.signalSemaphore != VK_NULL_HANDLE ? &signalInfo : nullptr;
        VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdSubmitInfo, pWaitInfo, pSignalInfo);
        VK_CHECK(vkQueueSubmit2(context->graphicsQueue, 1, &submitInfo, req.fence));

        req.submittedSignal->release();
    }
}

void GPUDispatcher::WorkerThreadMain(WorkerChannel& channel, const char* threadName)
{
    ZoneScoped;
    tracy::SetThreadName(threadName);
    Platform::SetThreadName(threadName);

    scheduler->RegisterExternalTaskThread();

    while (!bShouldExit.load(std::memory_order_acquire)) {
        {
            ZoneScopedN("Dispatch Requests");
            constexpr size_t MAX_BATCH = 16;
            GPUDispatchRequest batch[MAX_BATCH];

            size_t count = channel.requests.try_dequeue_bulk(batch, MAX_BATCH);
            if (count > 0) {
                for (size_t i = 0; i < count; ++i) {
                    VkCommandBufferSubmitInfo cmdSubmitInfo = VkHelpers::CommandBufferSubmitInfo(batch[i].cmd);
                    VkSemaphoreSubmitInfo waitInfo = VkHelpers::SemaphoreSubmitInfo(batch[i].waitSemaphore, batch[i].stageMask);
                    VkSemaphoreSubmitInfo signalInfo = VkHelpers::SemaphoreSubmitInfo(batch[i].signalSemaphore, batch[i].stageMask);
                    const VkSemaphoreSubmitInfo* pWaitInfo = batch[i].waitSemaphore != VK_NULL_HANDLE ? &waitInfo : nullptr;
                    const VkSemaphoreSubmitInfo* pSignalInfo = batch[i].signalSemaphore != VK_NULL_HANDLE ? &signalInfo : nullptr;
                    VkSubmitInfo2 submitInfo = VkHelpers::SubmitInfo(&cmdSubmitInfo, pWaitInfo, pSignalInfo);
                    VK_CHECK(vkQueueSubmit2(channel.queue, 1, &submitInfo, batch[i].fence));
                    batch[i].submittedSignal->release();
                }
            }
        }

        if (channel.workCounter.load(std::memory_order_acquire) > 0) {
            channel.workCounter.fetch_sub(1);
        }
        else {
            ZoneScopedN("Idle - Waiting for Work");
            std::unique_lock lock(channel.wakeMutex);
            channel.wakeCV.wait(lock, [&] {
                return channel.workCounter.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            if (channel.workCounter.load(std::memory_order_acquire) > 0) {
                channel.workCounter.fetch_sub(1);
            }
        }
    }
}

void GPUDispatcher::JoinWorker(WorkerChannel& channel)
{
    if (channel.thread.joinable()) {
        channel.workCounter.fetch_add(1);
        channel.wakeCV.notify_one();
        channel.thread.join();
    }
}

void GPUDispatcher::Shutdown()
{
    bShouldExit.store(true, std::memory_order_release);
    JoinWorker(transferWorker);
    JoinWorker(computeWorker);
}
} // Render
