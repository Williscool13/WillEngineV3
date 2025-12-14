//
// Created by William on 2025-12-14.
//

#include "engine_api.h"

#include "will_engine.h"

namespace Engine
{
void EngineAPI::UpdateCamera(glm::vec3 pos, glm::vec3 look, glm::vec3 up, float fov, float aspect, float n, float f)
{
    Core::FrameBuffer& fb = WillEngine::Get().GetStagingFrameBuffer();
    fb.rawCameraData.cameraWorldPos = pos;
    fb.rawCameraData.cameraLook = look;
    fb.rawCameraData.cameraUp = up;
    fb.rawCameraData.fovDegrees = fov;
    fb.rawCameraData.aspectRatio = aspect;
    fb.rawCameraData.nearPlane = n;
    fb.rawCameraData.farPlane = f;
}
} // Engine
