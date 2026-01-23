//
// Created by William on 2025-12-09.
//

#include "will_engine.h"

#include <tracy/Tracy.hpp>
#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <entt/entt.hpp>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include "asset_manager.h"
#include "engine_api.h"
#include "core/include/game_interface.h"
#include "core/input/input_manager.h"
#include "core/time/time_manager.h"
#include "asset-load/asset_load_thread.h"
#include "physics/physics_system.h"
#include "platform/paths.h"
#include "platform/thread_utils.h"
#include "render/render_thread.h"
#include "render/pipelines/pipeline_manager.h"

#if WILL_EDITOR
#include "editor/asset-generation/asset_generator.h"
#endif

namespace Engine
{
WillEngine::WillEngine(Platform::CrashHandler* crashHandler_)
    : crashHandler(crashHandler_)
{}

WillEngine::~WillEngine() = default;

void WillEngine::Initialize()
{
    ZoneScoped;
    tracy::SetThreadName("EngineThread");
    Platform::SetThreadName("EngineThread");

    //
    {
        ZoneScopedN("SchedulerInit");
        static constexpr std::array<const char*, 64> kTaskThreadNames = {
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

        enki::TaskSchedulerConfig config;
        config.numTaskThreadsToCreate = glm::min(64u, enki::GetNumHardwareThreads() - 1);
        config.profilerCallbacks.threadStart = [](uint32_t threadNum_) {
            // 0 is Engine Thread
            // N - 1 is Render Thread
            // N - 2 is Asset Load Thread
            if (threadNum_ < enki::GetNumHardwareThreads() - 3) {
                const char* name = kTaskThreadNames[threadNum_];
                tracy::SetThreadName(name);
                Platform::SetThreadName(name);
            }
        };
        config.profilerCallbacks.waitForNewTaskSuspendStart = [](uint32_t) {};
        config.profilerCallbacks.waitForNewTaskSuspendStop = [](uint32_t) {};
        config.profilerCallbacks.waitForTaskCompleteStart = [](uint32_t) {};
        config.profilerCallbacks.waitForTaskCompleteStop = [](uint32_t) {};
        config.profilerCallbacks.waitForTaskCompleteSuspendStart = [](uint32_t) {};
        config.profilerCallbacks.waitForTaskCompleteSuspendStop = [](uint32_t) {};

        SPDLOG_INFO("Scheduler operating with {} threads.", config.numTaskThreadsToCreate + 1);
        scheduler = std::make_unique<enki::TaskScheduler>();
        scheduler->Initialize(config);
    }

    //
    {
        ZoneScopedN("SDL_Init");
        bool sdlInitSuccess = SDL_Init(SDL_INIT_VIDEO);
        if (!sdlInitSuccess) {
            SPDLOG_ERROR("SDL_Init failed: {}", SDL_GetError());
            exit(1);
        }
    }

    int32_t w;
    int32_t h;
    //
    {
        ZoneScopedN("WindowCreation");
        window = SDLWindowPtr(
            SDL_CreateWindow(
                "Will Engine",
                640,
                480,
                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE),
            SDL_DestroyWindow
        );
        SDL_SetWindowPosition(window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(window.get());
        SDL_GetWindowSize(window.get(), &w, &h);
    }

    //
    {
        ZoneScopedN("CreateInputManager");
        inputManager = std::make_unique<Core::InputManager>(w, h);
    }

    //
    {
        ZoneScopedN("CreateTimeManager");
        timeManager = std::make_unique<Core::TimeManager>();
    }

    //
    {
        ZoneScopedN("CreateRenderThread");
        engineRenderSynchronization = std::make_unique<Core::FrameSync>();
        renderThread = std::make_unique<Render::RenderThread>(engineRenderSynchronization.get(), scheduler.get(), window.get(), w, h);
    }

    //
    {
        ZoneScopedN("CreateAssetLoadThread");
        assetLoadThread = std::make_unique<AssetLoad::AssetLoadThread>(scheduler.get(), renderThread->GetVulkanContext(), renderThread->GetResourceManager(), renderThread->GetPipelineManager());
    }

    //
    {
        ZoneScopedN("InitializePipelineManager");
        renderThread->InitializePipelineManager(assetLoadThread.get());
    }


    //
    {
        ZoneScopedN("CreateAssetManager");
        assetManager = std::make_unique<AssetManager>(assetLoadThread.get(), renderThread->GetResourceManager());
    }

    //
    {
        ZoneScopedN("CreatePhysicsSystem");
        physicsSystem = std::make_unique<Physics::PhysicsSystem>(scheduler.get());
    }


#if WILL_EDITOR
    //
    {
        ZoneScopedN("CreateModelGenerator");
        modelGenerator = std::make_unique<Render::AssetGenerator>(renderThread->GetVulkanContext(), scheduler.get());
    }

#endif

    //
    {
        ZoneScopedN("InitializeGameStateAndEngineContext");
#if !WILL_EDITOR
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
        bCursorHidden = true;
#endif
        if (bCursorHidden) {
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
            SDL_SetWindowRelativeMouseMode(window.get(), true);
        }
        else {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            SDL_SetWindowRelativeMouseMode(window.get(), false);
        }

        gameState = std::make_unique<GameState>();

        engineContext = std::make_unique<Core::EngineContext>();
        engineContext->logger = spdlog::default_logger();
        engineContext->imguiContext = ImGui::GetCurrentContext();
        engineContext->windowContext.windowWidth = w;
        engineContext->windowContext.windowHeight = h;
        engineContext->windowContext.bCursorHidden = bCursorHidden;
        engineContext->assetManager = assetManager.get();
        engineContext->physicsSystem = physicsSystem.get();
    }
    //
    {
        ZoneScopedN("PrepareGameFunctions");
#ifdef GAME_STATIC
        gameFunctions.gameStartup = &GameStartup;
        gameFunctions.gameLoad = &GameLoad;
        gameFunctions.gameUpdate = &GameUpdate;
        gameFunctions.gamePrepareFrame = &GamePrepareFrame;
        gameFunctions.gameUnload = &GameUnload;
        gameFunctions.gameShutdown = &GameShutdown;
#else
        if (gameDll.Load("game.dll", "game_temp.dll")) {
            gameFunctions.gameStartup = gameDll.GetFunction<Core::GameStartUpFunc>("GameStartup");
            gameFunctions.gameLoad = gameDll.GetFunction<Core::GameLoadFunc>("GameLoad");
            gameFunctions.gameUpdate = gameDll.GetFunction<Core::GameUpdateFunc>("GameUpdate");
            gameFunctions.gamePrepareFrame = gameDll.GetFunction<Core::GamePrepareFrameFunc>("GamePrepareFrame");
            gameFunctions.gameUnload = gameDll.GetFunction<Core::GameUnloadFunc>("GameUnload");
            gameFunctions.gameShutdown = gameDll.GetFunction<Core::GameShutdownFunc>("GameShutdown");
        }
        else {
            gameFunctions.Stub();
        }
#endif

        gameFunctions.gameStartup(engineContext.get(), gameState.get());
        gameFunctions.gameLoad(engineContext.get(), gameState.get());
    }

#if WILL_EDITOR
#if !GAME_STATIC
    auto gameDirectory = Platform::GetExecutablePath() / "src/game";
    if (exists(gameDirectory)) {
        gameDllWatcher.Start(gameDirectory.string(), [&]() {
            gameFunctions.gameUnload(engineContext.get(), gameState.get());
            auto reloadResponse = gameDll.Reload();
            switch (reloadResponse) {
                case Platform::DllLoadResponse::Loaded:
                    SPDLOG_DEBUG("Game lib was hot-reloaded");
                // Fallthrough
                case Platform::DllLoadResponse::NoChanges:
                    gameFunctions.gameStartup = gameDll.GetFunction<Core::GameStartUpFunc>("GameStartup");
                    gameFunctions.gameLoad = gameDll.GetFunction<Core::GameLoadFunc>("GameLoad");
                    gameFunctions.gameUpdate = gameDll.GetFunction<Core::GameUpdateFunc>("GameUpdate");
                    gameFunctions.gamePrepareFrame = gameDll.GetFunction<Core::GamePrepareFrameFunc>("GamePrepareFrame");
                    gameFunctions.gameUnload = gameDll.GetFunction<Core::GameUnloadFunc>("GameUnload");
                    gameFunctions.gameShutdown = gameDll.GetFunction<Core::GameShutdownFunc>("GameShutdown");
                    break;
                case Platform::DllLoadResponse::FailedToLoad:
                    gameFunctions.Stub();
                    SPDLOG_DEBUG("Game lib failed to be hot-reloaded");
                    break;
            }

            gameFunctions.gameLoad(engineContext.get(), gameState.get());
        });
    }
    else {
        SPDLOG_WARN("Game dll path not found.");
    }
#endif
    auto shaderDirectory = Platform::GetShaderPath();
    if (exists(shaderDirectory)) {
        shaderWatcher.Start(shaderDirectory.string(), [&]() {
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

void WillEngine::Run()
{
    renderThread->Start();
    assetLoadThread->Start();
    timeManager->Reset();

    SDL_Event e;
    bool exit = false;
    while (true) {
        while (SDL_PollEvent(&e) != 0) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    exit = true;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_ESCAPE) { exit = true; }
                    break;
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
                }
                break;
                default:
                    break;
            }

            inputManager->ProcessEvent(e);
        }

        if (exit) {
            assetLoadThread->RequestShutdown();
            renderThread->RequestShutdown();
            break;
        }

        inputManager->UpdateFocus(SDL_GetWindowFlags(window.get()));
        timeManager->UpdateGame();

#if WILL_EDITOR
        InputFrame editorInput = inputManager->GetCurrentInput();
        TimeFrame editorTime = timeManager->GetTime();
#if !GAME_STATIC
        gameDllWatcher.Poll();
#endif
        shaderWatcher.Poll();

        if (editorInput.isWindowInputFocus && !ImGui::GetIO().WantCaptureKeyboard && editorInput.GetKey(Key::PERIOD).pressed) {
            bCursorHidden = !bCursorHidden;
            if (bCursorHidden) {
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                SDL_SetWindowRelativeMouseMode(window.get(), true);
                ImGui::SetWindowFocus(nullptr);
            }
            else {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                SDL_SetWindowRelativeMouseMode(window.get(), false);
            }

            engineContext->windowContext.bCursorHidden = bCursorHidden;
        }
#endif

        assetManager->ResolveLoads(stagingFrameBuffer);
        assetManager->ResolveUnloads();

        engineContext->bImguiKeyboardCaptured = ImGui::GetIO().WantCaptureKeyboard;
        engineContext->bImguiMouseCaptured = ImGui::GetIO().WantCaptureMouse;

        //
        {
            ZoneScopedN("GameFrame");
            gameState->inputFrame = &inputManager->GetCurrentInput();
            gameState->timeFrame = &timeManager->GetTime();
            gameFunctions.gameUpdate(engineContext.get(), gameState.get());
            inputManager->FrameReset();
        }


        //
        {
            ZoneScopedN("PrepareRenderFrameData");
            const bool canTransmit = engineRenderSynchronization->gameFrames.try_acquire();
            if (canTransmit) {
                timeManager->UpdateRender();

                Core::FrameBuffer& currentFrameBuffer = engineRenderSynchronization->frameBuffers[frameBufferIndex];
                stagingFrameBuffer.currentFrameBuffer = frameBufferIndex;
                stagingFrameBuffer.swapchainRecreateCommand.bIsMinimized = bMinimized;
                if (bRequireSwapchainRecreate) {
                    stagingFrameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = true;

                    int32_t w;
                    int32_t h;
                    SDL_GetWindowSize(window.get(), &w, &h);
                    stagingFrameBuffer.swapchainRecreateCommand.width = w;
                    stagingFrameBuffer.swapchainRecreateCommand.height = h;
                    bRequireSwapchainRecreate = false;
                }
                else {
                    stagingFrameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = false;
                }

                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                gameFunctions.gamePrepareFrame(engineContext.get(), gameState.get(), &stagingFrameBuffer);
                stagingFrameBuffer.bFreezeVisibility = bFreezeVisibility;
                stagingFrameBuffer.bLogRDG = bLogRDG;
                stagingFrameBuffer.bDrawImgui = bDrawImgui;

                std::swap(currentFrameBuffer, stagingFrameBuffer);
                stagingFrameBuffer.mainViewFamily.modelMatrices.clear();
                stagingFrameBuffer.mainViewFamily.instances.clear();
                stagingFrameBuffer.mainViewFamily.materials.clear();
                stagingFrameBuffer.mainViewFamily.mainView = {};
                stagingFrameBuffer.bufferAcquireOperations.clear();
                stagingFrameBuffer.imageAcquireOperations.clear();
                stagingFrameBuffer.timeFrame = timeManager->GetTime();

                PrepareImgui(frameBufferIndex);
                frameBufferIndex = (frameBufferIndex + 1) % Core::FRAME_BUFFER_COUNT;
                engineRenderSynchronization->renderFrames.release();
            }
        }

        FrameMark;
    }
}

void WillEngine::PrepareImgui(uint32_t currentFrameBufferIndex)
{
    if (ImGui::Begin("Engine")) {
#if !GAME_STATIC
        float gameDllTimeSinceReload = gameDllWatcher.GetTimeSinceLastTrigger();
        int gameDllSeconds = static_cast<int>(gameDllTimeSinceReload);
        if (gameDllSeconds < 60) {
            ImGui::Text("Game DLL: %ds since reload", gameDllSeconds);
        } else {
            ImGui::Text("Game DLL: >60s since reload");
        }
#endif

        float shaderTimeSinceReload = shaderWatcher.GetTimeSinceLastTrigger();
        int shaderSeconds = static_cast<int>(shaderTimeSinceReload);
        if (shaderSeconds < 60) {
            ImGui::Text("Shaders: %ds since reload", shaderSeconds);
        } else {
            ImGui::Text("Shaders: >60s since reload");
        }

        ImGui::Checkbox("Freeze Visibility Calculations", &bFreezeVisibility);
        if (ImGui::Button("Log RDG")) {
            bLogRDG = true;
        }
        else {
            bLogRDG = false;
        }

        if (ImGui::CollapsingHeader("Visibility Debug")) {
            uint8_t* data = static_cast<uint8_t*>(renderThread->GetResourceManager()->debugReadbackBuffer.allocationInfo.pMappedData);

            uint32_t indirectCount = *reinterpret_cast<uint32_t*>(data);
            ImGui::Text("Indirect Draw Count: %u", indirectCount);

            InstancedMeshIndirectDrawParameters* params = reinterpret_cast<InstancedMeshIndirectDrawParameters*>(data + sizeof(uint32_t));

            for (uint32_t i = 0; i < std::min(indirectCount, 10u); i++) {
                if (ImGui::TreeNode((void*) (intptr_t) i, "Draw %u", i)) {
                    ImGui::Text("Dispatch: (%u, %u, %u)", params[i].groupCountX, params[i].groupCountY, params[i].groupCountZ);
                    ImGui::Text("Instance Start: %u", params[i].compactedInstanceStart);
                    ImGui::Text("Meshlet Offset: %u, Count: %u", params[i].meshletOffset, params[i].meshletCount);
                    ImGui::TreePop();
                }
            }
        }

        if (ImGui::CollapsingHeader("Luminance Histogram")) {
            uint8_t* data = static_cast<uint8_t*>(renderThread->GetResourceManager()->debugReadbackBuffer.allocationInfo.pMappedData);
            size_t histogramOffset = sizeof(uint32_t) + 10 * sizeof(InstancedMeshIndirectDrawParameters);
            uint32_t* histogram = reinterpret_cast<uint32_t*>(data + histogramOffset);

            ImGui::Text("First 64 bins (8x8):");
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    int bin = row * 8 + col;
                    ImGui::Text("%5u", histogram[bin]);
                    if (col < 7) ImGui::SameLine();
                }
            }
        }

        if (ImGui::CollapsingHeader("Auto Exposure")) {
            uint8_t* data = static_cast<uint8_t*>(renderThread->GetResourceManager()->debugReadbackBuffer.allocationInfo.pMappedData);
            size_t exposureOffset = sizeof(uint32_t) + 10 * sizeof(InstancedMeshIndirectDrawParameters) + 256 * sizeof(uint32_t);
            float exposure = *reinterpret_cast<float*>(data + exposureOffset);

            ImGui::Text("Current Exposure: %.4f", exposure);
        }
    }
#if WILL_EDITOR

    auto generateModel = [&](const std::filesystem::path& gltfPath, const std::filesystem::path& outPath) {
        auto loadResponse = modelGenerator->GenerateWillModelAsync(gltfPath, outPath);

        while (true) {
            auto progress = modelGenerator->GetModelGenerationProgress().value.load(std::memory_order::acquire);
            auto state = modelGenerator->GetModelGenerationProgress().loadingState.load(std::memory_order::acquire);

            SPDLOG_DEBUG("Progress: {}% - State: {}", progress, static_cast<int>(state));

            if (state == Render::WillModelGenerationProgress::LoadingProgress::SUCCESS ||
                state == Render::WillModelGenerationProgress::LoadingProgress::FAILED) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        SPDLOG_INFO("Generation finished");
    };

    if (ImGui::Button("Generate dragon.willmodel from dragon.glb")) {
        generateModel(
            Platform::GetAssetPath() / "dragon/dragon.gltf",
            Platform::GetAssetPath() / "dragon/dragon.willmodel"
        );
    }

    if (ImGui::Button("Generate BoxTextured.willmodel from BoxTextured.glb")) {
        generateModel(
            Platform::GetAssetPath() / "BoxTextured.glb",
            Platform::GetAssetPath() / "BoxTextured.willmodel"
        );
    }
    if (ImGui::Button("Generate BoxTextured4k.willmodel from BoxTextured4k.glb")) {
        generateModel(
            Platform::GetAssetPath() / "BoxTextured4k.glb",
            Platform::GetAssetPath() / "BoxTextured4k.willmodel"
        );
    }
    if (ImGui::Button("Generate sponza.willmodel from sponza.gltf")) {
        generateModel(
            Platform::GetAssetPath() / "sponza2/sponza.gltf",
            Platform::GetAssetPath() / "sponza2/sponza.willmodel"
        );
    }

    if (ImGui::Button("Create White Texture")) {
        modelGenerator->GenerateKtxTexture(
            Platform::GetAssetPath() / "textures/white.png",
            Platform::GetAssetPath() / "textures/white.ktx2",
            false);
    }

    if (ImGui::Button("Create Error Texture")) {
        modelGenerator->GenerateKtxTexture(
            Platform::GetAssetPath() / "textures/error.png",
            Platform::GetAssetPath() / "textures/error.ktx2",
            false);
    }
    if (ImGui::Button("Create Smiling Friend Texture")) {
        modelGenerator->GenerateKtxTexture(
            Platform::GetAssetPath() / "textures/smiling_friend.jpg",
            Platform::GetAssetPath() / "textures/smiling_friend.ktx2",
            false);
    }
#endif
    ImGui::End();
    ImGui::Render();

    ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[currentFrameBufferIndex];
    imguiSnapshot.SnapUsingSwap(ImGui::GetDrawData(), ImGui::GetTime());
    static int32_t first = 1;
    if (first > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        first--;
    }
}

void WillEngine::Cleanup()
{
    assetLoadThread->Join();
    renderThread->Join();

#if WILL_EDITOR
    modelGenerator->Join();
#endif
    scheduler->ShutdownNow();

    gameFunctions.gameUnload(engineContext.get(), gameState.get());
    gameFunctions.gameShutdown(engineContext.get(), gameState.get());
    gameState = nullptr;

    physicsSystem.reset();

#ifndef GAME_STATIC
    gameDll.Unload();
#endif
}
}
