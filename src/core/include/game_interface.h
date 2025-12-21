//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_GAME_INTERFACE_H
#define WILL_ENGINE_GAME_INTERFACE_H

#include "engine_context.h"

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;

using GameStartUpFunc = void(*)(EngineContext*, Engine::GameState*);
using GameLoadFunc = void(*)(EngineContext*, Engine::GameState*);
using GameUpdateFunc = void(*)(EngineContext*, Engine::GameState*);
using GamePrepareFrameFunc = void(*)(EngineContext*, Engine::GameState*, FrameBuffer*);
using GameUnloadFunc = void(*)(EngineContext*, Engine::GameState*);
using GameShutdownFunc = void(*)(EngineContext*, Engine::GameState*);

void StubStartup(EngineContext* ctx, Engine::GameState* state);

void StubLoad(EngineContext* ctx, Engine::GameState* state);

void StubUpdate(EngineContext* ctx, Engine::GameState* state);

void StubPrepareFrame(EngineContext* ctx, Engine::GameState* state, FrameBuffer* frameBuffer);

void StubUnload(EngineContext* ctx, Engine::GameState* state);

void StubShutdown(EngineContext* ctx, Engine::GameState* state);

struct GameAPI
{
    GameStartUpFunc gameStartup;
    GameLoadFunc gameLoad;
    GameUpdateFunc gameUpdate;
    GamePrepareFrameFunc gamePrepareFrame;
    GameUnloadFunc gameUnload;
    GameShutdownFunc gameShutdown;

    void Stub()
    {
        gameStartup = StubStartup;
        gameLoad = StubLoad;
        gameUpdate = StubUpdate;
        gamePrepareFrame = StubPrepareFrame;
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
/**
 * Called once when the application starts. Will not be called again during hot-reload.
 * @param ctx
 * @param state
 */
GAME_API void GameStartup(Core::EngineContext* ctx, Engine::GameState* state);

/**
 * Called once every time the dll is loaded, including on application start after GameStartup.
 * @param ctx
 * @param state
 */
GAME_API void GameLoad(Core::EngineContext* ctx, Engine::GameState* state);

/**
 * Called every tick. This is executed by the main engine loop.
 * @param ctx
 * @param state
 */
GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state);

/**
 * Called before frame buffer is sent directly to the render thread to be drawn.
 * @param ctx
 * @param state
 * @param frameBuffer
 */
GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);

/**
 * Called before unloading DLL during hot-reload. Clean up DLL-specific resources.
 * @param ctx
 * @param state
 */
GAME_API void GameUnload(Core::EngineContext* ctx, Engine::GameState* state);

/**
 * Called once on application exit after GameUnload.
 * @param ctx
 * @param state
 */
GAME_API void GameShutdown(Core::EngineContext* ctx, Engine::GameState* state);
}

#endif // WILL_ENGINE_GAME_INTERFACE_H
