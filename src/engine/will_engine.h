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


public:
    void AcquireRenderFrame() { renderFrames.acquire(); }
    void ReleaseGameFrame() { gameFrames.release(); }
    Core::FrameBuffer& GetFrameBuffer(const uint32_t index) { return frameBuffers[index]; }

private:
    SDLWindowPtr window{nullptr, nullptr};
    std::unique_ptr<enki::TaskScheduler> scheduler{};

    std::unique_ptr<Render::RenderThread> renderThread{};
    std::array<Core::FrameBuffer, Core::FRAME_BUFFER_COUNT> frameBuffers{};
    std::counting_semaphore<Core::FRAME_BUFFER_COUNT> gameFrames{Core::FRAME_BUFFER_COUNT};
    std::counting_semaphore<Core::FRAME_BUFFER_COUNT> renderFrames{0};

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