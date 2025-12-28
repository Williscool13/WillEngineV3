//
// Created by William on 2025-12-28.
//

#ifndef WILL_ENGINE_RENDER_GRAPH_CONFIG_H
#define WILL_ENGINE_RENDER_GRAPH_CONFIG_H
#include <cstdint>

#include "core/include/render_interface.h"

namespace Render
{
inline constexpr uint32_t IMPORTED_RESOURCES_PHYSICAL_LIFETIME = Core::FRAME_BUFFER_COUNT * 2;
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_CONFIG_H