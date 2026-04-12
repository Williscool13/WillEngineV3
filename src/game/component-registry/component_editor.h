//
// Created by William on 2026-03-16.
//

#ifndef WILL_ENGINE_COMPONENT_EDITOR_H
#define WILL_ENGINE_COMPONENT_EDITOR_H

#include <entt/entt.hpp>
#include <fmt/format.h>
#include <json/nlohmann/json_fwd.hpp>
#include "imgui.h"

#include "render/interface/render_interface.h"
#include "game/components/component_types.h"
#include "engine/engine_api.h"

namespace Game
{
template<typename T>
concept HasDrawEditor = requires(Core::ViewFamily& vf, entt::registry& r, entt::entity e, const char* n) {
    { T::DrawEditor(vf, r, e, n) } -> std::same_as<Engine::ComponentEditorResult>;
};

template<typename T>
concept HasSerialize = requires(const T& comp, nlohmann::json& json) {
    { T::Serialize(comp, json) } -> std::same_as<void>;
};

template<typename T>
concept HasDeserialize = requires(T& comp, const nlohmann::json& json) {
    { T::Deserialize(comp, json) } -> std::same_as<void>;
};

template<typename T>
concept HasCanAdd = requires(const entt::registry& r, entt::entity e) {
    { T::CanAdd(r, e) } -> std::same_as<bool>;
};

inline Engine::ComponentEditorResult DefaultDrawComponentEditor(const char* name)
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
