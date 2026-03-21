//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_RENDER_COMPONENTS_H
#define WILL_ENGINE_RENDER_COMPONENTS_H

#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <entt/entt.hpp>

#include "core/string_id.h"
#include "engine/resources/material/material_manager.h"
#include "engine/core/model_id.h"
#include "engine/resources/model/model_types.h"

namespace Game::Component
{
struct DirtyRenderTransformComponent
{
    // 2 to update Prev next frame.
    int32_t counter{2};
};

struct RenderTransformComponent
{
    glm::mat4 modelMatrix;
    glm::mat4 previousMatrix;
    glm::vec3 renderOffset{0.0f};
};
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
