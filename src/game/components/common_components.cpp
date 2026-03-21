//
// Created by William on 2026-02-26.
//

#include "common_components.h"

#include <json/nlohmann/json.hpp>

#include "imgui.h"

#include "engine/engine_api.h"
#include "game/component-registry/component_copy.h"
#include "game/component-registry/component_serialization.h"
#include "game/component-registry/component_initialization.h"
#include "game/component-registry/component_editor.h"

namespace Game
{
template<>
Component::StableIdComponent CopyComponent(const Component::StableIdComponent& src, entt::registry& dstReg)
{
    return Component::StableIdComponent{};
}

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
    json["name"] = comp.name;
}

template<>
void DeserializeComponent<Component::NameComponent>(Component::NameComponent& comp, const nlohmann::json& json)
{
    comp.name = json["name"].get<std::string>();
}
}

namespace Game
{
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

template<>
void OnComponentAdded<Component::StableIdComponent>(Component::StableIdComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::GameState*>();
    component.id = Component::StableIdComponent::Generate(state->rng);
    state->stableIdToEntityMap[component.id] = entity;
}

template<>
void OnComponentRemoved<Component::StableIdComponent>(Component::StableIdComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::GameState*>();
    state->stableIdToEntityMap.erase(component.id);
    registry.remove<Component::StableIdComponent>(entity);
}

template<>
ComponentEditorResult DrawComponentEditor<Component::NameComponent>(Component::NameComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
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
            component.name = buf;
        }
    }
    return {.requestRemoval = remove};
}
}
