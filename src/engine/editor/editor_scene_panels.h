//
// Created by William on 2026-06-26.
//

#ifndef WILL_ENGINE_EDITOR_SCENE_PANELS_H
#define WILL_ENGINE_EDITOR_SCENE_PANELS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
class SystemRegistry;
}

namespace Engine
{
/** Keeps the active scene a loaded one (or none) with its current name. */
void SyncActiveScene(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void DrawScenesPanel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void DrawOutliner(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void DrawSpawnPanel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
}

#endif //WILL_ENGINE_EDITOR_SCENE_PANELS_H
