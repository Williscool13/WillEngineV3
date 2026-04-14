//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_GAME_INTERFACE_H
#define WILL_ENGINE_GAME_INTERFACE_H

#include "engine_context.h"

namespace Engine
{
struct EngineState;
}

namespace Core
{
struct FrameBuffer;

using GameStartUpFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameLoadFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameUpdateFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GamePrepareFrameFunc = void(*)(Engine::EngineContext*, Engine::EngineState*, FrameBuffer*);
using GameUnloadFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameShutdownFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);

void StubStartup(Engine::EngineContext* ctx, Engine::EngineState* state);

void StubLoad(Engine::EngineContext* ctx, Engine::EngineState* state);

void StubUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);

void StubPrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, FrameBuffer* frameBuffer);

void StubUnload(Engine::EngineContext* ctx, Engine::EngineState* state);

void StubShutdown(Engine::EngineContext* ctx, Engine::EngineState* state);

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
GAME_API void GameStartup(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Called once every time the dll is loaded, including on application start after GameStartup.
 * @param ctx
 * @param state
 */
GAME_API void GameLoad(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Called every tick. This is executed by the main engine loop.
 * @param ctx
 * @param state
 */
GAME_API void GameUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Called before frame buffer is sent directly to the render thread to be drawn.
 * @param ctx
 * @param state
 * @param frameBuffer
 */
GAME_API void GamePrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);

/**
 * Called before unloading DLL during hot-reload. Clean up DLL-specific resources.
 * @param ctx
 * @param state
 */
GAME_API void GameUnload(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Called once on application exit after GameUnload.
 * @param ctx
 * @param state
 */
GAME_API void GameShutdown(Engine::EngineContext* ctx, Engine::EngineState* state);
}

#endif // WILL_ENGINE_GAME_INTERFACE_H
