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

/** Per-frame GPU descriptor chain (DDGICascadeSetGPU) sampled by lighting/remodulate and the trace's infinite-bounce feedback. */
inline const StringID DDGI_CASCADES_BUFFER = SID("ddgi_cascades");
inline const StringID DDGI_CASCADES_PREV_BUFFER = SID("ddgi_cascades_prev");

/** CPU-side cascade chain, finest first. Cascade k doubles cascade k-1's spacing with identical probe counts; bUpdated marks which cascades trace/blend/relocate this frame. */
struct DDGICascades
{
    DDGIVolumeParams volumes[DDGI_MAX_CASCADES]{};
    bool bUpdated[DDGI_MAX_CASCADES]{};
    uint32_t count{0};
};

/**
 * Camera-following rolling windows: each cascade's baseCell tracks the camera in whole probe steps of its own spacing, so probes never move in world space.
 * Cascade 0 updates every frame; outer cascades round-robin one per frame so trace cost stays flat at two volumes. A skipped cascade keeps last frame's window (frozen baseCell) so its carried atlas stays addressable.
 * Biases scale with each cascade's spacing when bScaleBiasPerCascade. Counts and spacing come from the params, clamped to dispatch/debug-sphere-safe ranges.
 * @param params
 * @param cameraPosition
 * @param previous cascades used last frame, for frozen windows on skipped cascades
 * @param frameNumber
 */
DDGICascades ComputeDDGICascades(const Core::DDGIParams& params, const glm::vec3& cameraPosition, const DDGICascades& previous, uint64_t frameNumber);

/**
 * Probe trace + irradiance/visibility blend, run per updated cascade. Traces params.raysPerProbe rays per probe (params.outerRaysPerProbe past cascade 0; misses sample the skybox; hits shade sun + one NEE-sampled local light (cascade 0 only unless bOuterLocalNEE) + emissive plus last frame's cascade chain when bInfiniteBounce; light-proxy hits are distance-only), then integrates
 * them into the per-cascade octahedral atlases "ddgi_irradiance_<k>" (stored as pow(E, 1/irradianceGamma), hysteresis-blended in encoded space with RTXGI's change/brightness thresholds) and "ddgi_visibility_<k>" (mean/mean^2 ray distance for the Chebyshev occlusion test).
 * With params.bRelocation, a per-probe pass applies RTXGI relocation to this frame's ray distances and writes "ddgi_probe_offsets_<k>" (world offset xyz capped at 45% of spacing, backface fraction w); rays trace from last frame's carried offsets, samplers use this frame's and skip dead probes.
 * Teleports and dead-to-alive flips write a one-frame restart flag ("ddgi_probe_restart_<k>", carried) that drops the probe's blend hysteresis on the cascade's next update.
 * With params.bClassification (rides the relocate pass, so it also needs bRelocation), the same pass classifies each probe by whether any ray hit geometry within ~1.5 cells ("ddgi_probe_active_<k>", carried); inactive probes trace only DDGI_SENTINEL_RAYS rays and their blends carry the history tiles through frozen, and a sentinel hit reactivates the probe via the restart flag.
 * Skipped cascades re-carry their history resources unchanged. Also uploads the DDGI_CASCADES_BUFFER descriptor chain (current-or-history per cascade) consumed via DDGISampleIrradianceCascaded, and DDGI_CASCADES_PREV_BUFFER for the trace feedback.
 * Slots invalidated by a window scroll, count/spacing change, or missing history restart fresh. No-op without the TLAS and geometry/material/light buffers.
 * @param graph
 * @param pipelineManager
 * @param arena frame arena backing the descriptor sources captured by the upload passes
 * @param params
 * @param cascades
 * @param previous cascades used last frame, for scroll/resize invalidation and feedback sampling
 * @param skyboxIndex
 * @param frameNumber
 * @param bBounceOnly debug: zero skybox radiance (and disable feedback) so probes show only one-bounce surface shading
 */
void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, Core::Arena& arena, const Core::DDGIParams& params, const DDGICascades& cascades, const DDGICascades& previous, int32_t skyboxIndex, uint64_t frameNumber, bool bBounceOnly);

/**
 * Declares the pass dependencies for sampling the cascade chain (descriptor buffer + every live atlas/offsets buffer, current and history). Returns false when the chain doesn't exist this frame.
 * @param graph
 * @param pass
 */
bool AddDDGISampleDependencies(RenderGraph& graph, RenderPass& pass);

/**
 * Appends one debug sphere per probe per cascade at its relocated position, shaded with an L1 SH fit of the probe's decoded irradiance atlas tile; dead probes (backface fraction past the sampler's skip threshold) render flat red, classification-inactive probes flat blue.
 * Skipped cascades draw from their carried history atlas. Requires the GPU debug buffers; skip when the debug lock is active.
 * @param graph
 * @param pipelineManager
 * @param cascades
 * @param probeDebugExposure linear scale applied to the fitted probe irradiance so bright probes do not blow out to flat white in the debug view
 * @param debugCascade -1 draws every cascade with a per-cascade identification tint (white/red/green/blue); 0-3 draws only that cascade, untinted
 * @param bHideInactive skip classification-inactive probes entirely instead of drawing them flat blue
 */
void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGICascades& cascades, float probeDebugExposure, int32_t debugCascade, bool bHideInactive);
} // Render

#endif //WILL_ENGINE_DDGI_PASSES_H
