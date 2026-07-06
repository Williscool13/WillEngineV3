//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_DDGI_PASSES_H
#define WILL_ENGINE_DDGI_PASSES_H

#include <glm/glm.hpp>

#include "render/render-graph/render_graph.h"
#include "render/shaders/ddgi_interop.h"

namespace Core
{
struct DDGIParams;
}

namespace Render
{
class PipelineManager;

/**
 * Camera-following rolling window: baseCell tracks the camera in whole probe steps so probes never move in world space.
 * Counts and spacing come from the params, clamped to dispatch/debug-sphere-safe ranges.
 * @param params
 * @param cameraPosition
 */
DDGIVolumeParams ComputeDDGIVolumeParams(const Core::DDGIParams& params, const glm::vec3& cameraPosition);

/**
 * Probe trace + irradiance blend. Traces params.raysPerProbe rays per probe (misses sample the skybox, light-proxy hits return the light's radiance, hits shade one sun bounce + emissive), then integrates them into the octahedral atlas "ddgi_irradiance" (stored as pow(E, 1/irradianceGamma)), hysteresis-blended in encoded space with RTXGI's change/brightness thresholds.
 * Slots invalidated by a window scroll, count/spacing change, or missing history restart fresh. No-op without the TLAS and geometry/material/light buffers.
 * @param graph
 * @param pipelineManager
 * @param params
 * @param volume
 * @param previousVolume volume used last frame, for scroll/resize invalidation
 * @param skyboxIndex
 * @param frameNumber
 * @param bBounceOnly debug: zero skybox and light-proxy radiance so probes show only one-bounce surface shading
 */
void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, const Core::DDGIParams& params, const DDGIVolumeParams& volume, const DDGIVolumeParams& previousVolume, int32_t skyboxIndex, uint64_t frameNumber, bool bBounceOnly);

/**
 * Appends one debug sphere per probe, shaded with an L1 SH fit of the probe's decoded irradiance atlas tile.
 * Requires the GPU debug buffers and this frame's "ddgi_irradiance" atlas; skip when the debug lock is active.
 * @param graph
 * @param pipelineManager
 * @param params
 * @param volume
 */
void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const Core::DDGIParams& params, const DDGIVolumeParams& volume);
} // Render

#endif //WILL_ENGINE_DDGI_PASSES_H
