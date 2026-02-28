//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_COMPONENT_REGISTRY_H
#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "core/string_id.h"
#include "core/allocators/inline_vector.h"
#include "game/systems/editor_systems.h"

namespace Core
{
using SerializeFn = void(*)(const entt::registry&, entt::entity, nlohmann::json&);
using DeserializeFn = void(*)(entt::registry&, entt::entity, const nlohmann::json&);
using HasComponentFn = bool(*)(const entt::registry&, entt::entity);
using DrawEditorFn = void(*)(entt::registry&, entt::entity);
struct ComponentEntry
{
    StringID typeId;
    SerializeFn serialize;
    DeserializeFn deserialize;
    DrawEditorFn drawEditor;
    HasComponentFn has;
};

struct ComponentRegistry
{
    InlineVector<ComponentEntry, 1024> registry{};
};

template<typename T>
concept TagComponent = std::is_empty_v<T>;

template<typename T>
concept DataComponent = !std::is_empty_v<T>;

template<DataComponent T>
void RegisterComponent(ComponentRegistry& componentRegistry, StringID typeId)
{
    componentRegistry.registry.PushBack({
        typeId,
        [](const entt::registry& reg, entt::entity e, nlohmann::json& json) {
            T::Serialize(reg.get<T>(e), json);
        },
        [](entt::registry& reg, entt::entity e, const nlohmann::json& json) {
            T::Deserialize(reg.get_or_emplace<T>(e), json);
        },
        [](entt::registry& reg, entt::entity e) {
            Game::System::DrawComponentEditor<T>(reg.get<T>(e), reg, e);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        }
    });
}

template<TagComponent T>
void RegisterComponent(ComponentRegistry& componentRegistry, StringID typeId)
{
    componentRegistry.registry.PushBack({
        typeId,
        [](const entt::registry& reg, entt::entity e, nlohmann::json& json) {
            T dummy{};
            T::Serialize(dummy, json);
        },
        [](entt::registry& reg, entt::entity e, const nlohmann::json& json) {
            (void)reg.get_or_emplace<T>(e);
            T dummy{};
            T::Deserialize(dummy, json);
        },
        [](entt::registry& reg, entt::entity e) {
            T dummy{};
            Game::System::DrawComponentEditor<T>(dummy, reg, e);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        }
    });
}

void RegisterComponents(ComponentRegistry& componentRegistry);
} // Core

#endif //WILL_ENGINE_COMPONENT_REGISTRY_H