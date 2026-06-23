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

#include "asset_manager.h"
#include "engine_api.h"
#include "engine/include/game_interface.h"
#include "core/input/input_manager.h"
#include "core/time/time_manager.h"
#include "asset-load/async_asset_load_manager.h"
#include "audio/audio_manager.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "logging/engine_logger.h"
#include "physics/physics_system.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "platform/thread_utils.h"
#include "profiles/profile_library.h"
#include "render/render_thread.h"
#include "render/resource_manager.h"
#include "render/pipelines/pipeline_manager.h"

#if WILL_EDITOR
#include "editor/asset-generation/asset_generator.h"
#endif

#if PROFILER_ENABLED
void* operator new(std::size_t count)
{
    auto ptr = malloc(count);
    TracyAlloc(ptr, count);
    return ptr;
}

void operator delete(void* ptr) noexcept
{
    TracyFree(ptr);
    free(ptr);
}

#endif
namespace Engine
{
static Core::MemoryManager* gMemory = nullptr;

static void* SdlMalloc(size_t size)
{
    return gMemory->PersistentAllocRaw(size, Core::AllocTag::SDL);
}

static void* SdlCalloc(size_t nmemb, size_t size)
{
    void* ptr = gMemory->PersistentAllocRaw(nmemb * size, Core::AllocTag::SDL);
    memset(ptr, 0, nmemb * size);
    return ptr;
}

static void* SdlRealloc(void* mem, size_t size)
{
    return gMemory->PersistentRealloc(mem, size, Core::AllocTag::SDL);
}

static void SdlFree(void* mem)
{
    gMemory->PersistentFree(mem);
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

void WillEngine::Initialize(Utils::Logger* logger)
{
    ZoneScoped;

    memoryManager.Init({
        .persistentSize = 48ull * 1024 * 1024, // 64 MB
        .generalPoolSize = 32ull * 1024 * 1024, // 16 MB
        .assetsScratchPoolSize = 4096ull * 1024 * 1024,
        .assetsPoolSize = 128ull * 1024 * 1024, // 128 MB
        .physicsAlignedPoolSize = 32ull * 1024 * 1024, // 64 MB
        .renderPoolSize = 4ull * 1024 * 1024, // 4 MB
        .arenaPoolSize = 128ull * 1024 * 1024, // 128 MB
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
        config.customAllocator.alloc = [](size_t, size_t size_, void* userData_, const char*, int) -> void* {
            return static_cast<Core::MemoryManager*>(userData_)->GeneralAllocRaw(size_, Core::AllocTag::TaskScheduler);
        };
        config.customAllocator.free = [](void* ptr_, size_t, void* userData_, const char*, int) {
            static_cast<Core::MemoryManager*>(userData_)->GeneralFree(ptr_);
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
        bool sdlInitSuccess = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        if (!sdlInitSuccess) {
            SPDLOG_ERROR("SDL_Init failed: {}", SDL_GetError());
            exit(1);
        }


        SDL_DisplayID mainDisplay = SDL_GetPrimaryDisplay();
        SDL_Rect rect;
        SDL_GetDisplayUsableBounds(mainDisplay, &rect);

        window = SDL_CreateWindow(
            "Will Engine",
            rect.w, rect.h,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

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
        asyncAssetLoadManager = new(memoryManager.PersistentAllocRaw(sizeof(AssetLoad::AsyncAssetLoadManager), Core::AllocTag::AsyncAssetLoadManager)) AssetLoad::AsyncAssetLoadManager(
            memoryManager,
            renderThread->GetVulkanContext(),
            renderThread->GetResourceManager(),
            renderThread->GetPipelineManager(),
            renderThread->GetPipelineManager()->GetPipelineCache(),
            scheduler);
        renderThread->InitializePipelineManager(asyncAssetLoadManager);
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
        physicsSystem = new(memoryManager.PhysicsAlignedAllocRaw(sizeof(Physics::PhysicsSystem), 64)) Physics::PhysicsSystem(memoryManager, scheduler);
    }


#if WILL_EDITOR
    //
    {
        ZoneScopedN("CreateModelGenerator");
        assetGenerator = new(memoryManager.PersistentAllocRaw(sizeof(Editor::AssetGenerator), Core::AllocTag::AssetGenerator)) Editor::AssetGenerator(
            memoryManager, engineContext, renderThread->GetVulkanContext(), renderThread, asyncAssetLoadManager, scheduler);
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
        engineState->lighting.aaConfig = engineState->projectConfig.aaConfig;
        if (!engineState->projectConfig.activeLightingProfile.IsEmpty()) {
            Profiles::LoadLightingProfile(engineState->projectConfig.activeLightingProfile.c_str(), engineState->lighting.lightingMode, engineState->debug.restir, engineState->lighting.gtaoConfig);
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
        engineContext->editorArena = Core::ManagedArena(memoryManager.ArenaPool(), 1ull * 1024 * 1024, Core::AllocTag::Editor);
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
            gameFunctions.Stub();
        }
#endif

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
                    memoryManager.PhysicsAligned().GetTagStats(cachedPhysicsAlignedTags.Data());
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
                                            + ms.assets.usedBytes + ms.physicsAligned.usedBytes + ms.render.usedBytes;
                    const size_t tlsfTotal = ms.persistent.totalBytes + ms.general.totalBytes + ms.assetsScratch.totalBytes
                                             + ms.assets.totalBytes + ms.physicsAligned.totalBytes + ms.render.totalBytes;
                    const size_t tlsfAllocs = ms.persistent.allocCount + ms.general.allocCount + ms.assetsScratch.allocCount
                                              + ms.assets.allocCount + ms.physicsAligned.allocCount + ms.render.allocCount;
                    ImGui::SeparatorText("TLSF Total");
                    drawMemBar("All Pools", tlsfUsed, tlsfTotal, tlsfAllocs);
                }

                ImGui::SeparatorText("General");
                drawMemBar("General", ms.general.usedBytes, ms.general.totalBytes, ms.general.allocCount);
                drawMemBar("Assets Scratch", ms.assetsScratch.usedBytes, ms.assetsScratch.totalBytes, ms.assetsScratch.allocCount);
                drawMemBar("Assets", ms.assets.usedBytes, ms.assets.totalBytes, ms.assets.allocCount);

                ImGui::SeparatorText("Physics");
                drawMemBar("Phys Aligned", ms.physicsAligned.usedBytes, ms.physicsAligned.totalBytes, ms.physicsAligned.allocCount);

                ImGui::SeparatorText("Render");
                drawMemBar("Render", ms.render.usedBytes, ms.render.totalBytes, ms.render.allocCount);

                ImGui::SeparatorText("Engine");
                drawMemBar("Persistent", ms.persistent.usedBytes, ms.persistent.totalBytes, ms.persistent.allocCount);

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
                    {"Phys Aligned", cachedPhysicsAlignedTags.Data()},
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

                ImGui::SeparatorText("VRAM Attribution");
                {
                    static int vramCategoryFilter = 0;
                    static Render::VRAMReport vramSnapshot{};

                    const char* filterPreview = (vramCategoryFilter == 0) ? "All" : Render::RESOURCE_CATEGORY_NAMES[vramCategoryFilter - 1];
                    if (ImGui::BeginCombo("Category##vram_filter", filterPreview)) {
                        if (ImGui::Selectable("All", vramCategoryFilter == 0)) { vramCategoryFilter = 0; }
                        for (int i = 0; i < static_cast<int>(Render::RESOURCE_CATEGORY_BIT_COUNT); ++i) {
                            if (ImGui::Selectable(Render::RESOURCE_CATEGORY_NAMES[i], vramCategoryFilter == i + 1)) {
                                vramCategoryFilter = i + 1;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Refresh##vram")) {
                        renderThread->RequestVRAMReport();
                    }
                    {
                        Render::VRAMReport fresh = renderThread->GetVRAMReport();
                        if (fresh.physicalTotal > 0) { vramSnapshot = fresh; }
                    }
                    const Render::VRAMReport& vram = vramSnapshot;
                    auto fmtMB = [](VkDeviceSize bytes) -> double { return static_cast<double>(bytes) / (1024.0 * 1024.0); };

                    constexpr ImGuiTableFlags vramTableFlags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

                    ImGui::Spacing();
                    ImGui::TextUnformatted("Logical (pre-alias, declared demand)");
                    if (ImGui::BeginTable("##vram_logical", 3, vramTableFlags)) {
                        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("MB", ImGuiTableColumnFlags_WidthFixed, 70.f);
                        ImGui::TableSetupColumn("% of Total", ImGuiTableColumnFlags_WidthFixed, 80.f);
                        ImGui::TableHeadersRow();

                        auto LogicalRow = [&](const char* name, VkDeviceSize bytes) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
                            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", fmtMB(bytes));
                            ImGui::TableSetColumnIndex(2);
                            if (vram.logicalTotal > 0) { ImGui::Text("%.1f%%", 100.0 * static_cast<double>(bytes) / static_cast<double>(vram.logicalTotal)); }
                            else { ImGui::TextUnformatted("-"); }
                        };

                        if (vramCategoryFilter == 0) {
                            for (uint32_t i = 0; i < Render::RESOURCE_CATEGORY_BIT_COUNT; ++i) {
                                if (vram.logical[i] > 0) { LogicalRow(Render::RESOURCE_CATEGORY_NAMES[i], vram.logical[i]); }
                            }
                            LogicalRow("Total", vram.logicalTotal);
                        } else {
                            const uint32_t idx = static_cast<uint32_t>(vramCategoryFilter - 1);
                            LogicalRow(Render::RESOURCE_CATEGORY_NAMES[idx], vram.logical[idx]);
                        }
                        ImGui::EndTable();
                    }

                    ImGui::Spacing();
                    ImGui::TextUnformatted("Physical (post-alias, committed)");
                    if (ImGui::BeginTable("##vram_physical", 3, vramTableFlags)) {
                        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("MB", ImGuiTableColumnFlags_WidthFixed, 70.f);
                        ImGui::TableSetupColumn("% of Total", ImGuiTableColumnFlags_WidthFixed, 80.f);
                        ImGui::TableHeadersRow();

                        auto PhysRow = [&](const char* name, VkDeviceSize bytes) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
                            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", fmtMB(bytes));
                            ImGui::TableSetColumnIndex(2);
                            if (vram.physicalTotal > 0) { ImGui::Text("%.1f%%", 100.0 * static_cast<double>(bytes) / static_cast<double>(vram.physicalTotal)); }
                            else { ImGui::TextUnformatted("-"); }
                        };

                        if (vramCategoryFilter == 0) {
                            for (uint32_t i = 0; i < Render::RESOURCE_CATEGORY_BIT_COUNT; ++i) {
                                if (vram.physicalExclusive[i] > 0) { PhysRow(Render::RESOURCE_CATEGORY_NAMES[i], vram.physicalExclusive[i]); }
                            }
                            if (vram.physicalSharedPoolBytes > 0) {
                                char sharedLabel[272] = "Shared [";
                                int written = static_cast<int>(strlen(sharedLabel));
                                const uint64_t mask = static_cast<uint64_t>(vram.sharedPoolCategories);
                                for (uint32_t i = 0; i < Render::RESOURCE_CATEGORY_BIT_COUNT; ++i) {
                                    if (mask & (1ull << i)) {
                                        written += snprintf(sharedLabel + written, sizeof(sharedLabel) - written, written > 8 ? ", %s" : "%s", Render::RESOURCE_CATEGORY_NAMES[i]);
                                    }
                                }
                                snprintf(sharedLabel + written, sizeof(sharedLabel) - written, "]");
                                PhysRow(sharedLabel, vram.physicalSharedPoolBytes);
                            }
                            PhysRow("Total", vram.physicalTotal);
                        } else {
                            const uint32_t idx = static_cast<uint32_t>(vramCategoryFilter - 1);
                            PhysRow(Render::RESOURCE_CATEGORY_NAMES[idx], vram.physicalExclusive[idx]);
                        }
                        ImGui::EndTable();
                    }
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

        ImGui::Checkbox("Freeze Visibility Calculations", &bFreezeVisibility);
        if (ImGui::Button("Log RDG")) {
            bLogRDG = true;
        }
        else {
            bLogRDG = false;
        }

        if (ImGui::CollapsingHeader("Renderer Statistics")) {
            const Render::RendererStatistics stats = renderThread->GetRendererStatistics();
            ImGui::Text("Visible Meshlets:            %u", stats.visibleMeshletCount);
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
    if (ImGui::Button("Recreate All Assets (excl. Intel Sponza)")) {
        const Core::Path assets = Platform::GetAssetPath();
        assetGenerator->RequestModelGenerate(assets / "dragon/dragon.gltf", assets / "dragon/dragon.wsmesh");
        assetGenerator->RequestModelGenerate(assets / "BoxTextured.glb", assets / "BoxTextured.wsmesh");
        assetGenerator->RequestModelGenerate(assets / "BoxTextured4k.glb", assets / "BoxTextured4k.wsmesh");
        assetGenerator->RequestModelGenerate(assets / "Sphere.glb", assets / "Sphere.wsmesh");
        assetGenerator->RequestModelGenerate(assets / "sponza2/sponza.gltf", assets / "sponza2/sponza.wsmesh");
        assetGenerator->RequestModelGenerate(assets / "Plane.glb", assets / "Plane.wsmesh");
        assetGenerator->RequestModelGenerate(assets / "LightPanel.glb", assets / "LightPanel.wsmesh");
        assetGenerator->RequestTextureGenerateFromFile(
            assets / "textures/smiling_friend.jpg",
            assets / "textures/smiling_friend.wtexture",
            true,
            DXGI_FORMAT_BC7_UNORM_SRGB);
        assetGenerator->RequestTextureGenerateFromFile(
            assets / "textures/prototype_texture_dark.png",
            assets / "textures/prototype_texture_dark.wtexture",
            true,
            DXGI_FORMAT_BC7_UNORM_SRGB);
        assetGenerator->RequestTextureGenerateFromFile(
            assets / "textures/sprites/sprite_point_light.png",
            assets / "textures/sprites/sprite_point_light.wtexture",
            true,
            DXGI_FORMAT_BC7_UNORM_SRGB);
        assetGenerator->RequestTextureGenerateFromFile(
            assets / "textures/sprites/sprite_area_light.png",
            assets / "textures/sprites/sprite_area_light.wtexture",
            true,
            DXGI_FORMAT_BC7_UNORM_SRGB);
        assetGenerator->RequestTextureGenerateFromFile(
            assets / "textures/sprites/sprite_directional_light.png",
            assets / "textures/sprites/sprite_directional_light.wtexture",
            true,
            DXGI_FORMAT_BC7_UNORM_SRGB);
        assetGenerator->GenerateBRDFLUT(assets / "textures/brdf_lut.wtexture");
        assetGenerator->GenerateSMAATextures(assets / "textures");
        assetGenerator->RequestFontGenerate(
            assets / "fonts/Roboto/Roboto-VariableFont_wdth,wght.ttf",
            assets / "fonts/Roboto/Roboto.wsfont");
        assetGenerator->RequestFontGenerate(
            assets / "fonts/JetBrainsMono/fonts/ttf/JetBrainsMonoNL-Regular.ttf",
            assets / "fonts/JetBrainsMono/JetBrainsMono.wsfont");
    }

    if (ImGui::CollapsingHeader("Asset Generation")) {
        if (ImGui::CollapsingHeader("Individual Assets##individual")) {
            ImGui::Separator();
            ImGui::Text("Generate Models:");

            if (ImGui::Button("Intel Sponza")) {
                assetGenerator->RequestModelGenerate(Platform::GetAssetPath() / "IntelSponza.glb", Platform::GetAssetPath() / "IntelSponza.wsmesh");
            }
            if (ImGui::Button("dragon.wsmesh")) {
                assetGenerator->RequestModelGenerate(Platform::GetAssetPath() / "dragon/dragon.gltf", Platform::GetAssetPath() / "dragon/dragon.wsmesh");
            }

            if (ImGui::Button("BoxTextured.wsmesh")) {
                assetGenerator->RequestModelGenerate(Platform::GetAssetPath() / "BoxTextured.glb", Platform::GetAssetPath() / "BoxTextured.wsmesh");
            }

            if (ImGui::Button("BoxTextured4k.wsmesh")) {
                assetGenerator->RequestModelGenerate(Platform::GetAssetPath() / "BoxTextured4k.glb", Platform::GetAssetPath() / "BoxTextured4k.wsmesh");
            }
            if (ImGui::Button("Sphere.wsmesh")) {
                assetGenerator->RequestModelGenerate(Platform::GetAssetPath() / "Sphere.glb", Platform::GetAssetPath() / "Sphere.wsmesh");
            }

            if (ImGui::Button("sponza.wsmesh")) {
                assetGenerator->RequestModelGenerate(Platform::GetAssetPath() / "sponza2/sponza.gltf", Platform::GetAssetPath() / "sponza2/sponza.wsmesh");
            }
            if (ImGui::Button("plane.wsmesh")) {
                assetGenerator->RequestModelGenerate(Platform::GetAssetPath() / "Plane.glb", Platform::GetAssetPath() / "Plane.wsmesh");
            }
            if (ImGui::Button("LightPanel.wsmesh")) {
                assetGenerator->RequestModelGenerate(Platform::GetAssetPath() / "LightPanel.glb", Platform::GetAssetPath() / "LightPanel.wsmesh");
            }
            if (ImGui::Button("LumberyardBistro.wsmesh")) {
                assetGenerator->RequestModelGenerate(
                    Core::Path{"D:/source/repos/RTXDI-Assets/bistro/bistro.gltf"},
                    Platform::GetAssetPath() / "models/LumberyardBistro/LumberyardBistro.wsmesh",
                    Platform::GetAssetPath() / "models/LumberyardBistro/textures"
                );
            }

            ImGui::SeparatorText("Generate Environment Map:"); {
                static Core::InlineVector<Core::Path, 32> hdrFiles;
                static int selectedHdrIdx = 0;
                static bool hdrScanned = false;

                if (ImGui::Button("Refresh##hdr") || !hdrScanned) {
                    hdrFiles.Clear();
                    Core::Path tempPaths[32];
                    const uint32_t found = Platform::FindFilesByExtension(
                        Platform::GetAssetPath(), ".hdr", tempPaths, 32);
                    for (uint32_t i = 0; i < found; ++i) {
                        hdrFiles.PushBack(std::move(tempPaths[i]));
                    }
                    selectedHdrIdx = 0;
                    hdrScanned = true;
                }

                if (!hdrFiles.IsEmpty()) {
                    selectedHdrIdx = std::clamp(selectedHdrIdx, 0, static_cast<int>(hdrFiles.Size()) - 1);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled(hdrFiles.IsEmpty());
                const char* previewLabel = hdrFiles.IsEmpty() ? "No .hdr files found" : hdrFiles[selectedHdrIdx].Filename().data();
                if (ImGui::BeginCombo("##hdr_list", previewLabel)) {
                    for (int i = 0; i < static_cast<int>(hdrFiles.Size()); ++i) {
                        const bool selected = (i == selectedHdrIdx);
                        if (ImGui::Selectable(hdrFiles[i].Filename().data(), selected)) {
                            selectedHdrIdx = i;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();

                ImGui::BeginDisabled(hdrFiles.IsEmpty());
                if (ImGui::Button("Generate##envmap")) {
                    const Core::Path& hdrPath = hdrFiles[selectedHdrIdx];
                    const auto stem = hdrPath.Stem();
                    Core::InlineString<256> outName;
                    outName.Append(stem.data(), stem.size());
                    outName.Append(".wenvmap");
                    const Core::Path outputPath = hdrPath.Parent() / outName.c_str();
                    assetGenerator->RequestEnvironmentMapGenerate(hdrPath, outputPath);
                }
                ImGui::EndDisabled();
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

            if (ImGui::Button("Generate BRDF LUT, Smiling Friend, and Prototype Texture")) {
                assetGenerator->RequestTextureGenerateFromFile(
                    Platform::GetAssetPath() / "textures/smiling_friend.jpg",
                    Platform::GetAssetPath() / "textures/smiling_friend.wtexture",
                    true,
                    DXGI_FORMAT_BC7_UNORM_SRGB);
                assetGenerator->RequestTextureGenerateFromFile(
                    Platform::GetAssetPath() / "textures/prototype_texture_dark.png",
                    Platform::GetAssetPath() / "textures/prototype_texture_dark.wtexture",
                    true,
                    DXGI_FORMAT_BC7_UNORM_SRGB);
                assetGenerator->GenerateBRDFLUT(Platform::GetAssetPath() / "textures/brdf_lut.wtexture");
                assetGenerator->GenerateSMAATextures(Platform::GetAssetPath() / "textures");
            }

            if (ImGui::Button("Generate Roboto")) {
                assetGenerator->RequestFontGenerate(
                    Platform::GetAssetPath() / "fonts/Roboto/Roboto-VariableFont_wdth,wght.ttf",
                    Platform::GetAssetPath() / "fonts/Roboto/Roboto.wsfont");
            }

            if (ImGui::Button("Generate JetBrainsMono")) {
                assetGenerator->RequestFontGenerate(
                    Platform::GetAssetPath() / "fonts/JetBrainsMono/fonts/ttf/JetBrainsMonoNL-Regular.ttf",
                    Platform::GetAssetPath() / "fonts/JetBrainsMono/JetBrainsMono.wsfont");
            }

            if (ImGui::Button("Generate FascinateInline")) {
                assetGenerator->RequestFontGenerate(
                    Platform::GetAssetPath() / "fonts/FascinateInline/FascinateInline-Regular.ttf",
                    Platform::GetAssetPath() / "fonts/FascinateInline/FascinateInline.wsfont");
            }

            ImGui::SeparatorText("Sprites:");
            static bool spriteFlipY = true;
            ImGui::Checkbox("Flip Y##sprite", &spriteFlipY);
            ImGui::SameLine();
            if (ImGui::Button("Generate sprite_point_light")) {
                assetGenerator->RequestTextureGenerateFromFile(
                    Platform::GetAssetPath() / "textures/sprites/sprite_point_light.png",
                    Platform::GetAssetPath() / "textures/sprites/sprite_point_light.wtexture",
                    true,
                    DXGI_FORMAT_BC7_UNORM_SRGB,
                    spriteFlipY);
            }
            ImGui::SameLine();
            if (ImGui::Button("Generate sprite_area_light")) {
                assetGenerator->RequestTextureGenerateFromFile(
                    Platform::GetAssetPath() / "textures/sprites/sprite_area_light.png",
                    Platform::GetAssetPath() / "textures/sprites/sprite_area_light.wtexture",
                    true,
                    DXGI_FORMAT_BC7_UNORM_SRGB,
                    spriteFlipY);
            }
            ImGui::SameLine();
            if (ImGui::Button("Generate sprite_directional_light")) {
                assetGenerator->RequestTextureGenerateFromFile(
                    Platform::GetAssetPath() / "textures/sprites/sprite_directional_light.png",
                    Platform::GetAssetPath() / "textures/sprites/sprite_directional_light.wtexture",
                    true,
                    DXGI_FORMAT_BC7_UNORM_SRGB,
                    spriteFlipY);
            }

            ImGui::SeparatorText("Procedural Textures:");
            if (ImGui::Button("Load Yellow Texture")) {
                assetManager->LoadProceduralTexture(SID("yellow_texture"), 256, 256, VK_FORMAT_R8G8B8A8_UNORM, true, Texture::Origin::RuntimeProcedural);
            }
            if (ImGui::Button("Load Domain Warp")) {
                assetManager->LoadProceduralTexture(SID("domain_warp"), 512, 512, VK_FORMAT_R8G8B8A8_UNORM, true, Texture::Origin::RuntimeProcedural);
            }
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

#if LOGGING_ENABLED
    if (ImGui::Begin("Log")) {
        auto entries = engineLogger->GetImGuiSink()->GetEntries();

        // Level filter
        static constexpr spdlog::level::level_enum kLevels[] = {
            spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
            spdlog::level::warn, spdlog::level::err, spdlog::level::critical
        };
        static constexpr const char* kLevelNames[] = {"Trace", "Debug", "Info", "Warn", "Error", "Critical"};
        static constexpr int kLevelCount = 6;
        static bool levelFilter[kLevelCount];
        static bool levelInit = false;
        if (!levelInit) {
            memset(levelFilter, 1, sizeof(levelFilter));
            levelFilter[0] = false; // Trace off by default
            levelInit = true;
        }

        static constexpr bool levelPresent[kLevelCount] = {
            SPDLOG_LEVEL_TRACE >= SPDLOG_ACTIVE_LEVEL,
            SPDLOG_LEVEL_DEBUG >= SPDLOG_ACTIVE_LEVEL,
            SPDLOG_LEVEL_INFO >= SPDLOG_ACTIVE_LEVEL,
            SPDLOG_LEVEL_WARN >= SPDLOG_ACTIVE_LEVEL,
            SPDLOG_LEVEL_ERROR >= SPDLOG_ACTIVE_LEVEL,
            SPDLOG_LEVEL_CRITICAL >= SPDLOG_ACTIVE_LEVEL,
        };
        const auto entryCount = static_cast<int32_t>(entries.Size());

        for (int l = 0; l < kLevelCount; l++) {
            if (l > 0) ImGui::SameLine();
            ImGui::BeginDisabled(!levelPresent[l]);
            ImGui::Checkbox(kLevelNames[l], &levelFilter[l]);
            ImGui::EndDisabled();
        }

        // Category filter
        static bool categoryFilter[static_cast<int>(LogCategory::Count)];
        static bool catInit = false;
        if (!catInit) {
            memset(categoryFilter, 1, sizeof(categoryFilter));
            catInit = true;
        }
        for (int i = 0; i < static_cast<int>(LogCategory::Count); i++) {
            if (i > 0) { ImGui::SameLine(); }
            ImGui::Checkbox(kCategoryNames[i], &categoryFilter[i]);
        }

        ImGui::Separator();

        if (ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGuiListClipper clipper;
            auto filtered = Core::ArenaFixedVector<int32_t>(&engineContext->editorArena.Get(), entryCount);
            for (int i = 0; i < entryCount; i++) {
                const auto& entry = entries[i];
                bool levelPass = false;
                for (int l = 0; l < kLevelCount; l++) {
                    if (entry.level == kLevels[l] && levelFilter[l]) {
                        levelPass = true;
                        break;
                    }
                }
                if (levelPass && categoryFilter[static_cast<int>(entry.category)])
                    filtered.PushBack(i);
            }

            clipper.Begin(static_cast<int>(filtered.Size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    ImGui::TextUnformatted(entries[filtered[i]].message.c_str());
                }
            }

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
#endif
#endif
}

void WillEngine::Run()
{
    renderThread->Start();
    timeManager->Reset();

    // engineState->lighting.skybox = assetManager->LoadCubemap(assetManager->FindCubemapByName("kloofendal_48d_partly_cloudy_puresky_4k"));
    engineState->lighting.skybox = assetManager->LoadCubemap(assetManager->FindCubemapByName("modern_evening_street_4k"));

    SDL_Event e;
    auto nextFrameTime = std::chrono::steady_clock::now();
    while (true) {
        ZoneScopedN("EngineFrame");
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

        if (inputManager->IsQuitRequested()) {
            renderThread->RequestShutdown();
            break;
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
#if WILL_EDITOR
        {
            engineState->pendingHotReloadModelIds.Clear();
            Editor::ModelGenerateComplete modelComplete{};
            while (!engineState->pendingHotReloadModelIds.IsFull() && assetGenerator->TryDequeueModelGenerateComplete(modelComplete)) {
                engineContext->bShouldRescanResources = true;
                if (modelComplete.success && modelComplete.modelId.IsValid()) {
                    engineState->pendingHotReloadModelIds.PushBack(modelComplete.modelId);
                }
            }
            engineState->pendingHotReloadFontIds.Clear();
            Editor::FontGenerateComplete fontComplete{};
            while (!engineState->pendingHotReloadFontIds.IsFull() && assetGenerator->TryDequeueFontGenerateComplete(fontComplete)) {
                engineContext->bShouldRescanResources = true;
                if (fontComplete.success && fontComplete.fontId.IsValid()) {
                    engineState->pendingHotReloadFontIds.PushBack(fontComplete.fontId);
                }
            }
            engineState->pendingHotReloadTextureIds.Clear();
            Editor::TextureGenerateComplete textureComplete{};
            while (!engineState->pendingHotReloadTextureIds.IsFull() && assetGenerator->TryDequeueTextureGenerateComplete(textureComplete)) {
                engineContext->bShouldRescanResources = true;
                if (textureComplete.success && textureComplete.textureId.IsValid()) {
                    engineState->pendingHotReloadTextureIds.PushBack(textureComplete.textureId);
                }
            }
            engineState->pendingHotReloadEnvironmentMapIds.Clear();
            Editor::EnvironmentMapGenerateComplete envMapComplete{};
            while (!engineState->pendingHotReloadEnvironmentMapIds.IsFull() && assetGenerator->TryDequeueCubemapGenerateComplete(envMapComplete)) {
                engineContext->bShouldRescanResources = true;
                if (envMapComplete.success && envMapComplete.environmentMapId.IsValid()) {
                    engineState->pendingHotReloadEnvironmentMapIds.PushBack(envMapComplete.environmentMapId);
                }
            }
        }
        materialManager->Scan();
        assetManager->Scan();
#endif

        engineContext->bImguiKeyboardCaptured = ImGui::GetIO().WantCaptureKeyboard;
        engineContext->bImguiMouseCaptured = ImGui::GetIO().WantCaptureMouse;
        engineContext->bImGuiWantsTextInput = ImGui::GetIO().WantTextInput;
        engineContext->bAssetsChangedThisFrame = loadCounts.modelLoadedCount > 0 || loadCounts.fontLoadedCount > 0 || assetsReclaimed;

        //
        {
            ZoneScopedN("GameFrame");
            const Core::InputFrame& currentInput = inputManager->GetCurrentInput();
            engineState->inputFrame = &currentInput;
            engineState->timeFrame = &timeManager->GetTime();
            gameFunctions.gameUpdate(engineContext, engineState);

            // Accumulate into render frame
            Core::InputFrame& ri = engineState->renderInputFrame;
            for (size_t i = 0; i < currentInput.keys.Size(); ++i) {
                ri.keys[i].pressed |= currentInput.keys[i].pressed;
                ri.keys[i].down |= currentInput.keys[i].down;
                ri.keys[i].released |= currentInput.keys[i].released;
            }
            for (size_t i = 0; i < currentInput.mouseButtons.Size(); ++i) {
                ri.mouseButtons[i].pressed |= currentInput.mouseButtons[i].pressed;
                ri.mouseButtons[i].down |= currentInput.mouseButtons[i].down;
                ri.mouseButtons[i].released |= currentInput.mouseButtons[i].released;
            }
            ri.mousePosition = currentInput.mousePosition;
            ri.mousePositionAbsolute = currentInput.mousePositionAbsolute;
            ri.mouseXDelta += currentInput.mouseXDelta;
            ri.mouseYDelta += currentInput.mouseYDelta;
            ri.mouseWheelDelta += currentInput.mouseWheelDelta;
            ri.isCursorActive = currentInput.isCursorActive;
            ri.isWindowInputFocus = currentInput.isWindowInputFocus;
            inputManager->FrameReset();

            Core::TimeFrame& rt = engineState->renderTimeFrame;
            const Core::TimeFrame& ct = *engineState->timeFrame;
            rt.deltaTime += ct.deltaTime;
            rt.totalTime = ct.totalTime;
            rt.frameCount = ct.frameCount;
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

                Core::FrameBuffer* currentFrameBuffer = engineRenderSynchronization->GetCurrentFrameBuffer();
                ImDrawDataSnapshot* currentImguiSnapshot = engineRenderSynchronization->GetCurrentImguiSnapshot();
                currentFrameBuffer->currentFrameBuffer = engineRenderSynchronization->currentRenderFrame;
                currentFrameBuffer->bFreezeVisibility = bFreezeVisibility;
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


                glm::uvec2 mousePos = {
                    engineState->inputFrame->mousePositionAbsolute.x - engineContext->windowContext.viewportOffsetX,
                    engineState->inputFrame->mousePositionAbsolute.y - engineContext->windowContext.viewportOffsetY
                };
                mousePos.y = engineContext->windowContext.viewportHeight - 1 - mousePos.y;
                engineRenderSynchronization->GetCurrentFrameBuffer()->currentMousePosition = {(mousePos.x), (mousePos.y)};
                //
                {
                    ZoneScopedN("GamePrepareFrame");
                    engineState->inputFrame = &engineState->renderInputFrame;
                    engineState->timeFrame = &engineState->renderTimeFrame;
                    gameFunctions.gamePrepareFrame(engineContext, engineState, engineRenderSynchronization->GetCurrentFrameBuffer());
                    engineState->renderInputFrame = {};
                    engineState->renderTimeFrame = {};
                }

                //
                {
                    ZoneScopedN("SwapAndPrepare");
                    // std::swap(currentFrameBuffer, engineRenderSynchronization->stagingFrameBuffer);
                    engineRenderSynchronization->GetCurrentFrameBuffer()->timeFrame = timeManager->GetTime();
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
    scheduler->WaitforAll();

    gameFunctions.gameUnload(engineContext, engineState);
    gameFunctions.gameShutdown(engineContext, engineState);
    engineState->~EngineState();
    scheduler->ShutdownNow();
    engineContext->scheduler = nullptr;
    engineContext->~EngineContext();

    inputManager->~InputManager();

#if WILL_EDITOR
    assetGenerator->Join();
    assetGenerator->~AssetGenerator();
#endif

    physicsSystem->~PhysicsSystem();
    materialManager->~MaterialManager();
    assetManager->~AssetManager();

    asyncAssetLoadManager->Join();
    asyncAssetLoadManager->~AsyncAssetLoadManager();

    audioManager->~AudioManager();

    engineRenderSynchronization->~FrameSync();

    renderThread->Join();
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
