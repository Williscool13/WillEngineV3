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
#include "engine/include/game_interface.h"
#include "core/input/input_manager.h"
#include "core/time/time_manager.h"
#include "asset-load/async_asset_load_manager.h"
#include "audio/audio_manager.h"
#include "logging/engine_logger.h"
#include "physics/physics_system.h"
#include "platform/paths.h"
#include "platform/thread_utils.h"
#include "render/render_thread.h"
#include "render/resource_manager.h"
#include "render/pipelines/pipeline_manager.h"
#include "utils/logging/logging.h"

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
WillEngine::WillEngine(Platform::CrashHandler* crashHandler_)
    : crashHandler(crashHandler_)
{}

WillEngine::~WillEngine() = default;

void WillEngine::Initialize(Utils::Logger* logger)
{
    ZoneScoped;

    memoryManager.Init({
        .persistentSize = 32ull * 1024 * 1024,  // 32 MB
        .generalPoolSize = 64ull * 1024 * 1024, // 64 MB
        .assetsPoolSize = 512ull * 1024 * 1024, // 512 MB
        .physicsPoolSize = 64ull * 1024 * 1024, // 64 MB
    });

#if LOGGING_ENABLED
    engineLogger = memoryManager.PersistentAlloc<EngineLogger>();
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
        scheduler = memoryManager.PersistentAlloc<enki::TaskScheduler>();
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
                640, 480,
                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE),
            SDL_DestroyWindow
        );
        SDL_SetWindowPosition(window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(window.get());
        SDL_MaximizeWindow(window.get());
        SDL_GetWindowSizeInPixels(window.get(), &w, &h);
    }

    engineContext = memoryManager.PersistentAlloc<Core::EngineContext>();

    //
    {
        ZoneScopedN("CreateInputManager");
        inputManager = memoryManager.PersistentAlloc<Core::InputManager>(w, h);
    }

    //
    {
        ZoneScopedN("CreateTimeManager");
        timeManager = memoryManager.PersistentAlloc<Core::TimeManager>();
    }

    //
    {
        ZoneScopedN("CreateRenderThread");
        engineRenderSynchronization = std::make_unique<Core::FrameSync>();
        renderThread = std::make_unique<Render::RenderThread>(engineRenderSynchronization.get(), scheduler, window.get(), w, h);
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
        assetManager = std::make_unique<AssetManager>(engineContext, asyncAssetLoadManager.get(), renderThread->GetResourceManager());
        materialManager = std::make_unique<MaterialManager>(engineContext, assetManager.get());
    }

    //
    {
        ZoneScopedN("CreatePhysicsSystem");
        physicsSystem = std::make_unique<Physics::PhysicsSystem>(scheduler);
    }


#if WILL_EDITOR
    //
    {
        ZoneScopedN("CreateModelGenerator");
        modelGenerator = std::make_unique<Editor::AssetGenerator>(engineContext, renderThread->GetVulkanContext(), renderThread.get(), asyncAssetLoadManager.get());
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

#if LOGGING_ENABLED
        engineContext->engineLogger = engineLogger;
#endif
        engineContext->imguiContext = ImGui::GetCurrentContext();
        engineContext->windowContext.windowWidth = w;
        engineContext->windowContext.windowHeight = h;
        engineContext->windowContext.viewportWidth = w;
        engineContext->windowContext.viewportHeight = h;
        engineContext->windowContext.viewportOffsetX = 0;
        engineContext->windowContext.viewportOffsetY = 0;
        engineContext->assetManager = assetManager.get();
        engineContext->materialManager = materialManager.get();
        engineContext->audioManager = audioManager.get();
        engineContext->physicsSystem = physicsSystem.get();
        engineContext->scheduler = scheduler;
        engineContext->setCursorHiddenFn = [this](bool hidden) {
            if (bCursorHidden == hidden) { return; }
            bCursorHidden = hidden;
            if (bCursorHidden) {
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                SDL_SetWindowRelativeMouseMode(window.get(), true);
                ImGui::SetWindowFocus(nullptr);
            }
            else {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                SDL_SetWindowRelativeMouseMode(window.get(), false);
            }
        };
#if DEBUG
        engineContext->internStringFn = [](uint64_t hash, const char* str) { DBG_InternString(hash, str); };
        engineContext->resolveStringIdFn = [](uint64_t hash) { return DBG_ResolveStringId(hash); };
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

        gameFunctions.gameStartup(engineContext, gameState.get());
        gameFunctions.gameLoad(engineContext, gameState.get());
    }

#if WILL_EDITOR
#if !GAME_STATIC
    auto gameDirectory = Platform::GetExecutablePath() / "src/game";
    if (exists(gameDirectory)) {
        gameDllWatcher.Start(gameDirectory.string(), [&]() {
            gameFunctions.gameUnload(engineContext, gameState.get());
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

            gameFunctions.gameLoad(engineContext, gameState.get());
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
        auto newWidth = static_cast<uint32_t>(centralNode->Size.x);
        auto newHeight = static_cast<uint32_t>(centralNode->Size.y);

        Core::WindowContext& wc = engineContext->windowContext;
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

        if (ImGui::CollapsingHeader("Memory")) {
            const Core::MemoryManager::Stats ms = memoryManager.GetStats();
            ImGui::Text("Total:   %zu MB", ms.totalBytes >> 20);
            ImGui::Separator();
            ImGui::Text("Persistent: %zu / %zu KB (%zu allocs)", ms.persistent.usedBytes >> 10, ms.persistent.totalBytes >> 10, ms.persistent.allocCount);
            ImGui::Separator();
            ImGui::Text("General: %zu / %zu MB (%zu allocs)", ms.general.usedBytes >> 20, ms.general.totalBytes >> 20, ms.general.allocCount);
            ImGui::Text("Assets:  %zu / %zu MB (%zu allocs)", ms.assets.usedBytes >> 20, ms.assets.totalBytes >> 20, ms.assets.allocCount);
            ImGui::Text("Physics: %zu / %zu MB (%zu allocs)", ms.physics.usedBytes >> 20, ms.physics.totalBytes >> 20, ms.physics.allocCount);

            ImGui::Spacing();
            if (ImGui::Button("Refresh Tag Breakdown")) {
                memoryManager.Persistent().GetTagStats(cachedPersistentTags);
                memoryManager.General().GetTagStats(cachedGeneralTags);
                memoryManager.Assets().GetTagStats(cachedAssetsTags);
                memoryManager.Physics().GetTagStats(cachedPhysicsTags);
            }

            constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
            auto drawTagTable = [](const char* label, const Core::TlsfAllocator::TagStats* tags) {
                ImGui::TextUnformatted(label);
                if (ImGui::BeginTable(label, 3, tableFlags)) {
                    ImGui::TableSetupColumn("Tag");
                    ImGui::TableSetupColumn("Allocs");
                    ImGui::TableSetupColumn("Used (KB)");
                    ImGui::TableHeadersRow();
                    for (size_t i = 0; i < static_cast<size_t>(Core::AllocTag::Count); ++i) {
                        const Core::TlsfAllocator::TagStats& t = tags[i];
                        if (t.count == 0) { continue; }
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(Core::AllocTagName(t.tag));
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%zu", t.count);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%zu", t.usedBytes >> 10);
                    }
                    ImGui::EndTable();
                }
            };

            drawTagTable("Persistent", cachedPersistentTags);
            drawTagTable("General", cachedGeneralTags);
            drawTagTable("Assets", cachedAssetsTags);
            drawTagTable("Physics", cachedPhysicsTags);
        }

        if (ImGui::CollapsingHeader("Asset Counts")) {
            ImGui::Text("Models:    %u", assetManager->GetActiveModelCount());
            ImGui::Text("Textures:  %u", assetManager->GetActiveTextureCount());
            ImGui::Text("Samplers:  %u", assetManager->GetActiveSamplerCount());
            ImGui::Text("Cubemaps:  %u", assetManager->GetActiveCubemapCount());
            ImGui::Text("Materials: %u", materialManager->GetActiveMaterialCount());
        }

        ImGui::Checkbox("Freeze Visibility Calculations", &bFreezeVisibility);
        if (ImGui::Button("Log RDG")) {
            bLogRDG = true;
        }
        else {
            bLogRDG = false;
        }

        auto* base = static_cast<uint8_t*>(renderThread->GetResourceManager()->debugReadbackBuffer.allocationInfo.pMappedData);
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

        if (ImGui::Button("Intel Sponza")) {
            modelGenerator->RequestModelGenerate(Platform::GetAssetPath() / "IntelSponza.glb",
                                                 Platform::GetAssetPath() / "IntelSponza.wsmesh");
        }
        if (ImGui::Button("dragon.wsmesh")) {
            startGeneration("dragon",
                            Platform::GetAssetPath() / "dragon/dragon.gltf",
                            Platform::GetAssetPath() / "dragon/dragon.wsmesh");
        }

        if (ImGui::Button("BoxTextured.wsmesh")) {
            startGeneration("BoxTextured",
                            Platform::GetAssetPath() / "BoxTextured.glb",
                            Platform::GetAssetPath() / "BoxTextured.wsmesh");
        }

        if (ImGui::Button("BoxTextured4k.wsmesh")) {
            startGeneration("BoxTextured4k",
                            Platform::GetAssetPath() / "BoxTextured4k.glb",
                            Platform::GetAssetPath() / "BoxTextured4k.wsmesh");
        }
        if (ImGui::Button("Sphere.wsmesh")) {
            startGeneration("Sphere",
                            Platform::GetAssetPath() / "Sphere.glb",
                            Platform::GetAssetPath() / "Sphere.wsmesh");
        }

        if (ImGui::Button("sponza.wsmesh")) {
            startGeneration("sponza",
                            Platform::GetAssetPath() / "sponza2/sponza.gltf",
                            Platform::GetAssetPath() / "sponza2/sponza.wsmesh");
        }
        if (ImGui::Button("plane.wsmesh")) {
            startGeneration("plane",
                            Platform::GetAssetPath() / "Plane.glb",
                            Platform::GetAssetPath() / "Plane.wsmesh");
        }

        ImGui::Separator();
        ImGui::Text("Generate Textures:");

        if (ImGui::Button("kloofendal_environment.ktx2")) {
            modelGenerator->RequestEnvironmentMapGenerate(
                Platform::GetAssetPath() / "environment-map/kloofendal_48d_partly_cloudy_puresky_4k.hdr",
                Platform::GetAssetPath() / "environment-map/kloofendal_48d_partly_cloudy_puresky_4k.ktx2");
        }

        if (ImGui::Button("Generate BRDF LUT, Smiling Friend, and Prototype Texture")) {
            modelGenerator->RequestTextureGenerateFromFile(
                Platform::GetAssetPath() / "textures/smiling_friend.jpg",
                Platform::GetAssetPath() / "textures/smiling_friend.wtexture",
                true,
                DXGI_FORMAT_BC7_UNORM_SRGB);
            modelGenerator->RequestTextureGenerateFromFile(
                Platform::GetAssetPath() / "textures/prototype_texture_dark.png",
                Platform::GetAssetPath() / "textures/prototype_texture_dark.wtexture",
                true,
                DXGI_FORMAT_BC7_UNORM_SRGB);
            modelGenerator->GenerateBRDFLUT(Platform::GetAssetPath() / "textures/brdf_lut.wtexture");
        }

        ImGui::Separator();
        ImGui::Text("Generation Progress:");
        const auto& genProgresses = modelGenerator->GetModelGenerationProgresses();
        for (uint32_t i = 0; i < genProgresses.size(); ++i) {
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

            const std::string modelName = modelGenerator->GetModelGenerateSlotPath(i).stem().string();
            ImGui::Text("Slot %u: %s", i, modelName.empty() ? "-" : modelName.c_str());
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%s (%d%%)", stateLabel, genValue);
            ImGui::ProgressBar(static_cast<float>(genValue) / 100.0f, ImVec2(-1.0f, 0.0f), overlay);
        }

        ImGui::Separator();
        ImGui::Text("Active Generates:");
        ImGui::Text("  Models: %u (%u active)", modelGenerator->GetTotalModelGenerateCount(), modelGenerator->GetActiveModelGenerateCount());
        ImGui::Text("  Textures: %u (%u active)", modelGenerator->GetTotalTextureGenerateCount(), modelGenerator->GetActiveTextureGenerateCount());
        ImGui::Text("Active Loads:");
        ImGui::Text("  Models: %u", asyncAssetLoadManager->GetActiveModelLoadCount());
        ImGui::Text("  Textures: %u", asyncAssetLoadManager->GetActiveTextureLoadCount());
    }
    ImGui::End();

#if LOGGING_ENABLED
    if (ImGui::Begin("Log")) {
        static std::vector<LogEntry> entries;
        engineLogger->GetImGuiSink().GetEntries(entries);

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
        const auto entryCount = static_cast<int32_t>(entries.size());

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
            static std::vector<int> filtered;
            filtered.clear();
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
                    filtered.push_back(i);
            }

            clipper.Begin(static_cast<int>(filtered.size()));
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

    gameState->skybox = assetManager->LoadCubemap("kloofendal"_sid);

    SDL_Event e;
    while (true) {
        ZoneScopedN("EngineFrame");
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
                    engineContext->windowContext.viewportWidth = w;
                    engineContext->windowContext.viewportHeight = h;
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

        inputManager->UpdateFocus(SDL_GetWindowFlags(window.get()));
        timeManager->UpdateGame();


#if !GAME_STATIC
        gameDllWatcher.Poll();
#endif
        shaderWatcher.Poll();

        ResolveLoadResult loadCounts = assetManager->ResolveLoads(stagingFrameBuffer);
        assetManager->ResolveUnloads();
#if WILL_EDITOR
        materialManager->Scan();
        assetManager->Scan();
#endif

        engineContext->bImguiKeyboardCaptured = ImGui::GetIO().WantCaptureKeyboard;
        engineContext->bImguiMouseCaptured = ImGui::GetIO().WantCaptureMouse;
        engineContext->bImGuiWantsTextInput = ImGui::GetIO().WantTextInput;
        engineContext->bModelLoadedThisFrame = loadCounts.modelLoadedCount > 0;

        //
        {
            ZoneScopedN("GameFrame");
            gameState->inputFrame = &inputManager->GetCurrentInput();
            gameState->timeFrame = &timeManager->GetTime();
            gameFunctions.gameUpdate(engineContext, gameState.get());
            inputManager->FrameReset();
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

                Core::FrameBuffer& currentFrameBuffer = engineRenderSynchronization->frameBuffers[frameBufferIndex];
                engineContext->lastKnownStableIdUnderCursor = currentFrameBuffer.stableIdUnderCursor;
                stagingFrameBuffer.currentFrameBuffer = frameBufferIndex;
                stagingFrameBuffer.bFreezeVisibility = bFreezeVisibility;
                stagingFrameBuffer.bLogRDG = bLogRDG;
                stagingFrameBuffer.bDrawImgui = bDrawImgui;

                //
                {
                    ZoneScopedN("SwapchainRecreate");
                    stagingFrameBuffer.swapchainRecreateCommand.bIsMinimized = bMinimized;
                    if (bRequireSwapchainRecreate) {
                        stagingFrameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = true;

                        int32_t w;
                        int32_t h;
                        SDL_GetWindowSize(window.get(), &w, &h);
                        stagingFrameBuffer.swapchainRecreateCommand.windowWidth = w;
                        stagingFrameBuffer.swapchainRecreateCommand.windowHeight = h;
                        bRequireSwapchainRecreate = false;
                    }
                    else {
                        stagingFrameBuffer.swapchainRecreateCommand.bEngineCommandsRecreate = false;
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
                        stagingFrameBuffer.viewportResizeCommand = {
                            true,
                            engineContext->windowContext.viewportOffsetX,
                            engineContext->windowContext.viewportOffsetY,
                            engineContext->windowContext.viewportWidth,
                            engineContext->windowContext.viewportHeight,
                        };
                        bRequireViewportRecreate = false;
                    }
                    else {
                        stagingFrameBuffer.viewportResizeCommand.bEngineCommandsResize = false;
                    }
                }


                glm::uvec2 mousePos = {
                    gameState->inputFrame->mousePositionAbsolute.x - engineContext->windowContext.viewportOffsetX,
                    gameState->inputFrame->mousePositionAbsolute.y - engineContext->windowContext.viewportOffsetY
                };
                mousePos.y = engineContext->windowContext.viewportHeight - 1 - mousePos.y;
                stagingFrameBuffer.currentMousePosition = {(mousePos.x), (mousePos.y)};
                //
                {
                    ZoneScopedN("GamePrepareFrame");
                    gameFunctions.gamePrepareFrame(engineContext, gameState.get(), &stagingFrameBuffer);
                }

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
                engineContext->currentFrame++;
                engineRenderSynchronization->renderFrames.fetch_add(1, std::memory_order_release);
                engineRenderSynchronization->renderCV.notify_one();
            }
        }
    }
}

void WillEngine::PrepareImgui(uint32_t currentFrameBufferIndex)
{
    ZoneScopedN("PreparingImGui")
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
    gameFunctions.gameUnload(engineContext, gameState.get());
    gameFunctions.gameShutdown(engineContext, gameState.get());
    gameState.reset();

#ifndef GAME_STATIC
    gameDll.Unload();
#endif

    scheduler->ShutdownNow();
    engineContext->scheduler = nullptr;
    engineContext->~EngineContext();

    inputManager->~InputManager();

#if WILL_EDITOR
    modelGenerator->Join();
    modelGenerator.reset();
#endif

    physicsSystem.reset();
    materialManager.reset();
    assetManager.reset();

    asyncAssetLoadManager->Join();
    asyncAssetLoadManager.reset();

    audioManager.reset();

    engineRenderSynchronization.reset();

    renderThread->Join();
    renderThread.reset();

    scheduler->~TaskScheduler();

#if LOGGING_ENABLED
    engineLogger->Shutdown();
    engineLogger->~EngineLogger();
#endif
}
}
