//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_DENOISING_PASSES_H
#define WILL_ENGINE_DENOISING_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Core { struct ViewFamily; }

namespace Render
{
class PipelineManager;

void SetupATrousWaveletDenoiser(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                Core::Array<uint32_t, 2> renderExtent,
                                const RenderTargets& targets,
                                const Core::ReSTIRParams::ATrousParams& params);

void SetupASVGFDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        Core::Array<uint32_t, 2> renderExtent,
                        const RenderTargets& targets,
                        const Core::ReSTIRParams::SVGFParams& params);

void SetupRELAXDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Array<uint32_t, 2> renderExtent,
                        Core::Array<uint32_t, 2> fullRenderExtent,
                        const RenderTargets& targets,
                        const Core::RELAXParams& params,
                        uint64_t frameNumber,
                        uint32_t remodulateOutputMode,
                        uint32_t pixelScale,
                        float iblIntensity);

void SetupReBLURDenoiser(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Array<uint32_t, 2> renderExtent,
                         Core::Array<uint32_t, 2> fullRenderExtent,
                         const RenderTargets& targets,
                         const Core::ReBLURParams& params,
                         uint64_t frameNumber,
                         uint32_t remodulateOutputMode,
                         uint32_t pixelScale,
                         float iblIntensity);
} // Render

#endif //WILL_ENGINE_DENOISING_PASSES_H
