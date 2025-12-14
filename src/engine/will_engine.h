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
#include "core/include/game_interface.h"
#include "core/include/render_interface.h"
#include "platform/crash_handler.h"
#include "platform/dll_loader.h"
#include "render/render_constants.h"

namespace Core
{
class InputManager;
}

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
    static WillEngine& Get()
    {
        return *instance;
    }

    WillEngine() = delete;

    explicit WillEngine(Platform::CrashHandler* crashHandler_);

    ~WillEngine();

    void Initialize();

    void Run();

    void PrepareFrameBuffer(uint32_t currentFrameBufferIndex, Core::FrameBuffer& frameBuffer, float renderDeltaTime);

#if WILL_EDITOR
    void DrawImgui();

    void PrepareEditor(uint32_t currentFrameBufferIndex);
#endif

    void Cleanup();

    [[nodiscard]] Core::FrameBuffer& GetStagingFrameBuffer() { return stagingFrameBuffer; }

private:
    static WillEngine* instance;
    SDLWindowPtr window{nullptr, nullptr};
    std::unique_ptr<enki::TaskScheduler> scheduler{};

    std::unique_ptr<Core::FrameSync> engineRenderSynchronization{};
    Core::FrameBuffer stagingFrameBuffer{};

    std::unique_ptr<Core::InputManager> inputManager{};
    std::unique_ptr<Render::RenderThread> renderThread{};

private:
    uint64_t gameFrameCount{0};
    uint32_t frameBufferIndex{0};
    float accumDeltaTime{0};
    bool bRequireSwapchainRecreate{false};
    bool bMinimized{false};
    bool bCursorActive;

private: // Game DLL
    Platform::DllLoader gameDll;
    Core::GameAPI gameFunctions{};
    std::unique_ptr<Core::EngineContext> engineContext{};
    std::unique_ptr<Core::GameState> gameState{};

private:
    Platform::CrashHandler* crashHandler;
};
}


#endif //WILLENGINEV3_WILL_ENGINE_H
