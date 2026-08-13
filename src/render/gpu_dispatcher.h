//
// Created by William on 2026-08-13.
//

#ifndef WILL_ENGINE_GPU_DISPATCHER_H
#define WILL_ENGINE_GPU_DISPATCHER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <semaphore>
#include <thread>

#include <volk.h>
#include <concurrentqueue/concurrentqueue.h>

#include "core/containers/inline_vector.h"

namespace enki
{
class TaskScheduler;
}

namespace Render
{
struct VulkanContext;

enum class DispatchChannel : uint8_t
{
    Transfer = 0,
    Compute = 1,
    Graphics = 2,
};

struct GPUDispatchRequest
{
    VkCommandBuffer cmd;
    VkFence fence;
    std::binary_semaphore* completionSignal;
    VkSemaphore signalSemaphore;
    VkSemaphore waitSemaphore;
    VkPipelineStageFlags2 stageMask;
};

class GPUDispatcher
{
public:
    GPUDispatcher() = default;

    GPUDispatcher(VulkanContext* context, enki::TaskScheduler* scheduler);

    ~GPUDispatcher();

    GPUDispatcher(const GPUDispatcher&) = delete;

    GPUDispatcher& operator=(const GPUDispatcher&) = delete;

    GPUDispatcher(GPUDispatcher&&) = delete;

    GPUDispatcher& operator=(GPUDispatcher&&) = delete;

    void Enqueue(DispatchChannel channel, VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore signalSemaphore = VK_NULL_HANDLE, VkSemaphore waitSemaphore = VK_NULL_HANDLE);

    /**
     * Render thread only.
     */
    void DrainGraphics();

    void Shutdown();

    /**
     * Render Thread only.
     */
    [[nodiscard]] bool IsGraphicsIdle() const { return pendingGraphics.Size() == 0 && graphicsRequests.size_approx() == 0; }

private:
    struct PendingGraphicsDispatch
    {
        VkFence fence;
        std::binary_semaphore* completionSignal;
    };

    struct WorkerChannel
    {
        moodycamel::ConcurrentQueue<GPUDispatchRequest> requests;
        std::jthread thread;
        std::atomic<uint32_t> workCounter{0};
        std::mutex wakeMutex;
        std::condition_variable wakeCV;
        VkQueue queue{};
    };

    void WorkerThreadMain(WorkerChannel& channel, const char* threadName);

    void JoinWorker(WorkerChannel& channel);

    VulkanContext* context{};
    enki::TaskScheduler* scheduler{};

    std::atomic<bool> bShouldExit{false};
    WorkerChannel transferWorker;
    WorkerChannel computeWorker;

    moodycamel::ConcurrentQueue<GPUDispatchRequest> graphicsRequests;

    // Render Thread only
    Core::InlineVector<PendingGraphicsDispatch, 32> pendingGraphics;
};
} // Render

#endif //WILL_ENGINE_GPU_DISPATCHER_H
