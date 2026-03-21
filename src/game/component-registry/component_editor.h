//
// Created by William on 2026-03-16.
//

#ifndef WILL_ENGINE_COMPONENT_EDITOR_H
#define WILL_ENGINE_COMPONENT_EDITOR_H

#include <entt/entt.hpp>
#include <fmt/format.h>
#include "imgui.h"

#include "core/include/render_interface.h"
#include "game/components/component_types.h"

namespace Game
{
template<typename T>
concept HasDrawEditor = requires(Core::ViewFamily& vf, entt::registry& r, entt::entity e, const char* n) {
    { T::DrawEditor(vf, r, e, n) } -> std::same_as<ComponentEditorResult>;
};

inline ComponentEditorResult DefaultDrawComponentEditor(const char* name)
{
    ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton(fmt::format("X##{}", name).c_str());
    ImGui::PopStyleColor();
    return {.requestRemoval = remove};
}
}

#endif //WILL_ENGINE_COMPONENT_EDITOR_H
