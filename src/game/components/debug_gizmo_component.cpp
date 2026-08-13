//
// Created by William on 2026-03-23.
//

#include "debug_gizmo_component.h"

#include <json/nlohmann/json.hpp>

#include "imgui.h"

#include "game/component-registry/component_editor.h"

namespace
{
constexpr const char* SHAPE_NAMES[] = {"None", "Sphere", "Box"};
static_assert(std::size(SHAPE_NAMES) == static_cast<size_t>(Game::Component::DebugGizmoShape::Count));
}

void Game::Component::DebugGizmoComponent::Serialize(const DebugGizmoComponent& comp, nlohmann::json& json)
{
    json["shape"] = static_cast<uint8_t>(comp.shape);
    json["extents"] = {comp.extents.x, comp.extents.y, comp.extents.z};
    json["color"] = {comp.color.r, comp.color.g, comp.color.b, comp.color.a};
    json["lineWidth"] = comp.lineWidth;
}

void Game::Component::DebugGizmoComponent::Deserialize(DebugGizmoComponent& comp, const nlohmann::json& json)
{
    comp.shape = static_cast<DebugGizmoShape>(json["shape"].get<uint8_t>());
    const auto& e = json["extents"];
    comp.extents = glm::vec3(e[0].get<float>(), e[1].get<float>(), e[2].get<float>());
    const auto& c = json["color"];
    comp.color = glm::vec4(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>());
    comp.lineWidth = json["lineWidth"].get<float>();
}

namespace Game
{

Engine::ComponentEditorResult Component::DebugGizmoComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& comp = registry.get<Component::DebugGizmoComponent>(entity);
    bool open = ImGui::CollapsingHeader("Debug Gizmo", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletedebuggizmo");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        int current = static_cast<int>(comp.shape);
        if (ImGui::Combo("Shape", &current, SHAPE_NAMES, IM_ARRAYSIZE(SHAPE_NAMES))) {
            comp.shape = static_cast<Component::DebugGizmoShape>(current);
            modified = true;
        }
        modified |= ImGui::DragFloat3("Extents", &comp.extents.x, 0.01f, 0.0f, 100.0f);
        modified |= ImGui::ColorEdit4("Color", &comp.color.r);
        modified |= ImGui::DragFloat("Line Width", &comp.lineWidth, 0.005f, 0.01f, 1.0f);
    }
    return {.bRequestRemoval = remove, .bModified = modified};
}

}
