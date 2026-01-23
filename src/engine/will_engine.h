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
#include "platform/directory_watcher.h"
#include "platform/dll_loader.h"

namespace Physics
{
class PhysicsSystem;
}

namespace Engine
{
class AssetManager;
}

namespace Engine
{
struct GameState;
}

namespace AssetLoad
{
class AssetLoadThread;
}

namespace Core
{
class TimeManager;
class InputManager;
}

namespace Render
{
class AssetGenerator;
class RenderThread;
}

namespace Engine
{
using SDLWindowPtr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;

class WillEngine
{
public:
    WillEngine() = delete;

    explicit WillEngine(Platform::CrashHandler* crashHandler_);

    ~WillEngine();

    void Initialize();

    void Run();

    void PrepareImgui(uint32_t currentFrameBufferIndex);

    void Cleanup();

    [[nodiscard]] Core::FrameBuffer& GetStagingFrameBuffer() { return stagingFrameBuffer; }

private: // Windowing
    SDLWindowPtr window{nullptr, nullptr};
    bool bRequireSwapchainRecreate{false};
    bool bMinimized{false};

private: // Main Systems
    std::unique_ptr<enki::TaskScheduler> scheduler{};
    std::unique_ptr<Render::RenderThread> renderThread{};
    std::unique_ptr<Core::FrameSync> engineRenderSynchronization{};
    std::unique_ptr<AssetLoad::AssetLoadThread> assetLoadThread{};
    std::unique_ptr<AssetManager> assetManager{};
    std::unique_ptr<Physics::PhysicsSystem> physicsSystem{};
#if WILL_EDITOR
    std::unique_ptr<Render::AssetGenerator> modelGenerator{};
    bool isGenerating = false;
    std::string currentAssetName;
    std::string lastCompletedAsset;
    bool lastSuccess = false;
    bool hasCompleted = false;
#endif
    Core::FrameBuffer stagingFrameBuffer{};

private: // Subsystems
    std::unique_ptr<Core::InputManager> inputManager{};
    std::unique_ptr<Core::TimeManager> timeManager{};
    bool bCursorHidden{false};
    uint32_t frameBufferIndex{0};

private: // Game DLL
#ifndef GAME_STATIC
    Platform::DllLoader gameDll{};
    Platform::DirectoryWatcher gameDllWatcher{};
    Platform::DirectoryWatcher shaderWatcher{};
#endif
    Core::GameAPI gameFunctions{};
    std::unique_ptr<Core::EngineContext> engineContext{};
    std::unique_ptr<GameState> gameState{};

private:
    Platform::CrashHandler* crashHandler;

private: // Debugging
#if WILL_EDITOR
    bool bDrawImgui = true;
#else
    bool bDrawImgui = false;
#endif
    bool bFreezeVisibility = false;
    bool bLogRDG = false;
    std::chrono::high_resolution_clock::time_point lastFrameAcquireTime;
    float lastFrameTimeMs = 0.0f;

};
}


#endif //WILLENGINEV3_WILL_ENGINE_H
