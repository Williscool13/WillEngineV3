//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_RENDER_COMPONENTS_H
#define WILL_ENGINE_RENDER_COMPONENTS_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

#include "engine/material_manager.h"

namespace Game::Component
{


struct DirtyRenderTransformTag{};


struct RenderTransformComponent
{
    glm::mat4 modelMatrix;
    glm::mat4 previousMatrix;
};

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
};

}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
