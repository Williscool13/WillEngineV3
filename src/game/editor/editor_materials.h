//
// Created by William on 2026-06-26.
//

#ifndef WILL_ENGINE_EDITOR_MATERIALS_H
#define WILL_ENGINE_EDITOR_MATERIALS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game
{
void DrawMaterialsWindow(Engine::EngineContext* ctx, Engine::EngineState* state);

void DrawTexturesWindow(Engine::EngineContext* ctx, Engine::EngineState* state);
}

#endif //WILL_ENGINE_EDITOR_MATERIALS_H
