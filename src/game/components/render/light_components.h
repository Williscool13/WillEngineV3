//
// Created by William on 2026-05-23.
//

#ifndef WILL_ENGINE_LIGHT_COMPONENTS_H
#define WILL_ENGINE_LIGHT_COMPONENTS_H

#include <glm/vec3.hpp>

namespace Game::Component
{
struct PointLightComponent
{
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float range{10.0f};
};

struct AreaLightComponent
{
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float halfWidth{1.0f};
    float halfHeight{1.0f};
};
}

#endif //WILL_ENGINE_LIGHT_COMPONENTS_H
