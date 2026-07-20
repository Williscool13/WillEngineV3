//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_DDGI_PASSES_H
#define WILL_ENGINE_DDGI_PASSES_H

#include <glm/glm.hpp>

#include "render/render-graph/render_graph.h"
#include "render/shaders/ddgi_interop.h"
#include "render/passes/world_cache_passes.h"

namespace Core
{
struct DDGIParams;
}

namespace Render
{
class PipelineManager;

/** Per-frame GPU descriptor chain sampled by lighting/remodulate and the trace's infinite-bounce feedback. */
inline const StringID DDGI_CASCADES_BUFFER = SID("ddgi_cascades");
inline const StringID DDGI_CASCADES_PREV_BUFFER = SID("ddgi_cascades_prev");

/** CPU-side cascade chain, finest first; bUpdated marks which cascades trace/blend/relocate this frame. */
struct DDGICascades
{
    DDGIVolumeParams volumes[DDGI_MAX_CASCADES]{};
    bool bUpdated[DDGI_MAX_CASCADES]{};
    uint32_t count{0};
};

/**
 * Camera-following rolling windows; cascade 0 updates every frame, outer cascades round-robin so trace cost stays flat.
 * @param params
 * @param cameraPosition
 * @param previous cascades used last frame, for frozen windows on skipped cascades
 * @param frameNumber
 */
DDGICascades ComputeDDGICascades(const Core::DDGIParams& params, const glm::vec3& cameraPosition, const DDGICascades& previous, uint64_t frameNumber);

/**
 * Probe trace + irradiance/visibility blend, run per updated cascade; also uploads DDGI_CASCADES_BUFFER/_PREV_BUFFER. No-op without the TLAS and geometry/material/light buffers.
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
 * @param worldCache this frame's world radiance cache buffers; trace populates it as a side effect when valid, keyed with scene.cameraWorldPos (never the per-cascade window center)
 */
void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, Core::Arena& arena, const Core::DDGIParams& params, const DDGICascades& cascades, const DDGICascades& previous, int32_t skyboxIndex, float iblIntensity, uint64_t frameNumber, bool bBounceOnly, const WorldCacheFrame& worldCache);

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
 * @param debugCascade -1 draws every cascade with a per-cascade identification tint; 0-3 draws only that cascade, untinted
 * @param bHideInactive skip classification-inactive probes entirely instead of drawing them flat blue
 * @param probeDebugMode 0 fits the irradiance atlas; 1 fits the visibility atlas (red = mean distance / miss clamp, green = std/mean)
 */
void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGICascades& cascades, float probeDebugExposure, int32_t debugCascade, bool bHideInactive, int32_t probeDebugMode);
} // Render

#endif //WILL_ENGINE_DDGI_PASSES_H
