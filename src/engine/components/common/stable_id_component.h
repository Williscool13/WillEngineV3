//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_STABLE_ID_COMPONENT_H
#define WILL_ENGINE_STABLE_ID_COMPONENT_H

#include "core/math/random.h"
#include "core/string_id.h"
#include "engine/core/registry.h"
#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "engine/reflection/reflection.h"


namespace Engine::Component
{
/**
 * Assigns a persistent 64-bit identifier to an entity. Unlike entt::entity,
 * which may be recycled, this ID survives save/load cycles and play/stop and
 * can be used for cross-system lookups via GameState::stableIdToEntityMap.
 *
 * On construct: if id is 0 (new entity) or already taken (double-load, prefab
 * copy), a new unique ID is generated. Otherwise the deserialized ID is kept.
 * OnConstruct/OnUpdate automatically register the mapping; OnDestroy removes it.
 */
struct StableIdComponent
{
    static constexpr const char* COMPONENT_NAME = "StableIdComponent";
    static constexpr bool INSTANCE_ONLY = true;

    static constexpr uint64_t NO_ID = 1234567890;

    StringID id;
    uint64_t sortOrder{0};

    static StringID Generate(Core::Random& rng)
    {
        return StringID(rng());
    }

    WILL_REFLECT(StableIdComponent, WILL_FIELD(id), WILL_FIELD(sortOrder))

    static void OnConstruct(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static void OnUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static void OnDestroy(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::EditContext& edit, const char* name);
};

}

#endif //WILL_ENGINE_STABLE_ID_COMPONENT_H
