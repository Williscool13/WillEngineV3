//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_DDGI_PASSES_H
#define WILL_ENGINE_DDGI_PASSES_H

#include <glm/glm.hpp>

#include "render/render-graph/render_graph.h"
#include "render/shaders/ddgi_interop.h"

namespace Render
{
class PipelineManager;

/**
 * Camera-following rolling window: baseCell tracks the camera in whole probe steps so probes never move in world space.
 * @param cameraPosition
 */
DDGIVolumeParams ComputeDDGIVolumeParams(const glm::vec3& cameraPosition);

/**
 * Appends one flat-colored debug sphere per probe, color keyed to the lattice cell so probe identity stays visible while the window scrolls.
 * Requires the GPU debug buffers; skip when the debug lock is active.
 * @param graph
 * @param pipelineManager
 * @param volume
 */
void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGIVolumeParams& volume);
} // Render

#endif //WILL_ENGINE_DDGI_PASSES_H
