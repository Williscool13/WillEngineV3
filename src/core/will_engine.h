//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_WILL_ENGINE_H
#define WILLENGINEV3_WILL_ENGINE_H
#include <memory>

#include <SDL3/SDL.h>
#include <enkiTS/src/TaskScheduler.h>

#include "platform/crash_handler.h"

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
    void Cleanup();


private:
    SDLWindowPtr window{nullptr, nullptr};
    std::unique_ptr<enki::TaskScheduler> scheduler{};

    std::unique_ptr<Render::RenderThread> renderThread{};

private:
    uint64_t frameNumber{0};
    uint32_t currentFrameBufferIndex{0};

private:
    Platform::CrashHandler* crashHandler;
};
}



#endif //WILLENGINEV3_WILL_ENGINE_H