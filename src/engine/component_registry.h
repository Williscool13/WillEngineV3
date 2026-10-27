//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_ENGINE_COMPONENT_REGISTRY_H
#define WILL_ENGINE_ENGINE_COMPONENT_REGISTRY_H

#include "core/containers/map.h"
#include "core/containers/span.h"
#include "core/containers/vector.h"
#include "core/string_id.h"
#include "engine/core/origin.h"
#include "engine/core/registry.h"

namespace Core
{
struct ViewFamily;
}

namespace Engine
{
class TextWriter;
class TextReader;

class EditContext;

struct ComponentEditorResult {
    bool bRequestRemoval{false};
};

using SerializeFn = void(*)(const SystemRegistry&, entt::entity, TextWriter&);
using DeserializeFn = void(*)(SystemRegistry&, entt::entity, const TextReader&);
using HasComponentFn = bool(*)(const SystemRegistry&, entt::entity);
using CanAddComponentFn = bool(*)(const SystemRegistry&, entt::entity);
using EmplaceDefaultFn = void(*)(SystemRegistry&, entt::entity);
using RemoveComponentFn = void(*)(SystemRegistry&, entt::entity);
using CopyComponentFn = void(*)(const SystemRegistry&, entt::entity, SystemRegistry&, entt::entity);
using DrawEditorFn = ComponentEditorResult(*)(Core::ViewFamily&, EditContext&, const char*);
using RestoreFn = void(*)(SystemRegistry&, entt::entity, const TextReader&);
using FillDefaultsFn =void(*)(const TextReader&, TextWriter&);
using PropagateFn = bool(*)(SystemRegistry&, entt::entity, const TextReader&, const TextReader&);
/** Points each EntityRef naming from[i] at to[i]. */
using RemapEntityRefsFn = void(*)(SystemRegistry&, entt::entity, Core::Span<const StringID> from, Core::Span<const StringID> to);

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

    Origin origin{Origin::Engine};

    /** Engine-managed components hidden from user-facing component lists (Add Component, filters). */
    bool hidden{false};

    /** Hide in Details inspector unless "Expose all" is enabled. */
    bool hideInInspector{false};

    // Undo/redo: copies snapshot fields onto the live component, then OnEditCommit
    RestoreFn restore{};
    FillDefaultsFn fillDefaults{};
    /** Writes each field that changed between before and after where the live component still holds before; true if anything changed. */
    PropagateFn propagate{};
    /** Null when the component holds no EntityRef. */
    RemapEntityRefsFn remapEntityRefs{};

    bool bMovesEntity{false};
    bool bInstanceOnly{false};
};

/** State a runtime save writes; never in scene files, the inspector or undo. */
struct RuntimeStateEntry
{
    StringID typeId{};
    const char* name{};
    HasComponentFn has{};
    SerializeFn serialize{};
    DeserializeFn deserialize{};
    Origin origin{Origin::Engine};
};

struct ComponentRegistry
{
    ComponentRegistry() = default;

    explicit ComponentRegistry(Core::TlsfAllocator* allocator);

    ~ComponentRegistry() = default;

    Core::Vector<ComponentEntry> registry{};
    Core::Map<StringID, size_t> registryMapping{};

    Core::Vector<RuntimeStateEntry> runtimeState{};
    Core::Map<StringID, size_t> runtimeStateMapping{};
};

void ClearGameComponents(ComponentRegistry& componentRegistry);
} // Engine

#endif //WILL_ENGINE_ENGINE_COMPONENT_REGISTRY_H
