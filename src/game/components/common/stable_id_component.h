//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_STABLE_ID_COMPONENT_H
#define WILL_ENGINE_STABLE_ID_COMPONENT_H

#include <random>

#include <entt/entt.hpp>

#include "core/string_id.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
/**
 * Assigns a random 64-bit identifier to an entity that remains stable for the
 * lifetime of that entity within the current session. Unlike entt::entity,
 * which may be recycled, this ID uniquely identifies an entity until it is
 * destroyed and can be used for cross-system lookups via
 * GameState::stableIdToEntityMap.
 *
 * Transient: IDs are NOT serialized or preserved across save/load cycles.
 * A new unique ID is generated each time the component is constructed,
 * so the same logical entity will receive a different StableId every session.
 *
 * The ID is generated via GameState::rng and is guaranteed to be non-zero and
 * unique within the current registry (collisions are re-rolled on construct).
 * OnConstruct/OnUpdate automatically register the mapping; OnDestroy removes it.
 */
struct StableIdComponent
{
    StringID id;

    static StringID Generate(std::mt19937_64& rng)
    {
        return StringID(rng());
    }

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnUpdate(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

}

#endif //WILL_ENGINE_STABLE_ID_COMPONENT_H
