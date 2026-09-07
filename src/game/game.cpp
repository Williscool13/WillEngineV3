//
// Created by William on 2025-12-14.
//

#include <tracy/Tracy.hpp>

#include "spdlog/spdlog.h"

#include "engine/include/game_interface.h"
#include "engine/systems/system_graph.h"
#include "../render/interface/render_interface.h"
#include "engine/input/input_frame.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"

#include "imgui.h"
#include "audio/audio_manager.h"

#include "fwd_components.h"
#include "components/component_registration.h"
#include "components/player_spawn_component.h"
#include "input/input_action_registry.h"
#include "engine/logging/engine_log.h"
#include "engine/logging/engine_logger.h"
#include "systems/debug_system.h"
#include "game_state.h"
#include "mcp/game_mcp_tools.h"
#include "logging/game_log_category.h"
#include "gameplay/player/physics_player_controller.h"
#include "systems/gameplay_systems.h"
#include "clay/clay.h"

#ifndef GAME_STATIC
#include <enkiTS/src/TaskScheduler.h>
#include "meshoptimizer/src/meshoptimizer.h"
#include "par/par_shapes_ext.h"
#include "asset-load/asset-load-jobs/text3d_geometry.h"
#include "core/memory/concurrent_queue_traits.h"
#include "core/memory/memory_manager.h"

static Core::MemoryManager* gDllMemory = nullptr;

static void* DllMeshoptAlloc(size_t size)
{
    return gDllMemory->AssetsScratch().Alloc(size, Core::AllocTag::Meshopt);
}

static void DllMeshoptFree(void* ptr)
{
    gDllMemory->AssetsScratch().Free(ptr);
}

static void RegisterDllEngineHooks(Engine::EngineContext* ctx)
{
    if (ctx->engineLogger) {
        ctx->engineLogger->RegisterLoggersForDLL(Engine::LogCategory::Game);
    }

    ImGui::SetCurrentContext(ctx->imguiContext);
    ImGui::SetAllocatorFunctions(ctx->imguiAllocFn, ctx->imguiFreeFn, ctx->imguiAllocUserData);
    Clay_SetCurrentContext(ctx->clayContext);

    ctx->physicsSystem->RegisterPhysics();
    ctx->scheduler->RegisterExternalTaskThread();

    gDllMemory = ctx->memoryManager;
    meshopt_setAllocator(DllMeshoptAlloc, DllMeshoptFree);
    par_shapes_set_allocator(&ctx->memoryManager->AssetsScratch());
    AssetLoad::SetEarcutAllocator(&ctx->memoryManager->AssetsScratch());
    Core::SetConcurrentQueueAllocator(&ctx->memoryManager->General(), Core::AllocTag::Queue);
}
#endif

static void GamePlayStart(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    glm::vec3 spawnPosition{0.0f, 3.0f, 0.0f};
    int32_t bestPriority = INT32_MIN;
    auto spawnView = state->registry.view<Game::Component::PlayerSpawnComponent, Game::Component::TransformComponent>();
    for (auto entity : spawnView) {
        auto& spawn = spawnView.get<Game::Component::PlayerSpawnComponent>(entity);
        if (spawn.priority > bestPriority) {
            bestPriority = spawn.priority;
            spawnPosition = spawnView.get<Game::Component::TransformComponent>(entity).translation + spawn.offset;
        }
    }

    ctx->GetGameState<Game::GameState>()->playerController.Initialize(state, ctx, spawnPosition);
}

static void GamePlayStop(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Game::PhysicsPlayerController& playerController = ctx->GetGameState<Game::GameState>()->playerController;
    if (playerController.GetCharacter()) {
        playerController.Shutdown(ctx->physicsSystem);
    }
}

extern "C"
{
GAME_API size_t GameGetStateSize()
{
    return sizeof(Game::GameState);
}

GAME_API void GameStartup(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Start Up");

    new(ctx->gameState) Game::GameState();
}

GAME_API void GameLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    RegisterDllEngineHooks(ctx);
#endif

    Audio::AudioManager::RegisterAudio();
    Game::RegisterGameComponents(state->componentRegistry);
    Game::RegisterInputActions(state->input);
    Game::RegisterLogCategories(ctx->engineLogger);
    Game::RegisterMCPTools(state);
    Game::ConnectGameplayObservers(state->registry);

    ctx->playStartFn = GamePlayStart;
    ctx->playStopFn = GamePlayStop;

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif
}

GAME_API void GameHotReloadSave(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Game::DisconnectGameplayObservers(state->registry);

    ctx->playStartFn = {};
    ctx->playStopFn = {};

#ifndef GAME_STATIC
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
    ctx->physicsSystem->UnregisterPhysics();
#endif
}

GAME_API void GameHotReloadLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    RegisterDllEngineHooks(ctx);
#endif

    Game::RegisterGameComponents(state->componentRegistry);
    Game::RegisterInputActions(state->input);
    Game::RegisterLogCategories(ctx->engineLogger);
    Game::RegisterMCPTools(state);
    Game::ConnectGameplayObservers(state->registry);

    ctx->playStartFn = GamePlayStart;
    ctx->playStopFn = GamePlayStop;

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif
}
}


GAME_API void GameCollect(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemGraph& graph)
{
    graph.Add("TickQuietFrames", &Game::TickQuietFrames);

    if (state->inputContext == Engine::InputContext::Gameplay) {
        graph.Add("DebugProcessPhysicsCollisions", &Game::DebugProcessPhysicsCollisions);
        graph.Add("DebugApplyGroundForces", &Game::DebugApplyGroundForces);

        graph.Add("UpdatePathMovers", &Game::UpdatePathMovers);
        graph.Add("UpdateRotateInPlace", &Game::UpdateRotateInPlace);
        graph.Add("CheckpointUpdate", &Game::CheckpointUpdate);
        graph.Add("DeathZoneUpdate", &Game::DeathZoneUpdate);

        graph.Add("PlayerControllerUpdate", [](Engine::EngineContext* ctx, Engine::EngineState* state) {
            Game::PhysicsPlayerController& playerController = ctx->GetGameState<Game::GameState>()->playerController;
            if (playerController.GetCharacter()) {
                playerController.Update(ctx, state);
            }
        });
    }

    graph.Add("DebugUpdate", &Game::DebugUpdate);
}

GAME_API void GamePrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
#ifdef WDEBUG
    Game::DebugRender(ctx, state, frameBuffer);
#endif
}

GAME_API void GameEndFrame(Engine::EngineContext* ctx, Engine::EngineState* state)
{
}

GAME_API void GameUnload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ctx->playStartFn = {};
    ctx->playStopFn = {};

#ifndef GAME_STATIC
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
    ctx->physicsSystem->UnregisterPhysics();
#endif
}

GAME_API void GameShutdown(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Shutdown");

    ctx->GetGameState<Game::GameState>()->~GameState();
}
