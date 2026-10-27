//
// Created by William on 2026-09-28.
//

#include "edit_context.h"

#include "engine/engine_api.h"

namespace Engine
{
EditContext::EditContext(EngineState* state, Core::Span<const entt::entity> targets, Engine::SystemRegistry& registry)
    : state(state), targets(targets), registry(registry)
{}

Engine::SystemRegistry& EditContext::Registry() const
{
    return registry;
}

void EditContext::BeginUndo(StringID typeId)
{
    state->editor.undo.Begin(state, typeId, targets, registry);
}

void EditContext::EndUndo(StringID typeId)
{
    state->editor.undo.End(state, typeId, registry);
}
}
