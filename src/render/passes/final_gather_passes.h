//
// Created by William on 2026-07-12.
//

#ifndef WILL_ENGINE_FINAL_GATHER_PASSES_H
#define WILL_ENGINE_FINAL_GATHER_PASSES_H

#include "render/render-graph/render_graph.h"
#include "render/renderer_types.h"

namespace Core
{
struct ViewFamily;
}

namespace Render
{
class PipelineManager;

inline const StringID GI_GATHER_SH_R = SID("gi_gather_sh_r");
inline const StringID GI_GATHER_SH_G = SID("gi_gather_sh_g");
inline const StringID GI_GATHER_SH_B = SID("gi_gather_sh_b");
inline const StringID GI_GATHER_DATA = SID("gi_gather_data");
inline const StringID GI_GATHER_GUIDE = SID("gi_gather_guide");
inline const StringID GI_GATHER_RAW_SH_R = SID("gi_gather_raw_sh_r");
inline const StringID GI_GATHER_RAW_SH_G = SID("gi_gather_raw_sh_g");
inline const StringID GI_GATHER_RAW_SH_B = SID("gi_gather_raw_sh_b");
inline const StringID GI_GATHER_TMP_SH_R = SID("gi_gather_tmp_sh_r");
inline const StringID GI_GATHER_TMP_SH_G = SID("gi_gather_tmp_sh_g");
inline const StringID GI_GATHER_TMP_SH_B = SID("gi_gather_tmp_sh_b");
inline const StringID GI_GATHER_RESOLVED = SID("gi_gather_resolved");
inline const StringID GI_GATHER_HISTORY = SID("gi_gather_history");
inline const StringID GI_DECONSTRUCT_TARGET = SID("gi_deconstruct_target");

inline constexpr uint32_t GI_GATHER_MAX_RAYS_PER_PIXEL = 8u;

/** Gates the composite passes' gather read this frame. */
struct FinalGatherFrame
{
    bool bValid{false};
};

/**
 * TDA-style final gather: one cosine-weighted ray per half-res pixel, radiance cache read at the hit (probes as fallback, skybox on miss), projected into per-channel 2-band SH targets.
 * @param graph
 * @param pipelineManager
 * @param viewFamily
 * @param renderExtent
 * @param targets
 * @param sceneIndex
 * @param frameNumber
 * @param bDenoise
 * @param chromaDenoisePasses Extra denoise iterations on CoCg chromaticity only, Y carried (strides doubling from 8, clamped to [0, 4]; 0 = off); targets low-frequency lighting-chroma noise the shared-radius chain cannot reach. Requires bDenoise.
 * @param bTemporalFilter Counter accumulation of the resolved output against carried history; off = this frame's resolve only (raw-signal inspection).
 * @param bSkipRay Skip the cosine ray entirely; sample the radiance cache at the pixel's own surface point (probes as fallback) instead.
 * @param raysPerPixel Gather rays per half-res pixel, clamped to [1, GI_GATHER_MAX_RAYS_PER_PIXEL]. Uniform across the frame, so cost is flat and rays stay coherent; relative noise falls as 1/sqrt(n), which is the only lever on dark bright-to-dark gradients where a single ray finds a bright aperture too rarely.
 * @param bDebugView A GI-gather debug view is active; disable the screen tier so the debug color written into the composite is not fed back as radiance.
 * @param bDisableScreenTier Disable the lit-history screen tier so ray hits resolve only against world-space sources; set while the GI field is frozen (lit history is view-dependent and keeps evolving, which face-seams probe bakes).
 * @return
 */
FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, uint64_t frameNumber, bool bDenoise, uint32_t chromaDenoisePasses, bool bTemporalFilter, bool bSkipRay, uint32_t raysPerPixel, bool bDebugView, bool bDisableScreenTier);

/**
 * Full-screen GI leak deconstruction at the primary surface, written to gi_deconstruct_target for the debug visualizer.
 * @param graph
 * @param pipelineManager
 * @param renderExtent
 * @param targets
 * @param sceneIndex
 * @param mode 1 cache cell identity hash, 2 cache radiance/servability, 3 DDGI Chebyshev-gate weight fractions, 4 dominant-probe mean-vs-distance margin, 5 coverage/confidence/serving cascade, 6 raw DDGI irradiance
 */
void SetupGIDeconstruct(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, int32_t mode);
} // Render

#endif //WILL_ENGINE_FINAL_GATHER_PASSES_H
