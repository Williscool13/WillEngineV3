//
// Created by William on 2026-04-19.
//

#ifndef WILL_ENGINE_RENDER_UTILS_H
#define WILL_ENGINE_RENDER_UTILS_H

#include <volk.h>

namespace Render
{
constexpr VkClearValue CLEAR_COLOR_EMPTY = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
constexpr VkClearValue CLEAR_COLOR_FULL = {.color = {{1.0f, 1.0f, 1.0f, 1.0f}}};
constexpr VkClearValue CLEAR_COLOR_BLACK = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
constexpr VkClearValue CLEAR_DEPTH_FAR   = {.depthStencil = {.depth = 0.0f, .stencil = 0}};
} // Render

#endif //WILL_ENGINE_RENDER_UTILS_H
