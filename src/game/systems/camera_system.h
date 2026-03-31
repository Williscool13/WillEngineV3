//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERA_SYSTEM_H
#define WILL_ENGINE_CAMERA_SYSTEM_H
#include "../../render/interface/render_interface.h"

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
}

namespace Game
{
void UpdateEditorCamera(Core::EngineContext* ctx, Engine::GameState* state);

void BuildViewFamily(Engine::GameState* state, Core::ViewFamily& mainViewFamily);
void BuildPortalViewFamily(Engine::GameState* state, Core::ViewFamily& mainViewFamily);
} // Game

#endif //WILL_ENGINE_CAMERA_SYSTEM_H
