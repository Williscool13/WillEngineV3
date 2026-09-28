//
// Created by William on 2026-09-28.
//

#ifndef WILL_ENGINE_UNDO_STACK_H
#define WILL_ENGINE_UNDO_STACK_H

#include <cstddef>
#include <cstdint>

#include <entt/entt.hpp>

#include "core/containers/span.h"
#include "core/containers/vector.h"
#include "core/string_id.h"

namespace Core
{
class TlsfAllocator;
}

namespace Engine
{
struct EngineState;

struct UndoEntry
{
    StringID stableId{};
    uint32_t beforeOffset{0};
    uint32_t beforeSize{0};
    uint32_t afterOffset{0};
    uint32_t afterSize{0};
};

struct UndoRecord
{
    StringID typeId{};
    Core::Vector<UndoEntry> entries{};
    Core::Vector<std::byte> data{};
};

/** Keyed by stable id so records survive entity handle reuse; entities that no longer exist are skipped. */
class UndoStack
{
public:
    static constexpr uint32_t MAX_RECORDS = 256;

    UndoStack() = default;
    explicit UndoStack(Core::TlsfAllocator* allocator);

    /** No-op while an interaction on the same type is open. */
    void Begin(EngineState* state, StringID typeId, Core::Span<const entt::entity> targets);
    void End(EngineState* state, StringID typeId);

    /** Closes an interaction whose widget went inactive without committing. */
    void Tick(EngineState* state, bool bAnyItemActive);

    bool Undo(EngineState* state);
    bool Redo(EngineState* state);
    void Clear();

    [[nodiscard]] bool CanUndo() const { return !undo.IsEmpty(); }
    [[nodiscard]] bool CanRedo() const { return !redo.IsEmpty(); }

    /** Oldest first. */
    [[nodiscard]] Core::Span<const UndoRecord> UndoRecords() const { return {undo.Data(), undo.Size()}; }
    [[nodiscard]] Core::Span<const UndoRecord> RedoRecords() const { return {redo.Data(), redo.Size()}; }
    [[nodiscard]] const UndoRecord* Pending() const { return bPending ? &pending : nullptr; }

private:
    void Apply(EngineState* state, const UndoRecord& record, bool bBefore);
    void Push(UndoRecord&& record);

    Core::TlsfAllocator* allocator{nullptr};
    Core::Vector<UndoRecord> undo{};
    Core::Vector<UndoRecord> redo{};
    UndoRecord pending{};
    bool bPending{false};
};
}

#endif //WILL_ENGINE_UNDO_STACK_H
