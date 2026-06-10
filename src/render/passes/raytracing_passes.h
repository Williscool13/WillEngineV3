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

/**
 * Uploads per-frame TLAS instance data and builds the TLAS.
 * Must be called after geometry passes so all BLAS device addresses are valid.
 */
void SetupTLASBuild(RenderGraph& graph,
                    VulkanContext* context,
                    const Core::ViewFamily& viewFamily,
                    Core::Array<uint32_t, 2> renderExtent);

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
                       uint32_t sceneIndex);

} // Render

#endif //WILL_ENGINE_RAYTRACING_PASSES_H
