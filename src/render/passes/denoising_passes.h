//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_DENOISING_PASSES_H
#define WILL_ENGINE_DENOISING_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

void SetupATrousWaveletDenoiser(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                Core::Array<uint32_t, 2> renderExtent,
                                const DeferredResolveTargets& targets,
                                const Core::ReSTIRParams::ATrousParams& params);

void SetupASVGFDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        Core::Array<uint32_t, 2> renderExtent,
                        const DeferredResolveTargets& targets,
                        const Core::ReSTIRParams::SVGFParams& params);
} // Render

#endif //WILL_ENGINE_DENOISING_PASSES_H
