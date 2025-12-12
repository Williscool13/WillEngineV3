//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_WILL_ENGINE_H
#define WILLENGINEV3_WILL_ENGINE_H
#include <array>
#include <memory>
#include <semaphore>

#include <SDL3/SDL.h>
#include <enkiTS/src/TaskScheduler.h>

#include "core/include/frame_sync.h"
#include "core/include/render_interface.h"
#include "platform/crash_handler.h"
#include "render/render_constants.h"

namespace Render
{
class RenderThread;
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

    void EngineMain();

    void PrepareFrameBuffer(uint32_t currentFrameBufferIndex, Core::FrameBuffer& frameBuffer);

    void DrawImgui();

    void Cleanup();

private:
    SDLWindowPtr window{nullptr, nullptr};
    std::unique_ptr<enki::TaskScheduler> scheduler{};

    std::unique_ptr<Core::FrameSync> engineRenderSynchronization{};
    std::unique_ptr<Render::RenderThread> renderThread{};

private:
    uint64_t gameFrameCount{0};
    uint32_t frameBufferIndex{0};
    float accumDeltaTime{0};
    bool bRequireSwapchainRecreate{false};
    bool bMinimized{false};

private:
    Platform::CrashHandler* crashHandler;

};
}



#endif //WILLENGINEV3_WILL_ENGINE_H