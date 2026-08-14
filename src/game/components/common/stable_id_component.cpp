//
// Created by William on 2026-03-21.
//

#include "stable_id_component.h"

#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

namespace Game::Component
{

void StableIdComponent::Serialize(const StableIdComponent& comp, Engine::TextWriter& w)
{
    w.KeyOpt("id", comp.id.id, uint64_t{0});
    w.KeyOpt("sortOrder", comp.sortOrder, uint64_t{0});
}

void StableIdComponent::Deserialize(StableIdComponent& comp, const Engine::TextReader& r)
{
    // id 0 stays 0; OnConstruct generates a new one
    comp.id = StringID(r.U64("id", comp.id.id));
    comp.sortOrder = r.U64("sortOrder", comp.sortOrder);
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
