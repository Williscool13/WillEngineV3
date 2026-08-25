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

#include "core/containers/inline_vector.h"
#include "core/memory/concurrent_queue_traits.h"

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
    std::binary_semaphore* submittedSignal;
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

    /**
     * @param submittedSignal released as soon as the command buffer reaches the queue. The caller owns the fence wait.
     */
    void Enqueue(DispatchChannel channel, VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* submittedSignal, VkSemaphore signalSemaphore = VK_NULL_HANDLE, VkSemaphore waitSemaphore = VK_NULL_HANDLE);

    /**
     * Render thread only.
     */
    void DrainGraphics();

    void Shutdown();

    /**
     * Render Thread only.
     */
    [[nodiscard]] bool IsGraphicsIdle() const { return graphicsRequests.size_approx() == 0; }

private:
    struct WorkerChannel
    {
        void Wake()
        {
            {
                std::lock_guard lock(wakeMutex);
                workCounter.fetch_add(1);
            }
            wakeCV.notify_one();
        }

        Core::ConcurrentQueue<GPUDispatchRequest> requests;
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

    Core::ConcurrentQueue<GPUDispatchRequest> graphicsRequests;
};
} // Render

#endif //WILL_ENGINE_GPU_DISPATCHER_H
