//
// Created by William on 2025-12-09.
//

#include "will_engine.h"

#include <mutex>

#include <tracy/Tracy.hpp>
#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <entt/entt.hpp>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include "meshoptimizer/src/meshoptimizer.h"

#include "asset_manager.h"
#include "engine_api.h"
#include "engine/include/game_interface.h"
#include "core/input/input_manager.h"
#include "input/input_resolve.h"
#include "input/input_rebinding.h"
#include "input_config.h"
#include "core/time/time_manager.h"
#include "asset-load/async_asset_load_manager.h"
#include "audio/audio_manager.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "logging/engine_assert.h"
#include "logging/engine_logger.h"
#include "physics/physics_system.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "platform/thread_utils.h"
#include "profiles/profile_library.h"
#include "render/render_thread.h"
#include "render/gpu_dispatcher.h"
#include "render/resource_manager.h"
#include "render/pipelines/pipeline_manager.h"

#if WILL_EDITOR
#include "editor/asset-generation/asset_generator.h"
#include "editor/asset-generation/asset_source_catalog.h"
#endif

#if PROFILER_ENABLED
void* operator new(std::size_t count)
{
    auto ptr = malloc(count);
    TracyAlloc(ptr, count);
    return ptr;
}

void* operator new[](std::size_t count)
{
    auto ptr = malloc(count);
    TracyAlloc(ptr, count);
    return ptr;
}

void* operator new(std::size_t count, std::align_val_t align)
{
    auto ptr = _aligned_malloc(count, static_cast<std::size_t>(align));
    TracyAlloc(ptr, count);
    return ptr;
}

void* operator new[](std::size_t count, std::align_val_t align)
{
    auto ptr = _aligned_malloc(count, static_cast<std::size_t>(align));
    TracyAlloc(ptr, count);
    return ptr;
}

void operator delete(void* ptr) noexcept
{
    TracyFree(ptr);
    free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    TracyFree(ptr);
    free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    TracyFree(ptr);
    free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    TracyFree(ptr);
    free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept
{
    TracyFree(ptr);
    _aligned_free(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept
{
    TracyFree(ptr);
    _aligned_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept
{
    TracyFree(ptr);
    _aligned_free(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept
{
    TracyFree(ptr);
    _aligned_free(ptr);
}

#endif
namespace Engine
{
static Core::MemoryManager* gMemory = nullptr;

static void* SdlMalloc(size_t size)
{
    return gMemory->GeneralAllocRaw(size, Core::AllocTag::SDL);
}

static void* SdlCalloc(size_t nmemb, size_t size)
{
    void* ptr = gMemory->GeneralAllocRaw(nmemb * size, Core::AllocTag::SDL);
    memset(ptr, 0, nmemb * size);
    return ptr;
}

static void* SdlRealloc(void* mem, size_t size)
{
    return gMemory->GeneralRealloc(mem, size, Core::AllocTag::SDL);
}

static void SdlFree(void* mem)
{
    gMemory->GeneralFree(mem);
}

static void* MeshoptAlloc(size_t size)
{
    return gMemory->AssetsScratch().Alloc(size, Core::AllocTag::Meshopt);
}

static void MeshoptFree(void* ptr)
{
    gMemory->AssetsScratch().Free(ptr);
}

static void* ImGuiAlloc(size_t size, void* userData)
{
    return static_cast<Core::MemoryManager*>(userData)->GeneralAllocRaw(size, Core::AllocTag::ImGui);
}

static void ImGuiFree(void* ptr, void* userData)
{
    static_cast<Core::MemoryManager*>(userData)->GeneralFree(ptr);
}

WillEngine::WillEngine(Platform::CrashHandler* crashHandler_)
    : crashHandler(crashHandler_)
{}

WillEngine::~WillEngine() = default;

void WillEngine::Initialize(Utils::Logger* logger, const AutomationConfig& automation)
{
    ZoneScoped;

    memoryManager.Init({
        .persistentSize = 48ull * 1024 * 1024, // 64 MB
        .assetsPoolSize = 128ull * 1024 * 1024, // 128 MB
        .physicsPoolSize = 32ull * 1024 * 1024, // 64 MB
        .renderPoolSize = 8ull * 1024 * 1024, // 4 MB
        .arenaPoolSize = 256ull * 1024 * 1024, // 256 MB
        .generalPoolSize = 64ull * 1024 * 1024,
        .generalPoolBudget = 512ull * 1024 * 1024,
        .assetsScratchPoolSize = 128ull * 1024 * 1024,
        .assetsScratchBudget = 4096ull * 1024 * 1024,
    });

#if LOGGING_ENABLED
    engineLogger = new(memoryManager.PersistentAllocRaw(sizeof(EngineLogger), Core::AllocTag::EngineLogger)) EngineLogger(memoryManager);
    engineLogger->Init(logger);
#endif

    tracy::SetThreadName("EngineThread");
    Platform::SetThreadName("EngineThread");

    //
    {
        ZoneScopedN("SchedulerInit");
        enki::TaskSchedulerConfig config;
        config.numTaskThreadsToCreate = glm::min(64u, enki::GetNumHardwareThreads() - 1);
        config.profilerCallbacks.threadStart = [](uint32_t threadNum_) {
            const char* name = TASK_THREAD_NAMES[threadNum_];
            tracy::SetThreadName(name);
            Platform::SetThreadName(name);
        };
        config.profilerCallbacks.waitForNewTaskSuspendStart = [](uint32_t) {};
        config.profilerCallbacks.waitForNewTaskSuspendStop = [](uint32_t) {};
        config.profilerCallbacks.waitForTaskCompleteStart = [](uint32_t) {};
        config.profilerCallbacks.waitForTaskCompleteStop = [](uint32_t) {};
        config.profilerCallbacks.waitForTaskCompleteSuspendStart = [](uint32_t) {};
        config.profilerCallbacks.waitForTaskCompleteSuspendStop = [](uint32_t) {};
        config.numExternalTaskThreads = 8;
        config.customAllocator.userData = &memoryManager;
        config.customAllocator.alloc = [](size_t align_, size_t size_, void* userData_, const char*, int) -> void* {
            return static_cast<Core::MemoryManager*>(userData_)->General().AlignedAlloc(size_, align_, Core::AllocTag::TaskScheduler);
        };
        config.customAllocator.free = [](void* ptr_, size_t, void* userData_, const char*, int) {
            static_cast<Core::MemoryManager*>(userData_)->General().AlignedFree(ptr_);
        };

        SPDLOG_INFO("Scheduler operating with {} threads.", config.numTaskThreadsToCreate + 1);
        scheduler = new(memoryManager.PersistentAllocRaw(sizeof(enki::TaskScheduler), Core::AllocTag::TaskScheduler)) enki::TaskScheduler();
        scheduler->Initialize(config);
    }


    int32_t w;
    int32_t h;

    //
    {
        ZoneScopedN("SDL_Init");
        gMemory = &memoryManager;
        SDL_SetMemoryFunctions(SdlMalloc, SdlCalloc, SdlRealloc, SdlFree);
        meshopt_setAllocator(MeshoptAlloc, MeshoptFree);
        bool sdlInitSuccess = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD);
        if (!sdlInitSuccess) {
            SPDLOG_ERROR("SDL_Init failed: {}", SDL_GetError());
            exit(1);
        }


        SDL_DisplayID targetDisplay = SDL_GetPrimaryDisplay();
        SDL_WindowFlags extraFlags = SDL_WINDOW_MAXIMIZED;
        if (automation.IsCaptureRun()) {
            // 1. Capture runs render unattended
            // 2. Don't yank focus from the user
            // 3. Prefer a secondary monitor
            SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
            SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
            extraFlags = 0;
            int32_t displayCount = 0;
            SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
            if (displays != nullptr) {
                for (int32_t i = 0; i < displayCount; ++i) {
                    if (displays[i] != targetDisplay) {
                        targetDisplay = displays[i];
                        break;
                    }
                }
                SDL_free(displays);
            }
        }
        SDL_Rect rect;
        SDL_GetDisplayUsableBounds(targetDisplay, &rect);

        window = SDL_CreateWindow(
            "Will Engine",
            rect.w, rect.h,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | extraFlags);

        if (automation.IsCaptureRun()) {
            SDL_SetWindowPosition(window, rect.x, rect.y);
        }
        SDL_ShowWindow(window);
        w = rect.w;
        h = rect.h;
    }

    //
    {
        ZoneScopedN("Engine Context");
        engineContext = new(memoryManager.PersistentAllocRaw(sizeof(EngineContext), Core::AllocTag::EngineContext)) EngineContext();
        inputManager = new(memoryManager.PersistentAllocRaw(sizeof(Core::InputManager), Core::AllocTag::InputManager)) Core::InputManager(w, h);
        timeManager = new(memoryManager.PersistentAllocRaw(sizeof(Core::TimeManager), Core::AllocTag::TimeManager)) Core::TimeManager();
    }

    //
    {
        ZoneScopedN("CreateRenderThread");
        ImGui::SetAllocatorFunctions(ImGuiAlloc, ImGuiFree, &memoryManager);
        engineRenderSynchronization = new(memoryManager.PersistentAllocRaw(sizeof(Core::FrameSync), Core::AllocTag::FrameSync)) Core::FrameSync(memoryManager);
        renderThread = new(memoryManager.PersistentAllocRaw(sizeof(Render::RenderThread), Core::AllocTag::RenderThread)) Render::RenderThread(
            memoryManager, engineRenderSynchronization, scheduler, window, w, h);
    }

    //
    {
        ZoneScopedN("CreateAssetLoadThread");
        gpuDispatcher = new(memoryManager.PersistentAllocRaw(sizeof(Render::GPUDispatcher), Core::AllocTag::RenderThread)) Render::GPUDispatcher(
            renderThread->GetVulkanContext(), scheduler);
        asyncAssetLoadManager = new(memoryManager.PersistentAllocRaw(sizeof(AssetLoad::AsyncAssetLoadManager), Core::AllocTag::AsyncAssetLoadManager)) AssetLoad::AsyncAssetLoadManager(
            memoryManager,
            renderThread->GetVulkanContext(),
            renderThread->GetResourceManager(),
            renderThread->GetPipelineManager(),
            renderThread->GetPipelineManager()->GetPipelineCache(),
            gpuDispatcher,
            scheduler);
        renderThread->InitializePipelineManager(asyncAssetLoadManager, gpuDispatcher);
    }

    //
    {
        ZoneScopedN("CreateAudioManager");
        audioManager = new(memoryManager.PersistentAllocRaw(sizeof(Audio::AudioManager), Core::AllocTag::AudioManager)) Audio::AudioManager(asyncAssetLoadManager);
    }


    //
    {
        ZoneScopedN("CreateAssetManager");
        assetManager = new(memoryManager.PersistentAllocRaw(sizeof(AssetManager), Core::AllocTag::AssetManager)) AssetManager(
            memoryManager, engineContext, asyncAssetLoadManager, renderThread->GetResourceManager());
        materialManager = new(memoryManager.PersistentAllocRaw(sizeof(MaterialManager), Core::AllocTag::MaterialManager)) MaterialManager(memoryManager, engineContext, assetManager);
    }

    //
    {
        ZoneScopedN("CreatePhysicsSystem");
        physicsSystem = new(memoryManager.PhysicsAllocRaw(sizeof(Physics::PhysicsSystem), 64)) Physics::PhysicsSystem(memoryManager, scheduler);
    }


#if WILL_EDITOR
    //
    {
        ZoneScopedN("CreateModelGenerator");
        assetGenerator = new(memoryManager.PersistentAllocRaw(sizeof(Editor::AssetGenerator), Core::AllocTag::AssetGenerator)) Editor::AssetGenerator(
            memoryManager, engineContext, renderThread->GetVulkanContext(), renderThread, gpuDispatcher, scheduler);
    }

    // Script-declared texture stub generate on startup with their declared id/name
    {
        ZoneScopedN("GenerateDeclaredTextures");
        for (const auto& pair : assetManager->GetTextureRegistry()) {
            const AssetManager::DiskTextureDesc& desc = pair.value;
            if (!desc.bUngenerated) { continue; }
            auto header = ReadWTextureHeader(desc.source);
            if (!header || header->genSource[0] == '\0') {
                LOG_WARN(Asset, "Ungenerated texture '{}' has no gen_source; skipping", desc.name.c_str());
                continue;
            }
            const Core::Path sourcePath(Core::InlineString<512>::Format("%s/%s", desc.source.Parent().c_str(), header->genSource).c_str());
            if (!sourcePath.Exists()) {
                LOG_WARN(Asset, "Ungenerated texture '{}' source missing: {}", desc.name.c_str(), sourcePath.c_str());
                continue;
            }
            assetGenerator->RequestTextureGenerateFromFile(sourcePath, desc.source, header->bGenMips, static_cast<DXGI_FORMAT>(header->genFormat), header->bGenFlipY);
            LOG_INFO(Asset, "Generating declared texture '{}' from {}", desc.name.c_str(), header->genSource);
        }
    }
#endif

    //
    {
        ZoneScopedN("InitializeEngineStateAndContext");
#if !WILL_EDITOR
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
        bCursorHidden = true;
#endif
        if (bCursorHidden) {
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
            SDL_SetWindowRelativeMouseMode(window, true);
        }
        else {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            SDL_SetWindowRelativeMouseMode(window, false);
        }

        // todo game state

        engineState = new(memoryManager.PersistentAllocRaw(sizeof(EngineState), Core::AllocTag::AssetGenerator)) EngineState(&memoryManager.General());
        engineState->projectConfig = ReadProjectConfig();
        engineState->automation = automation;
        engineState->lighting.aaConfig = engineState->projectConfig.aaConfig;
        if (!engineState->projectConfig.activeLightingProfile.IsEmpty()) {
            Profiles::LightingProfileBundle bundle = Profiles::CaptureLightingProfile(*engineState);
            if (Profiles::LoadLightingProfile(engineState->projectConfig.activeLightingProfile.c_str(), bundle)) {
                Profiles::ApplyLightingProfile(*engineState, bundle);
            }
        }
        if (!engineState->projectConfig.activePostProcessProfile.IsEmpty()) {
            Profiles::LoadPostProcessProfile(engineState->projectConfig.activePostProcessProfile.c_str(), engineState->lighting.postProcess);
        }

#if LOGGING_ENABLED
        engineContext->engineLogger = engineLogger;
#endif
        engineContext->imguiContext = ImGui::GetCurrentContext();
        engineContext->imguiAllocFn = ImGuiAlloc;
        engineContext->imguiFreeFn = ImGuiFree;
        engineContext->imguiAllocUserData = &memoryManager;
        engineContext->clayContext = Clay_GetCurrentContext();
        engineContext->windowContext.windowWidth = w;
        engineContext->windowContext.windowHeight = h;
        engineContext->windowContext.viewportWidth = w;
        engineContext->windowContext.viewportHeight = h;
        engineContext->windowContext.viewportOffsetX = 0;
        engineContext->windowContext.viewportOffsetY = 0;
        engineContext->assetManager = assetManager;
        engineContext->materialManager = materialManager;
        engineContext->pipelineManager = renderThread->GetPipelineManager();
        engineContext->audioManager = audioManager;
        engineContext->physicsSystem = physicsSystem;
        engineContext->scheduler = scheduler;
        engineContext->memoryManager = &memoryManager;
        engineContext->gameplayArena = Core::ManagedArena(memoryManager.ArenaPool(), 512ull * 1024, Core::AllocTag::ECS);
        engineContext->editorArena = Core::ManagedArena(memoryManager.ArenaPool(), 32ull * 1024 * 1024, Core::AllocTag::Editor);
        engineContext->setCursorHiddenFn = [this](bool hidden) {
            if (bCursorHidden == hidden) { return; }
            bCursorHidden = hidden;
            if (bCursorHidden) {
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                SDL_SetWindowRelativeMouseMode(window, true);
                ImGui::SetWindowFocus(nullptr);
            }
            else {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                SDL_SetWindowRelativeMouseMode(window, false);
            }
        };
        engineContext->setTextInputActiveFn = [this](bool active) {
            if (active) {
                SDL_StartTextInput(window);
            }
            else {
                SDL_StopTextInput(window);
            }
        };
#if DEBUG
        engineContext->internStringFn = [](uint64_t hash, const char* str) { DBG_InternString(hash, str); };
        engineContext->resolveStringIdFn = [](uint64_t hash) { return DBG_ResolveStringId(hash); };
        gResolveStringIdFn = [](uint64_t hash) { return DBG_ResolveStringId(hash); };
#endif
        engineContext->addImguiTextureFn = [](uint64_t sampler, uint64_t imageView) -> uint64_t {
            VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
                reinterpret_cast<VkSampler>(sampler),
                reinterpret_cast<VkImageView>(imageView),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            return reinterpret_cast<uint64_t>(ds);
        };
        engineContext->removeImguiTextureFn = [](uint64_t descriptorSet) {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(descriptorSet));
        };
    }
    //
    {
        ZoneScopedN("PrepareGameFunctions");
#ifdef GAME_STATIC
        gameFunctions.gameGetStateSize = &GameGetStateSize;
        gameFunctions.gameStartup = &GameStartup;
        gameFunctions.gameLoad = &GameLoad;
        gameFunctions.gameUpdate = &GameUpdate;
        gameFunctions.gamePrepareFrame = &GamePrepareFrame;
        gameFunctions.gameEndFrame = &GameEndFrame;
        gameFunctions.gameUnload = &GameUnload;
        gameFunctions.gameShutdown = &GameShutdown;
        gameFunctions.gameHotReloadSave = &GameHotReloadSave;
        gameFunctions.gameHotReloadLoad = &GameHotReloadLoad;
#else
        if (gameDll.Load("game.dll", "game_temp.dll")) {
            gameFunctions.gameGetStateSize = gameDll.GetFunction<Core::GameGetStateSizeFunc>("GameGetStateSize");
            gameFunctions.gameStartup = gameDll.GetFunction<Core::GameStartUpFunc>("GameStartup");
            gameFunctions.gameLoad = gameDll.GetFunction<Core::GameLoadFunc>("GameLoad");
            gameFunctions.gameUpdate = gameDll.GetFunction<Core::GameUpdateFunc>("GameUpdate");
            gameFunctions.gamePrepareFrame = gameDll.GetFunction<Core::GamePrepareFrameFunc>("GamePrepareFrame");
            gameFunctions.gameEndFrame = gameDll.GetFunction<Core::GameEndFrameFunc>("GameEndFrame");
            gameFunctions.gameUnload = gameDll.GetFunction<Core::GameUnloadFunc>("GameUnload");
            gameFunctions.gameShutdown = gameDll.GetFunction<Core::GameShutdownFunc>("GameShutdown");
            gameFunctions.gameHotReloadSave = gameDll.GetFunction<Core::GameHotReloadSaveFunc>("GameHotReloadSave");
            gameFunctions.gameHotReloadLoad = gameDll.GetFunction<Core::GameHotReloadLoadFunc>("GameHotReloadLoad");
        }
        else {
            LOG_CRITICAL(Engine, "game.dll failed to load; requesting shutdown");
            gameFunctions.Stub();
            engineState->requests.bRequestedQuit = true;
        }
#endif

        engineContext->gameStateSize = gameFunctions.gameGetStateSize();
        if (engineContext->gameStateSize > 0) {
            engineContext->gameState = memoryManager.PersistentAllocRaw(engineContext->gameStateSize, Core::AllocTag::GameState);
        }

        gameFunctions.gameStartup(engineContext, engineState);
        gameFunctions.gameLoad(engineContext, engineState);
    }

#if WILL_EDITOR
#if !GAME_STATIC
    auto gameDirectory = Platform::GetExecutablePath();
    if (gameDirectory.Exists()) {
        gameDllWatcher.Start(gameDirectory.c_str(), [&]() {
            gameFunctions.gameHotReloadSave(engineContext, engineState);
            engineState->registry = entt::registry{};

            auto reloadResponse = gameDll.Reload();

            engineState->registry.ctx().emplace<EngineContext*>(engineContext);
            engineState->registry.ctx().emplace<EngineState*>(engineState);

            switch (reloadResponse) {
                case Platform::DllLoadResponse::Loaded:
                    SPDLOG_DEBUG("Game lib was hot-reloaded");
                // Fallthrough
                case Platform::DllLoadResponse::NoChanges:
                    gameFunctions.gameGetStateSize = gameDll.GetFunction<Core::GameGetStateSizeFunc>("GameGetStateSize");
                    gameFunctions.gameStartup = gameDll.GetFunction<Core::GameStartUpFunc>("GameStartup");
                    gameFunctions.gameLoad = gameDll.GetFunction<Core::GameLoadFunc>("GameLoad");
                    gameFunctions.gameUpdate = gameDll.GetFunction<Core::GameUpdateFunc>("GameUpdate");
                    gameFunctions.gamePrepareFrame = gameDll.GetFunction<Core::GamePrepareFrameFunc>("GamePrepareFrame");
                    gameFunctions.gameEndFrame = gameDll.GetFunction<Core::GameEndFrameFunc>("GameEndFrame");
                    gameFunctions.gameUnload = gameDll.GetFunction<Core::GameUnloadFunc>("GameUnload");
                    gameFunctions.gameShutdown = gameDll.GetFunction<Core::GameShutdownFunc>("GameShutdown");
                    gameFunctions.gameHotReloadSave = gameDll.GetFunction<Core::GameHotReloadSaveFunc>("GameHotReloadSave");
                    gameFunctions.gameHotReloadLoad = gameDll.GetFunction<Core::GameHotReloadLoadFunc>("GameHotReloadLoad");
                    break;
                case Platform::DllLoadResponse::FailedToLoad:
                    gameFunctions.Stub();
                    SPDLOG_DEBUG("Game lib failed to be hot-reloaded");
                    break;
            }

            if (reloadResponse != Platform::DllLoadResponse::FailedToLoad) {
                const size_t reloadedStateSize = gameFunctions.gameGetStateSize();
                ENGINE_ASSERT(Engine, reloadedStateSize == engineContext->gameStateSize, "GameState size changed across hot reload ({} -> {} bytes) - layout changed, full restart required", engineContext->gameStateSize, reloadedStateSize);
            }

            // Reconnect observers and restore snapshot; skips default scene load.
            gameFunctions.gameHotReloadLoad(engineContext, engineState);
        }, 2.0f, "game.dll");
    }
    else {
        SPDLOG_WARN("Game dll path not found.");
    }
#endif
    auto shaderDirectory = Platform::GetShaderPath();
    if (shaderDirectory.Exists()) {
        shaderWatcher.Start(shaderDirectory.c_str(), [&]() {
            if (Render::PipelineManager* pipelineManager = renderThread->GetPipelineManager()) {
                pipelineManager->RequestReload();
            }
        });
    }
    else {
        SPDLOG_WARN("Shader path not found.");
    }
#endif
}

#if WILL_EDITOR
/** Shared two-level tree renderer for both the VRAM report and the GPU pass-timing report: a row per RenderCategoryGroup with its total, expandable to the RenderCategory leaves rolled into it. */
static void DrawCategoryGroupTree(const char* tableId, const double* leafValues, const double* groupValues, double total, const char* fmt)
{
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(tableId, 3, flags)) { return; }
    ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80.f);
    ImGui::TableSetupColumn("% of Total", ImGuiTableColumnFlags_WidthFixed, 80.f);
    ImGui::TableHeadersRow();

    auto Row = [&](bool bTree, const char* name, double value, bool* pOpen) -> bool {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool bOpen = false;
        if (bTree) {
            bOpen = ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_SpanFullWidth);
        }
        else {
            ImGui::Indent();
            ImGui::TextUnformatted(name);
            ImGui::Unindent();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::Text(fmt, value);
        ImGui::TableSetColumnIndex(2);
        if (total > 0.0) { ImGui::Text("%.1f%%", 100.0 * value / total); }
        else { ImGui::TextUnformatted("-"); }
        if (pOpen) { *pOpen = bOpen; }
        return bOpen;
    };

    for (uint32_t group = 0; group < Render::RENDER_CATEGORY_GROUP_COUNT; ++group) {
        if (groupValues[group] <= 0.0) { continue; }

        // Groups with exactly one contributing leaf render as a flat row (no expand arrow needed).
        uint32_t leafCount = 0;
        for (uint32_t bit = 0; bit < Render::RENDER_CATEGORY_BIT_COUNT; ++bit) {
            if (static_cast<uint32_t>(Render::RENDER_CATEGORY_GROUP_OF[bit]) == group && leafValues[bit] > 0.0) {
                ++leafCount;
            }
        }

        if (leafCount <= 1) {
            Row(false, Render::RENDER_CATEGORY_GROUP_NAMES[group], groupValues[group], nullptr);
            continue;
        }

        bool bOpen = false;
        Row(true, Render::RENDER_CATEGORY_GROUP_NAMES[group], groupValues[group], &bOpen);
        if (bOpen) {
            for (uint32_t bit = 0; bit < Render::RENDER_CATEGORY_BIT_COUNT; ++bit) {
                if (static_cast<uint32_t>(Render::RENDER_CATEGORY_GROUP_OF[bit]) == group && leafValues[bit] > 0.0) {
                    ImGui::Indent();
                    Row(false, Render::RENDER_CATEGORY_NAMES[bit], leafValues[bit], nullptr);
                    ImGui::Unindent();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndTable();
}
#endif

#if WILL_EDITOR
static uint32_t PendingGenerationCount(const Editor::AssetGenerator* gen, bool bActiveOnly)
{
    if (bActiveOnly) {
        return gen->GetActiveModelGenerateCount() + gen->GetActiveTextureGenerateCount() + gen->GetActiveEnvironmentMapGenerateCount() + gen->GetActiveFontGenerateCount();
    }
    return gen->GetTotalModelGenerateCount() + gen->GetTotalTextureGenerateCount() + gen->GetTotalEnvironmentMapGenerateCount() + gen->GetTotalFontGenerateCount();
}
#endif

void WillEngine::EditorImgui()
{
#if WILL_EDITOR
    ImGuiID dockspaceID = ImGui::GetID("My Dockspace");
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    if (ImGui::DockBuilderGetNode(dockspaceID) == nullptr) {
        ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->Size);

        ImGuiID dockMain = dockspaceID;
        ImGuiID dockLeft = 0;
        // ImGuiID dockCenter;
        ImGui::DockBuilderSplitNode(dockspaceID, ImGuiDir_Left, 0.25f, &dockLeft, &dockMain);

        ImGui::DockBuilderDockWindow("Editor", dockLeft);
        ImGui::DockBuilderDockWindow("Log", dockLeft);

        ImGui::DockBuilderFinish(dockspaceID);
    }

    ImGui::DockSpaceOverViewport(dockspaceID, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceID);
    if (centralNode) {
        auto newOffsetX = static_cast<uint32_t>(centralNode->Pos.x);
        auto newOffsetY = static_cast<uint32_t>(centralNode->Pos.y);
        auto newWidth = std::max(2u, static_cast<uint32_t>(centralNode->Size.x) & ~1u);
        auto newHeight = std::max(2u, static_cast<uint32_t>(centralNode->Size.y) & ~1u);

        WindowContext& wc = engineContext->windowContext;
        if (newOffsetX != wc.viewportOffsetX || newOffsetY != wc.viewportOffsetY ||
            newWidth != wc.viewportWidth || newHeight != wc.viewportHeight) {
            wc.viewportOffsetX = newOffsetX;
            wc.viewportOffsetY = newOffsetY;
            wc.viewportWidth = newWidth;
            wc.viewportHeight = newHeight;

            bRequireViewportRecreate = true;
        }
    }

    const uint32_t activeGeneration = PendingGenerationCount(assetGenerator, bForceQuitRequested);
    if (activeGeneration > 0) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + 12.0f}, ImGuiCond_Always, {1.0f, 0.0f});
        ImGui::SetNextWindowBgAlpha(0.65f);
        constexpr ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##GenerationIndicator", nullptr, overlayFlags)) {
            if (bForceQuitRequested) {
                ImGui::Text("Exiting - finishing %u in-flight bakes, new work refused", activeGeneration);
            }
            else if (bQuitPendingGeneration) {
                ImGui::Text("Generating assets (%u remaining) - exiting when finished", activeGeneration);
            }
            else {
                ImGui::Text("Generating assets (%u remaining)", activeGeneration);
            }
        }
        ImGui::End();
    }

    if (ImGui::Begin("Editor")) {
#if !GAME_STATIC
        float gameDllTimeSinceReload = gameDllWatcher.GetTimeSinceLastTrigger();
        int gameDllSeconds = static_cast<int>(gameDllTimeSinceReload);
        if (gameDllSeconds < 60) {
            ImGui::Text("Game DLL: %ds since reload", gameDllSeconds);
        }
        else {
            ImGui::Text("Game DLL: >60s since reload");
        }
#endif

        float shaderTimeSinceReload = shaderWatcher.GetTimeSinceLastTrigger();
        int shaderSeconds = static_cast<int>(shaderTimeSinceReload);
        if (shaderSeconds < 60) {
            ImGui::Text("Shaders: %ds since reload", shaderSeconds);
        }
        else {
            ImGui::Text("Shaders: >60s since reload");
        }

        const WindowContext& wc = engineContext->windowContext;
        ImGui::Text("Viewport: %u x %u", wc.viewportWidth, wc.viewportHeight);
        //
        {
            static bool bMemoryWasOpen = false;
            const bool bMemoryOpen = ImGui::CollapsingHeader("Memory");
            if (bMemoryOpen && !bMemoryWasOpen) {
                cachedLiveArenaCount = memoryManager.ArenaPool().GetLiveArenaStats(cachedLiveArenaStats.Data(), cachedLiveArenaStats.Size());
            }
            bMemoryWasOpen = bMemoryOpen;
            if (bMemoryOpen) {
                static float refreshTimer = 1.0f; // triggers immediately on first open
                static int selectedPool = 0;

                refreshTimer += ImGui::GetIO().DeltaTime;
                if (refreshTimer >= 1.0f) {
                    memoryManager.Persistent().GetTagStats(cachedPersistentTags.Data());
                    memoryManager.General().GetTagStats(cachedGeneralTags.Data());
                    memoryManager.AssetsScratch().GetTagStats(cachedAssetsScratchTags.Data());
                    memoryManager.Assets().GetTagStats(cachedAssetsTags.Data());
                    memoryManager.Physics().GetTagStats(cachedPhysicsTags.Data());
                    memoryManager.Render().GetTagStats(cachedRenderTags.Data());
                    memoryManager.ArenaPool().GetTagStats(cachedArenaPoolTags.Data());
                    refreshTimer = 0.0f;
                }

                const Core::MemoryManager::Stats ms = memoryManager.GetStats();
                constexpr float kToMB = 1.0f / (1024.0f * 1024.0f);
                // Fixed x-offset so all bars align regardless of label length
                constexpr float kLabelX = 125.0f;

                // TLSF bar: green->yellow->red gradient based on fill fraction; overlay shows capacity; tooltip shows details
                auto drawMemBar = [&](const char* label, size_t usedBytes_, size_t totalBytes_, size_t allocCount_) {
                    const float fraction = totalBytes_ > 0 ? static_cast<float>(usedBytes_) / static_cast<float>(totalBytes_) : 0.0f;
                    const float r = fraction < 0.5f ? fraction * 2.0f : 1.0f;
                    const float g = fraction < 0.5f ? 1.0f : (1.0f - (fraction - 0.5f) * 2.0f);
                    const auto overlay = Core::InlineString<48>::Format("%.2f / %.0f MB",
                                                                        static_cast<float>(usedBytes_) * kToMB,
                                                                        static_cast<float>(totalBytes_) * kToMB);

                    ImGui::TextUnformatted(label);
                    ImGui::SameLine(kLabelX);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(r, g, 0.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
                    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay.c_str());
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%.3f / %.0f MB  (%zu allocs)",
                                          static_cast<float>(usedBytes_) * kToMB,
                                          static_cast<float>(totalBytes_) * kToMB,
                                          allocCount_);
                    }
                };

                // Growable pool bar: fill/gradient against the BUDGET (committed shrinks per frame, so used/committed would jump around); overlay shows used / committed / budget
                auto drawGrowableBar = [&](const char* label, const Core::TlsfAllocator::Stats& s) {
                    const float fraction = s.budgetBytes > 0 ? static_cast<float>(s.usedBytes) / static_cast<float>(s.budgetBytes) : 0.0f;
                    const float r = fraction < 0.5f ? fraction * 2.0f : 1.0f;
                    const float g = fraction < 0.5f ? 1.0f : (1.0f - (fraction - 0.5f) * 2.0f);
                    const auto overlay = Core::InlineString<64>::Format("%.2f / %.0f cmt / %.0f MB",
                                                                        static_cast<float>(s.usedBytes) * kToMB,
                                                                        static_cast<float>(s.totalBytes) * kToMB,
                                                                        static_cast<float>(s.budgetBytes) * kToMB);

                    ImGui::TextUnformatted(label);
                    ImGui::SameLine(kLabelX);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(r, g, 0.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
                    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay.c_str());
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("used %.3f MB, committed %.0f MB, budget %.0f MB\npeak %.2f MB  (%zu allocs)",
                                          static_cast<float>(s.usedBytes) * kToMB,
                                          static_cast<float>(s.totalBytes) * kToMB,
                                          static_cast<float>(s.budgetBytes) * kToMB,
                                          static_cast<float>(s.highWaterBytes) * kToMB,
                                          s.allocCount);
                    }
                };

                // Arena bar: bar driven by peak (stable); overlay shows "cur / peak MB"; tooltip shows capacity
                auto drawArenaBar = [&](const char* label, const Core::Arena::Stats& s) {
                    const float peakFraction = s.totalBytes > 0 ? static_cast<float>(s.peakBytes) / static_cast<float>(s.totalBytes) : 0.0f;
                    const auto overlay = Core::InlineString<48>::Format("pk %.2f / %.0f MB",
                                                                        static_cast<float>(s.peakBytes) * kToMB,
                                                                        static_cast<float>(s.totalBytes) * kToMB);

                    ImGui::TextUnformatted(label);
                    ImGui::SameLine(kLabelX);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.45f, 0.8f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
                    ImGui::ProgressBar(peakFraction, ImVec2(-1.0f, 0.0f), overlay.c_str());
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("cur: %.3f MB", static_cast<float>(s.usedBytes) * kToMB);
                    }
                };

                // Grand total across all TLSF pools
                {
                    const size_t tlsfUsed = ms.persistent.usedBytes + ms.general.usedBytes + ms.assetsScratch.usedBytes
                                            + ms.assets.usedBytes + ms.physics.usedBytes + ms.render.usedBytes;
                    const size_t tlsfTotal = ms.persistent.totalBytes + ms.general.totalBytes + ms.assetsScratch.totalBytes
                                             + ms.assets.totalBytes + ms.physics.totalBytes + ms.render.totalBytes;
                    const size_t tlsfAllocs = ms.persistent.allocCount + ms.general.allocCount + ms.assetsScratch.allocCount
                                              + ms.assets.allocCount + ms.physics.allocCount + ms.render.allocCount;
                    ImGui::SeparatorText("TLSF Total");
                    drawMemBar("All Pools", tlsfUsed, tlsfTotal, tlsfAllocs);
                }

                ImGui::SeparatorText("General");
                drawMemBar("General", ms.general.usedBytes, ms.general.totalBytes, ms.general.allocCount);
                drawGrowableBar("Assets Scratch", ms.assetsScratch);
                drawMemBar("Assets", ms.assets.usedBytes, ms.assets.totalBytes, ms.assets.allocCount);

                ImGui::SeparatorText("Physics");
                drawMemBar("Physics", ms.physics.usedBytes, ms.physics.totalBytes, ms.physics.allocCount);

                ImGui::SeparatorText("Render");
                drawMemBar("Render", ms.render.usedBytes, ms.render.totalBytes, ms.render.allocCount);

                ImGui::SeparatorText("Engine");
                drawMemBar("Persistent", ms.persistent.usedBytes, ms.persistent.totalBytes, ms.persistent.allocCount);

                if (engineState->instanceStore.IsInitialized()) {
                    ImGui::SeparatorText("Instance Store");
                    const Core::RangeAllocator::Stats ss = engineState->instanceStore.GetStats();
                    ImGui::Text("used %u  watermark %u / %u", ss.used, ss.watermark, ss.capacity);
                    ImGui::Text("free spans %u  largest run %u", ss.freeSpanCount, ss.largestFreeRun);
                }

                if (engineState->modelStore.IsInitialized()) {
                    ImGui::SeparatorText("Model Store");
                    const Core::RangeAllocator::Stats ms2 = engineState->modelStore.GetStats();
                    ImGui::Text("used %u  watermark %u / %u", ms2.used, ms2.watermark, ms2.capacity);
                    ImGui::Text("free spans %u  largest run %u", ms2.freeSpanCount, ms2.largestFreeRun);
                }

                if (engineState->analyticLightStore.IsInitialized()) {
                    ImGui::SeparatorText("Analytic Light Store");
                    const Core::RangeAllocator::Stats ls = engineState->analyticLightStore.GetStats();
                    ImGui::Text("used %u  watermark %u / %u  pending free %u", ls.used, ls.watermark, ls.capacity, engineState->analyticLightStore.GetPendingFreeCount());
                    ImGui::Text("free spans %u  largest run %u", ls.freeSpanCount, ls.largestFreeRun);
                }

                if (engineState->triLightStore.IsInitialized()) {
                    ImGui::SeparatorText("Tri Light Store");
                    const Core::RangeAllocator::Stats ts = engineState->triLightStore.GetStats();
                    ImGui::Text("used %u  watermark %u / %u  pending free %u", ts.used, ts.watermark, ts.capacity, engineState->triLightStore.GetPendingFreeCount());
                    ImGui::Text("free spans %u  largest run %u", ts.freeSpanCount, ts.largestFreeRun);
                }

                if (engineContext->materialManager) {
                    ImGui::SeparatorText("Materials");
                    ImGui::Text("resident %u / %u  definitions %u / %u",
                                engineContext->materialManager->GetActiveMaterialCount(), Render::BINDLESS_MATERIAL_BUFFER_COUNT,
                                static_cast<uint32_t>(engineContext->materialManager->GetMaterials().Size()), Engine::MAX_LOADED_MATERIALS);
                }

                ImGui::SeparatorText("Arena Pool"); {
                    const Core::ArenaSuballocator::Stats as = memoryManager.ArenaPool().GetStats();
                    const float totalBytes = static_cast<float>(as.totalBytes);

                    constexpr float kBarHeight = 20.0f;
                    constexpr ImU32 kFreeColor = IM_COL32(40, 40, 40, 255);
                    constexpr ImU32 kChunkColors[] = {
                        IM_COL32(70, 130, 180, 255),
                        IM_COL32(180, 100, 60, 255),
                        IM_COL32(80, 160, 80, 255),
                        IM_COL32(160, 80, 160, 255),
                        IM_COL32(180, 160, 50, 255),
                        IM_COL32(60, 160, 160, 255),
                        IM_COL32(160, 60, 80, 255),
                        IM_COL32(100, 100, 200, 255),
                    };
                    constexpr int kNumColors = static_cast<int>(sizeof(kChunkColors) / sizeof(kChunkColors[0]));

                    // Collect active chunks
                    struct ChunkEntry
                    {
                        const char* name;
                        float bytes;
                        ImU32 color;
                    };
                    Core::InlineVector<ChunkEntry, 32> chunks;
                    int colorIdx = 0;
                    for (size_t i = 0; i < kTagCount; ++i) {
                        const auto& ts = cachedArenaPoolTags[i];
                        if (ts.count == 0) { continue; }
                        chunks.PushBack({Core::AllocTagName(ts.tag), static_cast<float>(ts.usedBytes), kChunkColors[colorIdx % kNumColors]});
                        ++colorIdx;
                    }

                    const ImVec2 barSize{ImGui::GetContentRegionAvail().x, kBarHeight};
                    const ImVec2 cursor = ImGui::GetCursorScreenPos();
                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    float x = cursor.x;
                    int hoveredChunk = -1;
                    const ImVec2 mousePos = ImGui::GetIO().MousePos;

                    // Draw chunks
                    for (int i = 0; i < static_cast<int>(chunks.Size()); ++i) {
                        const float w = (chunks[i].bytes / totalBytes) * barSize.x;
                        if (w < 1.0f) { continue; }
                        const ImVec2 p0{x, cursor.y};
                        const ImVec2 p1{x + w, cursor.y + kBarHeight};
                        dl->AddRectFilled(p0, p1, chunks[i].color);
                        dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 80));
                        if (mousePos.x >= p0.x && mousePos.x < p1.x && mousePos.y >= p0.y && mousePos.y < p1.y) {
                            hoveredChunk = i;
                        }
                        // Full name if it fits, otherwise first letter, otherwise nothing
                        if (w >= 10.0f) {
                            const ImVec2 fullSize = ImGui::CalcTextSize(chunks[i].name);
                            const char* label = chunks[i].name;
                            char initial[2] = {chunks[i].name[0], '\0'};
                            ImVec2 textSize = fullSize;
                            if (fullSize.x + 4.0f > w) {
                                label = initial;
                                textSize = ImGui::CalcTextSize(label);
                            }
                            dl->AddText({p0.x + (w - textSize.x) * 0.5f, p0.y + (kBarHeight - textSize.y) * 0.5f}, IM_COL32(255, 255, 255, 220), label);
                        }
                        x += w;
                    }

                    // Free space
                    const float freeW = cursor.x + barSize.x - x;
                    if (freeW > 0.0f) {
                        const ImVec2 p0{x, cursor.y};
                        const ImVec2 p1{x + freeW, cursor.y + kBarHeight};
                        dl->AddRectFilled(p0, p1, kFreeColor);
                        if (mousePos.x >= p0.x && mousePos.x < p1.x && mousePos.y >= p0.y && mousePos.y < p1.y) {
                            hoveredChunk = static_cast<int>(chunks.Size()); // sentinel for free
                        }
                    }

                    ImGui::Dummy(barSize);

                    if (hoveredChunk >= 0 && hoveredChunk < static_cast<int>(chunks.Size())) {
                        // Find matching live stats entry for peak info
                        const char* name = chunks[hoveredChunk].name;
                        const float chunkMB = chunks[hoveredChunk].bytes * kToMB;
                        bool foundLive = false;
                        for (size_t li = 0; li < cachedLiveArenaCount; ++li) {
                            if (Core::AllocTagName(cachedLiveArenaStats[li].tag) == name) {
                                const auto& ls = cachedLiveArenaStats[li].arenaStats;
                                ImGui::SetTooltip("%s\nallocated: %.2f MB\npeak: %.2f MB\ncapacity: %.2f MB",
                                                  name, chunkMB, static_cast<float>(ls.peakBytes) * kToMB, static_cast<float>(ls.totalBytes) * kToMB);
                                foundLive = true;
                                break;
                            }
                        }
                        if (!foundLive) {
                            ImGui::SetTooltip("%s\n%.2f MB", name, chunkMB);
                        }
                    }
                    else if (hoveredChunk == static_cast<int>(chunks.Size())) {
                        ImGui::SetTooltip("Free\n%.2f MB", static_cast<float>(as.freeBytes) * kToMB);
                    }

                    ImGui::Text("%.1f / %.0f MB  (%zu chunks)", static_cast<float>(as.usedBytes) * kToMB, static_cast<float>(as.totalBytes) * kToMB, as.activeChunks);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Refresh##arenaStats")) {
                        cachedLiveArenaCount = memoryManager.ArenaPool().GetLiveArenaStats(cachedLiveArenaStats.Data(), cachedLiveArenaStats.Size());
                    }

                    if (cachedLiveArenaCount > 0) {
                        constexpr ImGuiTableFlags arenaTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
                        if (ImGui::BeginTable("ArenaLiveTable", 4, arenaTableFlags)) {
                            ImGui::TableSetupColumn("Arena");
                            ImGui::TableSetupColumn("Used (KB)");
                            ImGui::TableSetupColumn("Peak (KB)");
                            ImGui::TableSetupColumn("Cap (KB)");
                            ImGui::TableHeadersRow();

                            for (size_t i = 0; i < cachedLiveArenaCount; ++i) {
                                const auto& la = cachedLiveArenaStats[i];
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(Core::AllocTagName(la.tag));
                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("%.1f", static_cast<float>(la.arenaStats.usedBytes) / 1024.0f);
                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%.1f", static_cast<float>(la.arenaStats.peakBytes) / 1024.0f);
                                ImGui::TableSetColumnIndex(3);
                                ImGui::Text("%.1f", static_cast<float>(la.arenaStats.totalBytes) / 1024.0f);
                            }
                            ImGui::EndTable();
                        }
                    }
                }

                ImGui::SeparatorText("GPU");
                ImGui::Text("Device: %zu allocs / %.3f MB",
                            static_cast<size_t>(ms.deviceMemory.allocationCount),
                            static_cast<float>(ms.deviceMemory.totalBytes) * kToMB);

                // Per-pool tag breakdown with pool selector
                ImGui::SeparatorText("Tag Breakdown");

                struct PoolEntry
                {
                    const char* name;
                    const Core::TlsfAllocator::TagStats* tags;
                };
                const PoolEntry pools[] = {
                    {"Persistent", cachedPersistentTags.Data()},
                    {"General", cachedGeneralTags.Data()},
                    {"Assets Scratch", cachedAssetsScratchTags.Data()},
                    {"Assets", cachedAssetsTags.Data()},
                    {"Physics", cachedPhysicsTags.Data()},
                    {"Render", cachedRenderTags.Data()},
                };
                constexpr int kPoolCount = 6;

                for (int i = 0; i < kPoolCount; ++i) {
                    if (i > 0) { ImGui::SameLine(); }
                    const bool sel = (selectedPool == i);
                    if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
                    if (ImGui::SmallButton(pools[i].name)) { selectedPool = i; }
                    if (sel) { ImGui::PopStyleColor(); }
                }

                ImGui::Spacing();
                constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
                if (ImGui::BeginTable("MemTagTable", 3, tableFlags)) {
                    ImGui::TableSetupColumn("Tag");
                    ImGui::TableSetupColumn("Allocs");
                    ImGui::TableSetupColumn("Used (KB)");
                    ImGui::TableHeadersRow();

                    const Core::TlsfAllocator::TagStats* tags = pools[selectedPool].tags;
                    for (size_t i = 0; i < static_cast<size_t>(Core::AllocTag::Count); ++i) {
                        const Core::TlsfAllocator::TagStats& t = tags[i];
                        if (t.count == 0) { continue; }
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(Core::AllocTagName(t.tag));
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%zu", t.count);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.1f", static_cast<float>(t.usedBytes) / 1024.0f);
                    }
                    ImGui::EndTable();
                }

                ImGui::SeparatorText("VRAM Attribution"); {
                    static Render::VRAMReport vramSnapshot{};

                    if (ImGui::SmallButton("Refresh##vram")) {
                        renderThread->RequestVRAMReport();
                    } {
                        Render::VRAMReport fresh = renderThread->GetVRAMReport();
                        if (fresh.physicalTotal > 0) { vramSnapshot = fresh; }
                    }
                    const Render::VRAMReport& vram = vramSnapshot;
                    auto fmtMB = [](VkDeviceSize bytes) -> double { return static_cast<double>(bytes) / (1024.0 * 1024.0); };

                    double logicalLeafMB[Render::RENDER_CATEGORY_BIT_COUNT];
                    double logicalGroupMB[Render::RENDER_CATEGORY_GROUP_COUNT];
                    double physicalLeafMB[Render::RENDER_CATEGORY_BIT_COUNT];
                    double physicalGroupMB[Render::RENDER_CATEGORY_GROUP_COUNT];
                    for (uint32_t i = 0; i < Render::RENDER_CATEGORY_BIT_COUNT; ++i) {
                        logicalLeafMB[i] = fmtMB(vram.logical[i]);
                        physicalLeafMB[i] = fmtMB(vram.physicalExclusive[i]);
                    }
                    for (uint32_t g = 0; g < Render::RENDER_CATEGORY_GROUP_COUNT; ++g) {
                        logicalGroupMB[g] = fmtMB(vram.logicalGroup[g]);
                        physicalGroupMB[g] = fmtMB(vram.physicalExclusiveGroup[g]);
                    }

                    ImGui::Spacing();
                    ImGui::Text("Logical (pre-alias, declared demand) -- Total: %.2f MB", fmtMB(vram.logicalTotal));
                    DrawCategoryGroupTree("##vram_logical_tree", logicalLeafMB, logicalGroupMB, fmtMB(vram.logicalTotal), "%.2f");

                    ImGui::Spacing();
                    ImGui::Text("Physical (post-alias, committed) -- Total: %.2f MB", fmtMB(vram.physicalTotal));
                    DrawCategoryGroupTree("##vram_physical_tree", physicalLeafMB, physicalGroupMB, fmtMB(vram.physicalTotal), "%.2f");
                    if (vram.physicalSharedPoolBytes > 0) {
                        char sharedLabel[272] = "Shared [";
                        int written = static_cast<int>(strlen(sharedLabel));
                        const uint64_t mask = static_cast<uint64_t>(vram.sharedPoolCategories);
                        for (uint32_t i = 0; i < Render::RENDER_CATEGORY_BIT_COUNT; ++i) {
                            if (mask & (1ull << i)) {
                                written += snprintf(sharedLabel + written, sizeof(sharedLabel) - written, written > 8 ? ", %s" : "%s", Render::RENDER_CATEGORY_NAMES[i]);
                            }
                        }
                        snprintf(sharedLabel + written, sizeof(sharedLabel) - written, "]");
                        ImGui::Text("%s: %.2f MB", sharedLabel, fmtMB(vram.physicalSharedPoolBytes));
                    }
                }

                ImGui::SeparatorText("GPU Pass Timing"); {
                    static bool bFreezeGPUProfile = false;
                    static Render::GPUProfileSnapshot frozenGpuProfile{};

                    ImGui::Checkbox("Freeze##gpu_profile", &bFreezeGPUProfile);
                    if (!bFreezeGPUProfile) {
                        frozenGpuProfile = renderThread->GetRendererStatistics().gpuProfile;
                    }
                    const Render::GPUProfileSnapshot& profile = frozenGpuProfile;

                    double leafMs[Render::RENDER_CATEGORY_BIT_COUNT];
                    double groupMs[Render::RENDER_CATEGORY_GROUP_COUNT];
                    for (uint32_t i = 0; i < Render::RENDER_CATEGORY_BIT_COUNT; ++i) { leafMs[i] = profile.leafMs[i]; }
                    for (uint32_t g = 0; g < Render::RENDER_CATEGORY_GROUP_COUNT; ++g) { groupMs[g] = profile.groupMs[g]; }

                    ImGui::Spacing();
                    ImGui::Text("GPU Frame Time -- Span: %.3f ms, Pass Sum: %.3f ms", profile.spanMs, profile.totalMs);
                    DrawCategoryGroupTree("##gpu_profile_tree", leafMs, groupMs, profile.totalMs, "%.3f");
                }
            }
        } // bMemoryOpen + outer block

        if (ImGui::CollapsingHeader("Asset Counts")) {
            ImGui::Text("Models:    %u", assetManager->GetActiveModelCount());
            ImGui::Text("Textures:  %u", assetManager->GetActiveTextureCount());
            ImGui::Text("Samplers:  %u", assetManager->GetActiveSamplerCount());
            ImGui::Text("Cubemaps:  %u", assetManager->GetActiveCubemapCount());
            ImGui::Text("Materials: %u", materialManager->GetActiveMaterialCount());
            bool verboseAsset = assetManager->bVerboseLogging.load(std::memory_order_relaxed);
            if (ImGui::Checkbox("Verbose Asset Logging", &verboseAsset)) {
                assetManager->bVerboseLogging.store(verboseAsset, std::memory_order_relaxed);
            }
            if (Render::PipelineManager* pipelineManager = renderThread->GetPipelineManager()) {
                bool verbosePipeline = pipelineManager->bVerbosePipelineLoading.load(std::memory_order_relaxed);
                if (ImGui::Checkbox("Verbose Pipeline Loading", &verbosePipeline)) {
                    pipelineManager->bVerbosePipelineLoading.store(verbosePipeline, std::memory_order_relaxed);
                }
            }
        }

        if (ImGui::Button("Log RDG")) {
            bLogRDG = true;
        }
        else {
            bLogRDG = false;
        }

        if (ImGui::CollapsingHeader("Renderer Statistics")) {
            const Render::RendererStatistics stats = renderThread->GetRendererStatistics();
            ImGui::Text("Visible Meshlets:            %u", stats.visibleMeshletCount);
            ImGui::Text("Culled Inst Frustum:         %u", stats.culledInstanceFrustum);
            ImGui::Text("Culled Inst Contribution:    %u", stats.culledInstanceContribution);
            ImGui::Text("Culled Inst Occlusion:       %u", stats.culledInstanceOcclusion);
            ImGui::Text("Culled Mlet Frustum:         %u", stats.culledMeshletFrustum);
            ImGui::Text("Culled Mlet Cone:            %u", stats.culledMeshletCone);
            ImGui::Text("Culled Mlet Contribution:    %u", stats.culledMeshletContribution);
            ImGui::Text("Culled Mlet Occlusion:       %u", stats.culledMeshletOcclusion);
            ImGui::Text("Shading Dispatches:          %u", stats.shadingDispatches);
            ImGui::Text("Lighting Dispatches:         %u", stats.lightingDispatches);
            ImGui::Separator();
            ImGui::Text("Mesh Invocations:            %llu", stats.meshInvocations);
            ImGui::Text("Fragment Invocations:        %.2f M", static_cast<double>(stats.fragmentInvocations) / 1'000'000.0);
            ImGui::Text("Compute Invocations:         %.2f M", static_cast<double>(stats.computeInvocations) / 1'000'000.0);
            ImGui::Text("Clipping Invocations:        %llu", stats.clippingInvocations);
            ImGui::Text("Clipping Primitives:         %llu", stats.clippingPrimitives);
        }
        renderThread->GetResourceManager()->debugReadback.Present();
    }

    bool fastMode = assetGenerator->GetFastMode();
    if (ImGui::Checkbox("Fast Mode##assetgen", &fastMode)) {
        assetGenerator->SetFastMode(fastMode);
    }
    if (ImGui::CollapsingHeader("Asset Generation")) {
        static Editor::AssetSourceCatalog* sourceCatalog = nullptr;
        if (sourceCatalog == nullptr) {
            sourceCatalog = new(memoryManager.PersistentAllocRaw(sizeof(Editor::AssetSourceCatalog), Core::AllocTag::AssetGenerator)) Editor::AssetSourceCatalog{};
            sourceCatalog->Scan(memoryManager);
        }

        static bool bSkipModelTextures = false;

        auto requestGenerate = [this](const Editor::AssetSourceEntry& entry) {
            const Core::Path output = Editor::AssetSourceCatalog::OutputPathFor(entry);
            if (output.IsEmpty()) {
                return;
            }
            switch (entry.kind) {
                case Editor::AssetSourceKind::Model:
                {
                    assetGenerator->RequestModelGenerate(entry.sourcePath, output, Editor::AssetSourceCatalog::TextureOutputDirFor(entry), bSkipModelTextures);
                    break;
                }
                case Editor::AssetSourceKind::Texture:
                {
                    bool mips = true;
                    bool flipY = true;
                    DXGI_FORMAT format = DXGI_FORMAT_BC7_UNORM_SRGB;
                    if (auto header = ReadWTextureHeaderAnyVersion(output); header && header->genSource[0] != '\0') {
                        format = static_cast<DXGI_FORMAT>(header->genFormat);
                        mips = header->bGenMips;
                        flipY = header->bGenFlipY;
                    }
                    assetGenerator->RequestTextureGenerateFromFile(entry.sourcePath, output, mips, format, flipY);
                    break;
                }
                case Editor::AssetSourceKind::EnvironmentMap:
                {
                    assetGenerator->RequestEnvironmentMapGenerate(entry.sourcePath, output);
                    break;
                }
                case Editor::AssetSourceKind::Font:
                {
                    assetGenerator->RequestFontGenerate(entry.sourcePath, output);
                    break;
                }
            }
        };

        if (ImGui::Button("Rescan Sources")) {
            sourceCatalog->Scan(memoryManager);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(sourceCatalog->outdatedCount == 0);
        const auto regenLabel = Core::InlineString<64>::Format("Regenerate Outdated (%u)###regen_outdated", sourceCatalog->outdatedCount);
        if (ImGui::Button(regenLabel.c_str())) {
            for (const Editor::AssetSourceEntry& entry : sourceCatalog->entries) {
                if (entry.state != Editor::AssetOutputState::Current && entry.state != Editor::AssetOutputState::Missing && entry.kind != Editor::AssetSourceKind::Font) {
                    requestGenerate(entry);
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Skip Existing Model Textures", &bSkipModelTextures);
        if (sourceCatalog->staleModelTextureCount > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Old-format model textures on disk: %u", sourceCatalog->staleModelTextureCount);
            ImGui::SameLine();
            ImGui::TextDisabled("(regenerate their models; whatever remains is orphaned)");
        }

        const std::string_view assetRootView = Platform::GetAssetPath().View();
        auto drawSourceSection = [&](const char* title, Editor::AssetSourceKind kind, ImGuiTreeNodeFlags flags) {
            uint32_t count = 0;
            for (const Editor::AssetSourceEntry& entry : sourceCatalog->entries) {
                if (entry.kind == kind) {
                    count++;
                }
            }
            const auto header = Core::InlineString<64>::Format("%s (%u)###%s", title, count, title);
            if (!ImGui::TreeNodeEx(header.c_str(), flags)) {
                return;
            }
            for (const Editor::AssetSourceEntry& entry : sourceCatalog->entries) {
                if (entry.kind != kind) {
                    continue;
                }
                ImGui::PushID(&entry);
                if (ImGui::SmallButton("Generate")) {
                    requestGenerate(entry);
                }
                ImGui::SameLine();
                std::string_view label = entry.sourcePath.View();
                if (label.size() > assetRootView.size() && label.substr(0, assetRootView.size()) == assetRootView) {
                    label = label.substr(assetRootView.size() + 1);
                }
                ImGui::TextUnformatted(label.data(), label.data() + label.size());
                if (entry.state == Editor::AssetOutputState::Missing) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("not generated");
                }
                if (entry.state == Editor::AssetOutputState::Outdated || entry.state == Editor::AssetOutputState::OutdatedAndDerivativeContent) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "outdated");
                }
                if (entry.state == Editor::AssetOutputState::DerivativeContentOutdated || entry.state == Editor::AssetOutputState::OutdatedAndDerivativeContent) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "textures outdated");
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        };

        drawSourceSection("Models", Editor::AssetSourceKind::Model, ImGuiTreeNodeFlags_DefaultOpen);
        drawSourceSection("Textures", Editor::AssetSourceKind::Texture, ImGuiTreeNodeFlags_DefaultOpen);
        drawSourceSection("Environment Maps", Editor::AssetSourceKind::EnvironmentMap, ImGuiTreeNodeFlags_DefaultOpen);
        drawSourceSection("Fonts", Editor::AssetSourceKind::Font, ImGuiTreeNodeFlags_None);

        if (ImGui::Button("Generate Derived (BRDF LUT, SMAA, Blue Noise)")) {
            assetGenerator->GenerateBRDFLUT(Platform::GetAssetPath() / "textures/brdf_lut.wtexture");
            assetGenerator->GenerateSMAATextures(Platform::GetAssetPath() / "textures");
            assetGenerator->GenerateBlueNoiseTexture(Platform::GetAssetPath() / "textures/blue_noise.wtexture");
        }

        ImGui::SeparatorText("Skybox LOD:"); {
            static constexpr const char* kLODLabels[] = {"Specular 0", "Specular 1", "Specular 2", "Specular 3", "Diffuse"};
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##skybox_lod", kLODLabels[engineState->lighting.skyboxLOD])) {
                for (int i = 0; i < 5; ++i) {
                    const bool selected = engineState->lighting.skyboxLOD == i;
                    if (ImGui::Selectable(kLODLabels[i], selected)) {
                        engineState->lighting.skyboxLOD = i;
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SeparatorText("Procedural Textures:");
        if (ImGui::Button("Load Yellow Texture")) {
            assetManager->LoadProceduralTexture(SID("yellow_texture"), 256, 256, VK_FORMAT_R8G8B8A8_UNORM, true, Texture::Origin::RuntimeProcedural);
        }
        if (ImGui::Button("Load Domain Warp")) {
            assetManager->LoadProceduralTexture(SID("domain_warp"), 512, 512, VK_FORMAT_R8G8B8A8_UNORM, true, Texture::Origin::RuntimeProcedural);
        }

        ImGui::Separator();
        ImGui::Text("Generation Progress:");
        const auto& genProgresses = assetGenerator->GetModelGenerationProgresses();
        for (uint32_t i = 0; i < genProgresses.Size(); ++i) {
            const auto& genProgress = genProgresses[i];
            const auto genState = genProgress.loadingState.load(std::memory_order_acquire);
            const int32_t genValue = genProgress.value.load(std::memory_order_acquire);

            const char* stateLabel = "None";
            switch (genState) {
                case Editor::StaticModelGenerationProgress::LOADING_GLTF: stateLabel = "Loading GLTF";
                    break;
                case Editor::StaticModelGenerationProgress::WRITING_STATIC_MODEL: stateLabel = "Writing Model";
                    break;
                case Editor::StaticModelGenerationProgress::FAILED: stateLabel = "Failed";
                    break;
                case Editor::StaticModelGenerationProgress::SUCCESS: stateLabel = "Done";
                    break;
                default: break;
            }

            const std::string_view modelName = assetGenerator->GetModelGenerateSlotPath(i).Stem();
            if (modelName.empty()) {
                ImGui::Text("Slot %u: -", i);
            }
            else {
                ImGui::Text("Slot %u: %.*s", i, static_cast<int>(modelName.size()), modelName.data());
            }
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%s (%d%%)", stateLabel, genValue);
            ImGui::ProgressBar(static_cast<float>(genValue) / 100.0f, ImVec2(-1.0f, 0.0f), overlay);
        }

        ImGui::Separator();
        ImGui::Text("Active Generates:");
        ImGui::Text("  Models: %u (%u active)", assetGenerator->GetTotalModelGenerateCount(), assetGenerator->GetActiveModelGenerateCount());
        ImGui::Text("  Textures: %u (%u active)", assetGenerator->GetTotalTextureGenerateCount(), assetGenerator->GetActiveTextureGenerateCount());
        ImGui::Text("  Env Maps: %u (%u active)", assetGenerator->GetTotalEnvironmentMapGenerateCount(), assetGenerator->GetActiveEnvironmentMapGenerateCount());
        ImGui::Text("  Fonts: %u (%u active)", assetGenerator->GetTotalFontGenerateCount(), assetGenerator->GetActiveFontGenerateCount());
        ImGui::Text("Active Loads:");
        ImGui::Text("  Models: %u", asyncAssetLoadManager->GetActiveModelLoadCount());
        ImGui::Text("  Textures: %u", asyncAssetLoadManager->GetActiveTextureLoadCount());
    }
    ImGui::End();
#endif
}

void WillEngine::Run()
{
    renderThread->Start();
    timeManager->Reset();

    const auto startupTime = std::chrono::steady_clock::now();
    auto startupStreamQuietTime = startupTime;
    bool bStartupStreamActivitySeen = false;
    bool bStartupStreamLogged = false;
    uint32_t startupStreamQuietFrames = 0;

    SDL_Event e;
    auto nextFrameTime = std::chrono::steady_clock::now();
    while (true) {
        ZoneScopedN("EngineFrame");

        if (!bStartupStreamLogged) {
            const uint32_t activeLoads = asyncAssetLoadManager->GetActiveModelLoadCount() + asyncAssetLoadManager->GetActiveProceduralModelLoadCount()
                                         + asyncAssetLoadManager->GetActiveTextureLoadCount() + asyncAssetLoadManager->GetActiveCubemapLoadCount()
                                         + asyncAssetLoadManager->GetActiveProceduralTextureLoadCount() + asyncAssetLoadManager->GetActivePhysicsColliderLoadCount()
                                         + asyncAssetLoadManager->GetActivePipelineLoadCount() + asyncAssetLoadManager->GetActiveAudioLoadCount();

            if (activeLoads > 0) {
                bStartupStreamActivitySeen = true;
                startupStreamQuietFrames = 0;
            }
            else if (bStartupStreamActivitySeen) {
                if (startupStreamQuietFrames == 0) { startupStreamQuietTime = std::chrono::steady_clock::now(); }
                startupStreamQuietFrames++;
                if (startupStreamQuietFrames >= 30) {
                    bStartupStreamLogged = true;
                    LOG_INFO(Engine, "Startup asset streaming complete in {:.3f}s", std::chrono::duration<double>(startupStreamQuietTime - startupTime).count());
                }
            }
        }
        if (engineState->projectConfig.bLimitFps && engineState->projectConfig.frameLimitTarget > 0) {
            const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / static_cast<double>(engineState->projectConfig.frameLimitTarget)));
            nextFrameTime += interval;
            const auto now = std::chrono::steady_clock::now();
            if (now < nextFrameTime) {
                std::this_thread::sleep_until(nextFrameTime);
            }
            else {
                nextFrameTime = now;
            }
        }
        else {
            nextFrameTime = std::chrono::steady_clock::now();
        }
        while (SDL_PollEvent(&e) != 0) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            switch (e.type) {
#if WILL_EDITOR
                case SDL_EVENT_QUIT:
                    // Second quit while waiting on generation
                    if (bQuitPendingGeneration && !bForceQuitRequested) {
                        bForceQuitRequested = true;
                        assetGenerator->BeginShutdown();
                        asyncAssetLoadManager->BeginShutdown();
                    }
                    break;
#endif
                case SDL_EVENT_WINDOW_MINIMIZED:
                    bMinimized = true;
                    bRequireSwapchainRecreate = true;
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                    bMinimized = false;
                    bRequireSwapchainRecreate = true;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                    // case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                {
                    bRequireSwapchainRecreate = true;
                    uint32_t w = e.window.data1;
                    uint32_t h = e.window.data2;
                    inputManager->UpdateWindowExtent(w, h);
                    engineContext->windowContext.windowWidth = w;
                    engineContext->windowContext.windowHeight = h;
#ifndef WILL_EDITOR
                    // Viewport always equal window outside of editor
                    bRequireViewportRecreate = true;
                    engineContext->windowContext.viewportWidth = std::max(2u, w & ~1u);
                    engineContext->windowContext.viewportHeight = std::max(2u, h & ~1u);
                    engineContext->windowContext.viewportOffsetX = 0;
                    engineContext->windowContext.viewportOffsetY = 0;
#endif
                }
                break;
                default:
                    break;
            }

            inputManager->ProcessEvent(e);
        }

        if (inputManager->IsQuitRequested() || renderThread->IsShutdownRequestedByRender() || engineState->requests.bRequestedQuit) {
#if WILL_EDITOR
            bQuitPendingGeneration = PendingGenerationCount(assetGenerator, bForceQuitRequested) > 0 && !renderThread->IsShutdownRequestedByRender();
            if (!bQuitPendingGeneration) {
                break;
            }
#else
            break;
#endif
        }

        audioManager->Update();

        inputManager->UpdateFocus(SDL_GetWindowFlags(window));
        timeManager->UpdateGame();


#if !GAME_STATIC
        gameDllWatcher.Poll();
#endif
        shaderWatcher.Poll();

        ResolveLoadResult loadCounts = assetManager->ResolveLoads(*engineRenderSynchronization->GetCurrentFrameBuffer());
        assetManager->KickOffRetires();
        const bool assetsReclaimed = assetManager->ResolveUnloads();

        // Chunks are 256MB mallocs; releasing mid-burst thrashes when a load straddles the baseline, so only release after the loaders have been quiet for a while
        {
            constexpr uint32_t SCRATCH_RELEASE_QUIET_FRAMES = 120;
            const uint32_t activeScratchLoads = asyncAssetLoadManager->GetActiveModelLoadCount() + asyncAssetLoadManager->GetActiveProceduralModelLoadCount()
                                                + asyncAssetLoadManager->GetActiveTextureLoadCount() + asyncAssetLoadManager->GetActiveCubemapLoadCount()
                                                + asyncAssetLoadManager->GetActivePhysicsColliderLoadCount();
            static uint32_t scratchQuietFrames = 0;
            if (activeScratchLoads > 0) {
                scratchQuietFrames = 0;
            }
            else if (scratchQuietFrames <= SCRATCH_RELEASE_QUIET_FRAMES) {
                scratchQuietFrames++;
            }
            if (scratchQuietFrames >= SCRATCH_RELEASE_QUIET_FRAMES) {
                memoryManager.AssetsScratch().ReleaseEmptyChunks();
            }
        }
#if WILL_EDITOR
        bool bTextureGenerated = false;
        //
        {
            engineState->assetLoad.pendingHotReloadModelIds.Clear();
            Editor::ModelGenerateComplete modelComplete{};
            while (assetGenerator->TryDequeueModelGenerateComplete(modelComplete)) {
                engineContext->rescan.bResources = true;
            }
            engineState->assetLoad.pendingHotReloadFontIds.Clear();
            Editor::FontGenerateComplete fontComplete{};
            while (assetGenerator->TryDequeueFontGenerateComplete(fontComplete)) {
                engineContext->rescan.bResources = true;
            }
            engineState->assetLoad.pendingHotReloadTextureIds.Clear();
            Editor::TextureGenerateComplete textureComplete{};
            while (assetGenerator->TryDequeueTextureGenerateComplete(textureComplete)) {
                engineContext->rescan.bResources = true;
                bTextureGenerated = true;
            }
            engineState->assetLoad.pendingHotReloadEnvironmentMapIds.Clear();
            Editor::EnvironmentMapGenerateComplete envMapComplete{};
            while (assetGenerator->TryDequeueCubemapGenerateComplete(envMapComplete)) {
                engineContext->rescan.bResources = true;
            }

            if (engineContext->IsProbeAssemblePending()) {
                Engine::ProbeAssembleStaging& probeReq = engineContext->probeAssemble;
                assetGenerator->RequestProbeAssemble(probeReq.faces, probeReq.captureSize, probeReq.targetResolution, probeReq.outputPath, probeReq.probeId, probeReq.snapshot);
                probeReq.bPending.store(false, std::memory_order_release);
            }
        }
        materialManager->Scan();
        assetManager->Scan();
        if (bTextureGenerated) {
            materialManager->ResolveMissingTextures();
        }

        for (const Engine::ModelID& id : assetManager->GetChangedModelIds()) {
            if (engineState->assetLoad.pendingHotReloadModelIds.IsFull()) { break; }
            engineState->assetLoad.pendingHotReloadModelIds.PushBack(id);
        }
        for (const Engine::FontID& id : assetManager->GetChangedFontIds()) {
            if (engineState->assetLoad.pendingHotReloadFontIds.IsFull()) { break; }
            engineState->assetLoad.pendingHotReloadFontIds.PushBack(id);
        }
        for (const Engine::TextureID& id : assetManager->GetChangedTextureIds()) {
            if (engineState->assetLoad.pendingHotReloadTextureIds.IsFull()) { break; }
            engineState->assetLoad.pendingHotReloadTextureIds.PushBack(id);
        }
        for (const Engine::EnvironmentMapID& id : assetManager->GetChangedEnvironmentMapIds()) {
            if (engineState->assetLoad.pendingHotReloadEnvironmentMapIds.IsFull()) { break; }
            engineState->assetLoad.pendingHotReloadEnvironmentMapIds.PushBack(id);
        }
#endif

        engineContext->bImguiKeyboardCaptured = ImGui::GetIO().WantCaptureKeyboard;
        engineContext->bImguiMouseCaptured = ImGui::GetIO().WantCaptureMouse;
        engineContext->bImGuiWantsTextInput = ImGui::GetIO().WantTextInput;
        engineContext->frameStatus.bAssetsChangedThisFrame = loadCounts.modelLoadedCount > 0 || loadCounts.fontLoadedCount > 0 || assetsReclaimed;
        engineContext->frameStatus.bScreenshotInFlight = renderThread->IsScreenshotInFlight();
#if WILL_EDITOR
        engineContext->frameStatus.bAssetGenerationPending = assetGenerator->GetTotalTextureGenerateCount() + assetGenerator->GetTotalModelGenerateCount() > 0;
        if (!bGenPipelineWakeSent && renderThread->GetPipelineManager()->IsCategoryReady(Render::PipelineCategory::AssetGeneration)) {
            assetGenerator->Wake();
            bGenPipelineWakeSent = true;
        }
#endif

        //
        {
            ZoneScopedN("GameFrame");
            const Core::InputFrame& currentInput = inputManager->GetCurrentInput();
            PollCapture(engineState->input, currentInput);
            ResolveInputActions(currentInput, engineState->inputContext, engineState->input);
            if (engineState->input.bBindingsDirty) {
                SaveInputConfig(engineState->input, engineState->projectConfig);
                engineState->input.bBindingsDirty = false;
            }
            engineState->timeFrame = &timeManager->GetTime();
            gameFunctions.gameUpdate(engineContext, engineState);

            inputManager->FrameReset();

            Core::TimeFrame& rt = engineState->renderTimeFrame;
            const Core::TimeFrame& ct = *engineState->timeFrame;
            rt.deltaTime += ct.deltaTime;
            rt.totalTime = ct.totalTime;
            rt.frameCount = ct.frameCount;
            rt.gameFps = ct.gameFps;
            rt.renderDeltaTime = ct.renderDeltaTime;
            rt.renderTotalTime = ct.renderTotalTime;
            rt.renderFps = ct.renderFps;
        }


        //
        {
            ZoneScopedN("PrepareRenderFrameData");
            const bool bRenderReadyToReceive = engineRenderSynchronization->gameFrames.load(std::memory_order_acquire) > 0;
            if (bRenderReadyToReceive) {
                engineRenderSynchronization->gameFrames.fetch_sub(1, std::memory_order_release);

                //
                {
                    ZoneScopedN("UpdateRender");
                    timeManager->UpdateRender();
                }

                Core::FrameBuffer* frameBufferFramesInFlightAgo = engineRenderSynchronization->GetFrameBufferMinusFIF();
                engineContext->lastKnownStableIdUnderCursor = frameBufferFramesInFlightAgo->stableIdUnderCursor;

                if (renderThread->IsProbeCaptureReady() && !engineContext->probeCapture.bReady.load(std::memory_order_acquire)) {
                    const uint32_t captureSquare = renderThread->GetProbeCaptureSize();
                    const uint16_t* capturePixels = renderThread->GetProbeCapturePixels();
                    if (captureSquare > 0 && capturePixels != nullptr) {
                        const size_t halfCount = static_cast<size_t>(captureSquare) * captureSquare * 4;
                        engineContext->probeCapture.pixels = Core::HeapArray<uint16_t>(&memoryManager.AssetsScratch(), Core::AllocTag::EngineContext, halfCount);
                        memcpy(engineContext->probeCapture.pixels.Data(), capturePixels, halfCount * sizeof(uint16_t));
                        engineContext->probeCapture.captureSize = captureSquare;
                        engineContext->probeCapture.bReady.store(true, std::memory_order_release);
                    }
                    renderThread->ReleaseProbeCapture();
                }

                const Render::RadianceCacheStatistics wcStats = renderThread->GetRendererStatistics().radianceCache;
                engineContext->radianceCacheStats = {wcStats.occupiedSlots, wcStats.cellsCarried, wcStats.cellsEvicted, wcStats.insertsFailed, wcStats.cellsDumped, wcStats.cellsDark, wcStats.cellsShaded};

                Core::FrameBuffer* currentFrameBuffer = engineRenderSynchronization->GetCurrentFrameBuffer();
                ImDrawDataSnapshot* currentImguiSnapshot = engineRenderSynchronization->GetCurrentImguiSnapshot();
                currentFrameBuffer->currentFrameBuffer = engineRenderSynchronization->currentRenderFrame;
                currentFrameBuffer->bLogRDG = bLogRDG;
                currentFrameBuffer->bDrawImgui = bDrawImgui;

                engineRenderSynchronization->renderFrameBuffer[engineRenderSynchronization->currentRenderFrame] = engineRenderSynchronization->currentFrameBufferIndex;

                //
                {
                    ZoneScopedN("SwapchainRecreate");
                    engineRenderSynchronization->GetCurrentFrameBuffer()->swapchainRecreateCommand.bIsMinimized = bMinimized;
                    if (bRequireSwapchainRecreate) {
                        engineRenderSynchronization->GetCurrentFrameBuffer()->swapchainRecreateCommand.bEngineCommandsRecreate = true;

                        int32_t w;
                        int32_t h;
                        SDL_GetWindowSize(window, &w, &h);
                        engineRenderSynchronization->GetCurrentFrameBuffer()->swapchainRecreateCommand.windowWidth = w;
                        engineRenderSynchronization->GetCurrentFrameBuffer()->swapchainRecreateCommand.windowHeight = h;
                        bRequireSwapchainRecreate = false;
                    }
                    else {
                        engineRenderSynchronization->GetCurrentFrameBuffer()->swapchainRecreateCommand.bEngineCommandsRecreate = false;
                    }
                }

                //
                {
                    ZoneScopedN("ImGui");
                    ImGui_ImplVulkan_NewFrame();
                    ImGui_ImplSDL3_NewFrame();
                    ImGui::NewFrame();

                    EditorImgui();
                }

                // Viewport
                {
                    if (bRequireViewportRecreate) {
                        engineRenderSynchronization->GetCurrentFrameBuffer()->viewportResizeCommand = {
                            true,
                            engineContext->windowContext.viewportOffsetX,
                            engineContext->windowContext.viewportOffsetY,
                            engineContext->windowContext.viewportWidth,
                            engineContext->windowContext.viewportHeight,
                        };
                        bRequireViewportRecreate = false;
                    }
                    else {
                        engineRenderSynchronization->GetCurrentFrameBuffer()->viewportResizeCommand.bEngineCommandsResize = false;
                    }
                }


                const Vec2 currentMousePositionAbsolute = inputManager->GetCurrentInput().mousePositionAbsolute;
                glm::uvec2 mousePos = {
                    currentMousePositionAbsolute.x - engineContext->windowContext.viewportOffsetX,
                    currentMousePositionAbsolute.y - engineContext->windowContext.viewportOffsetY
                };
                mousePos.y = engineContext->windowContext.viewportHeight - 1 - mousePos.y;
                engineRenderSynchronization->GetCurrentFrameBuffer()->currentMousePosition = {(mousePos.x), (mousePos.y)};
                //
                {
                    ZoneScopedN("GamePrepareFrame");
                    engineState->timeFrame = &engineState->renderTimeFrame;
                    gameFunctions.gamePrepareFrame(engineContext, engineState, engineRenderSynchronization->GetCurrentFrameBuffer());
                    engineState->renderTimeFrame = {};
                }

                //
                {
                    ZoneScopedN("SwapAndPrepare");
                    // std::swap(currentFrameBuffer, engineRenderSynchronization->stagingFrameBuffer);
                    Core::TimeFrame& handoffTimeFrame = engineRenderSynchronization->GetCurrentFrameBuffer()->timeFrame;
                    handoffTimeFrame = timeManager->GetTime();
                    const Render::RendererStatistics renderStats = renderThread->GetRendererStatistics();
                    handoffTimeFrame.renderWallMs = renderStats.wallFrameMs;
                    handoffTimeFrame.gpuFrameMs = renderStats.gpuSpanMs;
                    PrepareImgui(currentImguiSnapshot);
                }

                engineContext->currentRenderFrame++;
                engineRenderSynchronization->NextFrameBuffer();
                engineRenderSynchronization->NextRenderFrame();
                // Clear the frame buffer to be accumulated until the next render frame
                engineRenderSynchronization->GetCurrentFrameBuffer()->Reinitialize();

                engineRenderSynchronization->renderFrames.fetch_add(1, std::memory_order_release);
                engineRenderSynchronization->renderCV.notify_one();
            }
        }

        gameFunctions.gameEndFrame(engineContext, engineState);
    }
}

void WillEngine::PrepareImgui(ImDrawDataSnapshot* imguiSnapshot)
{
    ZoneScopedN("PreparingImGui")
    ImGui::Render();
    imguiSnapshot->SnapUsingSwap(ImGui::GetDrawData(), ImGui::GetTime());
    static int32_t first = 1;
    if (first > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        first--;
    }
}

void WillEngine::Cleanup()
{
#if WILL_EDITOR
    assetGenerator->Join();
#endif
    asyncAssetLoadManager->Join();
    scheduler->WaitforAll();
    gpuDispatcher->Shutdown();
    renderThread->RequestShutdown();
    renderThread->Join();

    gameFunctions.gameUnload(engineContext, engineState);
    gameFunctions.gameShutdown(engineContext, engineState);
    if (engineContext->gameState) {
        memoryManager.PersistentFree(engineContext->gameState);
        engineContext->gameState = nullptr;
    }
    engineState->~EngineState();
    scheduler->ShutdownNow();
    engineContext->scheduler = nullptr;
    engineContext->~EngineContext();

    inputManager->~InputManager();

#if WILL_EDITOR
    assetGenerator->~AssetGenerator();
#endif

    physicsSystem->~PhysicsSystem();
    materialManager->~MaterialManager();
    assetManager->~AssetManager();

    asyncAssetLoadManager->~AsyncAssetLoadManager();
    gpuDispatcher->~GPUDispatcher();

    audioManager->~AudioManager();

    engineRenderSynchronization->~FrameSync();

    renderThread->~RenderThread();

    scheduler->~TaskScheduler();

    SDL_DestroyWindow(window);
    window = nullptr;
    gMemory = nullptr;

#ifndef GAME_STATIC
    gameDll.Unload();
#endif

#if LOGGING_ENABLED
    engineLogger->Shutdown();
    engineLogger->~EngineLogger();
#endif
}
}
