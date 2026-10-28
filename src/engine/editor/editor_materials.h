//
// Created by William on 2026-06-26.
//

#ifndef WILL_ENGINE_EDITOR_MATERIALS_H
#define WILL_ENGINE_EDITOR_MATERIALS_H

#include "engine/core/material_id.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
struct MaterialBrowserState;
class SystemRegistry;
}

namespace Engine
{
void DrawMaterialsWindow(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void DrawTexturesWindow(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

/**
 * Searchable, grouped list of mutable materials. Must be called inside an open combo, popup or child region; draws its own search field.
 * @param view Search/sort/group state. Each call site owns an instance so their filters stay independent.
 * @param current Highlighted material.
 * @return The material clicked this frame, or INVALID.
 */
Engine::MaterialID DrawMaterialSelector(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::MaterialBrowserState& view, Engine::MaterialID current);

/** BeginCombo previewing `current`: noneLabel when unset, "--" when mixed, red "Missing <id>" when the id is not registered. */
bool BeginMaterialCombo(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, const char* label, Engine::MaterialID current, const char* noneLabel, bool bMixed = false);
}

#endif //WILL_ENGINE_EDITOR_MATERIALS_H
