//
// Created by William on 2026-06-06.
//

#ifndef WILL_ENGINE_RESTIR_PASSES_H
#define WILL_ENGINE_RESTIR_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Core
{
struct ViewFamily;
struct Arena;
}

namespace Render
{
class PipelineManager;

// Precompute the half-res quad-corner selection once; ReSTIR DI, the RELAX DI denoiser, and the sun-shadow pass read it via FetchFullPixel. Runs whenever any of those is half-res.
void SetupQuadSelectionPass(RenderGraph& graph,
                           PipelineManager* pipelineManager,
                           Core::Array<uint32_t, 2> renderExtent,
                           const RenderTargets& targets,
                           uint32_t sceneIndex,
                           uint64_t frameNumber);

void SetupReSTIRPasses(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex,
                       Core::Arena& arena,
                       uint64_t frameNumber,
                       const Core::ReSTIRParams& restirParams,
                       bool bUseReGIR);

void SetupReSTIRLightingResolvePass(RenderGraph& graph,
                                    PipelineManager* pipelineManager,
                                    const Core::ViewFamily& viewFamily,
                                    Core::Array<uint32_t, 2> renderExtent,
                                    const RenderTargets& targets,
                                    uint32_t sceneIndex,
                                    Core::Arena& arena,
                                    uint64_t frameNumber,
                                    uint32_t pixelScale);

void SetupReSTIRRemodulatePass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               Core::Array<uint32_t, 2> renderExtent,
                               const RenderTargets& targets,
                               uint32_t sceneIndex,
                               uint32_t outputMode,
                               uint32_t pixelScale,
                               float iblIntensity,
                               uint64_t frameNumber);
} // Render

#endif //WILL_ENGINE_RESTIR_PASSES_H
