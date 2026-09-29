//
// Created by William on 2026-06-26.
//

#ifndef WILL_ENGINE_EDITOR_SCENE_PANELS_H
#define WILL_ENGINE_EDITOR_SCENE_PANELS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
}

namespace Engine
{
/** Keeps the active scene a loaded one (or none) with its current name. */
void SyncActiveScene(Engine::EngineContext* ctx, Engine::EngineState* state);

void DrawScenesPanel(Engine::EngineContext* ctx, Engine::EngineState* state);

void DrawOutliner(Engine::EngineContext* ctx, Engine::EngineState* state);

void DrawSpawnPanel(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
}

#endif //WILL_ENGINE_EDITOR_SCENE_PANELS_H
