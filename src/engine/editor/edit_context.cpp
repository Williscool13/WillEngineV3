//
// Created by William on 2026-09-28.
//

#include "edit_context.h"

#include "engine/engine_api.h"

namespace Engine
{
EditContext::EditContext(EngineState* state, Core::Span<const entt::entity> targets)
    : state(state), targets(targets)
{}

entt::registry& EditContext::Registry() const
{
    return state->registry;
}

void EditContext::BeginUndo(StringID typeId)
{
    state->editor.undo.Begin(state, typeId, targets);
}

void EditContext::EndUndo(StringID typeId)
{
    state->editor.undo.End(state, typeId);
}
}
