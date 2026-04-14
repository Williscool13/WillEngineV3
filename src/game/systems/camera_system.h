//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERA_SYSTEM_H
#define WILL_ENGINE_CAMERA_SYSTEM_H
#include "../../render/interface/render_interface.h"

namespace Engine
{
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
}

namespace Game
{
void UpdateEditorCamera(Engine::EngineContext* ctx, Engine::EngineState* state);

void BuildViewFamily(Engine::EngineState* state, Core::ViewFamily& mainViewFamily);
void BuildPortalViewFamily(Engine::EngineState* state, Core::ViewFamily& mainViewFamily);
} // Game

#endif //WILL_ENGINE_CAMERA_SYSTEM_H
