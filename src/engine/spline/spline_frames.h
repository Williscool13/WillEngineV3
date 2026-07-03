//
// Created by William on 2026-07-03.
//

#ifndef WILL_ENGINE_SPLINE_FRAMES_H
#define WILL_ENGINE_SPLINE_FRAMES_H

#include <glm/glm.hpp>

#include "spline.h"
#include "core/containers/vector.h"

namespace Engine
{
/**
 * A parallel-transport frame sampled at one ring along a spline. `right`/`up` are the twist-free transported basis/ `rRight`/`rUp` fold in the base roll angle plus interpolated per-point roll (`roll`, radians).
 */
struct SplineFrame
{
    glm::vec3 pos;
    glm::vec3 tangent;
    glm::vec3 right;
    glm::vec3 up;
    glm::vec3 rRight;
    glm::vec3 rUp;
    float roll;
};


/**
 * Samples rotation-minimizing frames along a spline: one ring per `segmentsPerSpan` step (plus a terminal ring on open splines).
 * `out` is cleared and resized to the ring count.
 * @param spline
 * @param segmentsPerSpan
 * @param rollAngle
 * @param out
 * @return
 */
bool SampleSplineFrames(const Spline& spline, int segmentsPerSpan, float rollAngle, Core::Vector<SplineFrame>& out);
} // Engine

#endif //WILL_ENGINE_SPLINE_FRAMES_H
