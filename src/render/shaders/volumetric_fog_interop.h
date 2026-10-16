//
// Created by William on 2026-10-03.
//

#ifndef WILL_ENGINE_VOLUMETRIC_FOG_INTEROP_H
#define WILL_ENGINE_VOLUMETRIC_FOG_INTEROP_H

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

SHADER_PUBLIC SHADER_CONST uint VOLUMETRIC_FOG_TILE_SIZE = 16u;
SHADER_PUBLIC SHADER_CONST uint VOLUMETRIC_FOG_SLICES = 64u;
SHADER_PUBLIC SHADER_CONST float VOLUMETRIC_FOG_NEAR = 1.0;
SHADER_PUBLIC SHADER_CONST float VOLUMETRIC_FOG_SKY_DISTANCE = 10000.0;
SHADER_PUBLIC SHADER_CONST uint VOLUMETRIC_FOG_MAX_LIGHT_CANDIDATES = 8u;
// Weight of the new jittered sample against the reprojected history.
SHADER_PUBLIC SHADER_CONST float VOLUMETRIC_FOG_HISTORY_ALPHA = 0.05;
SHADER_PUBLIC SHADER_CONST uint VOLUMETRIC_FOG_MAX_VOLUMES = 32u;
// Midpoint samples per pixel through each absorbing volume; exact when no edge fade is in play.
SHADER_PUBLIC SHADER_CONST uint VOLUMETRIC_FOG_ABSORPTION_SAMPLES = 12u;
SHADER_PUBLIC SHADER_CONST uint FOG_VOLUME_SHAPE_BOX = 0u;
SHADER_PUBLIC SHADER_CONST uint FOG_VOLUME_SHAPE_CYLINDER = 1u;

/** Local fog volume in its unit space: box [-1,1]^3, cylinder of radius 1 around local Y over y in [-1,1]. */
SHADER_PUBLIC struct FogVolumeGPU
{
    SHADER_PUBLIC float4 worldToLocal0; // rows of the world -> unit space affine map
    SHADER_PUBLIC float4 worldToLocal1;
    SHADER_PUBLIC float4 worldToLocal2;
    SHADER_PUBLIC float4 albedoScattering; // rgb albedo, a scattering per metre (froxel path)
    SHADER_PUBLIC float4 fade; // xyz edge fade width in unit space per axis (x = radial for cylinders), w unused
    SHADER_PUBLIC float4 scales; // x density scale at local y = -1, y at y = +1, z ambient scale, w local light scale
    SHADER_PUBLIC float absorption; // per metre (analytic per-pixel path)
    SHADER_PUBLIC uint shape; // FOG_VOLUME_SHAPE_*
    SHADER_PUBLIC uint _pad0;
    SHADER_PUBLIC uint _pad1;
};

#endif //WILL_ENGINE_VOLUMETRIC_FOG_INTEROP_H
