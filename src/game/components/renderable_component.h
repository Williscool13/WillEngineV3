//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_RENDERABLE_COMPONENT_H
#define WILL_ENGINE_RENDERABLE_COMPONENT_H
#include "engine/asset_manager.h"
#include "render/shaders/model_interop.h"

namespace Game
{

struct PrimitiveData
{
    uint32_t primitiveIndex;
    Engine::MaterialID materialID;
};

struct RenderableComponent
{
    glm::vec4 modelFlags;// x: visible, y: shadow-caster, zw: reserved

    std::array<PrimitiveData, 64> primitives;
    uint8_t primitiveCount = 0;

    glm::mat4 previousModelMatrix{1.0f};
};
}
#endif //WILL_ENGINE_RENDERABLE_COMPONENT_H