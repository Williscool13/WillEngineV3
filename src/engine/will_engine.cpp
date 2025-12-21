//
// Created by William on 2025-12-09.
//

#include "will_engine.h"

#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <entt/entt.hpp>

#include "engine_api.h"
#include "core/include/game_interface.h"
#include "core/input/input_manager.h"
#include "core/time/time_manager.h"
#include "asset-load/asset_load_thread.h"
#include "platform/paths.h"
#include "render/render_thread.h"

#if WILL_EDITOR
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
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
    assetLoadThread = std::make_unique<AssetLoad::AssetLoadThread>();
    assetLoadThread->Initialize(scheduler.get(), renderThread->GetVulkanContext(), renderThread->GetResourceManager());
#if WILL_EDITOR
    modelGenerator = std::make_unique<Render::ModelGenerator>(renderThread->GetVulkanContext(), scheduler.get());
#endif
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

    gameState = std::make_unique<GameState>();

    engineContext = std::make_unique<Core::EngineContext>();
    engineContext->logger = spdlog::default_logger();
    // engineContext->updateCamera = EngineAPI::UpdateCamera;
    engineContext->windowContext.windowWidth = w;
    engineContext->windowContext.windowHeight = h;
    engineContext->windowContext.bCursorHidden = bCursorHidden;

    gameFunctions.gameStartup(engineContext.get(), gameState.get());
    gameFunctions.gameLoad(engineContext.get(), gameState.get());
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
            assetLoadThread->RequestShutdown();
            renderThread->RequestShutdown();
            engineRenderSynchronization->renderFrames.release();
            break;
        }

        inputManager->UpdateFocus(SDL_GetWindowFlags(window.get()));
        timeManager->UpdateGame();

#if WILL_EDITOR
        InputFrame editorInput = inputManager->GetCurrentInput();
        if (editorInput.GetKey(Key::F5).pressed) {
            gameFunctions.gameUnload(engineContext.get(), gameState.get());
            if (gameDll.Reload()) {
                gameFunctions.gameStartup = gameDll.GetFunction<Core::GameStartUpFunc>("GameStartup");
                gameFunctions.gameLoad = gameDll.GetFunction<Core::GameLoadFunc>("GameLoad");
                gameFunctions.gameUpdate = gameDll.GetFunction<Core::GameUpdateFunc>("GameUpdate");
                gameFunctions.gamePrepareFrame = gameDll.GetFunction<Core::GamePrepareFrameFunc>("GamePrepareFrame");
                gameFunctions.gameUnload = gameDll.GetFunction<Core::GameUnloadFunc>("GameUnload");
                gameFunctions.gameShutdown = gameDll.GetFunction<Core::GameShutdownFunc>("GameShutdown");
                SPDLOG_DEBUG("Game lib was hot-reloaded");
            }
            else {
                gameFunctions.Stub();
                SPDLOG_DEBUG("Game lib failed to be hot-reloaded");
            }

            gameFunctions.gameLoad(engineContext.get(), gameState.get());
        }

        if (editorInput.isWindowInputFocus && editorInput.GetKey(Key::PERIOD).pressed) {
            bCursorHidden = !bCursorHidden;
            SDL_SetWindowRelativeMouseMode(window.get(), bCursorHidden);
            engineContext->windowContext.bCursorHidden = bCursorHidden;
        }
#endif

        std::vector<Render::WillModelHandle> modelHandles;
        assetLoadThread->ResolveLoads(modelHandles);

        for (Render::WillModelHandle& modelHandle : modelHandles) {
            Render::ResourceManager* resourceManager = renderThread->GetResourceManager();
            Render::WillModel* model = resourceManager->models.Get(modelHandle);

            stagingFrameBuffer.bufferAcquireOperations.insert(stagingFrameBuffer.bufferAcquireOperations.end(),
                                                              model->bufferAcquireOps.begin(),
                                                              model->bufferAcquireOps.end());

            stagingFrameBuffer.imageAcquireOperations.insert(stagingFrameBuffer.imageAcquireOperations.end(),
                                                             model->imageAcquireOps.begin(),
                                                             model->imageAcquireOps.end());
        }

        gameState->inputFrame = &inputManager->GetCurrentInput();
        gameState->timeFrame = &timeManager->GetTime();
        gameFunctions.gameUpdate(engineContext.get(), gameState.get());
        inputManager->FrameReset();

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

            gameFunctions.gamePrepareFrame(engineContext.get(), gameState.get(), &stagingFrameBuffer);

            std::swap(currentFrameBuffer, stagingFrameBuffer);
            stagingFrameBuffer.modelMatrixOperations.clear();
            stagingFrameBuffer.instanceOperations.clear();
            stagingFrameBuffer.jointMatrixOperations.clear();
            stagingFrameBuffer.bufferAcquireOperations.clear();
            stagingFrameBuffer.imageAcquireOperations.clear();
            stagingFrameBuffer.timeFrame = timeManager->GetTime();
#if WILL_EDITOR
            PrepareEditor(frameBufferIndex);
#endif
            frameBufferIndex = (frameBufferIndex + 1) % Core::FRAME_BUFFER_COUNT;
            engineRenderSynchronization->renderFrames.release();
        }
    }
}

#if WILL_EDITOR
void WillEngine::DrawImgui()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (ImGui::Begin("Main")) {
        ImGui::Text("Hello!");
    }


    auto loadModel = [&](const std::filesystem::path& path) {
        Render::ResourceManager* resourceManager = renderThread->GetResourceManager();
        Render::WillModelHandle modelHandle = resourceManager->models.Add();
        Render::WillModel* model = resourceManager->models.Get(modelHandle);
        model->source = path;
        model->name = model->source.filename().string();
        assetLoadThread->RequestLoad(modelHandle);
        return modelHandle;
    };

    auto generateModel = [&](const std::filesystem::path& gltfPath, const std::filesystem::path& outPath) {
        auto loadResponse = modelGenerator->GenerateWillModelAsync(gltfPath, outPath);

        while (true) {
            auto progress = modelGenerator->GetProgress().value.load(std::memory_order::acquire);
            auto state = modelGenerator->GetProgress().loadingState.load(std::memory_order::acquire);

            SPDLOG_DEBUG("Progress: {}% - State: {}", progress, static_cast<int>(state));

            if (state == Render::WillModelGenerationProgress::LoadingProgress::SUCCESS ||
                state == Render::WillModelGenerationProgress::LoadingProgress::FAILED) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        SPDLOG_INFO("Generation finished");
    };

    /*if (ImGui::Button("Add One BoxTextured")) {
        if (boxModelHandle.IsValid() && bCanGenerate && !bHasAdded) {
            Render::ResourceManager* resourceManager = renderThread->GetResourceManager();
            Game::ModelInstance mi{};

            Render::WillModel* model = resourceManager->models.Get(boxModelHandle);
            if (model) {
                mi.transform = Transform::IDENTITY;
                mi.nodes.reserve(model->modelData.nodes.size());
                mi.nodeRemap = model->modelData.nodeRemap;

                // todo: skinned mesh support
                // size_t jointMatrixCount = model->modelData.inverseBindMatrices.size();
                // bool bHasSkinning = jointMatrixCount > 0;
                // if (bHasSkinning) {
                //     rm.jointMatrixAllocation = resourceManager->jointMatrixAllocator.allocate(jointMatrixCount * sizeof(Renderer::Model));
                //     rm.jointMatrixOffset = rm.jointMatrixAllocation.offset / sizeof(uint32_t);
                // }

                mi.modelEntryHandle = boxModelHandle;
                for (const Render::Node& n : model->modelData.nodes) {
                    mi.nodes.emplace_back(n);
                    Game::NodeInstance& rn = mi.nodes.back();
                    // if (n.inverseBindIndex != ~0u) {
                    //     rn.inverseBindMatrix = model->modelData.inverseBindMatrices[n.inverseBindIndex];
                    // }
                }

                for (Game::NodeInstance& node : mi.nodes) {
                    if (node.meshIndex != ~0u) {
                        node.modelMatrixHandle = resourceManager->modelEntryAllocator.Add();

                        for (const Render::PrimitiveProperty& primitiveProperty : model->modelData.meshes[node.meshIndex].primitiveIndices) {
                            Render::InstanceEntryHandle instanceEntry = resourceManager->instanceEntryAllocator.Add();
                            node.instanceEntryHandles.push_back(instanceEntry);

                            Core::InstanceOperation instanceOp{
                                .index = instanceEntry.index,
                                .modelIndex = node.modelMatrixHandle.index,
                                .primitiveIndex = primitiveProperty.index,
                                .materialIndex = primitiveProperty.materialIndex, // todo: should also generate material to use here
                                .jointMatrixOffset = 0,
                                .bIsAllocated = 1,
                            };
                            stagingFrameBuffer.instanceOperations.push_back(instanceOp);
                        }
                    }
                }

                // Update entire transform hierarchy
                {
                    glm::mat4 baseTopLevel = mi.transform.GetMatrix();
                    // Nodes are sorted
                    for (Game::NodeInstance& ni : mi.nodes) {
                        glm::mat4 localTransform = ni.transform.GetMatrix();

                        if (ni.parent == ~0u) {
                            ni.cachedWorldTransform = baseTopLevel * localTransform;
                        }
                        else {
                            ni.cachedWorldTransform = mi.nodes[ni.parent].cachedWorldTransform * localTransform;
                        }
                    }
                    mi.bNeedToSendToRender = true;
                }

                // Model matrix send to gpu
                {
                    if (mi.bNeedToSendToRender) {
                        for (Game::NodeInstance& node : mi.nodes) {
                            if (node.meshIndex != ~0u) {
                                stagingFrameBuffer.modelMatrixOperations.push_back({node.modelMatrixHandle.index, Transform::IDENTITY.GetMatrix()}); //node.cachedWorldTransform});
                            }

                            // if (node.jointMatrixIndex != ~0u) {
                            //     glm::mat4 jointMatrix = node.cachedWorldTransform * node.inverseBindMatrix;
                            //     uint32_t jointMatrixFinalIndex = node.jointMatrixIndex + runtimeMesh.jointMatrixOffset;
                            //     jointMatrixOperations.push_back({jointMatrixFinalIndex, jointMatrix});
                            // }
                        }

                        mi.bNeedToSendToRender = false;
                    }
                }


                bHasAdded = true;
            }
        }
    }

    if (ImGui::Button("Load BoxTextured .willmodel")) {
        if (!boxModelHandle.IsValid()) {
            boxModelHandle = loadModel(Platform::GetAssetPath() / "BoxTextured.willmodel");
        }
    }

    if (ImGui::Button("Generate BoxTextured.willmodel from BoxTextured.glb")) {
        generateModel(
            Platform::GetAssetPath() / "BoxTextured.glb",
            Platform::GetAssetPath() / "BoxTextured.willmodel"
        );
    }

    if (ImGui::Button("Load dragon.willmodel")) {
        loadModel(Platform::GetAssetPath() / "dragon/dragon.willmodel");
    }

    if (ImGui::Button("Generate dragon.willmodel from dragon.glb")) {
        generateModel(
            Platform::GetAssetPath() / "dragon/dragon.gltf",
            Platform::GetAssetPath() / "dragon/dragon.willmodel"
        );
    }*/


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
    assetLoadThread->Join();
    renderThread->Join();

    gameFunctions.gameUnload(engineContext.get(), gameState.get());
    gameFunctions.gameShutdown(engineContext.get(), gameState.get());
    gameState = nullptr;
#ifndef GAME_STATIC
    gameDll.Unload();
#endif
}
}
