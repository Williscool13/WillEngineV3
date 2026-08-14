//
// Created by William on 2026-03-23.
//

#include "debug_gizmo_component.h"

#include "imgui.h"

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/component_editor.h"

namespace
{
constexpr const char* SHAPE_NAMES[] = {"None", "Sphere", "Box"};
static_assert(std::size(SHAPE_NAMES) == static_cast<size_t>(Game::Component::DebugGizmoShape::Count));
}

void Game::Component::DebugGizmoComponent::Serialize(const DebugGizmoComponent& comp, Engine::TextWriter& w)
{
    static const DebugGizmoComponent DEF{};
    w.KeyOpt("shape", static_cast<uint32_t>(comp.shape), static_cast<uint32_t>(DEF.shape));
    w.KeyOpt("extents", comp.extents, DEF.extents);
    w.KeyOpt("color", comp.color, DEF.color);
    w.KeyOpt("lineWidth", comp.lineWidth, DEF.lineWidth);
}

void Game::Component::DebugGizmoComponent::Deserialize(DebugGizmoComponent& comp, const Engine::TextReader& r)
{
    comp.shape = static_cast<DebugGizmoShape>(r.UInt("shape", static_cast<uint32_t>(comp.shape)));
    comp.extents = r.Vec3("extents", comp.extents);
    comp.color = r.Vec4("color", comp.color);
    comp.lineWidth = r.Float("lineWidth", comp.lineWidth);
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
