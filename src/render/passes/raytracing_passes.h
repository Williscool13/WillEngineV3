//
// Created by William on 2026-06-10.
//

#ifndef WILL_ENGINE_RAYTRACING_PASSES_H
#define WILL_ENGINE_RAYTRACING_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "core/types/extent.h"

namespace Core
{
struct ViewFamily;
}

namespace Render
{
class PipelineManager;
struct FrameResourceLimits;

RDGBufferRing SetupTLASBuild(RenderGraph& graph,
                             VulkanContext* context,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Extent2D renderExtent,
                             const FrameResourceLimits& limits,
                             const SceneResources& scene);

void SetupRTShadowTest(RenderGraph& graph,
                       VulkanContext* context,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Extent2D renderExtent,
                       const RenderTargets& targets,
                       const SceneResources& scene,
                       RDGTexture outputTarget,
                       uint32_t sceneIndex);

SunShadowFrame SetupRTSunShadow(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Extent2D shadowExtent,
                                Core::Extent2D fullExtent,
                                const RenderTargets& targets,
                                const SceneResources& scene,
                                uint32_t sceneIndex,
                                uint64_t frameNumber,
                                uint32_t pixelScale);

/** Advance the accumulation count only on true, or skipped frames average in as zeros. */
bool SetupRTGroundTruthDI(RenderGraph& graph,
                           PipelineManager* pipelineManager,
                           const Core::ViewFamily& viewFamily,
                           Core::Extent2D renderExtent,
                           const RenderTargets& targets,
                           const SceneResources& scene,
                           uint32_t sceneIndex,
                           bool bReset,
                           uint32_t& accumulationCount,
                           uint64_t frameNumber);

/** Advance the accumulation count only on true. */
bool SetupRTGroundTruthGI(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Extent2D renderExtent,
                          const RenderTargets& targets,
                          const SceneResources& scene,
                          uint32_t sceneIndex,
                          bool bReset,
                          uint32_t& accumulationCount,
                          uint64_t frameNumber);

/** Advance the accumulation count only on true. */
bool SetupRTGroundTruthFull(RenderGraph& graph,
                            PipelineManager* pipelineManager,
                            const Core::ViewFamily& viewFamily,
                            Core::Extent2D renderExtent,
                            const RenderTargets& targets,
                            const SceneResources& scene,
                            uint32_t sceneIndex,
                            bool bReset,
                            uint32_t& accumulationCount,
                            uint64_t frameNumber,
                            uint32_t samplesPerFrame);

} // Render

#endif //WILL_ENGINE_RAYTRACING_PASSES_H
