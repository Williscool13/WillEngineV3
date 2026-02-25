//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_EDITOR_SYSTEMS_H
#define WILL_ENGINE_EDITOR_SYSTEMS_H

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
}

namespace Game::System
{
void EditorUpdate(Core::EngineContext* ctx, Engine::GameState* state);
void DrawEditorInterface(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);
}

#endif //WILL_ENGINE_EDITOR_SYSTEMS_H
