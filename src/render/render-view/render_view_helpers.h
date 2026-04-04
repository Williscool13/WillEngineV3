//
// Created by William on 2026-01-21.
//

#ifndef WILL_ENGINE_RENDER_VIEW_HELPERS_H
#define WILL_ENGINE_RENDER_VIEW_HELPERS_H
#include "core/containers/array.h"
#include "render/interface/render_interface.h"
#include "render/shaders/common_interop.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;
struct FrameResourceLimits;

SceneData GenerateSceneData(const Core::RenderView& view, const Core::PostProcessConfiguration& ppConfig, Core::Array<uint32_t, 2> renderExtent, uint64_t frameNumber, float deltaTime);

RenderFamilyProperties PrepareRenderFamilyProperties(Core::ViewFamily& viewFamily, ReadbackStruct* readbackData, PipelineManager* _pipelineManager, FrameResourceLimits& _limits);

float Halton(uint32_t i, uint32_t b);


} // Render

#endif //WILL_ENGINE_RENDER_VIEW_HELPERS_H
