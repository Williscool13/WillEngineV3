//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_WILL_ENGINE_H
#define WILLENGINEV3_WILL_ENGINE_H
#include <memory>

#include <SDL3/SDL.h>
#include <enkiTS/src/TaskScheduler.h>

#include "core/include/frame_sync.h"
#include "core/include/game_interface.h"
#include "core/include/render_interface.h"
#include "platform/crash_handler.h"
#include "platform/dll_loader.h"

namespace AssetLoad
{
class AssetLoadThread;
}

namespace Game
{
struct GameState;
}


namespace Core
{
class TimeManager;
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

    void PrepareFrameBuffer(uint32_t currentFrameBufferIndex, Core::FrameBuffer& frameBuffer, const TimeFrame& time);

#if WILL_EDITOR
    void DrawImgui();

    void PrepareEditor(uint32_t currentFrameBufferIndex);
#endif

    void Cleanup();

    [[nodiscard]] Core::FrameBuffer& GetStagingFrameBuffer() { return stagingFrameBuffer; }

private:
    static WillEngine* instance;

private: // Windowing
    SDLWindowPtr window{nullptr, nullptr};
    bool bRequireSwapchainRecreate{false};
    bool bMinimized{false};

private: // Main Systems
    std::unique_ptr<enki::TaskScheduler> scheduler{};
    std::unique_ptr<Render::RenderThread> renderThread{};
    std::unique_ptr<Core::FrameSync> engineRenderSynchronization{};
    std::unique_ptr<AssetLoad::AssetLoadThread> assetLoadThread{};
    Core::FrameBuffer stagingFrameBuffer{};

private: // Subsystems
    std::unique_ptr<Core::InputManager> inputManager{};
    std::unique_ptr<Core::TimeManager> timeManager{};
    bool bCursorHidden{true};

private:
    uint32_t frameBufferIndex{0};

private: // Game DLL
#ifndef GAME_STATIC
    Platform::DllLoader gameDll;
#endif
    Core::GameAPI gameFunctions{};
    std::unique_ptr<Core::EngineContext> engineContext{};
    void* gameState{};

private:
    Platform::CrashHandler* crashHandler;
};
}


#endif //WILLENGINEV3_WILL_ENGINE_H
