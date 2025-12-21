//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERACOMPONENT_H
#define WILL_ENGINE_CAMERACOMPONENT_H

#include <glm/glm.hpp>

namespace Engine
{
struct CameraComponent
{
    glm::mat4 view;
    glm::mat4 projection;
};

struct MainViewportComponent
{};
}


#endif //WILL_ENGINE_CAMERACOMPONENT_H
