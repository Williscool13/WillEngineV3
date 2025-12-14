//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_API_H
#define WILL_ENGINE_ENGINE_API_H

#include <glm/glm.hpp>

namespace Engine
{
class EngineAPI
{
public:
    static void UpdateCamera(glm::vec3 pos, glm::vec3 look, glm::vec3 up, float fov, float aspect, float n, float f);
};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H