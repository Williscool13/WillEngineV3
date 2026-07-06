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

/**
 * Uploads per-frame TLAS instance data and builds the TLAS.
 */
void SetupTLASBuild(RenderGraph& graph,
                    VulkanContext* context,
                    const Core::ViewFamily& viewFamily,
                    Core::Array<uint32_t, 2> renderExtent,
                    const FrameResourceLimits& limits);

/**
 * Temporary smoke-test: traces a ray per pixel, writes linearized depth on hit and 0 otherwise.
 * Exercises the TLAS read path before a real shadow/lighting pass exists.
 */
void SetupRTShadowTest(RenderGraph& graph,
                       VulkanContext* context,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       StringID outputTarget,
                       uint32_t sceneIndex);

/**
 * Traces one hard shadow ray per pixel toward the directional (sun) light.
 * Writes binary visibility (1 lit, 0 occluded) to the rt_sun_shadow target. No-ops if the TLAS is absent.
 */
void SetupRTSunShadow(RenderGraph& graph,
                      PipelineManager* pipelineManager,
                      const Core::ViewFamily& viewFamily,
                      Core::Array<uint32_t, 2> shadowExtent,
                      Core::Array<uint32_t, 2> fullExtent,
                      const RenderTargets& targets,
                      uint32_t sceneIndex,
                      uint64_t frameNumber,
                      uint32_t pixelScale);

/**
 * RT ground truth direct illumination via next-event estimation.
 * Casts one shadow ray per pixel per frame toward a randomly sampled area light, evaluates PBR BRDF if visible, and accumulates into a persistent buffer.
 */
void SetupRTGroundTruthDI(RenderGraph& graph,
                           PipelineManager* pipelineManager,
                           const Core::ViewFamily& viewFamily,
                           Core::Array<uint32_t, 2> renderExtent,
                           const RenderTargets& targets,
                           uint32_t sceneIndex,
                           bool bReset,
                           uint32_t accumulationCount,
                           uint64_t frameNumber);

/**
 * Progressive path-traced reference for the DDGI indirect diffuse: primary direct like the DI ground truth, plus a cosine-sampled diffuse-only bounce path with NEE at each vertex (skybox on miss).
 * Accumulates into a persistent buffer; reset on camera move or mode entry. No-op without the TLAS and geometry/material buffers.
 */
void SetupRTGroundTruthGI(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          const RenderTargets& targets,
                          uint32_t sceneIndex,
                          bool bReset,
                          uint32_t accumulationCount,
                          uint64_t frameNumber);

} // Render

#endif //WILL_ENGINE_RAYTRACING_PASSES_H
