//
// Created by William on 2026-10-08.
//

#ifndef WILL_ENGINE_LOCAL_SHADOW_VIEWS_H
#define WILL_ENGINE_LOCAL_SHADOW_VIEWS_H

#include <glm/glm.hpp>

#include "render/shaders/shadow_interop.h"

namespace Render
{
/** Views a light needs this phase: 1 for an area or disk light coned to 60 degrees, 0 when it cannot be shadowed. */
uint32_t LocalShadowViewCount(const LightInfo& light);

/** Tile grid (columns, rows) of an atlas that holds viewBudget views. */
glm::uvec2 LocalShadowAtlasTiles(uint32_t viewBudget);

/**
 * Picks this frame's shadowed lights by contribution to the camera; last frame's picks are favoured so the set does not flip.
 * @param previous light indices picked last frame
 * @param outLights receives at most LOCAL_SHADOW_MAX_VIEWS light indices
 * @return number of lights picked
 */
uint32_t SelectLocalShadowLights(const LightInfo* lights, uint32_t lightCount, const glm::mat4& cameraViewProj, const glm::vec3& cameraPos, const uint32_t* previous, uint32_t previousCount,
                                 uint32_t viewBudget, uint32_t* outLights);

/**
 * Writes the light's views into consecutive atlas tiles starting at firstTile.
 * @param tiles atlas tile grid from LocalShadowAtlasTiles
 * @return views written
 */
uint32_t BuildLocalShadowViews(const LightInfo& light, uint32_t firstTile, glm::uvec2 tiles, uint32_t resolution, ShadowViewGPU* outViews);
} // Render

#endif //WILL_ENGINE_LOCAL_SHADOW_VIEWS_H
