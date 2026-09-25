//
// Created by William on 2026-05-25.
//

#ifndef WILL_ENGINE_RETIR_INTEROP_H
#define WILL_ENGINE_RETIR_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST static const
#else
#include <cstdint>
#include <glm/glm.hpp>
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
#endif // __SLANG__

/** sampleOffsetPacked: snorm16x2 light-local offset over half-extents, low 16 = u (right), high 16 = v (up). lightIdx ~0u = empty. Negative W = occluded winner. */
SHADER_PUBLIC struct Reservoir
{
    SHADER_PUBLIC uint sampleOffsetPacked;
    SHADER_PUBLIC uint lightIdx;
    SHADER_PUBLIC float W;
    SHADER_PUBLIC uint M;
};

/** key = analytic light index or REGIR_KEY_MESHLET | meshlet index. Sorted by key; cumMass is the inclusive mass prefix. */
SHADER_PUBLIC struct ReGIREntry
{
    SHADER_PUBLIC uint key;
    SHADER_PUBLIC float cumMass;
};

// A cell slot owns entries at cellSlot * REGIR_ENTRIES_PER_CELL plus a uint2 {entryCount, asuint(totalMass)}.
SHADER_PUBLIC SHADER_CONST uint REGIR_ENTRIES_PER_CELL = 1024u;
// Candidates past it are dropped and counted into regirGatherOverflow.
SHADER_PUBLIC SHADER_CONST uint REGIR_GATHER_SCRATCH = 2048u;
SHADER_PUBLIC SHADER_CONST uint REGIR_KEY_MESHLET = 0x80000000u;
// Below 1 flattens the table so fresh pixels do not concentrate on the brightest light.
SHADER_PUBLIC SHADER_CONST float REGIR_SELECT_TEMPER = 0.5;
SHADER_PUBLIC SHADER_CONST uint REGIR_HASH_CAPACITY = 16384u;
SHADER_PUBLIC SHADER_CONST uint REGIR_HASH_PROBE = 32u;
SHADER_PUBLIC SHADER_CONST uint REGIR_HASH_EMPTY = 0u;
SHADER_PUBLIC SHADER_CONST uint REGIR_HASH_INVALID = 0xFFFFFFFFu;
SHADER_PUBLIC SHADER_CONST float REGIR_LOD_BASE_DIST = 32.0;
SHADER_PUBLIC SHADER_CONST uint REGIR_MAX_LEVEL = 8u;
SHADER_PUBLIC SHADER_CONST float REGIR_MAX_DIST = 1024.0;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_X = 2.0;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_Y = 2.0;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_Z = 2.0;
SHADER_PUBLIC SHADER_CONST float REGIR_TARGET_MIN_DIST_SCALE = 0.5;
SHADER_PUBLIC SHADER_CONST float REGIR_TARGET_CONE_FLOOR = 0.05;
SHADER_PUBLIC SHADER_CONST float REGIR_TARGET_RANGE_FLOOR = 0.05;
SHADER_PUBLIC SHADER_CONST float REGIR_TARGET_FACING_FLOOR = 0.1;
SHADER_PUBLIC SHADER_CONST float REGIR_CONE_SLACK = 0.01;
SHADER_PUBLIC SHADER_CONST float REGIR_KEY_CAMERA_OFFSET_SCALE = 0.005;

SHADER_PUBLIC SHADER_CONST int RESTIR_M_LIGHT = 4;
SHADER_PUBLIC SHADER_CONST int RESTIR_M_BRDF = 1;
SHADER_PUBLIC SHADER_CONST uint RESTIR_MESHLET_TRI_CDF = 1u;

#endif //WILL_ENGINE_RETIR_INTEROP_H
