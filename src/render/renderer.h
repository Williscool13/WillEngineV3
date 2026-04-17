//
// Created by William on 2026-04-16.
//

#ifndef WILL_ENGINE_RENDERER_H
#define WILL_ENGINE_RENDERER_H
#include "render-graph/render_graph.h"
#include "types/render_types.h"

namespace Render
{
class PipelineManager;

struct VisibilityBufferTargets
{
    StringID visibility;
    StringID stableId;
    StringID depthStencil;
};

void SetupGeometryPass(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       const RenderFamilyProperties& renderFamilyProperties,
                       Core::Array<uint32_t, 2> renderExtent,
                       const VisibilityBufferTargets& targets,
                       uint32_t sceneIndex);

struct VisibilityBufferResolveTargets
{
    // In
    StringID visibility;

    // Out
    StringID barycentric;
    StringID derivatives;
};


void SetupVisibilityResolvePass(RenderGraph& graph,
    PipelineManager* pipelineManager,
    const Core::ViewFamily& viewFamily,
    Core::Array<uint32_t, 2> renderExtent,
    const VisibilityBufferResolveTargets& targets,
    uint32_t sceneIndex);
} // Render

#endif //WILL_ENGINE_RENDERER_H
