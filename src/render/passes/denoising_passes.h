//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_DENOISING_PASSES_H
#define WILL_ENGINE_DENOISING_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/shaders/ddgi_interop.h"

namespace Core { struct ViewFamily; struct RTReflectionConfiguration; }

namespace Render
{
class PipelineManager;

void SetupRELAXDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Array<uint32_t, 2> renderExtent,
                        const RenderTargets& targets,
                        const Core::RELAXParams& params,
                        uint64_t frameNumber,
                        uint32_t remodulateOutputMode,
                        float iblIntensity,
                        uint32_t activeCheckerboardField,
                        float checkerboardResolveAccumSpeed,
                        bool bDDGIApply,
                        const Core::RTReflectionConfiguration& reflectionConfig,
                        float brdfRoughnessMax,
                        uint32_t giGatherMode);

void SetupReBLURDenoiser(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Array<uint32_t, 2> renderExtent,
                         const RenderTargets& targets,
                         const Core::ReBLURParams& params,
                         uint64_t frameNumber,
                         uint32_t remodulateOutputMode,
                         float iblIntensity,
                         uint32_t activeCheckerboardField,
                         float checkerboardResolveAccumSpeed,
                         bool bDDGIApply,
                         const Core::RTReflectionConfiguration& reflectionConfig,
                         float brdfRoughnessMax,
                         uint32_t giGatherMode);
} // Render

#endif //WILL_ENGINE_DENOISING_PASSES_H
