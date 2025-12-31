//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERACOMPONENT_H
#define WILL_ENGINE_CAMERACOMPONENT_H

#include "core/include/render_interface.h"

namespace Game
{
struct CameraComponent
{
    Core::ViewData currentViewData;
    Core::ViewData previousViewData;
};

struct MainViewportComponent
{};
}


#endif //WILL_ENGINE_CAMERACOMPONENT_H
