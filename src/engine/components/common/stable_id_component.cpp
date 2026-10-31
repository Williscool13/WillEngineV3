//
// Created by William on 2026-03-21.
//

#include "stable_id_component.h"

#include "engine/engine_api.h"
#include "engine/editor/edit_context.h"

namespace Engine::Component
{
void StableIdComponent::OnUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity)
{
    assert(false && "StableIdComponent should never be updated");
}

void StableIdComponent::OnConstruct(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& comp = registry.GetMut<StableIdComponent>(entity);

    while (comp.id.id == 0 || state->stableIdToEntityMap.Contains(comp.id)) {
        comp.id = Generate(state->rng);
    }
    state->stableIdToEntityMap[comp.id] = entity;
}


void StableIdComponent::OnDestroy(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity)
{
    const auto& comp = registry.Get<StableIdComponent>(entity);
    state->stableIdToEntityMap.Remove(comp.id);
}

} // Engine::Component

namespace Engine
{
Engine::ComponentEditorResult Component::StableIdComponent::DrawEditor(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::EditContext& edit, const char* name)
{
    const auto& component = edit.Get<Component::StableIdComponent>();
    char headerLabel[64];
    snprintf(headerLabel, sizeof(headerLabel), "Stable ID: %llu", component.id.id);
    ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_Leaf);
    return {};
}
} // Engine
