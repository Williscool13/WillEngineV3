//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_GAME_INTERFACE_H
#define WILL_ENGINE_GAME_INTERFACE_H

#include "engine_context.h"
#include "core/input/input_frame.h"

namespace Core
{
struct GameState
{
    float data;
};

using GameInitFunc = void(*)(EngineContext*, GameState*);
using GameUpdateFunc = void(*)(EngineContext*, GameState*, InputFrame*, float);
using GameShutdownFunc = void(*)(EngineContext*, GameState*);

void StubInit(EngineContext*, GameState* state);

void StubUpdate(EngineContext*, GameState* state, InputFrame* inputFrame, float deltaTime);

void StubShutdown(EngineContext*, GameState* state);

struct GameAPI
{
    GameInitFunc gameInit;
    GameUpdateFunc gameUpdate;
    GameShutdownFunc gameShutdown;

    void Stub()
    {
        gameInit = StubInit;
        gameUpdate = StubUpdate;
        gameShutdown = StubShutdown;
    }
};
} // Core

#ifdef GAME_EXPORTS
#define GAME_API __declspec(dllexport)
#else
#define GAME_API __declspec(dllimport)
#endif

extern "C" {
    GAME_API void GameInit(Core::EngineContext* ctx, Core::GameState* state);
    GAME_API void GameUpdate(Core::EngineContext* ctx, Core::GameState* state, InputFrame* inputFrame, float dt);
    GAME_API void GameShutdown(Core::EngineContext* ctx, Core::GameState* state);
}

#endif // WILL_ENGINE_GAME_INTERFACE_H
