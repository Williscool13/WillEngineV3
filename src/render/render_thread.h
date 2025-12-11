//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_RENDER_THREAD_H
#define WILLENGINEV3_RENDER_THREAD_H

#include <memory>
#include <glm/glm.hpp>

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
struct RenderExtents;
struct RenderTargets;
struct Swapchain;
struct VulkanContext;
}

namespace Render
{
class RenderThread
{
public:
    RenderThread();

    ~RenderThread();

    void Initialize(Engine::WillEngine* engine_, enki::TaskScheduler* scheduler_, SDL_Window* window_, uint32_t width, uint32_t height);

    void Start();

    void RequestShutdown();

    void Join();

    void ThreadMain();

private:
    // Non-owning
    SDL_Window* window{};
    Engine::WillEngine* engine{};
    enki::TaskScheduler* scheduler{};

    // Threading
    std::atomic<bool> bShouldExit{false};
    std::unique_ptr<enki::LambdaPinnedTask> pinnedTask{};

    // Owning
    std::unique_ptr<VulkanContext> context{};
    std::unique_ptr<Swapchain> swapchain{};
    std::unique_ptr<RenderTargets> renderTargets{};
    // std::unique_ptr<ResourceManager> resourceManager{};

    bool bSwapchainOutdated{false};
    std::unique_ptr<RenderExtents> renderExtents{};

    uint64_t frameNumber{0};
    uint32_t currentFrameBufferIndex{0};
};
} // Render

#endif //WILLENGINEV3_RENDER_THREAD_H