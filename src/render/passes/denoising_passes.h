//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_DENOISING_PASSES_H
#define WILL_ENGINE_DENOISING_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/shaders/ddgi_interop.h"
#include "core/types/extent.h"

namespace Core { struct ViewFamily; struct ReflectionConfiguration; }

namespace Render
{
class PipelineManager;

/**
 * RELAX port denoise plus remodulate into targets.colorOutput.
 * @return reflection virtual motion; invalid unless reflections are on and motion blur or FSR2 reads it
 */
RDGTexture SetupRELAXDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Extent2D renderExtent,
                        const RenderTargets& targets,
                        const SceneResources& scene,
                        const ReSTIRFrame& restir,
                        const ReflectionFrame& reflection,
                        const FinalGatherFrame& finalGather,
                        const DDGIFrame& ddgi,
                        const WorldGridFrame& worldGrid,
                        const Core::RELAXParams& params,
                        uint64_t frameNumber,
                        uint32_t remodulateOutputMode,
                        float iblIntensity,
                        uint32_t activeCheckerboardField,
                        float checkerboardResolveAccumSpeed,
                        bool bDDGIApply,
                        const Core::ReflectionConfiguration& reflectionConfig,
                        uint32_t giGatherMode,
                        float historyExposureRatio);

void SetupReBLURDenoiser(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Extent2D renderExtent,
                         const RenderTargets& targets,
                         const SceneResources& scene,
                         const ReSTIRFrame& restir,
                         const ReflectionFrame& reflection,
                         const FinalGatherFrame& finalGather,
                         const DDGIFrame& ddgi,
                         const WorldGridFrame& worldGrid,
                         const Core::ReBLURParams& params,
                         uint64_t frameNumber,
                         uint32_t remodulateOutputMode,
                         float iblIntensity,
                         uint32_t activeCheckerboardField,
                         float checkerboardResolveAccumSpeed,
                         bool bDDGIApply,
                         const Core::ReflectionConfiguration& reflectionConfig,
                          uint32_t giGatherMode,
                          float historyExposureRatio);
} // Render

#endif //WILL_ENGINE_DENOISING_PASSES_H
