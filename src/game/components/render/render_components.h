//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_RENDER_COMPONENTS_H
#define WILL_ENGINE_RENDER_COMPONENTS_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/material_manager.h"

namespace Game::Component
{
struct PrimitiveData
{
    uint32_t primitiveIndex;
    Engine::MaterialID materialID;
};

struct RenderableComponent
{
    glm::vec4 modelFlags;// x: visible, y: shadow-caster, zw: reserved

    std::array<PrimitiveData, 128> primitives;
    uint8_t primitiveCount = 0;

    glm::mat4 previousModelMatrix{1.0f};
};

struct TransformComponent
{
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    // todo: dirty tracking to reduce repeat calculations
};

inline glm::mat4 GetMatrix(const TransformComponent& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.translation) * mat4_cast(transform.rotation) * glm::scale(glm::mat4(1.0f), transform.scale);
}
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
