//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_DDGI_INTEROP_H
#define WILL_ENGINE_DDGI_INTEROP_H

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

/**
 * Rolling probe window over an infinite world-space lattice. Probe cell g sits at g * probeSpacing; the window spans probeCount cells from baseCell.
 * Storage slot s holds cell baseCell + EuclideanMod(s - baseCell, probeCount), so a scroll only changes the cells of the newly exposed planes.
 */
SHADER_PUBLIC struct DDGIVolumeParams
{
    SHADER_PUBLIC int3 baseCell;
    SHADER_PUBLIC uint pad0;
    SHADER_PUBLIC uint3 probeCount;
    SHADER_PUBLIC uint pad1;
    SHADER_PUBLIC float3 probeSpacing;
    SHADER_PUBLIC float pad2;
};

#endif //WILL_ENGINE_DDGI_INTEROP_H
