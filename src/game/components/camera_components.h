//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERACOMPONENT_H
#define WILL_ENGINE_CAMERACOMPONENT_H

#include "core/include/render_interface.h"

namespace Game::Component
{
struct CameraComponent
{
    Core::ViewData currentViewData;
    Core::ViewData previousViewData;
};

struct GameCameraTag
{};

struct EditorCameraTag
{};

struct FreeCameraComponent
{
    float moveSpeed = 5.0f;
    float lookSpeed = 0.1f;
    bool bOrtho = false;
    float orthoSize = 10.0f;
};
}


#endif //WILL_ENGINE_CAMERACOMPONENT_H
