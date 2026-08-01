//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_ENGINE_COMPONENT_REGISTRY_H

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "core/containers/map.h"
#include "core/containers/vector.h"
#include "core/string_id.h"

namespace Core
{
struct ViewFamily;
}

namespace Engine
{
struct ComponentEditorResult {
    bool requestRemoval{false};
};

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
    StringID typeId{};
    const char* name{};
    SerializeFn serialize{};
    DeserializeFn deserialize{};
    CanAddComponentFn canAdd{};

    // Type erased fns
    EmplaceDefaultFn emplaceDefault{};
    RemoveComponentFn remove{};
    CopyComponentFn copy{};

    DrawEditorFn drawEditor{};
    HasComponentFn has{};

    /** Engine-managed components hidden from user-facing component lists (Add Component, filters). */
    bool hidden{false};

    /** Hide in Details inspector unless "Expose all" is enabled. */
    bool hideInInspector{false};
};

struct ComponentRegistry
{
    ComponentRegistry() = default;

    explicit ComponentRegistry(Core::TlsfAllocator* allocator);

    ~ComponentRegistry() = default;

    Core::Vector<ComponentEntry> registry{};
    Core::Map<StringID, size_t> registryMapping{};
};
} // Engine

#endif //WILL_ENGINE_ENGINE_COMPONENT_REGISTRY_H
