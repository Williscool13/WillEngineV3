//
// Created by William on 2026-09-07.
//

#ifndef WILL_ENGINE_SYSTEM_GRAPH_H
#define WILL_ENGINE_SYSTEM_GRAPH_H

#include <cstdint>

#include <entt/entt.hpp>

#include "core/string_id.h"
#include "core/containers/inline_vector.h"

namespace Core
{
struct FrameBuffer;
}

namespace Engine
{
struct EngineContext;
struct EngineState;

inline constexpr uint32_t MAX_SYSTEM_NODES = 128;
inline constexpr uint32_t MAX_SYSTEM_ACCESS = 16;

enum class SystemPhase : uint8_t
{
    PreUpdate,
    GameUpdate,
    PostUpdate,
    PrepareFrame,
};

using SystemFn = void(*)(EngineContext*, EngineState*);
using SystemFrameFn = void(*)(EngineContext*, EngineState*, Core::FrameBuffer*);

template<typename T>
constexpr uint32_t ComponentAccessId() { return entt::type_hash<T>::value(); }

struct SystemAccess
{
    bool bExclusive{true};
    Core::InlineVector<uint32_t, MAX_SYSTEM_ACCESS> componentReads{};
    Core::InlineVector<uint32_t, MAX_SYSTEM_ACCESS> componentWrites{};
    Core::InlineVector<StringID, MAX_SYSTEM_ACCESS> resourceReads{};
    Core::InlineVector<StringID, MAX_SYSTEM_ACCESS> resourceWrites{};
};

struct SystemNode
{
    const char* name{};
    SystemFn fn{};
    SystemFrameFn frameFn{};
    SystemPhase phase{SystemPhase::PreUpdate};
    SystemAccess access{};
};

/**
 * Per-frame system schedule rebuilt every tick.
 */
class SystemGraph
{
public:
    void BeginFrame()
    {
        nodes.Clear();
        cursor = 0;
    }

    void BeginPhase(SystemPhase phase) { currentPhase = phase; }

    void Add(const char* name, SystemFn fn, const SystemAccess& access = {})
    {
        nodes.PushBack({.name = name, .fn = fn, .phase = currentPhase, .access = access});
    }

    void Add(const char* name, SystemFrameFn fn, const SystemAccess& access = {})
    {
        nodes.PushBack({.name = name, .frameFn = fn, .phase = currentPhase, .access = access});
    }

    void ExecutePhase(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
    {
        while (cursor < nodes.Size()) {
            const SystemNode& node = nodes[cursor];
            ++cursor;
            if (node.frameFn != nullptr) { node.frameFn(ctx, state, frameBuffer); }
            else { node.fn(ctx, state); }
        }
    }

private:
    Core::InlineVector<SystemNode, MAX_SYSTEM_NODES> nodes{};
    SystemPhase currentPhase{SystemPhase::PreUpdate};
    size_t cursor{};
};
} // Engine

#endif //WILL_ENGINE_SYSTEM_GRAPH_H
