//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_COMPONENT_REGISTRY_H
#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "string_id.h"
#include "allocators/inline_vector.h"

namespace Core
{
using SerializeFn = void(*)(const entt::registry&, entt::entity, nlohmann::json&);
using DeserializeFn = void(*)(entt::registry&, entt::entity, const nlohmann::json&);
using HasComponentFn = bool(*)(const entt::registry&, entt::entity);

struct ComponentEntry
{
    StringID typeId;
    SerializeFn serialize;
    DeserializeFn deserialize;
    HasComponentFn has;
};

struct ComponentRegistry
{
    InlineVector<ComponentEntry, 1024> registry{};
};

template<typename T>
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
        [](const entt::registry& reg, entt::entity e) -> bool {
            return reg.all_of<T>(e);
        }
    });
}
} // Core

#endif //WILL_ENGINE_COMPONENT_REGISTRY_H