//
// Created by William on 2025-12-09.
//

#include "will_engine.h"

#include <SDL3/SDL.h>
#include <fmt/format.h>

#include "engine_api.h"
#include "core/include/game_interface.h"

#include "core/input/input_manager.h"
#include "core/time/time_manager.h"
#include "platform/paths.h"
#include "render/render_thread.h"
#include "spdlog/spdlog.h"

#if WILL_EDITOR
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"
#include "editor/model-generation/model_generator.h"
#endif

namespace Engine
{
WillEngine* WillEngine::instance = nullptr;

WillEngine::WillEngine(Platform::CrashHandler* crashHandler_)
    : crashHandler(crashHandler_)
{
    instance = this;
}

WillEngine::~WillEngine() = default;

void WillEngine::Initialize()
{
    enki::TaskSchedulerConfig config;
    config.numTaskThreadsToCreate = enki::GetNumHardwareThreads() - 1;
    SPDLOG_INFO("Scheduler operating with {} threads.", config.numTaskThreadsToCreate + 1);
    scheduler = std::make_unique<enki::TaskScheduler>();
    scheduler->Initialize(config);

    bool sdlInitSuccess = SDL_Init(SDL_INIT_VIDEO);
    if (!sdlInitSuccess) {
        SPDLOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        exit(1);
    }

    window = SDLWindowPtr(
        SDL_CreateWindow(
            "Template",
            640,
            480,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE),
        SDL_DestroyWindow
    );
    SDL_SetWindowPosition(window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window.get());
    int32_t w;
    int32_t h;
    SDL_GetWindowSize(window.get(), &w, &h);
    SDL_SetWindowRelativeMouseMode(window.get(), bCursorHidden);

    inputManager = std::make_unique<Core::InputManager>(w, h);
    timeManager = std::make_unique<Core::TimeManager>();

    engineRenderSynchronization = std::make_unique<Core::FrameSync>();
    renderThread = std::make_unique<Render::RenderThread>();
    renderThread->Initialize(engineRenderSynchronization.get(), scheduler.get(), window.get(), w, h);
    // assetLoadingThread.Initialize(renderThread.GetVulkanContext(), renderThread.GetResourceManager());

#ifdef GAME_STATIC
    gameFunctions.gameGetStateSize = &GameGetStateSize;
    gameFunctions.gameStartup = &GameStartup;
    gameFunctions.gameLoad = &GameLoad;
    gameFunctions.gameUpdate = &GameUpdate;
    gameFunctions.gameUnload = &GameUnload;
    gameFunctions.gameShutdown = &GameShutdown;
#else
    if (gameDll.Load("game.dll", "game_temp.dll")) {
        gameFunctions.gameGetStateSize = gameDll.GetFunction<Core::GameGetGameStateSizeFunc>("GameGetStateSize");
        gameFunctions.gameStartup = gameDll.GetFunction<Core::GameStartUpFunc>("GameStartup");
        gameFunctions.gameLoad = gameDll.GetFunction<Core::GameLoadFunc>("GameLoad");
        gameFunctions.gameUpdate = gameDll.GetFunction<Core::GameUpdateFunc>("GameUpdate");
        gameFunctions.gameUnload = gameDll.GetFunction<Core::GameUnloadFunc>("GameUnload");
        gameFunctions.gameShutdown = gameDll.GetFunction<Core::GameShutdownFunc>("GameShutdown");
    }
    else {
        gameFunctions.Stub();
    }
#endif

    size_t stateSize = gameFunctions.gameGetStateSize();
    gameState = malloc(stateSize);

    engineContext = std::make_unique<Core::EngineContext>();
    engineContext->logger = spdlog::default_logger();
    engineContext->updateCamera = EngineAPI::UpdateCamera;
    engineContext->windowContext.windowWidth = w;
    engineContext->windowContext.windowHeight = h;
    engineContext->windowContext.bCursorHidden = bCursorHidden;

    gameFunctions.gameStartup(engineContext.get(), static_cast<Game::GameState*>(gameState));
    gameFunctions.gameLoad(engineContext.get(), static_cast<Game::GameState*>(gameState));
}

void WillEngine::Run()
{
    renderThread->Start();
    timeManager->Reset();

    SDL_Event e;
    bool exit = false;
    while (true) {
        while (SDL_PollEvent(&e) != 0) {
            // input->ProcessEvents(&e);
#if WILL_EDITOR
            ImGui_ImplSDL3_ProcessEvent(&e);
#endif
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
                    SDL_SetWindowRelativeMouseMode(window.get(), bCursorHidden);
                }
                break;
                default:
                    break;
            }

            inputManager->ProcessEvent(e);
        }

        if (exit) {
            renderThread->RequestShutdown();
            engineRenderSynchronization->renderFrames.release();
            break;
        }

        inputManager->UpdateFocus(SDL_GetWindowFlags(window.get()));
        timeManager->Update();

        const InputFrame& input = inputManager->GetCurrentInput();
        const TimeFrame& time = timeManager->GetTime();
        stagingFrameBuffer.deltaTime += time.deltaTime;
#if WILL_EDITOR
        if (input.GetKey(Key::F5).pressed) {
            gameFunctions.gameUnload(engineContext.get(), static_cast<Game::GameState*>(gameState));
            if (gameDll.Reload()) {
                gameFunctions.gameGetStateSize = gameDll.GetFunction<Core::GameGetGameStateSizeFunc>("GameGetStateSize");
                gameFunctions.gameStartup = gameDll.GetFunction<Core::GameStartUpFunc>("GameStartup");
                gameFunctions.gameLoad = gameDll.GetFunction<Core::GameLoadFunc>("GameLoad");
                gameFunctions.gameUpdate = gameDll.GetFunction<Core::GameUpdateFunc>("GameUpdate");
                gameFunctions.gameUnload = gameDll.GetFunction<Core::GameUnloadFunc>("GameUnload");
                gameFunctions.gameShutdown = gameDll.GetFunction<Core::GameShutdownFunc>("GameShutdown");
                SPDLOG_DEBUG("Game lib was hot-reloaded");
            }
            else {
                gameFunctions.Stub();
                SPDLOG_DEBUG("Game lib failed to be hot-reloaded");
            }

            gameFunctions.gameLoad(engineContext.get(), static_cast<Game::GameState*>(gameState));
        }

        if (input.isWindowInputFocus && input.GetKey(Key::PERIOD).pressed) {
            bCursorHidden = !bCursorHidden;
            SDL_SetWindowRelativeMouseMode(window.get(), bCursorHidden);
            engineContext->windowContext.bCursorHidden = bCursorHidden;
        }

        if (input.GetKey(Key::L).pressed) {
            auto boxPath = Platform::GetAssetPath() / "BoxTextured.glb";

            Render::MeshInformation mi = {};
            // Render::UIVertex uiv = {};
            auto model = Render::ModelGenerator::LoadGltf(boxPath);
        }
#endif


        // assetLoadingThread.ResolveLoads(loadedModelEntryHandles, bufferAcquireOperations, imageAcquireOperations);
        gameFunctions.gameUpdate(engineContext.get(), static_cast<Game::GameState*>(gameState), input, &time);
        inputManager->FrameReset();

        const bool canTransmit = engineRenderSynchronization->gameFrames.try_acquire();
        if (canTransmit) {
            PrepareFrameBuffer(frameBufferIndex, engineRenderSynchronization->frameBuffers[frameBufferIndex], time);
#if WILL_EDITOR
            PrepareEditor(frameBufferIndex);
#endif
            frameBufferIndex = (frameBufferIndex + 1) % Core::FRAME_BUFFER_COUNT;
            engineRenderSynchronization->renderFrames.release();
        }
    }
}

void WillEngine::PrepareFrameBuffer(uint32_t currentFrameBufferIndex, Core::FrameBuffer& frameBuffer, const TimeFrame& time)
{
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

    stagingFrameBuffer.timeElapsed = time.totalTime;
    stagingFrameBuffer.currentFrameBuffer = currentFrameBufferIndex;

    // add acquires to acquire buffers..
    // add instance, model, and joint matrix changes...


    std::swap(frameBuffer, stagingFrameBuffer);
    stagingFrameBuffer.modelMatrixOperations.clear();
    stagingFrameBuffer.instanceOperations.clear();
    stagingFrameBuffer.jointMatrixOperations.clear();
    stagingFrameBuffer.bufferAcquireOperations.clear();
    stagingFrameBuffer.imageAcquireOperations.clear();
    stagingFrameBuffer.deltaTime = 0;
}

#if WILL_EDITOR
void WillEngine::DrawImgui()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (ImGui::Begin("Main")) {
        ImGui::Text("Hello!");
        // float camPos[3];
        // camPos[0] = freeCamera.transform.translation.x;
        // camPos[1] = freeCamera.transform.translation.y;
        // camPos[2] = freeCamera.transform.translation.z;
        // if (ImGui::DragFloat3("Position", camPos)) {
        //     freeCamera.transform.translation = {camPos[0], camPos[1], camPos[2]};
        // }
    }

    ImGui::End();
    ImGui::Render();
}

void WillEngine::PrepareEditor(uint32_t currentFrameBufferIndex)
{
    ImDrawDataSnapshot& imguiSnapshot = engineRenderSynchronization->imguiDataSnapshots[currentFrameBufferIndex];
    DrawImgui();
    imguiSnapshot.SnapUsingSwap(ImGui::GetDrawData(), ImGui::GetTime());
    static int32_t first = 1;
    if (first > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        first--;
    }

}
#endif


void WillEngine::Cleanup()
{
    renderThread->Join();

    gameFunctions.gameUnload(engineContext.get(), static_cast<Game::GameState*>(gameState));
    gameFunctions.gameShutdown(engineContext.get(), static_cast<Game::GameState*>(gameState));
    free(gameState);
    gameState = nullptr;
#ifndef GAME_STATIC
    gameDll.Unload();
#endif
}
}
