//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_COMMAND_QUEUE_H
#define WILL_ENGINE_COMMAND_QUEUE_H

#include <entt/entt.hpp>

#include "core/containers/vector.h"
#include "engine/asset_manager_types.h"

namespace Engine
{
struct EngineContext;
struct EngineState;

enum class CommandType : uint32_t
{
    None,
    PhysicsBodyConstruct,
    PhysicsBodyRemove,
    ColliderRelease,
    BodyDestroy,
    MeshRelease,
    AreaLightConstruct,
    SphereLightConstruct,
    LightSlotFree,
    CubemapRelease,
    FontRelease,
    ProbeConstruct,
    TextConstruct,
    DestroyEntity,
};

struct MeshReleasePayload
{
    uint32_t rangeOffset;
    uint32_t rangeCount;
    uint32_t modelRangeOffset;
    uint32_t modelRangeCount;
    StaticModelHandle modelHandle;
};

struct Command
{
    CommandType type{CommandType::None};
    entt::entity entity{entt::null};
    union
    {
        uint64_t raw;
        PhysicsColliderHandle colliderHandle;
        uint32_t bodyId;
        MeshReleasePayload meshRelease;
        uint32_t lightSlot;
        CubemapHandle cubemapHandle;
        FontHandle fontHandle;
    } payload{};
};

struct CommandQueue
{
    CommandQueue() = default;

    explicit CommandQueue(Core::TlsfAllocator* allocator)
        : commands(allocator, Core::AllocTag::EngineState)
    {}

    void Push(const Command& command) { commands.PushBack(command); }

    Core::Vector<Command> commands;
};

void PlaybackCommands(EngineContext* ctx, EngineState* state);
} // Engine

#endif //WILL_ENGINE_COMMAND_QUEUE_H
