//
// Created by William on 2026-02-26.
//

#include "common_components.h"

#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include "component_serialization.h"
#include "engine/engine_api.h"
#include "game/systems/editor_systems.h"
#include "game/components/component_initialization.h"

namespace Game
{
template<>
void SerializeComponent<Component::StableIdComponent>(const Component::StableIdComponent& comp, nlohmann::json& json)
{
    json["id"] = comp.id.id;
}

template<>
void DeserializeComponent<Component::StableIdComponent>(Component::StableIdComponent& comp, const nlohmann::json& json)
{
    comp.id = StringID(json["id"].get<uint64_t>());
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
ComponentEditorResult DrawComponentEditor<Component::StableIdComponent>(Component::StableIdComponent& component, const Core::ViewFamily& viewFamily, entt::registry& registry,
                                                       entt::entity entity)
{
    ImGui::Text("StableID: %llu", component.id.id);
    return {};
}

template<>
void OnComponentAdded<Component::StableIdComponent>(Component::StableIdComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::GameState*>();
    assert(!state->stableIdToEntityMap.contains(component.id) && "Duplicate stable ID detected");
    state->stableIdToEntityMap[component.id] = entity;
}

template<>
ComponentEditorResult DrawComponentEditor<Component::NameComponent>(Component::NameComponent& component, const Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity)
{
    char buf[256];
    strncpy_s(buf, component.name.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Name", buf, sizeof(buf))) {
        component.name = buf;
    }
    return {};
}
}
