//
// Created by William on 2025-12-21.
//

#include "camera_system.h"

#include "game/camera/free_camera_component.h"

namespace Game::System
{
void UpdateCameras(Core::EngineContext* ctx, Engine::GameState* state)
{
    UpdateFreeCamera(ctx, state);
}
} // Game