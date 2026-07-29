//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_STABLE_ID_COMPONENT_H
#define WILL_ENGINE_STABLE_ID_COMPONENT_H

#include <random>

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "core/string_id.h"
#include "engine/engine_api.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
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

    StringID id;
    uint64_t sortOrder{0};

    static StringID Generate(std::mt19937_64& rng)
    {
        return StringID(rng());
    }

    static void Serialize(const StableIdComponent& comp, nlohmann::json& json);
    static void Deserialize(StableIdComponent& comp, const nlohmann::json& json);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnUpdate(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

}

#endif //WILL_ENGINE_STABLE_ID_COMPONENT_H
