//
// Created by William on 2026-06-10.
//

#ifndef WILL_ENGINE_RAYTRACING_PASSES_H
#define WILL_ENGINE_RAYTRACING_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"

namespace Core
{
struct ViewFamily;
}

namespace Render
{
class PipelineManager;
struct FrameResourceLimits;

void SetupTLASBuild(RenderGraph& graph,
                    VulkanContext* context,
                    PipelineManager* pipelineManager,
                    const Core::ViewFamily& viewFamily,
                    Core::Array<uint32_t, 2> renderExtent,
                    const FrameResourceLimits& limits);

void SetupRTShadowTest(RenderGraph& graph,
                       VulkanContext* context,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       StringID outputTarget,
                       uint32_t sceneIndex);

void SetupRTSunShadow(RenderGraph& graph,
                      PipelineManager* pipelineManager,
                      const Core::ViewFamily& viewFamily,
                      Core::Array<uint32_t, 2> shadowExtent,
                      Core::Array<uint32_t, 2> fullExtent,
                      const RenderTargets& targets,
                      uint32_t sceneIndex,
                      uint64_t frameNumber,
                      uint32_t pixelScale);

/** Advance the accumulation count only on true, or skipped frames average in as zeros. */
bool SetupRTGroundTruthDI(RenderGraph& graph,
                           PipelineManager* pipelineManager,
                           const Core::ViewFamily& viewFamily,
                           Core::Array<uint32_t, 2> renderExtent,
                           const RenderTargets& targets,
                           uint32_t sceneIndex,
                           bool bReset,
                           uint32_t& accumulationCount,
                           uint64_t frameNumber);

/** Advance the accumulation count only on true. */
bool SetupRTGroundTruthGI(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          const RenderTargets& targets,
                          uint32_t sceneIndex,
                          bool bReset,
                          uint32_t& accumulationCount,
                          uint64_t frameNumber);

/** Advance the accumulation count only on true. */
bool SetupRTGroundTruthFull(RenderGraph& graph,
                            PipelineManager* pipelineManager,
                            const Core::ViewFamily& viewFamily,
                            Core::Array<uint32_t, 2> renderExtent,
                            const RenderTargets& targets,
                            uint32_t sceneIndex,
                            bool bReset,
                            uint32_t& accumulationCount,
                            uint64_t frameNumber,
                            uint32_t samplesPerFrame);

} // Render

#endif //WILL_ENGINE_RAYTRACING_PASSES_H
