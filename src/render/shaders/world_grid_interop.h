//
// Created by William on 2026-07-10.
//

#ifndef WILL_ENGINE_WORLD_GRID_INTEROP_H
#define WILL_ENGINE_WORLD_GRID_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_PTR(T) T*
#define SHADER_ATOMIC(T) Atomic<T>
#else
#include <glm/glm.hpp>
#include <cstdint>

using uint = uint32_t;
using int32 = int32_t;
using uint32 = uint32_t;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using int2 = glm::ivec2;
using int3 = glm::ivec3;
using int4 = glm::ivec4;

using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;

using float2x2 = glm::mat2;
using float3x3 = glm::mat3;
using float4x4 = glm::mat4;

#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#define SHADER_PTR(T) VkDeviceAddress
#define SHADER_ATOMIC(T) T
#endif // __SLANG__

// World-space cascaded light grid ("WorldGridBinning")
//   - Camera-centered AABB cells
//   - Cascaded/exponential
//   - Rebuilt from scratch every frame
// Sourced from idTech8's Cascaded Light Grid (SIGGRAPH 2025): 8 cascades, 16x16x16 cells/cascade, exponential (not defined, but I've settled on 2x) distribution, first cascade 32m bounds / 2m cells.

SHADER_PUBLIC SHADER_CONST uint WORLD_GRID_CASCADES = 8u;
SHADER_PUBLIC SHADER_CONST uint WORLD_GRID_RES = 16u; // cells per axis, per cascade
SHADER_PUBLIC SHADER_CONST uint WORLD_GRID_CELLS_PER_CASCADE = WORLD_GRID_RES * WORLD_GRID_RES * WORLD_GRID_RES;
SHADER_PUBLIC SHADER_CONST uint WORLD_GRID_CELL_COUNT = WORLD_GRID_CELLS_PER_CASCADE * WORLD_GRID_CASCADES;
SHADER_PUBLIC SHADER_CONST float WORLD_GRID_BASE_CELL_SIZE = 2.0; // 32m bounds at cascade 0, ~4096m at cascade 7.
SHADER_PUBLIC SHADER_CONST uint MAX_LIGHTS_PER_WORLD_GRID_CELL = 64u;
SHADER_PUBLIC SHADER_CONST uint MAX_EMISSIVE_GROUPS_PER_WORLD_GRID_CELL = 16u;
SHADER_PUBLIC SHADER_CONST uint WORLD_GRID_PROBE_SENTINEL = 0xFFFFFFFFu;
SHADER_PUBLIC SHADER_CONST uint MAX_DDGI_VOLUMES_PER_WORLD_GRID_CELL = 8u;
SHADER_PUBLIC SHADER_CONST uint WORLD_GRID_DDGI_OVERFLOW = 0xFFFFFFFFu;

#endif //WILL_ENGINE_WORLD_GRID_INTEROP_H
