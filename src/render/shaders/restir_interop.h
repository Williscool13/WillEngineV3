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
// World-space offset
SHADER_PUBLIC SHADER_CONST float REGIR_GRID_OFFSET_X = 0.0;
SHADER_PUBLIC SHADER_CONST float REGIR_GRID_OFFSET_Y = 0.0;
SHADER_PUBLIC SHADER_CONST float REGIR_GRID_OFFSET_Z = 0.0;
SHADER_PUBLIC SHADER_CONST uint REGIR_HISTORY_LENGTH = 8u;

// Initial-candidate counts for ReSTIR DI Talbot MIS: light (uniform) samples and BRDF-guided samples.
SHADER_PUBLIC SHADER_CONST int RESTIR_M_LIGHT = 16;
SHADER_PUBLIC SHADER_CONST int RESTIR_M_BRDF = 0;

#endif //WILL_ENGINE_RETIR_INTEROP_H
