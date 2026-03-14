//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_PAR_SHAPES_EXT_H
#define WILL_ENGINE_PAR_SHAPES_EXT_H

#include "par_shapes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Solid staircase extruded along the X axis.
 * @param steps number of steps (>= 1)
 * @param width extent along X
 * @param total_depth extent along Z (direction stairs travel)
 * @param total_height total rise along Y
 * @return Returns a mesh with correct per-face normals after par_shapes_unweld + par_shapes_compute_normals.
 */
/**
 * step_height > 0: all steps except the last use exactly step_height; the last step takes the
 * remainder (total_height - (steps-1)*step_height), preserving total_height exactly.
 * step_height == 0: uniform — each step is total_height/steps.
 */
/**
 * closed == 0: left (-X), right (+X), and back (z=total_depth) walls are omitted.
 */
par_shapes_mesh* par_shapes_create_staircase(int steps, float width, float total_depth, float total_height, float step_height, int closed);

#ifdef __cplusplus
}
#endif
#endif // WILL_ENGINE_PAR_SHAPES_EXT_H