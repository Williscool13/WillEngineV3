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
inline const StringID GI_GATHER_RAW_SH_R = SID("gi_gather_raw_sh_r");
inline const StringID GI_GATHER_RAW_SH_G = SID("gi_gather_raw_sh_g");
inline const StringID GI_GATHER_RAW_SH_B = SID("gi_gather_raw_sh_b");
inline const StringID GI_GATHER_TMP_SH_R = SID("gi_gather_tmp_sh_r");
inline const StringID GI_GATHER_TMP_SH_G = SID("gi_gather_tmp_sh_g");
inline const StringID GI_GATHER_TMP_SH_B = SID("gi_gather_tmp_sh_b");
inline const StringID GI_GATHER_RESOLVED = SID("gi_gather_resolved");
inline const StringID GI_GATHER_HISTORY = SID("gi_gather_history");

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
} // Render

#endif //WILL_ENGINE_FINAL_GATHER_PASSES_H
