//
// Created by William on 2026-04-19.
//

#ifndef WILL_ENGINE_RENDER_UTILS_H
#define WILL_ENGINE_RENDER_UTILS_H

#include <volk.h>

#include "core/types/math.h"
#include "engine/resources/model/model_types.h"
#include "engine/resources/model/static_model.h"

namespace Render
{
constexpr VkClearValue CLEAR_COLOR_EMPTY = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
constexpr VkClearValue CLEAR_COLOR_FULL = {.color = {{1.0f, 1.0f, 1.0f, 1.0f}}};
constexpr VkClearValue CLEAR_COLOR_BLACK = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
constexpr VkClearValue CLEAR_DEPTH_FAR   = {.depthStencil = {.depth = 0.0f, .stencil = 0}};
} // Render

Vec2 OctEncode(Vec3 n);
Vec3 OctDecode(Vec2 f);

Engine::Vertex CompressVertex(const Engine::FullVertex& fullVertex, const Engine::MeshBounds& bounds);

#endif //WILL_ENGINE_RENDER_UTILS_H
