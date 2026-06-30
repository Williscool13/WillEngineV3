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

/**
 * Per-pixel reservoir written by the ReSTIR DI generate pass and read by the shade pass.
 *
 * sampleOffsetPacked: snorm16x2 encoding of the light-local sample position, normalized by half-extents.
 *   u = dot(samplePos - lightCenter, lightRight) / halfWidth  in [-1, 1]
 *   v = dot(samplePos - lightCenter, lightUp)    / halfHeight in [-1, 1]
 *   Packed as: lower 16 bits = u, upper 16 bits = v (both signed, scaled by 32767).
 * lightIdx == ~0u indicates an empty reservoir.
 * M is the candidate count; used by temporal/spatial reuse combination.
 */
SHADER_PUBLIC struct Reservoir
{
    SHADER_PUBLIC uint sampleOffsetPacked;
    SHADER_PUBLIC uint lightIdx;
    SHADER_PUBLIC float W;
    SHADER_PUBLIC uint M;
};

SHADER_PUBLIC SHADER_CONST uint LIGHT_IDX_SUN = 0xFFFFFFFEu;

/**
 * Grid reservoir for ReGIR (RTG2 Ch.23). Stores raw RIS pieces from fill (not a pre-divided W):
 *   totalWeight = wSum / M_build  (running reservoir weight)
 *   targetPdf   = survivor's cell-center build target (intensity * geom, EvalCellTarget); kept separate for BRDF-MIS denom + temporal merge
 * Shading-side grid tap source pdf = targetPdf / totalWeight (per-reservoir 1/W), unbiased at any fill count.
 * lightIdx == ~0u indicates an empty reservoir.
 */
SHADER_PUBLIC struct ReGIRReservoir
{
    SHADER_PUBLIC uint sampleOffsetPacked;
    SHADER_PUBLIC uint lightIdx;
    SHADER_PUBLIC float totalWeight;
    SHADER_PUBLIC float targetPdf;
};

/**
 * Vose alias-table entry
 */
SHADER_PUBLIC struct LightAliasEntry
{
    SHADER_PUBLIC float prob;     // Vose split threshold for the primary light (this bin)
    SHADER_PUBLIC uint alias;     // alias light index drawn when u >= prob
    SHADER_PUBLIC float pdf;      // p(primary) = power(primary) / totalPower
    SHADER_PUBLIC float pdfAlias; // p(alias)   = power(alias)   / totalPower
};

/**
 * One presampled-tile slot
 */
SHADER_PUBLIC struct ReGIRTileSlot
{
    SHADER_PUBLIC uint lightIdx;
    SHADER_PUBLIC float sourcePdf;
};

SHADER_PUBLIC SHADER_CONST uint REGIR_RESERVOIRS_PER_CELL = 512u;

// ReGIR Hash Grid
// A cell is a hash-table slot in [0, REGIR_HASH_CAPACITY).
// Each owns REGIR_RESERVOIRS_PER_CELL reservoirs at base cellSlot * REGIR_RESERVOIRS_PER_CELL.
SHADER_PUBLIC SHADER_CONST uint REGIR_HASH_CAPACITY = 16384u;
SHADER_PUBLIC SHADER_CONST uint REGIR_HASH_PROBE = 8u;
SHADER_PUBLIC SHADER_CONST uint REGIR_HASH_EMPTY = 0u;
SHADER_PUBLIC SHADER_CONST uint REGIR_HASH_INVALID = 0xFFFFFFFFu;
SHADER_PUBLIC SHADER_CONST float REGIR_LOD_BASE_DIST = 32.0;
SHADER_PUBLIC SHADER_CONST uint REGIR_MAX_LEVEL = 8u;
SHADER_PUBLIC SHADER_CONST float REGIR_MAX_DIST = 1024.0;
SHADER_PUBLIC SHADER_CONST uint REGIR_FILL_CANDIDATES = 8u;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_X = 2.0;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_Y = 2.0;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_Z = 2.0;

// Presampled light tiles
SHADER_PUBLIC SHADER_CONST uint REGIR_TILE_COUNT = 128u;
SHADER_PUBLIC SHADER_CONST uint REGIR_TILE_SIZE = 1024u;

// Initial-candidate counts for ReSTIR DI Talbot MIS: light (uniform) samples and BRDF-guided samples.
SHADER_PUBLIC SHADER_CONST int RESTIR_M_LIGHT = 8;
SHADER_PUBLIC SHADER_CONST int RESTIR_M_BRDF = 1;

// BRDF ray only if not rough
SHADER_PUBLIC SHADER_CONST float RESTIR_BRDF_ROUGHNESS_MAX = 0.3;

#endif //WILL_ENGINE_RETIR_INTEROP_H
