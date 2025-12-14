//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_GAME_INTERFACE_H
#define WILL_ENGINE_GAME_INTERFACE_H

#include "engine_context.h"
#include "core/input/input_frame.h"
#include "core/time/time_frame.h"

namespace Game
{
struct GameState;
}

namespace Core
{
using GameGetGameStateSizeFunc = size_t(*)();
using GameStartUpFunc = void(*)(EngineContext*, Game::GameState*);
using GameLoadFunc = void(*)(EngineContext*, Game::GameState*);
using GameUpdateFunc = void(*)(EngineContext*, Game::GameState*, InputFrame, const TimeFrame*);
using GameUnloadFunc = void(*)(EngineContext*, Game::GameState*);
using GameShutdownFunc = void(*)(EngineContext*, Game::GameState*);

size_t StubGetGameStateSize();

void StubStartup(EngineContext*, Game::GameState* state);

void StubLoad(EngineContext*, Game::GameState* state);

void StubUpdate(EngineContext*, Game::GameState* state, InputFrame inputFrame, const TimeFrame* timeFrame);

void StubUnload(EngineContext*, Game::GameState* state);

void StubShutdown(EngineContext*, Game::GameState* state);

struct GameAPI
{
    GameGetGameStateSizeFunc gameGetStateSize;
    GameStartUpFunc gameStartup;
    GameLoadFunc gameLoad;
    GameUpdateFunc gameUpdate;
    GameUnloadFunc gameUnload;
    GameShutdownFunc gameShutdown;

    void Stub()
    {
        gameGetStateSize = StubGetGameStateSize;
        gameStartup = StubStartup;
        gameLoad = StubLoad;
        gameUpdate = StubUpdate;
        gameUnload = StubUnload;
        gameShutdown = StubShutdown;
    }
};
} // Core

#ifdef GAME_STATIC
#define GAME_API
#else
#ifdef GAME_EXPORTS
#define GAME_API __declspec(dllexport)
#else
#define GAME_API __declspec(dllimport)
#endif
#endif

extern "C"
{
GAME_API size_t GameGetStateSize();

/**
 * Called once when the application starts. Will not be called again during hot-reload.
 * @param ctx
 * @param state
 */
GAME_API void GameStartup(Core::EngineContext* ctx, Game::GameState* state);

/**
 * Called once every time the dll is loaded, including on application start after GameStartup.
 * @param ctx
 * @param state
 */
GAME_API void GameLoad(Core::EngineContext* ctx, Game::GameState* state);

/**
 * Called every tick. This is executed by the main engine loop.
 * @param ctx
 * @param state
 * @param inputFrame
 * @param timeFrame
 */
GAME_API void GameUpdate(Core::EngineContext* ctx, Game::GameState* state, InputFrame inputFrame, const TimeFrame* timeFrame);

/**
 * Called before unloading DLL during hot-reload. Clean up DLL-specific resources.
 * @param ctx
 * @param state
 */
GAME_API void GameUnload(Core::EngineContext* ctx, Game::GameState* state);

/**
 * Called once on application exit after GameUnload.
 * @param ctx
 * @param state
 */
GAME_API void GameShutdown(Core::EngineContext* ctx, Game::GameState* state);
}

#endif // WILL_ENGINE_GAME_INTERFACE_H
