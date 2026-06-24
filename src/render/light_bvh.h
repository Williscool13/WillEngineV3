//
// Created by William on 2026-06-22.
//

#ifndef WILL_ENGINE_LIGHT_BVH_H
#define WILL_ENGINE_LIGHT_BVH_H

#include <cstdint>

#include "shaders/lights_interop.h"
#include "shaders/restir_interop.h"

namespace Render
{
/**
 * Builds an implicit complete-tree light BVH from world-space lights based on bounds + power.
 * Children of node i are at 2i+1 / 2i+2; leaves occupy [numLeaves-1 .. 2*numLeaves-2] in Morton order.
 *
 * @param lights World-space light array.
 * @param count Number of lights (<= MAX_LIGHTS).
 * @param outNodes Receives 2*NextPowerOfTwo(count)-1 nodes; caller allocates LIGHT_BVH_NODE_COUNT.
 * @param outLightLeaf Per-light leaf-slot inverse map; caller allocates MAX_LIGHTS uints, valid for [0, count).
 * @return numLeaves = NextPowerOfTwo(count), or 0 when count == 0.
 */
uint32_t BuildLightBVH(const LightInfo* lights, uint32_t count, LightBVHNode* outNodes, uint32_t* outLightLeaf);
} // namespace Render

#endif // WILL_ENGINE_LIGHT_BVH_H
