//
// Created by William on 2025-12-09.
//

#include "will_engine.h"

#include <SDL3/SDL.h>
#include <fmt/format.h>

#include "engine_api.h"
#if WILL_EDITOR
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"
#endif
#include "core/include/game_interface.h"
#include "core/input/input_manager.h"
#include "render/render_thread.h"
#include "render/vk_render_targets.h"
#include "render/vk_swapchain.h"
#include "spdlog/spdlog.h"

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
    // Input::Get().Init(window, w, h);

    engineRenderSynchronization = std::make_unique<Core::FrameSync>();

    inputManager = std::make_unique<Core::InputManager>();
    inputManager->Init(w, h);
    renderThread = std::make_unique<Render::RenderThread>();
    renderThread->Initialize(engineRenderSynchronization.get(), scheduler.get(), window.get(), w, h);
    // assetLoadingThread.Initialize(renderThread.GetVulkanContext(), renderThread.GetResourceManager());

    if (gameDll.Load("game.dll", "game_temp.dll")) {
        gameFunctions.gameInit = gameDll.GetFunction<Core::GameInitFunc>("GameInit");
        gameFunctions.gameUpdate = gameDll.GetFunction<Core::GameUpdateFunc>("GameUpdate");
        gameFunctions.gameShutdown = gameDll.GetFunction<Core::GameShutdownFunc>("GameShutdown");
    }
    else {
        gameFunctions.Stub();
    }

    engineContext = std::make_unique<Core::EngineContext>();
    gameState = std::make_unique<Core::GameState>();
    engineContext->logger = spdlog::default_logger();
    engineContext->updateCamera = EngineAPI::UpdateCamera;
    engineContext->windowWidth = w;
    engineContext->windowHeight = h;

    gameFunctions.gameInit(engineContext.get(), gameState.get());
}

void WillEngine::Run()
{
    renderThread->Start();

    // Input& input = Input::Input::Get();
    // Time& time = Time::Get();

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

        InputFrame input = inputManager->GetCurrentInput();
#if WILL_EDITOR
        if (input.isWindowInputFocus && input.GetKey(Key::PERIOD).pressed) {
            bCursorActive = !bCursorActive;
            SDL_SetWindowRelativeMouseMode(window.get(), bCursorActive);
        }
#endif

        // SDL_WindowFlags windowFlags = SDL_GetWindowFlags(window);
        // input.UpdateFocus(windowFlags);
        // time.Update();

        // assetLoadingThread.ResolveLoads(loadedModelEntryHandles, bufferAcquireOperations, imageAcquireOperations);
        gameFunctions.gameUpdate(engineContext.get(), gameState.get(), &input, 0.1f);
        inputManager->FrameReset();


        uint32_t currentFrameBufferIndex = frameBufferIndex % Core::FRAME_BUFFER_COUNT;

        // accumDeltaTime += time.GetDeltaTime();

        const bool canTransmit = engineRenderSynchronization->gameFrames.try_acquire();
        if (canTransmit) {
            PrepareFrameBuffer(currentFrameBufferIndex, engineRenderSynchronization->frameBuffers[currentFrameBufferIndex], 0.1f);
#if WILL_EDITOR
            PrepareEditor(currentFrameBufferIndex);
#endif
            frameBufferIndex++;
            engineRenderSynchronization->renderFrames.release();
        }

        gameFrameCount++;
    }
}

void WillEngine::PrepareFrameBuffer(uint32_t currentFrameBufferIndex, Core::FrameBuffer& frameBuffer, float renderDeltaTime)
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

    //stagingFrameBuffer->timeElapsed = ;
    stagingFrameBuffer.deltaTime = renderDeltaTime;
    stagingFrameBuffer.currentFrameBuffer = currentFrameBufferIndex;

    // add acquires to acquire buffers..
    // add instance, model, and joint matrix changes...


    std::swap(frameBuffer, stagingFrameBuffer);
    stagingFrameBuffer.modelMatrixOperations.clear();
    stagingFrameBuffer.instanceOperations.clear();
    stagingFrameBuffer.jointMatrixOperations.clear();
    stagingFrameBuffer.bufferAcquireOperations.clear();
    stagingFrameBuffer.imageAcquireOperations.clear();
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
}
#endif


void WillEngine::Cleanup()
{
    renderThread->Join();

    gameFunctions.gameShutdown(engineContext.get(), gameState.get());
    gameDll.Unload();
}
}
