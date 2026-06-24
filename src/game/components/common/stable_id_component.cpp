//
// Created by William on 2026-03-21.
//

#include "stable_id_component.h"

#include <json/nlohmann/json.hpp>

#include "engine/engine_api.h"

namespace Game::Component
{

void StableIdComponent::Serialize(const StableIdComponent& comp, nlohmann::json& json)
{
    json["id"] = comp.id.id;
    json["sortOrder"] = comp.sortOrder;
}

void StableIdComponent::Deserialize(StableIdComponent& comp, const nlohmann::json& json)
{
    if (json.contains("id")) {
        comp.id = StringID(json["id"].get<uint64_t>());
    }
    // else id remains 0, OnConstruct will generate a new one
    if (json.contains("sortOrder")) {
        comp.sortOrder = json["sortOrder"].get<uint64_t>();
    }
}
void StableIdComponent::OnUpdate(entt::registry& registry, entt::entity entity)
{
    assert(false && "StableIdComponent should never be updated");
}

void StableIdComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<StableIdComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();

    while (comp.id.id == 0 || state->stableIdToEntityMap.Contains(comp.id)) {
        comp.id = Generate(state->rng);
    }
    state->stableIdToEntityMap[comp.id] = entity;
}


void StableIdComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<StableIdComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->stableIdToEntityMap.Remove(comp.id);
}

} // Game::Component

namespace Game
{
Engine::ComponentEditorResult Component::StableIdComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry,
                                                       entt::entity entity, const char* name)
{
    auto& component = registry.get<Component::StableIdComponent>(entity);
    char headerLabel[64];
    snprintf(headerLabel, sizeof(headerLabel), "Stable ID: %llu", component.id.id);
    ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_Leaf);
    return {};
}
} // Game
