//
// Created by William on 2026-02-26.
//

#include "common_components.h"

#include <json/nlohmann/json.hpp>

#include "imgui.h"

#include "engine/engine_api.h"
#include "game/component-registry/component_serialization.h"
#include "game/component-registry/component_initialization.h"
#include "game/component-registry/component_editor.h"

namespace Game
{

template<>
void SerializeComponent<Component::PrefabInstanceComponent>(const Component::PrefabInstanceComponent& comp, nlohmann::json& json)
{
    json["prefabId"] = comp.prefabId.id;
}

template<>
void DeserializeComponent<Component::PrefabInstanceComponent>(Component::PrefabInstanceComponent& comp, const nlohmann::json& json)
{
    comp.prefabId = StringID(json["prefabId"].get<uint64_t>());
}

template<>
void SerializeComponent<Component::NameComponent>(const Component::NameComponent& comp, nlohmann::json& json)
{
    json["name"] = comp.name.c_str();
}

template<>
void DeserializeComponent<Component::NameComponent>(Component::NameComponent& comp, const nlohmann::json& json)
{
    comp.name = StackString<256>(json["name"].get<std::string>().c_str());
}
} // Game

namespace Game
{

ComponentEditorResult Component::NameComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<Component::NameComponent>(entity);
    bool open = ImGui::CollapsingHeader("Name##componentname", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletename");
    ImGui::PopStyleColor();

    if (open) {
        char buf[256];
        strncpy_s(buf, component.name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Name", buf, sizeof(buf))) {
            component.name = StackString<256>(buf);
        }
    }
    return {.requestRemoval = remove};
}
}
