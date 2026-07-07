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
 * Probe trace + irradiance/visibility blend. Traces params.raysPerProbe rays per probe (misses sample the skybox; hits shade sun + one NEE-sampled local light + emissive plus last frame's carried atlas when bInfiniteBounce; light-proxy hits are distance-only), then integrates
 * them into the octahedral atlases "ddgi_irradiance" (stored as pow(E, 1/irradianceGamma), hysteresis-blended in encoded space with RTXGI's change/brightness thresholds) and "ddgi_visibility" (mean/mean^2 ray distance for the Chebyshev occlusion test, DDGISampleIrradiance).
 * With params.bRelocation, a per-probe pass applies RTXGI relocation to this frame's ray distances and writes "ddgi_probe_offsets" (world offset xyz capped at 45% of spacing, backface fraction w); rays trace from last frame's carried offsets, samplers use this frame's and skip dead probes.
 * Slots invalidated by a window scroll, count/spacing change, or missing history restart fresh. No-op without the TLAS and geometry/material/light buffers.
 * @param graph
 * @param pipelineManager
 * @param params
 * @param volume
 * @param previousVolume volume used last frame, for scroll/resize invalidation and feedback sampling
 * @param skyboxIndex
 * @param frameNumber
 * @param bBounceOnly debug: zero skybox radiance (and disable feedback) so probes show only one-bounce surface shading
 */
void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, const Core::DDGIParams& params, const DDGIVolumeParams& volume, const DDGIVolumeParams& previousVolume, int32_t skyboxIndex, uint64_t frameNumber, bool bBounceOnly);

/**
 * Appends one debug sphere per probe at its relocated position, shaded with an L1 SH fit of the probe's decoded irradiance atlas tile; dead probes (backface fraction past the sampler's skip threshold) render flat red.
 * Requires the GPU debug buffers and this frame's "ddgi_irradiance" atlas; skip when the debug lock is active.
 * @param graph
 * @param pipelineManager
 * @param volume
 * @param probeDebugExposure linear scale applied to the fitted probe irradiance so bright probes do not blow out to flat white in the debug view
 */
void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGIVolumeParams& volume, float probeDebugExposure);
} // Render

#endif //WILL_ENGINE_DDGI_PASSES_H
