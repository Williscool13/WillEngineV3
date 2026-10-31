//
// Created by William on 2026-09-28.
//

#include "edit_context.h"

#include "engine/engine_api.h"

namespace Engine
{
EditContext::EditContext(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, Core::Span<const entt::entity> targets)
    : ctx(ctx), state(state), registry(registry), targets(targets)
{}

void EditContext::BeginUndo(StringID typeId)
{
    state->editor.undo.Begin(state, typeId, targets, registry);
}

void EditContext::EndUndo(StringID typeId)
{
    state->editor.undo.End(state, typeId, registry);
}
}
