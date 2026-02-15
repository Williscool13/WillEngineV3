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
#include "asset-load/async_asset_load_manager.h"
#include "audio/audio_manager.h"
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

        SPDLOG_INFO("Scheduler operating with {} threads.", config.numTaskThreadsToCreate + 1);
        scheduler = std::make_unique<enki::TaskScheduler>();
        scheduler->Initialize(config);
    }

    //
    {
        ZoneScopedN("SDL_Init");
        bool sdlInitSuccess = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
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
        asyncAssetLoadManager = std::make_unique<AssetLoad::AsyncAssetLoadManager>(
            renderThread->GetVulkanContext(),
            renderThread->GetResourceManager(),
            renderThread->GetPipelineManager()->GetPipelineCache());
    }

    //
    {
        ZoneScopedN("CreateAudioManager");
        audioManager = std::make_unique<Audio::AudioManager>(asyncAssetLoadManager.get());
    }

    //
    {
        ZoneScopedN("InitializePipelineManager");
        renderThread->InitializePipelineManager(asyncAssetLoadManager.get());
    }


    //
    {
        ZoneScopedN("CreateAssetManager");
        assetManager = std::make_unique<AssetManager>(asyncAssetLoadManager.get(), renderThread->GetResourceManager());
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
        modelGenerator = std::make_unique<Editor::AssetGenerator>(renderThread->GetVulkanContext(), renderThread.get(), asyncAssetLoadManager.get());
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
        engineContext->audioManager = audioManager.get();
        engineContext->physicsSystem = physicsSystem.get();
        engineContext->scheduler = scheduler.get();
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
            renderThread->RequestShutdown();
            break;
        }

        audioManager->Update();

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
            const bool bRenderReadyToReceive = engineRenderSynchronization->gameFrames.load(std::memory_order_acquire) > 0;
            if (bRenderReadyToReceive) {
                engineRenderSynchronization->gameFrames.fetch_sub(1, std::memory_order_release); {
                    ZoneScopedN("UpdateRender");
                    timeManager->UpdateRender();
                }

                Core::FrameBuffer& currentFrameBuffer = engineRenderSynchronization->frameBuffers[frameBufferIndex];
                stagingFrameBuffer.currentFrameBuffer = frameBufferIndex; {
                    ZoneScopedN("SwapchainRecreate");
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
                } {
                    ZoneScopedN("ImGuiNewFrame");
                    ImGui_ImplVulkan_NewFrame();
                    ImGui_ImplSDL3_NewFrame();
                    ImGui::NewFrame();
                } {
                    ZoneScopedN("GamePrepareFrame");
                    gameFunctions.gamePrepareFrame(engineContext.get(), gameState.get(), &stagingFrameBuffer);
                }

                stagingFrameBuffer.bFreezeVisibility = bFreezeVisibility;
                stagingFrameBuffer.bLogRDG = bLogRDG;
                stagingFrameBuffer.bDrawImgui = bDrawImgui;
                //
                {
                    ZoneScopedN("SwapAndPrepare");
                    std::swap(currentFrameBuffer, stagingFrameBuffer);
                    stagingFrameBuffer.timeFrame = timeManager->GetTime();
                    stagingFrameBuffer.bufferAcquireOperations.clear();
                    stagingFrameBuffer.imageAcquireOperations.clear();
                    PrepareImgui(frameBufferIndex);
                }

                frameBufferIndex = (frameBufferIndex + 1) % Core::FRAME_BUFFER_COUNT;
                engineRenderSynchronization->renderFrames.fetch_add(1, std::memory_order_release);
                engineRenderSynchronization->renderCV.notify_one();
            }
        }
    }
}

void WillEngine::PrepareImgui(uint32_t currentFrameBufferIndex)
{
#if WILL_EDITOR
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

        ImGui::Checkbox("Freeze Visibility Calculations", &bFreezeVisibility);
        if (ImGui::Button("Log RDG")) {
            bLogRDG = true;
        }
        else {
            bLogRDG = false;
        }

        uint8_t* base = static_cast<uint8_t*>(renderThread->GetResourceManager()->debugReadbackBuffer.allocationInfo.pMappedData);
        uint8_t* ptr = base;
        if (ImGui::CollapsingHeader("Meshlet Instancing Debug")) {
            auto* instanceMeshletOffsets = reinterpret_cast<InstanceMeshletOffsetPrefixSum*>(ptr);

            if (ImGui::BeginTable("InstanceMeshletOffsetsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Instance");
                ImGui::TableSetupColumn("Offset");
                ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("LOD");
                ImGui::TableSetupColumn("Primitive Index");
                ImGui::TableHeadersRow();

                for (uint32_t i = 0; i < 640; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", instanceMeshletOffsets[i].offset);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", instanceMeshletOffsets[i].count);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", instanceMeshletOffsets[i].lod);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", instanceMeshletOffsets[i].primitiveIndex);
                }

                ImGui::EndTable();
            }
        }
        ptr += 640 * sizeof(InstanceMeshletOffsetPrefixSum);

        if (ImGui::CollapsingHeader("Meshlet Dispatch Args")) {
            auto* dispatchArgs = reinterpret_cast<InstancingMeshletDispatchIndirect*>(ptr);

            ImGui::Text("Total Meshlets: %u", dispatchArgs->totalMeshlets);
            ImGui::Text("Dispatch Groups: (%u, %u, %u)", dispatchArgs->x, dispatchArgs->y, dispatchArgs->z);
        }
        ptr += sizeof(InstancingMeshletDispatchIndirect);


        if (ImGui::CollapsingHeader("Intermediate Meshlets")) {
            auto* intermediateMeshlets = reinterpret_cast<IntermediateMeshlet*>(ptr);

            if (ImGui::BeginTable("IntermediateMeshletsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Global Meshlet Index");
                ImGui::TableSetupColumn("Instance Index");
                ImGui::TableSetupColumn("Visible");
                ImGui::TableSetupColumn("Local Meshlet Index");
                ImGui::TableSetupColumn("LOD");
                ImGui::TableHeadersRow();

                for (uint32_t i = 0; i < 128; ++i) {
                    uint32_t packedInstance = intermediateMeshlets[i].instanceIndex;
                    uint32_t packedLocal = intermediateMeshlets[i].localMeshletIndex;

                    uint32_t instanceIndex = packedInstance & 0x7FFFFFFF;
                    bool visible = (packedInstance >> 31) & 1;
                    uint32_t localMeshletIndex = packedLocal & 0x3FFFFFFF;
                    uint32_t lod = packedLocal >> 30;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", instanceIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", visible ? "Yes" : "No");
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", localMeshletIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", lod);
                }

                ImGui::EndTable();
            }
        }
        ptr += sizeof(IntermediateMeshlet) * 128;

        if (ImGui::CollapsingHeader("Visible Meshlets (Compacted)")) {
            auto* visibleMeshlets = reinterpret_cast<CompactedMeshlet*>(ptr);

            if (ImGui::BeginTable("VisibleMeshletsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Compact Index");
                ImGui::TableSetupColumn("Instance Index");
                ImGui::TableSetupColumn("Local Meshlet Index");
                ImGui::TableSetupColumn("LOD");
                ImGui::TableHeadersRow();

                for (uint32_t i = 0; i < 128; ++i) {
                    uint32_t packedLocal = visibleMeshlets[i].localMeshletIndex;
                    uint32_t localMeshletIndex = packedLocal & 0x3FFFFFFF;
                    uint32_t lod = packedLocal >> 30;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", visibleMeshlets[i].instanceIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", localMeshletIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", lod);
                }

                ImGui::EndTable();
            }
        }
        ptr += sizeof(CompactedMeshlet) * 128;

        if (ImGui::CollapsingHeader("Meshlet Scanned Level2 Block Sums")) {
            auto* scannedBlockSums = reinterpret_cast<uint32_t*>(ptr);

            if (ImGui::BeginTable("ScannedBlockSumsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Block Index");
                ImGui::TableSetupColumn("Scanned Sum");
                ImGui::TableHeadersRow();

                for (uint32_t i = 0; i < 256; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", scannedBlockSums[i]);
                }

                ImGui::EndTable();
            }
        }
        ptr += sizeof(uint32_t) * 256;

        if (ImGui::CollapsingHeader("Compacted Meshlet Dispatch Args")) {
            auto* compactedDispatchArgs = reinterpret_cast<InstancingCompactedMeshletDispatchIndirect*>(ptr);

            ImGui::Text("Total Visible Meshlets: %u", compactedDispatchArgs->totalVisibleMeshlets);
            ImGui::Text("Dispatch Groups: (%u, %u, %u)", compactedDispatchArgs->x, compactedDispatchArgs->y, compactedDispatchArgs->z);
        }
        ptr += sizeof(InstancingCompactedMeshletDispatchIndirect);

        if (ImGui::CollapsingHeader("Visible Meshlets")) {
            auto* visibleMeshlets = reinterpret_cast<CompactedMeshlet*>(ptr);

            if (ImGui::BeginTable("VisibleMeshletsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Instance Index");
                ImGui::TableSetupColumn("Local Meshlet Index");
                ImGui::TableSetupColumn("LOD");
                ImGui::TableHeadersRow();

                for (uint32_t i = 0; i < 128; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", visibleMeshlets[i].instanceIndex);
                    uint32_t packedLocal = visibleMeshlets[i].localMeshletIndex;
                    uint32_t localMeshletIndex = packedLocal & 0x3FFFFFFF;
                    uint32_t lod = packedLocal >> 30;
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", localMeshletIndex);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", lod);
                }

                ImGui::EndTable();
            }
        }
        ptr += sizeof(CompactedMeshlet) * 128;
    }


    if (ImGui::CollapsingHeader("Asset Generation")) {
        auto startGeneration = [&](const std::string& name, const std::filesystem::path& gltfPath, const std::filesystem::path& outPath) {
            modelGenerator->RequestModelGenerate(gltfPath, outPath);
        };


        ImGui::Separator();
        ImGui::Text("Generate Models:");

        if (ImGui::Button("dragon.willmodel")) {
            startGeneration("dragon",
                            Platform::GetAssetPath() / "dragon/dragon.gltf",
                            Platform::GetAssetPath() / "dragon/dragon.willmodel");
        }

        if (ImGui::Button("BoxTextured.willmodel")) {
            startGeneration("BoxTextured",
                            Platform::GetAssetPath() / "BoxTextured.glb",
                            Platform::GetAssetPath() / "BoxTextured.willmodel");
        }

        if (ImGui::Button("BoxTextured4k.willmodel")) {
            startGeneration("BoxTextured4k",
                            Platform::GetAssetPath() / "BoxTextured4k.glb",
                            Platform::GetAssetPath() / "BoxTextured4k.willmodel");
        }

        if (ImGui::Button("sponza.willmodel")) {
            startGeneration("sponza",
                            Platform::GetAssetPath() / "sponza2/sponza.gltf",
                            Platform::GetAssetPath() / "sponza2/sponza.willmodel");
        }
        if (ImGui::Button("plane.willmodel")) {
            startGeneration("plane",
                            Platform::GetAssetPath() / "Plane.glb",
                            Platform::GetAssetPath() / "Plane.willmodel");
        }

        ImGui::Separator();
        ImGui::Text("Generate Textures:");

        if (ImGui::Button("white.ktx2")) {
            modelGenerator->RequestTextureGenerate(
                Platform::GetAssetPath() / "textures/white.png",
                Platform::GetAssetPath() / "textures/white.ktx2",
                false,
                DXGI_FORMAT_BC7_UNORM_SRGB);
        }

        if (ImGui::Button("error.ktx2")) {
            modelGenerator->RequestTextureGenerate(
                Platform::GetAssetPath() / "textures/error.png",
                Platform::GetAssetPath() / "textures/error.ktx2",
                false,
                DXGI_FORMAT_BC7_UNORM_SRGB);
        }

        if (ImGui::Button("smiling_friend.ktx2")) {
            modelGenerator->RequestTextureGenerate(
                Platform::GetAssetPath() / "textures/smiling_friend.jpg",
                Platform::GetAssetPath() / "textures/smiling_friend.ktx2",
                false,
                DXGI_FORMAT_BC7_UNORM_SRGB);
        }

        if (ImGui::Button("kloofendal_environment.ktx2")) {
            modelGenerator->RequestEnvironmentMapGenerate(
                Platform::GetAssetPath() / "environment-map/kloofendal_48d_partly_cloudy_puresky_4k.hdr",
                Platform::GetAssetPath() / "environment-map/kloofendal_48d_partly_cloudy_puresky_4k.ktx2");
        }
    }
    ImGui::End();
#endif

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
    asyncAssetLoadManager->Join();
    asyncAssetLoadManager.reset();

#if WILL_EDITOR
    modelGenerator->Join();
    modelGenerator.reset();
#endif

    renderThread->Join();
    audioManager.reset();


    scheduler->ShutdownNow();
    engineContext->scheduler = nullptr;

    gameFunctions.gameUnload(engineContext.get(), gameState.get());
    gameFunctions.gameShutdown(engineContext.get(), gameState.get());
    gameState = nullptr;

    physicsSystem.reset();

#ifndef GAME_STATIC
    gameDll.Unload();
#endif
}
}
