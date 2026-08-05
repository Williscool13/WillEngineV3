//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_DDGI_PASSES_H
#define WILL_ENGINE_DDGI_PASSES_H

#include <glm/glm.hpp>

#include "render/render-graph/render_graph.h"
#include "render/shaders/ddgi_interop.h"
#include "render/passes/radiance_cache_passes.h"

namespace Core
{
struct DDGIParams;
struct LocalDDGIVolume;
}

namespace Render
{
class PipelineManager;

/** Per-frame GPU descriptor chain sampled by lighting/remodulate and the trace's infinite-bounce feedback. */
inline const StringID DDGI_CASCADES_BUFFER = SID("ddgi_cascades");
inline const StringID DDGI_CASCADES_PREV_BUFFER = SID("ddgi_cascades_prev");
inline constexpr int32_t DDGI_PROBE_DEBUG_LOCALS_ONLY = -2;

/** CPU-side cascade chain, finest first; bUpdated marks which entries trace/blend/relocate this frame. Local volumes occupy entries [count, count + localCount), slot-sticky by localIds so a resident volume keeps its history. */
struct DDGICascades
{
    DDGIVolumeParams volumes[DDGI_MAX_VOLUME_SLOTS]{};
    bool bUpdated[DDGI_MAX_VOLUME_SLOTS]{};
    uint64_t localIds[DDGI_MAX_VOLUME_SLOTS]{};
    uint32_t count{0};
    uint32_t localCount{0};
};

/**
 * Camera-following rolling windows; cascade 0 updates every frame, outer cascades round-robin so trace cost stays flat.
 * The nearest local volumes are appended as fixed fine-spacing windows, one updating per frame on its own round-robin.
 * @param params
 * @param cameraPosition
 * @param localVolumes hand-placed local volumes gathered this frame (may be null when none)
 * @param localVolumeCount
 * @param previous cascades used last frame, for frozen windows on skipped cascades and local slot stickiness
 * @param frameNumber
 */
DDGICascades ComputeDDGICascades(const Core::DDGIParams& params, const glm::vec3& cameraPosition, const Core::LocalDDGIVolume* localVolumes, uint32_t localVolumeCount, const DDGICascades& previous, uint64_t frameNumber, bool bFreeze);

/**
 * Probe trace + irradiance/visibility blend, run per updated entry (cascades and resident local volumes); also uploads DDGI_CASCADES_BUFFER/_PREV_BUFFER. No-op without the TLAS and geometry/material/light buffers.
 * @param graph
 * @param pipelineManager
 * @param arena frame arena backing the descriptor sources captured by the upload passes
 * @param params
 * @param cascades
 * @param previous cascades used last frame, for scroll/resize invalidation and feedback sampling
 * @param skyboxIndex
 * @param iblIntensity skybox ambient fallback for hash-miss hits outside DDGI coverage, matching the cache shade's indirect term
 * @param frameNumber
 * @param bBounceOnly debug: zero skybox radiance (and disable feedback) so probes show only one-bounce surface shading
 * @param radianceCache this frame's radiance cache buffers; trace populates it as a side effect when valid, keyed with scene.cameraWorldPos (never the per-cascade window center)
 * @param reflectionProbeCount baked probes this frame; inside a probe volume the trace's cache-miss fallback shading uses probe irradiance ahead of the skybox ambient
 * @param bReflectionProbeBruteForce debug: bypass the world-grid probe bin for the fallback shading's probe pick
 */
void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, Core::Arena& arena, const Core::DDGIParams& params, const DDGICascades& cascades, const DDGICascades& previous, int32_t skyboxIndex, float iblIntensity, uint64_t frameNumber, bool bBounceOnly, const RadianceCacheFrame& radianceCache, uint32_t reflectionProbeCount, bool bReflectionProbeBruteForce);

/**
 * Declares the pass dependencies for sampling the cascade chain. Returns false when the chain doesn't exist this frame.
 * @param graph
 * @param pass
 */
bool AddDDGISampleDependencies(RenderGraph& graph, RenderPass& pass);

/**
 * One debug sphere per probe per cascade; dead probes render flat red, classification-inactive flat blue.
 * @param graph
 * @param pipelineManager
 * @param cascades
 * @param probeDebugExposure linear scale applied to the fitted probe irradiance so bright probes do not blow out to flat white
 * @param debugCascade -1 draws every entry (cascades and resident locals) with a per-entry identification tint; DDGI_PROBE_DEBUG_LOCALS_ONLY (-2) draws only local entries, tinted; >= 0 draws only that entry, untinted
 * @param bHideInactive skip classification-inactive probes entirely instead of drawing them flat blue
 * @param probeDebugMode 0 fits the irradiance atlas; 1 fits the visibility atlas (red = mean distance / miss clamp, green = std/mean); 2 draws flat volume tint for reading placement and window coverage
 */
void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGICascades& cascades, float probeDebugExposure, int32_t debugCascade, bool bHideInactive, int32_t probeDebugMode);
} // Render

#endif //WILL_ENGINE_DDGI_PASSES_H
