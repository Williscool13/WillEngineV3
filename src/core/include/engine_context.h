//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_CONTEXT_H
#define WILL_ENGINE_ENGINE_CONTEXT_H

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

#include "render_interface.h"
#include "spdlog/logger.h"

namespace Core
{
struct EngineContext
{
    // const uint8_t* keyboardState;
    // uint32_t keyboardStateSize;
    // glm::vec2 mouseDelta;
    // bool bMouseCaptured;

    float deltaTime;
    float timeElapsed;

    uint32_t windowWidth;
    uint32_t windowHeight;

    std::shared_ptr<spdlog::logger> logger;

    void (*updateCamera)(glm::vec3 position, glm::vec3 lookDir, glm::vec3 up, float fovDegrees, float aspect, float nearPlane, float farPlane);
};
} // Core

#endif //WILL_ENGINE_ENGINE_CONTEXT_H