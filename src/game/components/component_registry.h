//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_COMPONENT_REGISTRY_H
#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "component_initialization.h"
#include "core/string_id.h"
#include "core/allocators/inline_vector.h"
#include "game/systems/editor_systems.h"

namespace Game
{
using SerializeFn = void(*)(const entt::registry&, entt::entity, nlohmann::json&);
using DeserializeFn = void(*)(entt::registry&, entt::entity, const nlohmann::json&);
using HasComponentFn = bool(*)(const entt::registry&, entt::entity);
using AddComponentFn = void(*)(entt::registry&, entt::entity);
using DrawEditorFn = void(*)(const Core::ViewFamily&, entt::registry&, entt::entity);

struct ComponentEntry
{
    StringID typeId;
    const char* name;
    SerializeFn serialize;
    DeserializeFn deserialize;
    AddComponentFn addComponent;
    DrawEditorFn drawEditor;
    HasComponentFn has;
};

struct ComponentRegistry
{
    Core::InlineVector<ComponentEntry, 1024> registry{};
};

template<typename T>
concept TagComponent = std::is_empty_v<T>;

template<typename T>
concept DataComponent = !std::is_empty_v<T>;

template<DataComponent T>
void RegisterComponent(ComponentRegistry& componentRegistry, StringID typeId, const char* name)
{
    componentRegistry.registry.PushBack({
        typeId,
        name,
        [](const entt::registry& reg, entt::entity e, nlohmann::json& json) {
            T::Serialize(reg.get<T>(e), json);
        },
        [](entt::registry& reg, entt::entity e, const nlohmann::json& json) {
            T::Deserialize(reg.get_or_emplace<T>(e), json);
        },
        [](entt::registry& reg, entt::entity e) {
            OnComponentAdded<T>(reg.get_or_emplace<T>(e), reg, e);
        },
        [](const Core::ViewFamily& viewFamily, entt::registry& reg, entt::entity e) {
            DrawComponentEditor<T>(reg.get<T>(e), viewFamily, reg, e);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        }
    });
}

template<TagComponent T>
void RegisterComponent(ComponentRegistry& componentRegistry, StringID typeId, const char* name)
{
    componentRegistry.registry.PushBack({
        typeId,
        name,
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
            (void)reg.get_or_emplace<T>(e);
            T dummy{};
            OnComponentAdded<T>(dummy, reg, e);
        },
        [](const Core::ViewFamily& viewFamily, entt::registry& reg, entt::entity e) {
            T dummy{};
            DrawComponentEditor<T>(dummy, viewFamily, reg, e);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        }
    });
}

void RegisterComponents(ComponentRegistry& componentRegistry);
} // Core

#endif //WILL_ENGINE_COMPONENT_REGISTRY_H