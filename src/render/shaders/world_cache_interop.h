//
// Created by William on 2026-07-10.
//

#ifndef WILL_ENGINE_WORLD_CACHE_INTEROP_H
#define WILL_ENGINE_WORLD_CACHE_INTEROP_H

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

// World Radiance Cache: spatial hash grid.
// Key = cell pos + LOD + normal bucket (Normal is quantized to dominant axis).

SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_HASH_CAPACITY = 131072u;
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_HASH_PROBE = 8u;
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_HASH_EMPTY = 0u;
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_HASH_INVALID = 0xFFFFFFFFu;
SHADER_PUBLIC SHADER_CONST float WORLD_CACHE_CELL_SIZE_BASE = 0.25;
SHADER_PUBLIC SHADER_CONST float WORLD_CACHE_LOD_BASE_DIST = 4.0;
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_MAX_LEVEL = 8u;
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_NORMAL_BUCKETS = 6u;
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_LRU_THRESHOLD = 60u;
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_LOD_REVALIDATE_MARGIN = 2u;
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_ACCUM_FRAMES = 25u; // count-based blend cap; steady state keeps 24/25 of history (matches the old 0.96 hysteresis)
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_RADIANCE_UNSHADED = 0xFFFFFFFFu;
SHADER_PUBLIC SHADER_CONST float WORLD_CACHE_CHANGE_THRESHOLD = 0.35; // relative luma delta that counts toward a change streak
SHADER_PUBLIC SHADER_CONST uint WORLD_CACHE_CHANGE_STREAK = 3u; // consecutive same-direction large deltas before the EMA history is dumped

SHADER_PUBLIC struct WorldCacheCell
{
    SHADER_PUBLIC uint packedRadiance; // RGB9E5, outgoing radiosity for probe-ray read-back
    SHADER_PUBLIC uint lastTouched;
    SHADER_PUBLIC uint changeStreak; // bits 0-7 consecutive large-delta touches, bit 8 last delta direction, bits 16-23 accumulated shade count
};

SHADER_PUBLIC struct WorldCacheHitDescriptor
{
    SHADER_PUBLIC uint instanceID;
    SHADER_PUBLIC uint primitiveIndex;
    SHADER_PUBLIC uint packedBary;
    SHADER_PUBLIC uint pad;
};

SHADER_PUBLIC struct WorldCacheBuffers
{
    SHADER_PUBLIC SHADER_PTR(uint) entries;
    SHADER_PUBLIC SHADER_PTR(uint2) keys;
    SHADER_PUBLIC SHADER_PTR(WorldCacheCell) cells;
    SHADER_PUBLIC SHADER_PTR(uint) active;
    SHADER_PUBLIC SHADER_PTR(WorldCacheHitDescriptor) descriptors;
};

#endif //WILL_ENGINE_WORLD_CACHE_INTEROP_H
