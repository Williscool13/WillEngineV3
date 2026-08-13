//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_WILL_ENGINE_H
#define WILLENGINEV3_WILL_ENGINE_H
#include <memory>

#include <SDL3/SDL.h>
#include <enkiTS/src/TaskScheduler.h>

#include "engine/include/automation_config.h"
#include "engine/include/frame_sync.h"
#include "engine/include/game_interface.h"
#include "render/interface/render_interface.h"
#include "core/containers/array.h"
#include "core/memory/memory_manager.h"
#include "platform/crash_handler.h"
#include "platform/directory_watcher.h"
#include "platform/dll_loader.h"

namespace Utils
{
class Logger;
}

namespace Editor
{
class AssetGenerator;
}

namespace Audio
{
class AudioManager;
}

namespace Physics
{
class PhysicsSystem;
}

namespace Engine
{
class EngineLogger;
class AssetManager;
}

namespace Engine
{
struct EngineState;
}

namespace AssetLoad
{
class AsyncAssetLoadManager;
}

namespace Core
{
class TimeManager;
class InputManager;
}

namespace Render
{
class GPUDispatcher;
class RenderThread;
}

namespace Engine
{

static constexpr Core::Array<const char*, 64> TASK_THREAD_NAMES{
    "TaskThread0", "TaskThread1", "TaskThread2", "TaskThread3",
    "TaskThread4", "TaskThread5", "TaskThread6", "TaskThread7",
    "TaskThread8", "TaskThread9", "TaskThread10", "TaskThread11",
    "TaskThread12", "TaskThread13", "TaskThread14", "TaskThread15",
    "TaskThread16", "TaskThread17", "TaskThread18", "TaskThread19",
    "TaskThread20", "TaskThread21", "TaskThread22", "TaskThread23",
    "TaskThread24", "TaskThread25", "TaskThread26", "TaskThread27",
    "TaskThread28", "TaskThread29", "TaskThread30", "TaskThread31",
    "TaskThread32", "TaskThread33", "TaskThread34", "TaskThread35",
    "TaskThread36", "TaskThread37", "TaskThread38", "TaskThread39",
    "TaskThread40", "TaskThread41", "TaskThread42", "TaskThread43",
    "TaskThread44", "TaskThread45", "TaskThread46", "TaskThread47",
    "TaskThread48", "TaskThread49", "TaskThread50", "TaskThread51",
    "TaskThread52", "TaskThread53", "TaskThread54", "TaskThread55",
    "TaskThread56", "TaskThread57", "TaskThread58", "TaskThread59",
    "TaskThread60", "TaskThread61", "TaskThread62", "TaskThread63"
};

class WillEngine
{
public:
    WillEngine() = delete;

    explicit WillEngine(Platform::CrashHandler* crashHandler_);

    ~WillEngine();

    void Initialize(Utils::Logger* logger, const AutomationConfig& automation = {});

    void Run();

    void PrepareImgui(ImDrawDataSnapshot* imguiSnapshot);

    void Cleanup();

private:
    void EditorImgui();

private: // Windowing
    SDL_Window* window{};
    bool bRequireSwapchainRecreate{false};
    bool bRequireViewportRecreate{false};
    bool bMinimized{false};

private: // Main Systems
    Core::MemoryManager memoryManager;

#if LOGGING_ENABLED
    EngineLogger* engineLogger{};
#endif
    enki::TaskScheduler* scheduler{};
    Render::RenderThread* renderThread{};
    Render::GPUDispatcher* gpuDispatcher{};
    Core::FrameSync* engineRenderSynchronization{};
    Audio::AudioManager* audioManager{};

    AssetLoad::AsyncAssetLoadManager* asyncAssetLoadManager{};
    AssetManager* assetManager{};
    MaterialManager* materialManager{};
    Physics::PhysicsSystem* physicsSystem{};
#if WILL_EDITOR
    Editor::AssetGenerator* assetGenerator{};
#endif

private: // Subsystems
    Core::InputManager* inputManager{};
    Core::TimeManager* timeManager{};
    bool bCursorHidden{false};

private: // Game DLL
#ifndef GAME_STATIC
    Platform::DllLoader gameDll{};
    Platform::DirectoryWatcher gameDllWatcher{};
#endif
    Platform::DirectoryWatcher shaderWatcher{};
    Core::GameAPI gameFunctions{};
    EngineContext* engineContext{};
    EngineState* engineState{};

private:
    Platform::CrashHandler* crashHandler;

private: // Debugging
#if WILL_EDITOR
    bool bDrawImgui = true;
#else
    bool bDrawImgui = false;
#endif
    bool bLogRDG = false;

    // Cached tag stats, auto-refreshed every second when the Memory panel is open.
    static constexpr size_t kTagCount = static_cast<size_t>(Core::AllocTag::Count);
    Core::Array<Core::TlsfAllocator::TagStats, kTagCount> cachedPersistentTags{};
    Core::Array<Core::TlsfAllocator::TagStats, kTagCount> cachedGeneralTags{};
    Core::Array<Core::TlsfAllocator::TagStats, kTagCount> cachedAssetsScratchTags{};
    Core::Array<Core::TlsfAllocator::TagStats, kTagCount> cachedAssetsTags{};
    Core::Array<Core::TlsfAllocator::TagStats, kTagCount> cachedPhysicsTags{};
    Core::Array<Core::TlsfAllocator::TagStats, kTagCount> cachedRenderTags{};
    Core::Array<Core::TlsfAllocator::TagStats, kTagCount> cachedArenaPoolTags{};
    Core::Array<Core::ArenaSuballocator::LiveArenaStats, Core::ArenaSuballocator::kMaxTracked> cachedLiveArenaStats{};
    size_t cachedLiveArenaCount{0};
    std::chrono::high_resolution_clock::time_point lastFrameAcquireTime;
    float lastFrameTimeMs = 0.0f;

};
}


#endif //WILLENGINEV3_WILL_ENGINE_H
