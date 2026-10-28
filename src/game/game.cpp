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
#include "physics/physics_world.h"

#include "imgui.h"
#include "ImGuizmo.h"
#include "audio/audio_manager.h"

#include "input/input_action_registry.h"
#include "engine/logging/engine_log.h"
#include "engine/logging/engine_logger.h"
#include "mcp/game_mcp_tools.h"
#include "logging/game_log_category.h"
#include "ballance/ballance.h"
#include "ballance/state.h"
#include "clay/clay.h"

extern "C"
{
GAME_API size_t GameGetStateSize()
{
    return sizeof(Ballance::State);
}

GAME_API void GameStartup(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Start Up");

    new(ctx->gameState) Ballance::State();
}

GAME_API void GameLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Audio::AudioManager::RegisterAudio();
    Ballance::RegisterComponents(state->componentRegistry);
    Ballance::RegisterCommands(state);
    Game::RegisterInputActions(state->input);
    Game::RegisterLogCategories(ctx->engineLogger);
    Game::RegisterMCPTools(state);
    Ballance::ConnectObservers(state->registry);

    ctx->playStartFn = Ballance::PlayStart;
    ctx->playStopFn = Ballance::PlayStop;
}

GAME_API void GameHotReloadSave(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Ballance::DisconnectObservers(state->registry);

    ctx->playStartFn = {};
    ctx->playStopFn = {};
}

GAME_API void GameHotReloadLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Ballance::RegisterComponents(state->componentRegistry);
    Ballance::RegisterCommands(state);
    Game::RegisterInputActions(state->input);
    Game::RegisterLogCategories(ctx->engineLogger);
    Game::RegisterMCPTools(state);
    Ballance::ConnectObservers(state->registry);

    ctx->playStartFn = Ballance::PlayStart;
    ctx->playStopFn = Ballance::PlayStop;
}
}


GAME_API void GameCollect(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemGraph& graph)
{
    Ballance::CollectSystems(graph);
}

GAME_API void GamePrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Game/PrepareFrame"};
    Ballance::PrepareFrame(ctx, state, registry, frameBuffer);
}

GAME_API void GameEndFrame(Engine::EngineContext* ctx, Engine::EngineState* state)
{
}

GAME_API void GameUnload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ctx->playStartFn = {};
    ctx->playStopFn = {};
}

GAME_API void GameShutdown(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Shutdown");

    ctx->GetGameState<Ballance::State>()->~State();

    ImGuizmo::DestroyContext();
}
