//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_RENDERABLE_COMPONENT_H
#define WILL_ENGINE_RENDERABLE_COMPONENT_H
#include "engine/asset_manager.h"
#include "render/shaders/model_interop.h"

namespace Game
{
struct RenderableComponent
{
    Engine::ModelHandle modelEntry;
    Engine::InstanceHandle instanceEntry;
    Engine::MaterialHandle materialEntry;

    glm::vec4 modelFlags;
    Instance instance;
    MaterialProperties material;
};
}
#endif //WILL_ENGINE_RENDERABLE_COMPONENT_H