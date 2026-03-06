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

namespace Game
{
void UpdateCameras(Core::EngineContext* ctx, Engine::GameState* state);
void UpdateFreeCamera(Core::EngineContext* ctx, Engine::GameState* state);
void UpdateEditorCamera(Core::EngineContext* ctx, Engine::GameState* state);
void RenderEditorCamera(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);

void BuildViewFamily(Engine::GameState* state, Core::ViewFamily& mainViewFamily);
void BuildPortalViewFamily(Engine::GameState* state, Core::ViewFamily& mainViewFamily);
} // Game

#endif //WILL_ENGINE_CAMERA_SYSTEM_H
