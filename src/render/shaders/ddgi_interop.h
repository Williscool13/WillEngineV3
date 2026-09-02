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

SHADER_PUBLIC SHADER_CONST uint DDGI_MAX_RAYS_PER_PROBE = 256u;
/** Ray budget for probes classified inactive (no nearby geometry) */
SHADER_PUBLIC SHADER_CONST uint DDGI_SENTINEL_RAYS = 16u;
// Camera
SHADER_PUBLIC SHADER_CONST uint DDGI_MAX_CAMERA_CASCADES = 6u;
// World Volume
SHADER_PUBLIC SHADER_CONST uint32_t DDGI_MAX_RESIDENT_LOCAL_VOLUMES = 100u;
/** Both kinds share one descriptor array: camera cascades occupy the first entries, resident world volumes the rest. */
SHADER_PUBLIC SHADER_CONST uint DDGI_MAX_VOLUME_SLOTS = 106u;

SHADER_PUBLIC SHADER_CONST uint32_t DDGI_LOCAL_WARMUP_UPDATES = 16u;
SHADER_PUBLIC SHADER_CONST uint32_t DDGI_LOCAL_AGE_CAP = 64u;

SHADER_PUBLIC SHADER_CONST uint DDGI_IRRADIANCE_TILE = 8u;
SHADER_PUBLIC SHADER_CONST uint DDGI_IRRADIANCE_INTERIOR = 6u;
SHADER_PUBLIC SHADER_CONST uint DDGI_VISIBILITY_TILE = 16u;
SHADER_PUBLIC SHADER_CONST uint DDGI_VISIBILITY_INTERIOR = 14u;

/**
 * Rolling probe window over a world-space lattice. Probe cell g sits at origin + g * probeSpacing; the window spans probeCount cells from baseCell.
 * Camera cascades use origin 0 and scroll baseCell; world volumes use origin = authored corner and baseCell 0, so the authored box is exact.
 * Storage slot s holds cell baseCell + EuclideanMod(s - baseCell, probeCount), so a scroll only changes the cells of the newly exposed planes.
 * Also carries the sampling parameters (biases, encoding gamma) so consumers need only this struct plus the two atlas textures.
 */
SHADER_PUBLIC struct DDGIVolumeParams
{
    SHADER_PUBLIC int3 baseCell;
    SHADER_PUBLIC float normalBias;
    SHADER_PUBLIC uint3 probeCount;
    SHADER_PUBLIC float viewBias;
    SHADER_PUBLIC float3 origin;
    SHADER_PUBLIC float probeSpacing;
    SHADER_PUBLIC float irradianceGamma;
    SHADER_PUBLIC float edgeFadeCells;
    /** Row atlasSlot of atlasRows in the atlas. Every volume now owns its atlas outright (slot 0 of 1); the fields stay so the tile math is layout-agnostic. */
    SHADER_PUBLIC uint atlasSlot;
    SHADER_PUBLIC uint atlasRows;
};

/** One sampleable cascade: the volume it was built with plus the atlas/offsets it should be read through. Explicit pads keep the C++ size at the std430 array stride (96). */
SHADER_PUBLIC struct DDGICascadeDescriptor
{
    SHADER_PUBLIC DDGIVolumeParams volume;
    SHADER_PUBLIC SHADER_PTR(float4) probeOffsets;
    SHADER_PUBLIC uint irradianceIndex;
    SHADER_PUBLIC uint visibilityIndex;
    SHADER_PUBLIC uint bOffsetsValid;
    SHADER_PUBLIC uint bValid;
    SHADER_PUBLIC uint pad0;
    SHADER_PUBLIC uint pad1;
};

/** Per-frame cascade chain, finest first; consumers get one pointer to this instead of inline volume fields.
 * Local volumes (fixed hand-placed windows, finer than cascade 0) occupy [cascadeCount, cascadeCount + localCount) and are sampled before the cascades. */
SHADER_PUBLIC struct DDGICascadeSetGPU
{
    SHADER_PUBLIC uint cascadeCount;
    SHADER_PUBLIC uint localCount;
    SHADER_PUBLIC uint bVolumeGridValid;
    SHADER_PUBLIC uint pad2;
    SHADER_PUBLIC DDGICascadeDescriptor cascades[DDGI_MAX_VOLUME_SLOTS];
    SHADER_PUBLIC SHADER_PTR(uint2) volumeGrid;
    SHADER_PUBLIC SHADER_PTR(uint) volumeIndexList;
    SHADER_PUBLIC float4 gridCamPos;
};

#endif //WILL_ENGINE_DDGI_INTEROP_H
