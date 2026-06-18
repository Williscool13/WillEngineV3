//
// Created by William on 2026-05-25.
//

#ifndef WILL_ENGINE_RETIR_INTEROP_H
#define WILL_ENGINE_RETIR_INTEROP_H

#ifdef __SLANG__
module restir_interop;
#define SHADER_PUBLIC public
#define SHADER_CONST static const
#else
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

/**
 * Grid reservoir for ReGIR (RTG2 Ch.23). Unlike the per-pixel Reservoir it does NOT pre-divide into W;
 * it stores the raw RIS pieces from the fill pass so the shading-side RIS can resample correctly:
 *   totalWeight = wSum / M_build  (the cell's running reservoir weight)
 *   targetPdf   = the survivor's cell-center build target (intensity * geom, the EvalCellTarget value)
 * At shading, sourcePdf = targetPdf / cellAverageWeight (averaged over the cell's slots in a separate pass).
 * Carrying the per-slot W instead is what inflates variance / fires bright spots, hence this split.
 * lightIdx == ~0u indicates an empty reservoir.
 */
SHADER_PUBLIC struct ReGIRReservoir
{
    SHADER_PUBLIC uint sampleOffsetPacked;
    SHADER_PUBLIC uint lightIdx;
    SHADER_PUBLIC float totalWeight;
    SHADER_PUBLIC float targetPdf;
};

// Per-axis cell counts and cell sizes; the grid need not be a cube.
SHADER_PUBLIC SHADER_CONST uint REGIR_GRID_DIM_X = 16u;
SHADER_PUBLIC SHADER_CONST uint REGIR_GRID_DIM_Y = 16u;
SHADER_PUBLIC SHADER_CONST uint REGIR_GRID_DIM_Z = 16u;
SHADER_PUBLIC SHADER_CONST uint REGIR_CELL_COUNT = REGIR_GRID_DIM_X * REGIR_GRID_DIM_Y * REGIR_GRID_DIM_Z;
SHADER_PUBLIC SHADER_CONST uint REGIR_RESERVOIRS_PER_CELL = 512u;


// === Modifiable at Runtime ===
SHADER_PUBLIC SHADER_CONST uint REGIR_FILL_CANDIDATES = 8u;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_X = 2.0;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_Y = 2.0;
SHADER_PUBLIC SHADER_CONST float REGIR_CELL_SIZE_Z = 2.0;
// World-space grid-centre offset is a runtime push-constant (ReSTIRParams.regirGridOffset), not a constant here.
SHADER_PUBLIC SHADER_CONST uint REGIR_HISTORY_LENGTH = 8u;

// Initial-candidate counts for ReSTIR DI Talbot MIS: light (uniform) samples and BRDF-guided samples.
SHADER_PUBLIC SHADER_CONST int RESTIR_M_LIGHT = 16;
SHADER_PUBLIC SHADER_CONST int RESTIR_M_BRDF = 0;

#endif //WILL_ENGINE_RETIR_INTEROP_H
