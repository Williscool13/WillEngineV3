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

namespace Engine
{
void UpdateEditorCamera(Engine::EngineContext* ctx, Engine::EngineState* state);

bool BuildViewFamily(Engine::EngineContext* ctx, Engine::EngineState* state, Core::ViewFamily& mainViewFamily);
void BuildPortalViewFamily(Engine::EngineState* state, Core::ViewFamily& mainViewFamily);

Core::ViewData BuildPerspectiveView(glm::vec3 pos, glm::vec3 forward, glm::vec3 up, float aspectRatio, float fovRadians, float nearPlane);
} // Engine

#endif //WILL_ENGINE_CAMERA_SYSTEM_H
