//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_GAME_MCP_TOOLS_H
#define WILL_ENGINE_GAME_MCP_TOOLS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game
{
void RegisterMCPTools(Engine::EngineState* state);

void TickQuietFrames(Engine::EngineContext* ctx, Engine::EngineState* state);
} // Game

#endif //WILL_ENGINE_GAME_MCP_TOOLS_H
