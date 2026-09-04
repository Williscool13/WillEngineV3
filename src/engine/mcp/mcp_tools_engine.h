//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_MCP_TOOLS_ENGINE_H
#define WILL_ENGINE_MCP_TOOLS_ENGINE_H

namespace Engine
{
struct EngineState;
}

namespace Engine::MCP
{
/** Registers the engine-owned tools. Call once after EngineState and EngineContext are wired. */
void RegisterEngineTools(EngineState* state);
} // Engine::MCP

#endif //WILL_ENGINE_MCP_TOOLS_ENGINE_H
