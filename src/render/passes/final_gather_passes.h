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
inline const StringID GI_GATHER_SKY_VIS = SID("gi_gather_sky_vis");
inline const StringID GI_GATHER_VARIANCE_GUIDE = SID("gi_gather_variance_guide");
inline const StringID GI_GATHER_RAW_SKY_VIS = SID("gi_gather_raw_sky_vis");
inline const StringID GI_GATHER_TMP_SKY_VIS = SID("gi_gather_tmp_sky_vis");
inline const StringID GI_GATHER_RESOLVED = SID("gi_gather_resolved");
inline const StringID GI_GATHER_HISTORY = SID("gi_gather_history");
inline const StringID GI_GATHER_MOMENTS = SID("gi_gather_moments");
inline const StringID GI_GATHER_MOMENTS_HISTORY = SID("gi_gather_moments_history");
inline const StringID GI_MOTION_TILED_MAX = SID("gi_motion_tiled_max");
inline const StringID GI_MOTION_TILED_NEIGHBOR_MAX = SID("gi_motion_tiled_neighbor_max");
inline const StringID OBJECT_MOTION = SID("object_motion");
inline const StringID GI_DECONSTRUCT_TARGET = SID("gi_deconstruct_target");
inline const StringID GI_GATHER_DEBUG_TARGET = SID("gi_gather_debug_target");
inline const StringID GI_UPSCALE_PATH_DEBUG = SID("gi_upscale_path_debug");

inline constexpr uint32_t GI_GATHER_MAX_RAYS_PER_PIXEL = 8u;

/** Gates the composite passes' gather read this frame. */
struct FinalGatherFrame
{
    bool bValid{false};
};

/**
 * Shared per-pixel object motion at render extent, RGBA16F (motionUv.xy = gbuffer MV minus camera-static reprojection, linear viewZ, motion blur mask).
 * Idempotent; the GI motion tile max and object-only motion blur both call it and the first caller adds the pass.
 */
void SetupObjectMotion(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex);

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
 * @param chromaLumaPower Falloff exponent on the tap/center luminance ratio in the chroma passes; the only guard stopping a lit region's hue from bleeding across a cast shadow, whose other edge-stops are all geometric. 0 disables it.
 * @param bTemporalFilter Counter accumulation of the resolved output against carried history; off = this frame's resolve only (raw-signal inspection).
 * @param bSkipRay Skip the cosine ray entirely; sample the radiance cache at the pixel's own surface point (probes as fallback) instead.
 * @param raysPerPixel Gather rays per half-res pixel, clamped to [1, GI_GATHER_MAX_RAYS_PER_PIXEL]. Uniform across the frame, so cost is flat and rays stay coherent; relative noise falls as 1/sqrt(n), which is the only lever on dark bright-to-dark gradients where a single ray finds a bright aperture too rarely.
 * @param bDebugView A GI-gather debug view is active; disable the screen tier so the debug color written into the composite is not fed back as radiance.
 * @param bDisableScreenTier Disable the lit-history screen tier so ray hits resolve only against world-space sources; set while the GI field is frozen (lit history is view-dependent and keeps evolving, which face-seams probe bakes).
 * @param bQuarterRes Gather at quarter render resolution instead of half: 1/4 the rays and denoise work; the upscale footprint spans 4x4 full-res pixels per gather texel, so sub-footprint detail leans harder on the guides and history.
 * @param bDebugUpscalePath Write gi_upscale_path_debug: per-pixel tint of which source built the upscale's current (footprint/fallback/world tier), brightness = current's weight in the temporal blend.
 * @return
 */
FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, uint64_t frameNumber, bool bDenoise, uint32_t chromaDenoisePasses, float chromaLumaPower, bool bTemporalFilter, bool bSkipRay, uint32_t raysPerPixel, bool bDebugView, bool bDisableScreenTier, bool bQuarterRes, bool bDebugUpscalePath);

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

/**
 * Gather debug views written to gi_gather_debug_target for the debug visualizer instead of hijacking the lit composite, so lit_color_history and the screen tier stay authentic while inspecting.
 * @param graph
 * @param pipelineManager
 * @param renderExtent
 * @param mode UI mode: 1 resolved irradiance, 2 fallback tier, 3 hit distance, 4 accumulation, 5 first-ray escape
 * @param bQuarterRes Must match the SetupFinalGather that produced this frame's gather targets.
 */
void SetupGIGatherDebug(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, int32_t mode, bool bQuarterRes);
} // Render

#endif //WILL_ENGINE_FINAL_GATHER_PASSES_H
