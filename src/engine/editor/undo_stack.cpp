//
// Created by William on 2026-09-28.
//

#include "undo_stack.h"

#include <cstring>

#include "engine/engine_api.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/components/scene_components.h"
#include "engine/editor/editor_systems.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

namespace Engine
{
UndoStack::UndoStack(Core::TlsfAllocator* allocator)
    : allocator(allocator),
      undo(allocator, Core::AllocTag::EngineState),
      redo(allocator, Core::AllocTag::EngineState)
{}

static const ComponentEntry* FindEntry(const EngineState* state, StringID typeId)
{
    const size_t* index = state->componentRegistry.registryMapping.Find(typeId);
    return index ? &state->componentRegistry.registry[*index] : nullptr;
}

static uint32_t AppendSnapshot(const ComponentEntry& entry, entt::registry& registry, entt::entity e, Core::Vector<std::byte>& data, uint32_t& outSize)
{
    const auto offset = static_cast<uint32_t>(data.Size());
    TextWriter writer(data);
    entry.serialize(registry, e, writer);
    outSize = static_cast<uint32_t>(data.Size()) - offset;
    return offset;
}

void UndoStack::Begin(EngineState* state, StringID typeId, Core::Span<const entt::entity> targets)
{
    if (bPending && pending.typeId == typeId) { return; }
    if (bPending) {
        End(state, pending.typeId);
    }

    const ComponentEntry* entry = FindEntry(state, typeId);
    if (entry == nullptr) { return; }

    pending = UndoRecord{typeId, Core::Vector<UndoEntry>(allocator, Core::AllocTag::EngineState), Core::Vector<std::byte>(allocator, Core::AllocTag::EngineState)};
    bPending = true;
    for (const entt::entity e : targets) {
        const auto* stable = state->registry.try_get<Component::StableIdComponent>(e);
        if (stable == nullptr || !entry->has(state->registry, e)) { continue; }
        UndoEntry& u = pending.entries.EmplaceBack();
        u.stableId = stable->id;
        u.beforeOffset = AppendSnapshot(*entry, state->registry, e, pending.data, u.beforeSize);
    }
}

void UndoStack::End(EngineState* state, StringID typeId)
{
    if (!bPending || pending.typeId != typeId) { return; }
    bPending = false;

    const ComponentEntry* entry = FindEntry(state, typeId);
    if (entry == nullptr) { return; }

    bool bChanged = false;
    for (UndoEntry& u : pending.entries) {
        const entt::entity* e = state->stableIdToEntityMap.Find(u.stableId);
        if (e == nullptr || !entry->has(state->registry, *e)) { continue; }
        u.afterOffset = AppendSnapshot(*entry, state->registry, *e, pending.data, u.afterSize);
        const bool bEntityChanged = u.beforeSize != u.afterSize || memcmp(pending.data.Data() + u.beforeOffset, pending.data.Data() + u.afterOffset, u.beforeSize) != 0;
        if (bEntityChanged) {
            if (const auto* sc = state->registry.try_get<Component::SceneComponent>(*e)) {
                MarkSceneModified(state, sc->sceneId);
            }
        }
        bChanged |= bEntityChanged;
    }
    if (bChanged) {
        Push(std::move(pending));
    }
}

void UndoStack::Tick(EngineState* state, bool bAnyItemActive)
{
    if (bPending && !bAnyItemActive) {
        End(state, pending.typeId);
    }
}

void UndoStack::Push(UndoRecord&& record)
{
    if (undo.Size() == MAX_RECORDS) {
        undo.RemoveAt(0);
    }
    undo.PushBack(std::move(record));
    redo.Clear();
}

void UndoStack::Apply(EngineState* state, const UndoRecord& record, bool bBefore)
{
    const ComponentEntry* entry = FindEntry(state, record.typeId);
    if (entry == nullptr || entry->restore == nullptr) { return; }

    for (const UndoEntry& u : record.entries) {
        const entt::entity* e = state->stableIdToEntityMap.Find(u.stableId);
        if (e == nullptr || !state->registry.valid(*e) || !entry->has(state->registry, *e)) { continue; }
        const uint32_t offset = bBefore ? u.beforeOffset : u.afterOffset;
        const uint32_t size = bBefore ? u.beforeSize : u.afterSize;
        entry->restore(state->registry, *e, TextReader(record.data.Data() + offset, size));
        if (auto* sc = state->registry.try_get<Component::SceneComponent>(*e)) {
            MarkSceneModified(state, sc->sceneId);
        }
    }
}

bool UndoStack::Undo(EngineState* state)
{
    if (undo.IsEmpty()) { return false; }
    bPending = false;
    UndoRecord record = undo.PopBackValue();
    Apply(state, record, true);
    redo.PushBack(std::move(record));
    return true;
}

bool UndoStack::Redo(EngineState* state)
{
    if (redo.IsEmpty()) { return false; }
    bPending = false;
    UndoRecord record = redo.PopBackValue();
    Apply(state, record, false);
    undo.PushBack(std::move(record));
    return true;
}

void UndoStack::Clear()
{
    undo.Clear();
    redo.Clear();
    bPending = false;
}
}
