//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_GAME_INTERFACE_H
#define WILL_ENGINE_GAME_INTERFACE_H

#include "engine_context.h"

namespace Engine
{
struct EngineState;
class SystemGraph;
}

namespace Core
{
struct FrameBuffer;

using GameGetStateSizeFunc = size_t(*)();
using GameStartUpFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameLoadFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameCollectFunc = void(*)(Engine::EngineContext*, Engine::EngineState*, Engine::SystemGraph&);
using GamePrepareFrameFunc = void(*)(Engine::EngineContext*, Engine::EngineState*, FrameBuffer*);
using GameEndFrameFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameUnloadFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameHotReloadSaveFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameHotReloadLoadFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);
using GameShutdownFunc = void(*)(Engine::EngineContext*, Engine::EngineState*);

size_t StubGetStateSize();

void StubStartup(Engine::EngineContext* ctx, Engine::EngineState* state);

void StubLoad(Engine::EngineContext* ctx, Engine::EngineState* state);

void StubCollect(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemGraph& graph);

void StubPrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, FrameBuffer* frameBuffer);

void StubEndFrame(Engine::EngineContext* ctx, Engine::EngineState* state);

void StubUnload(Engine::EngineContext* ctx, Engine::EngineState* state);

void StubShutdown(Engine::EngineContext* ctx, Engine::EngineState* state);

struct GameAPI
{
    GameGetStateSizeFunc gameGetStateSize;
    GameStartUpFunc gameStartup;
    GameLoadFunc gameLoad;
    GameCollectFunc gameCollect;
    GamePrepareFrameFunc gamePrepareFrame;
    GameEndFrameFunc gameEndFrame;
    GameUnloadFunc gameUnload;
    GameShutdownFunc gameShutdown;
    GameHotReloadSaveFunc gameHotReloadSave;
    GameHotReloadLoadFunc gameHotReloadLoad;

    void Stub()
    {
        gameGetStateSize = StubGetStateSize;
        gameStartup = StubStartup;
        gameLoad = StubLoad;
        gameCollect = StubCollect;
        gamePrepareFrame = StubPrepareFrame;
        gameEndFrame = StubEndFrame;
        gameUnload = StubUnload;
        gameShutdown = StubShutdown;
        gameHotReloadSave = StubUnload;
        gameHotReloadLoad = StubLoad;
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
* Called once, before GameStartup, so the engine can reserve GameState's persistent storage.
* Engine owned memory, should return same value across hot-reload or an engine assert will be tripped.
 * @return
 */
GAME_API size_t GameGetStateSize();

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
 * Called every tick to add the game's systems to the frame's SystemGraph. The engine executes the phase after this returns.
 * @param ctx
 * @param state
 * @param graph
 */
GAME_API void GameCollect(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemGraph& graph);

/**
 * Called before frame buffer is sent directly to the render thread to be drawn.
 * @param ctx
 * @param state
 * @param frameBuffer
 */
GAME_API void GamePrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);

/**
 * Called after the frame buffer is swapped. Game resets its per-frame arenas here.
 * @param ctx
 * @param state
 */
GAME_API void GameEndFrame(Engine::EngineContext* ctx, Engine::EngineState* state);

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

/**
 * Called before hot-reload DLL unload. Snapshots live scene state and disconnects all observers while vtables are still valid.
 * If PIE is active it must be stopped first by the caller.
 * @param ctx
 * @param state
 */
GAME_API void GameHotReloadSave(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Called after the new hot-reloaded DLL is loaded. Reconnects observers and restores the snapshot. Does not load the default scene.
 * @param ctx
 * @param state
 */
GAME_API void GameHotReloadLoad(Engine::EngineContext* ctx, Engine::EngineState* state);
}

#endif // WILL_ENGINE_GAME_INTERFACE_H
