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

struct MainViewportTag
{};

struct FreeCameraComponent
{
    float moveSpeed = 5.0f;
    float lookSpeed = 0.1f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};
}


#endif //WILL_ENGINE_CAMERACOMPONENT_H
