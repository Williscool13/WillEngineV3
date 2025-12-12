//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_RENDER_THREAD_H
#define WILLENGINEV3_RENDER_THREAD_H

#include <array>
#include <memory>
#include <glm/glm.hpp>

#include "frame_resources.h"
#include "vk_operation_ring_buffer.h"
#include "vk_synchronization.h"

namespace enki
{
class LambdaPinnedTask;
class TaskScheduler;
}

struct SDL_Window;

namespace Engine
{
class WillEngine;
}

namespace Render
{
struct ImguiWrapper;
struct RenderExtents;
struct RenderTargets;
struct Swapchain;
struct VulkanContext;
}

namespace Render
{
/**
 * The main render thread
 */
class RenderThread
{
    enum RenderResponse
    {
        SUCCESS,
        SWAPCHAIN_OUTDATED
    };
public:
    RenderThread();

    ~RenderThread();

    void Initialize(Engine::WillEngine* engine_, enki::TaskScheduler* scheduler_, SDL_Window* window_, uint32_t width, uint32_t height);

    void Start();

    void RequestShutdown();

    void Join();

    void ThreadMain();

    RenderResponse Render(uint32_t frameInFlight, FrameSynchronization& frameSync, Core::FrameBuffer& frameBuffer);

private:
    // Non-owning
    SDL_Window* window{};
    Engine::WillEngine* engine{}; // maybe create new engine sync to reduce coupling
    enki::TaskScheduler* scheduler{};

    // Threading
    std::atomic<bool> bShouldExit{false};
    std::unique_ptr<enki::LambdaPinnedTask> pinnedTask{};

    // Owning
    std::unique_ptr<VulkanContext> context{};
    std::unique_ptr<Swapchain> swapchain{};
    std::unique_ptr<ImguiWrapper> imgui{};
    std::unique_ptr<RenderTargets> renderTargets{};
    // std::unique_ptr<ResourceManager> resourceManager{};
    std::unique_ptr<RenderExtents> renderExtents{};

    std::array<FrameSynchronization, 3> frameSynchronization;
    std::array<FrameResources, 3> frameResources;

    ModelMatrixOperationRingBuffer modelMatrixOperationRingBuffer;
    InstanceOperationRingBuffer instanceOperationRingBuffer;
    JointMatrixOperationRingBuffer jointMatrixOperationRingBuffer;
    uint32_t highestInstanceIndex{0};

    uint64_t frameNumber{0};
};
} // Render

#endif //WILLENGINEV3_RENDER_THREAD_H