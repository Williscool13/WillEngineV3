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
}

namespace Game
{
void DrawMaterialsWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

void DrawTexturesWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Searchable, grouped list of mutable materials. Must be called inside an open combo, popup or child region; draws its own search field.
 * @param view Search/sort/group state. Each call site owns an instance so their filters stay independent.
 * @param current Highlighted material.
 * @return The material clicked this frame, or INVALID.
 */
Engine::MaterialID DrawMaterialSelector(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::MaterialBrowserState& view, Engine::MaterialID current);
}

#endif //WILL_ENGINE_EDITOR_MATERIALS_H
