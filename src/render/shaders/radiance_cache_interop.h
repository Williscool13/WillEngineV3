//
// Created by William on 2026-07-10.
//

#ifndef WILL_ENGINE_RADIANCE_CACHE_INTEROP_H
#define WILL_ENGINE_RADIANCE_CACHE_INTEROP_H

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

// Radiance Cache: spatial hash grid.
// Key = cell pos + LOD + normal bucket (Normal is quantized to dominant axis).

SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_HASH_CAPACITY = 524288u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_HASH_PROBE = 32u; // Find still stops at the first empty slot
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_HASH_EMPTY = 0u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_HASH_INVALID = 0xFFFFFFFFu;
SHADER_PUBLIC SHADER_CONST float RADIANCE_CACHE_CELL_SIZE_BASE = 0.25;
SHADER_PUBLIC SHADER_CONST float RADIANCE_CACHE_LOD_BASE_DIST = 4.0;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_MAX_LEVEL = 8u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_NORMAL_BUCKETS = 6u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_LRU_THRESHOLD = 60u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_LOD_REVALIDATE_MARGIN = 2u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_ACCUM_FRAMES = 16u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_SHADE_INTERVAL = 8u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_SHADE_BUDGET = 20480u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_NEE_SAMPLES = 4u;
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_WARMSTART_SEED_CAP = 8u; // ~half RADIANCE_CACHE_ACCUM_FRAMES so a coarser parent estimate can't dominate
SHADER_PUBLIC SHADER_CONST uint RADIANCE_CACHE_RADIANCE_UNSHADED = 0xFFFFFFFFu;

SHADER_PUBLIC struct RadianceCacheCell
{
    SHADER_PUBLIC uint2 packedRadiance; // fp16x3 non-emissive radiance / diffuseColor / RADIANCE_CACHE_PACK_SCALE; .y high half 0xFFFF = unshaded
    SHADER_PUBLIC uint lastTouched;
    SHADER_PUBLIC uint lastShaded;
    SHADER_PUBLIC uint changeStreak; // bits 0-7 consecutive large-delta touches, bit 8 last delta direction, bits 16-23 accumulated shade count
};

SHADER_PUBLIC struct RadianceCacheHitDescriptor
{
    SHADER_PUBLIC uint instanceID;
    SHADER_PUBLIC uint primitiveIndex;
    SHADER_PUBLIC uint packedBary;
    SHADER_PUBLIC uint bMirror;
};

SHADER_PUBLIC struct RadianceCacheStats
{
    SHADER_PUBLIC uint occupiedSlots; // previous frame's live set
    SHADER_PUBLIC uint cellsCarried;
    SHADER_PUBLIC uint cellsEvicted;
    SHADER_PUBLIC uint insertsFailed;
};

SHADER_PUBLIC struct RadianceCacheBuffers
{
    SHADER_PUBLIC SHADER_PTR(uint) entries;
    SHADER_PUBLIC SHADER_PTR(uint2) keys;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheCell) cells;
    SHADER_PUBLIC SHADER_PTR(uint) active;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheHitDescriptor) descriptors;
    SHADER_PUBLIC SHADER_PTR(uint) activeList;
    SHADER_PUBLIC SHADER_PTR(uint) activeCount;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheStats) stats;
};

#endif //WILL_ENGINE_RADIANCE_CACHE_INTEROP_H
