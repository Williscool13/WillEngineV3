//
// Created by William on 2026-04-16.
//

#ifndef WILL_ENGINE_RENDERER_H
#define WILL_ENGINE_RENDERER_H

#include "render/passes/geometry_passes.h"
#include "render/passes/lighting_passes.h"
#include "render/passes/restir_passes.h"
#include "render/passes/denoising_passes.h"
#include "render/passes/ambient_occlusion_passes.h"
#include "render/passes/shadow_passes.h"
#include "render/passes/scene_passes.h"
#include "render/passes/anti_aliasing_passes.h"
#include "render/passes/ui_passes.h"
#include "render/passes/raytracing_passes.h"
#include "render/passes/debug_passes.h"

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

StringID SetupPostProcessing(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Array<uint32_t, 2> renderExtent,
                             const RenderTargets& targets,
                             float deltaTime,
                             uint64_t frameNumber);
} // Render

#endif //WILL_ENGINE_RENDERER_H
