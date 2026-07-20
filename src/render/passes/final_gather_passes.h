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

/** Gates the composite passes' gather read this frame. */
struct FinalGatherFrame
{
    bool bValid{false};
};

/**
 * TDA-style final gather: one cosine-weighted ray per half-res pixel, world radiance cache read at the hit (probes as fallback, skybox on miss), projected into per-channel 2-band SH targets.
 * @param graph
 * @param pipelineManager
 * @param viewFamily
 * @param renderExtent
 * @param targets
 * @param sceneIndex
 * @param frameNumber
 * @param bDenoise
 * @param bSkipRay Skip the cosine ray entirely; sample the world radiance cache at the pixel's own surface point (probes as fallback) instead.
 * @param bDebugView A GI-gather debug view is active; disable the screen tier so the debug color written into the composite is not fed back as radiance.
 * @return
 */
FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, uint64_t frameNumber, bool bDenoise, bool bSkipRay, bool bDebugView);

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
