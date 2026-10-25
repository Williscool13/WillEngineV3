//
// Created by William on 2026-10-17.
//

#ifndef WILL_ENGINE_BALLANCE_BREAKABLE_COMPONENT_H
#define WILL_ENGINE_BALLANCE_BREAKABLE_COMPONENT_H

#include <entt/entt.hpp>

#include "core/containers/inline_vector.h"
#include "engine/asset_manager_types.h"
#include "engine/component_registry.h"
#include "engine/core/model_id.h"
#include "engine/reflection/reflection.h"
#include "game/ballance/fwd_components.h"

namespace Ballance::Component
{
struct BreakableComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "BreakableComponent";

    Engine::ModelID brokenModelId{};
    /** m/s each shard gains away from the centre of mass. */
    float scatterSpeed{1.0f};
    /** rad/s of spin each shard gains about a random axis, scaled by a random 0.5..1. */
    float scatterSpin{3.0f};

    WILL_REFLECT(BreakableComponent,
        WILL_FIELD(brokenModelId),
        WILL_FIELD(scatterSpeed, .speed = 0.05f),
        WILL_FIELD(scatterSpin, .speed = 0.1f))
};

struct BreakableRuntime
{
    Engine::StaticModelHandle model{};
    Core::InlineVector<Engine::PhysicsColliderHandle, 32> colliders{};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};

struct ShatterOnContactTag
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "ShatterOnContactTag";
};

struct ShardTag
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "ShardTag";
};

struct SelfDestructComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "SelfDestructComponent";

    float delay{2.0f};

    WILL_REFLECT(SelfDestructComponent,
        WILL_FIELD(delay, .speed = 0.1f))
};

struct SelfDestructRuntime
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "SelfDestructRuntime";

    float elapsed{0.0f};

    WILL_REFLECT(SelfDestructRuntime, WILL_FIELD(elapsed))
};
}

#endif //WILL_ENGINE_BALLANCE_BREAKABLE_COMPONENT_H
