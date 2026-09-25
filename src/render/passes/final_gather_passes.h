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

inline const StringID RESTIR_DIFFUSE_RATIO = "restir_diffuse_ratio"_sid;
inline const StringID GI_SCREEN_DIFFUSE = "gi_screen_diffuse"_sid;
inline const StringID GI_GATHER_SH_R = "gi_gather_sh_r"_sid;
inline const StringID GI_GATHER_SH_G = "gi_gather_sh_g"_sid;
inline const StringID GI_GATHER_SH_B = "gi_gather_sh_b"_sid;
inline const StringID GI_GATHER_DATA = "gi_gather_data"_sid;
inline const StringID GI_GATHER_GUIDE = "gi_gather_guide"_sid;
inline const StringID GI_GATHER_RAW_SH_R = "gi_gather_raw_sh_r"_sid;
inline const StringID GI_GATHER_RAW_SH_G = "gi_gather_raw_sh_g"_sid;
inline const StringID GI_GATHER_RAW_SH_B = "gi_gather_raw_sh_b"_sid;
inline const StringID GI_GATHER_TMP_SH_R = "gi_gather_tmp_sh_r"_sid;
inline const StringID GI_GATHER_TMP_SH_G = "gi_gather_tmp_sh_g"_sid;
inline const StringID GI_GATHER_TMP_SH_B = "gi_gather_tmp_sh_b"_sid;
inline const StringID GI_GATHER_SKY_VIS = "gi_gather_sky_vis"_sid;
inline const StringID GI_GATHER_RAW_SKY_VIS = "gi_gather_raw_sky_vis"_sid;
inline const StringID GI_GATHER_TMP_SKY_VIS = "gi_gather_tmp_sky_vis"_sid;
inline const StringID GI_GATHER_RESOLVED = "gi_gather_resolved"_sid;
inline const StringID GI_GATHER_HISTORY = "gi_gather_history"_sid;
inline const StringID GI_GATHER_FAST = "gi_gather_fast"_sid;
inline const StringID GI_GATHER_SKY_VIS_HISTORY = "gi_gather_sky_vis_history"_sid;
inline const StringID GI_GATHER_NOISE = "gi_gather_noise"_sid;
inline const StringID GI_GATHER_GUIDE_NORMAL = "gi_gather_guide_normal"_sid;
inline const StringID OBJECT_MOTION = "object_motion"_sid;
inline const StringID GI_DECONSTRUCT_TARGET = "gi_deconstruct_target"_sid;
inline const StringID GI_GATHER_DEBUG_TARGET = "gi_gather_debug_target"_sid;

inline constexpr uint32_t GI_GATHER_MAX_RAYS_PER_PIXEL = 8u;

/** Gates the composite passes' gather read this frame. */
struct FinalGatherFrame
{
    bool bValid{false};
};

/**
 * Shared per-pixel object motion at render extent, RGBA16F (motionUv.xy = gbuffer MV minus camera-static reprojection, linear viewZ, motion blur mask).
 * Idempotent; the first caller adds the pass.
 */
void SetupObjectMotion(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex);

/**
 * TDA-style final gather: one cosine-weighted ray per half-res pixel, resolved against last frame's lit screen, then the radiance cache, then probes (skybox on miss), projected into per-channel 2-band SH targets.
 * @param graph
 * @param pipelineManager
 * @param viewFamily
 * @param renderExtent
 * @param targets
 * @param sceneIndex
 * @param frameNumber
 * @param bDenoise
 * @param bTemporalFilter Counter accumulation of the resolved output against carried history; off = this frame's resolve only (raw-signal inspection).
 * @param raysPerPixel Gather rays per half-res pixel, clamped to [1, GI_GATHER_MAX_RAYS_PER_PIXEL]. Uniform across the frame, so cost is flat and rays stay coherent; relative noise falls as 1/sqrt(n), which is the only lever on dark bright-to-dark gradients where a single ray finds a bright aperture too rarely.
 * @param bDebugView A GI-gather debug view is active; disable the screen tier so the debug color written into the composite is not fed back as radiance.
 * @param bDisableScreenTier Disable the lit-history screen tier so ray hits resolve only against world-space sources; set while the GI field is frozen (lit history is view-dependent and keeps evolving, which face-seams probe bakes).
 * @param bQuarterRes Gather at quarter render resolution instead of half: 1/4 the rays and denoise work; the upscale footprint spans 4x4 full-res pixels per gather texel, so sub-footprint detail leans harder on the guides and history.
 * @param bounceIntensity The radiance cache's DDGI bounce scale; the probe tier uses the same scale so cache and probes agree at a hit.
 * @param maxRayRadiance DDGI ray firefly cap, shared by gather samples; 0 = off
 * @return
 */
FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, uint64_t frameNumber,
    bool bDenoise, bool bTemporalFilter, uint32_t raysPerPixel, bool bDebugView, bool bDisableScreenTier, bool bQuarterRes, float bounceIntensity, float maxRayRadiance);

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
 * Gather debug views written to gi_gather_debug_target for the debug visualizer instead of hijacking the lit composite, so the lit-color snapshot and the screen tier stay authentic while inspecting.
 * @param graph
 * @param pipelineManager
 * @param renderExtent
 * @param mode UI mode: 1 resolved irradiance, 2 fallback tier, 3 hit distance, 4 accumulation, 5 first-ray escape
 * @param bQuarterRes Must match the SetupFinalGather that produced this frame's gather targets.
 */
void SetupGIGatherDebug(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, int32_t mode, bool bQuarterRes);
} // Render

#endif //WILL_ENGINE_FINAL_GATHER_PASSES_H
