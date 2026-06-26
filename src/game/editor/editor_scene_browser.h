//
// Created by William on 2026-06-26.
//

#ifndef WILL_ENGINE_EDITOR_SCENE_BROWSER_H
#define WILL_ENGINE_EDITOR_SCENE_BROWSER_H

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
}

namespace Game
{
void DrawSceneBrowser(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
}

#endif //WILL_ENGINE_EDITOR_SCENE_BROWSER_H
