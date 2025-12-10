//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_WILL_ENGINE_H
#define WILLENGINEV3_WILL_ENGINE_H
#include <memory>

#include <SDL3/SDL.h>

#include "platform/crash_handler.h"

namespace Render
{
struct VulkanContext;
struct Swapchain;
struct RenderTargets;
}

namespace Engine
{
using SDLWindowPtr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;

class WillEngine
{
public:
    WillEngine()  = delete;
    explicit WillEngine(Platform::CrashHandler* crashHandler_);
    ~WillEngine();

    void Initialize();
    void Run();
    void Cleanup();


private:
    SDLWindowPtr window{nullptr, nullptr};
    std::unique_ptr<Render::VulkanContext> context{};
    std::unique_ptr<Render::Swapchain> swapchain;
    std::unique_ptr<Render::RenderTargets> renderTargets;

    uint64_t frameNumber{0};

private:
    Platform::CrashHandler* crashHandler;
};
}



#endif //WILLENGINEV3_WILL_ENGINE_H