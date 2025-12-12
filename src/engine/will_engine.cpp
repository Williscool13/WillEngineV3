//
// Created by William on 2025-12-09.
//

#include "will_engine.h"

#include <SDL3/SDL.h>
#include <fmt/format.h>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"
#include "render/render_thread.h"
#include "render/vk_render_targets.h"
#include "render/vk_swapchain.h"
#include "spdlog/spdlog.h"

namespace Engine
{
WillEngine::WillEngine(Platform::CrashHandler* crashHandler_)
    : crashHandler(crashHandler_)
{}

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

    // if (gameDll.Load("game/hot-reload-game.dll", "hot-reload-game_temp.dll")) {
    //     gameFunctions.gameInit = gameDll.GetFunction<GameInitFunc>("GameInit");
    //     gameFunctions.gameUpdate = gameDll.GetFunction<GameUpdateFunc>("GameUpdate");
    //     gameFunctions.gameShutdown = gameDll.GetFunction<GameShutdownFunc>("GameShutdown");
    // }
    // else {
    //     gameFunctions.Stub();
    // }
    // gameFunctions.gameInit(&gameState);


    renderThread = std::make_unique<Render::RenderThread>();
    renderThread->Initialize(this, scheduler.get(), window.get(), w, h);
    // assetLoadingThread.Initialize(renderThread.GetVulkanContext(), renderThread.GetResourceManager());
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
            ImGui_ImplSDL3_ProcessEvent(&e);
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    exit = true;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_ESCAPE) {exit = true;}
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
                //case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    bRequireSwapchainRecreate = true;
                    break;
                default:
                    break;
            }
        }

        if (exit) {
            renderThread->RequestShutdown();
            renderFrames.release();
            break;
        }

        // SDL_WindowFlags windowFlags = SDL_GetWindowFlags(window);
        // input.UpdateFocus(windowFlags);
        // time.Update();

        // assetLoadingThread.ResolveLoads(loadedModelEntryHandles, bufferAcquireOperations, imageAcquireOperations);

        EngineMain();
        // gameFunctions.gameUpdate(&gameState, time.GetDeltaTime());
        // accumDeltaTime += time.GetDeltaTime();

        const bool canTransmit = gameFrames.try_acquire();
        if (canTransmit) {
            uint64_t currentFrameBufferIndex = frameBufferIndex % Core::FRAME_BUFFER_COUNT;
            PrepareFrameBuffer(currentFrameBufferIndex, frameBuffers[currentFrameBufferIndex]);
            frameBufferIndex++;
            renderFrames.release();
        }

        gameFrameCount++;
    }
}

void WillEngine::EngineMain()
{}

void WillEngine::PrepareFrameBuffer(uint32_t currentFrameBufferIndex, Core::FrameBuffer& frameBuffer)
{
    DrawImgui();
    frameBuffer.imguiDataSnapshot.SnapUsingSwap(ImGui::GetDrawData(), ImGui::GetTime());


    // add acquires to acquire buffers..

    // frameBuffer.rawSceneData = sceneData;
    // frameBuffer.timeElapsed = timeElapsed;
    // frameBuffer.deltaTime = deltaTime;
    // frameBuffer.currentFrameBuffer; // only used for validation on render thread

    frameBuffer.swapchainRecreateCommand.bIsMinimized = bMinimized;
    if (bRequireSwapchainRecreate) {
        frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = true;

        int32_t w;
        int32_t h;
        SDL_GetWindowSize(window.get(), &w, &h);
        frameBuffer.swapchainRecreateCommand.width = w;
        frameBuffer.swapchainRecreateCommand.height = h;
        bRequireSwapchainRecreate = false;
    }
    else {
        frameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = false;
    }
    frameBuffer.currentFrameBuffer = currentFrameBufferIndex;
    // add instance, model, and joint matrix changes...
}

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

void WillEngine::Cleanup()
{
    renderThread->Join();
}
}
