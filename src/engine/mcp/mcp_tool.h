//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_MCP_TOOL_H
#define WILL_ENGINE_MCP_TOOL_H

#include <cstdint>
#include <mutex>

#include "core/containers/map.h"
#include "core/containers/vector.h"
#include "core/string_id.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Engine::MCP
{
enum class ToolResult : uint8_t
{
    Complete,
    Error,
};

enum class ToolOrigin : uint8_t
{
    Engine,
    Game,
};

class Call;

using ToolFn = ToolResult(*)(EngineContext* ctx, EngineState* state, Call& call);

struct ToolEntry
{
    StringID id{};
    const char* name{};
    const char* description{};
    /** JSON Schema object for the tool's arguments; nullptr means "no arguments". */
    const char* inputSchemaJson{};
    ToolFn invoke{};
    ToolOrigin origin{ToolOrigin::Engine};
    /** false serves the call on the socket thread; only allowed for Engine-owned tools. */
    bool bNeedsDrain{true};
    bool bLogMarkers{false};
};

/**
 * Engine-owned tool table the game populates on load and hot reload, mirroring ComponentRegistry.
 * Mutated on the engine thread only; the socket thread reads under `mutex`. Game entries are cleared
 * before the DLL unloads so no reader can observe a name pointer into an unmapped module.
 */
struct ToolRegistry
{
    ToolRegistry() = default;

    explicit ToolRegistry(Core::TlsfAllocator* allocator);

    Core::Vector<ToolEntry> tools{};
    Core::Map<StringID, size_t> mapping{};
    std::mutex mutex;
};

/** Engine thread only. Asserts on a StringID collision between two differently named tools. */
void RegisterTool(EngineState* state, const ToolEntry& entry);

/** Engine thread only. Call before the game DLL unloads. */
void ClearGameTools(EngineState* state);

/**
 * Argument reader and result writer handed to a tool handler. Handlers never see the JSON library.
 * Results are built as a tree: Set adds a member to the current object, Push appends to the current
 * array, BeginObject/BeginArray/PushObject/PushArray open a nested container and End() closes it. The root is an object.
 */
class Call
{
public:
    struct Impl;

    explicit Call(Impl* impl_) : impl(impl_) {}

    [[nodiscard]] bool HasArg(const char* key) const;
    [[nodiscard]] const char* GetString(const char* key, const char* fallback = "") const;
    [[nodiscard]] int64_t GetInt(const char* key, int64_t fallback = 0) const;
    [[nodiscard]] double GetFloat(const char* key, double fallback = 0.0) const;
    [[nodiscard]] bool GetBool(const char* key, bool fallback = false) const;

    void SetString(const char* key, const char* value);
    void SetInt(const char* key, int64_t value);
    void SetFloat(const char* key, double value);
    void SetBool(const char* key, bool value);
    void SetNull(const char* key);
    void BeginObject(const char* key);
    void BeginArray(const char* key);

    void PushString(const char* value);
    void PushInt(int64_t value);
    void PushFloat(double value);
    void PushBool(bool value);
    void PushObject();
    void PushArray();

    void End();

    /** Marks the call failed; the message becomes the tool's error text. */
    void SetError(const char* message);

private:
    Impl* impl{};
};
} // Engine::MCP

#endif //WILL_ENGINE_MCP_TOOL_H
