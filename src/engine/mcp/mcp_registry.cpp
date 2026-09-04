//
// Created by William on 2026-09-05.
//

#include "mcp_tool.h"

#include <cassert>
#include <cstring>

#include "engine/engine_api.h"

namespace Engine::MCP
{
static constexpr size_t TOOL_REGISTRY_RESERVE = 64;

ToolRegistry::ToolRegistry(Core::TlsfAllocator* allocator)
{
    tools = Core::Vector<ToolEntry>(allocator, Core::AllocTag::MCPServer, TOOL_REGISTRY_RESERVE);
    mapping = Core::Map<StringID, size_t>(allocator, Core::AllocTag::MCPServer, TOOL_REGISTRY_RESERVE);
}

void RegisterTool(EngineState* state, const ToolEntry& entry)
{
    assert(entry.invoke && entry.name && entry.id.IsValid());
    assert((entry.bNeedsDrain || entry.origin == ToolOrigin::Engine) && "socket-thread MCP tools must be engine-owned, a game function pointer can dangle across a reload");

    ToolRegistry& r = state->mcpTools;
    std::lock_guard lock(r.mutex);

    if (const size_t* existing = r.mapping.Find(entry.id)) {
        assert(strcmp(r.tools[*existing].name, entry.name) == 0 && "MCP tool StringID collision between two differently named tools");
        r.tools[*existing] = entry;
        return;
    }

    r.mapping.Insert(entry.id, r.tools.Size());
    r.tools.PushBack(entry);
}

void ClearGameTools(EngineState* state)
{
    ToolRegistry& r = state->mcpTools;
    std::lock_guard lock(r.mutex);

    for (size_t i = r.tools.Size(); i-- > 0;) {
        if (r.tools[i].origin == ToolOrigin::Game) {
            r.tools.RemoveAt(i);
        }
    }

    r.mapping.Clear();
    for (size_t i = 0; i < r.tools.Size(); ++i) {
        r.mapping.Insert(r.tools[i].id, i);
    }
}
} // Engine::MCP
