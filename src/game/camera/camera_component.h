//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERACOMPONENT_H
#define WILL_ENGINE_CAMERACOMPONENT_H

#include <glm/glm.hpp>

namespace Game
{
struct CameraComponent
{
    float fovRadians;
    float aspectRatio;
    float nearPlane;
    float farPlane;
    glm::vec3 cameraPos;
    glm::vec3 cameraLookAt;
    glm::vec3 cameraUp;
};

struct MainViewportComponent
{};
}


#endif //WILL_ENGINE_CAMERACOMPONENT_H
