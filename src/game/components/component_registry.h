//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_COMPONENT_REGISTRY_H
#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "component_copy.h"
#include "component_initialization.h"
#include "component_serialization.h"
#include "component_types.h"
#include "core/string_id.h"
#include "core/allocators/inline_vector.h"
#include "game/systems/editor_systems.h"

namespace Game
{
using SerializeFn = void(*)(const entt::registry&, entt::entity, nlohmann::json&);
using DeserializeFn = void(*)(entt::registry&, entt::entity, const nlohmann::json&);
using HasComponentFn = bool(*)(const entt::registry&, entt::entity);
using CanAddComponentFn = bool(*)(const entt::registry&, entt::entity);
using OnAddComponentFn = void(*)(entt::registry&, entt::entity);
using OnRemoveComponentFn = void(*)(entt::registry&, entt::entity);
using OnPlayStartFn = void(*)(entt::registry&, entt::entity);
using OnPlayStopFn = void(*)(entt::registry&, entt::entity);
using CopyComponentFn = void(*)(const entt::registry&, entt::entity, entt::registry&, entt::entity);
using DrawEditorFn = ComponentEditorResult(*)(Core::ViewFamily&, entt::registry&, entt::entity, const char*);

struct ComponentEntry
{
    StringID typeId;
    const char* name;
    SerializeFn serialize;
    DeserializeFn deserialize;
    CanAddComponentFn canAdd;
    OnAddComponentFn onAddComponent;
    OnRemoveComponentFn onRemoveComponent;
    OnPlayStartFn onPlayStart;
    OnPlayStopFn onPlayStop;
    CopyComponentFn copy;
    DrawEditorFn drawEditor;
    HasComponentFn has;
};

struct ComponentRegistry
{
    Core::InlineVector<ComponentEntry, 1024> registry{};
    std::unordered_map<StringID, size_t> registryMapping;
};

template<typename T>
concept TagComponent = std::is_empty_v<T>;

template<typename T>
concept DataComponent = !std::is_empty_v<T>;

template<DataComponent T>
void RegisterComponent(ComponentRegistry& componentRegistry, const char* name)
{
    auto typeId = TypeSID<T>();
    auto index = componentRegistry.registry.Size();
    componentRegistry.registry.PushBack({
        typeId,
        name,
        [](const entt::registry& reg, entt::entity e, nlohmann::json& json) {
            SerializeComponent<T>(reg.get<T>(e), json);
        },
        [](entt::registry& reg, entt::entity e, const nlohmann::json& json) {
            DeserializeComponent<T>(reg.get_or_emplace<T>(e), json);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return CanAddComponent<T>(reg, e);
        },
        [](entt::registry& reg, entt::entity e) {
            OnComponentAdded<T>(reg.get_or_emplace<T>(e), reg, e);
        },
        [](entt::registry& reg, entt::entity e) {
            OnComponentRemoved<T>(reg.get<T>(e), reg, e);
        },
        [](entt::registry& reg, entt::entity e) {
            OnPlayStart<T>(reg.get<T>(e), reg, e);
        },
        [](entt::registry& reg, entt::entity e) {
            OnPlayStop<T>(reg.get<T>(e), reg, e);
        },
        [](const entt::registry& srcReg, entt::entity srcEntity, entt::registry& dstReg, entt::entity dstEntity) {
            dstReg.emplace_or_replace<T>(dstEntity, CopyComponent<T>(srcReg.get<T>(srcEntity), dstReg));
        },
        [](Core::ViewFamily& viewFamily, entt::registry& reg, entt::entity e, const char* n) {
            return DrawComponentEditor<T>(reg.get<T>(e), viewFamily, reg, e, n);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        }
    });

    componentRegistry.registryMapping[typeId] = index;
}

template<TagComponent T>
void RegisterComponent(ComponentRegistry& componentRegistry, const char* name)
{
    auto typeId = TypeSID<T>();
    auto index = componentRegistry.registry.Size();
    componentRegistry.registry.PushBack({
        typeId,
        name,
        [](const entt::registry& reg, entt::entity e, nlohmann::json& json) {
            T dummy{};
            SerializeComponent<T>(dummy, json);
        },
        [](entt::registry& reg, entt::entity e, const nlohmann::json& json) {
            (void)reg.get_or_emplace<T>(e);
            T dummy{};
            DeserializeComponent<T>(dummy, json);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return CanAddComponent<T>(reg, e);
        },
        [](entt::registry& reg, entt::entity e) {
            (void)reg.get_or_emplace<T>(e);
            T dummy{};
            OnComponentAdded<T>(dummy, reg, e);
        },
        [](entt::registry& reg, entt::entity e) {
            T dummy{};
            OnComponentRemoved<T>(dummy, reg, e);
        },
        [](entt::registry& reg, entt::entity e) {
            T dummy{};
            OnPlayStart<T>(dummy, reg, e);
        },
        [](entt::registry& reg, entt::entity e) {
            T dummy{};
            OnPlayStop<T>(dummy, reg, e);
        },
        [](const entt::registry&, entt::entity, entt::registry& dstReg, entt::entity dstEntity) {
            (void)dstReg.get_or_emplace<T>(dstEntity);
        },
        [](Core::ViewFamily& viewFamily, entt::registry& reg, entt::entity e, const char* n) {
            T dummy{};
            return DrawComponentEditor<T>(dummy, viewFamily, reg, e, n);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        }
    });
    componentRegistry.registryMapping[typeId] = index;
}

void RegisterComponents(ComponentRegistry& componentRegistry);
} // Core

#endif //WILL_ENGINE_COMPONENT_REGISTRY_H