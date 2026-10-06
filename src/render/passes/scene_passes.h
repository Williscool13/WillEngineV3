//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_SCENE_PASSES_H
#define WILL_ENGINE_SCENE_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

void SetupSkyboxRendering(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Extent2D renderExtent,
                          const RenderTargets& targets,
                          const SceneResources& scene,
                          uint32_t sceneIndex);

/**
 * Prepares the render graph pass for text rendering in a forward-shading blending pass.
 * Note: Run before TAA. Rationale is text is almost never "free-floating" so the surface right behind the text will emit consistent motion vectors.
 * @param graph
 * @param pipelineManager
 * @param viewFamily
 * @param renderExtent
 * @param targets
 * @param scene
 */
void SetupTextForwardPass(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Extent2D renderExtent,
                          const RenderTargets& targets,
                          const SceneResources& scene);

void SetupSpritesPass(RenderGraph& graph,
                      PipelineManager* pipelineManager,
                      const Core::ViewFamily& viewFamily,
                      Core::Extent2D renderExtent,
                      const RenderTargets& targets,
                      const SceneResources& scene);
} // Render

#endif //WILL_ENGINE_SCENE_PASSES_H
