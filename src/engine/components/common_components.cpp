//
// Created by William on 2026-02-26.
//

#include "common_components.h"

#include "imgui.h"

#include "engine/component_registry.h"
#include "engine/components/component_editor.h"

namespace Engine::Component
{


Engine::ComponentEditorResult NameComponent::DrawEditor(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::EditContext& edit, const char* name)
{
    if (ImGui::CollapsingHeader("Name##componentname", ImGuiTreeNodeFlags_DefaultOpen)) {
        EditWidgets::InputText(edit, "Name", &NameComponent::name);
    }
    return {};
}
} // Engine::Component
