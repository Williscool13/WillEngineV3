//
// Created by William on 2026-03-21.
//

#include "stable_id_component.h"

#include "engine/engine_api.h"

namespace Game::Component
{

void StableIdComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<StableIdComponent>(entity);
    auto* state = registry.ctx().get<Engine::GameState*>();
    if (comp.id.id == 0) {
        comp.id = Generate(state->rng);
    }
    state->stableIdToEntityMap[comp.id] = entity;
}

void StableIdComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<StableIdComponent>(entity);
    auto* state = registry.ctx().get<Engine::GameState*>();
    state->stableIdToEntityMap.erase(comp.id);
}

} // Game::Component

namespace Game
{
template<>
Component::StableIdComponent CopyComponent(const Component::StableIdComponent& src, entt::registry& dstReg)
{
    return Component::StableIdComponent{};
}
template<>
ComponentEditorResult DrawComponentEditor<Component::StableIdComponent>(Component::StableIdComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry,
                                                       entt::entity entity, const char* name)
{
    char headerLabel[64];
    snprintf(headerLabel, sizeof(headerLabel), "Stable ID: %llu", component.id.id);
    ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletestableid");
    ImGui::PopStyleColor();
    return {.requestRemoval = remove};
}
} // Game
