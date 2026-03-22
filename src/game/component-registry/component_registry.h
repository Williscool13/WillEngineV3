//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_COMPONENT_REGISTRY_H
#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "component_editor.h"
#include "game/components/component_types.h"
#include "core/string_id.h"
#include "core/allocators/inline_vector.h"

namespace Game
{
using SerializeFn = void(*)(const entt::registry&, entt::entity, nlohmann::json&);
using DeserializeFn = void(*)(entt::registry&, entt::entity, const nlohmann::json&);
using HasComponentFn = bool(*)(const entt::registry&, entt::entity);
using CanAddComponentFn = bool(*)(const entt::registry&, entt::entity);
using EmplaceDefaultFn = void(*)(entt::registry&, entt::entity);
using RemoveComponentFn = void(*)(entt::registry&, entt::entity);
using CopyComponentFn = void(*)(const entt::registry&, entt::entity, entt::registry&, entt::entity);
using DrawEditorFn = ComponentEditorResult(*)(Core::ViewFamily&, entt::registry&, entt::entity, const char*);

struct ComponentEntry
{
    StringID typeId;
    const char* name;
    SerializeFn serialize;
    DeserializeFn deserialize;
    CanAddComponentFn canAdd;

    // Type erased fns
    EmplaceDefaultFn emplaceDefault;
    RemoveComponentFn remove;
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
            if constexpr (HasSerialize<T>) {
                T::Serialize(reg.get<T>(e), json);
            }
        },
        [](entt::registry& reg, entt::entity e, const nlohmann::json& json) {
            T comp{};
            if constexpr (HasDeserialize<T>) {
                T::Deserialize(comp, json);
            }
            reg.emplace_or_replace<T>(e, std::move(comp));
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            if constexpr (HasCanAdd<T>) {
                return T::CanAdd(reg, e);
            } else {
                return true;
            }
        },
        [](entt::registry& reg, entt::entity e) {
            T a = reg.get_or_emplace<T>(e);
        },
        [](entt::registry& reg, entt::entity e) {
            reg.remove<T>(e);
        },
        [](const entt::registry& srcReg, entt::entity srcEntity, entt::registry& dstReg, entt::entity dstEntity) {
            dstReg.emplace_or_replace<T>(dstEntity, srcReg.get<T>(srcEntity));
        },
        [](Core::ViewFamily& viewFamily, entt::registry& reg, entt::entity e, const char* n) {
            if constexpr (HasDrawEditor<T>) {
                return T::DrawEditor(viewFamily, reg, e, n);
            } else {
                return DefaultDrawComponentEditor(n);
            }
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
        [](const entt::registry&, entt::entity, nlohmann::json&) {},
        [](entt::registry& reg, entt::entity e, const nlohmann::json&) {
            (void)reg.get_or_emplace<T>(e);
        },
        [](const entt::registry& reg, entt::entity e) -> bool {
            if constexpr (HasCanAdd<T>) {
                return T::CanAdd(reg, e);
            } else {
                return true;
            }
        },
        [](entt::registry& reg, entt::entity e) {
            reg.get_or_emplace<T>(e);
        },
        [](entt::registry& reg, entt::entity e) {
            reg.remove<T>(e);
        },
        [](const entt::registry&, entt::entity, entt::registry& dstReg, entt::entity dstEntity) {
            (void)dstReg.get_or_emplace<T>(dstEntity);
        },
        [](Core::ViewFamily& viewFamily, entt::registry& reg, entt::entity e, const char* n) {
            if constexpr (HasDrawEditor<T>) {
                return T::DrawEditor(viewFamily, reg, e, n);
            } else {
                return DefaultDrawComponentEditor(n);
            }
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