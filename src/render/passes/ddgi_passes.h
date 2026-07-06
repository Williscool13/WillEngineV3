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
 * Probe trace + irradiance blend. Traces params.raysPerProbe rays per probe against the TLAS (miss-only skybox radiance for now), then integrates them into the octahedral irradiance atlas "ddgi_irradiance" (stored as pow(E, 1/irradianceGamma)), hysteresis-blended in encoded space against the carried previous frame with RTXGI's change/brightness thresholds.
 * Slots invalidated by a window scroll, a count/spacing change, or a missing history atlas restart fresh. Requires the TLAS; no-op without it.
 * @param graph
 * @param pipelineManager
 * @param params
 * @param volume
 * @param previousVolume volume used last frame, for scroll/resize invalidation
 * @param skyboxIndex
 * @param frameNumber
 */
void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, const Core::DDGIParams& params, const DDGIVolumeParams& volume, const DDGIVolumeParams& previousVolume, int32_t skyboxIndex, uint64_t frameNumber);

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
