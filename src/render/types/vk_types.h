//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_VK_TYPES_H
#define WILL_ENGINE_VK_TYPES_H

#include <filesystem>
#include <glm/glm.hpp>

namespace Render
{

struct UIVertex
{
    glm::vec2 position{0, 0};
    glm::vec2 uv{0, 0};
    uint32_t color{0xFFFFFFFF};
    uint32_t samplerIndex{0};
    uint32_t textureIndex{0};
    uint32_t bIsText{1};
};
} // Render

#endif //WILL_ENGINE_VK_TYPES_H
