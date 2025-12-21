//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERA_SYSTEM_H
#define WILL_ENGINE_CAMERA_SYSTEM_H
#include "core/include/render_interface.h"

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
void UpdateCameras(Core::EngineContext* ctx, Engine::GameState* state);
void BuildViewFamily(Engine::GameState* state, Core::ViewFamily& mainViewFamily);
} // Game

#endif //WILL_ENGINE_CAMERA_SYSTEM_H
