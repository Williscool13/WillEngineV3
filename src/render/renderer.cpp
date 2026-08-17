//
// Created by William on 2026-04-16.
//

#include "render/renderer.h"

#include "render/post-processing/post_processing.h"

namespace Render
{
StringID SetupPostProcessing(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Array<uint32_t, 2> renderExtent,
                             Core::Array<uint32_t, 2> preAaExtent,
                             Core::Array<uint32_t, 2> displayExtent,
                             const RenderTargets& targets,
                             float deltaTime,
                             uint64_t frameNumber)
{
    PostProcessContext ctx{
        .graph = graph,
        .config = viewFamily.postProcessConfig,
        .targets = targets,
        .view = viewFamily,
        .extent = renderExtent,
        .preAaExtent = preAaExtent,
        .displayExtent = displayExtent,
        .deltaTime = deltaTime,
        .frameNumber = frameNumber,
        .pipelines = pipelineManager,
    };

    StringID current = ctx.targets.colorOutput;
    current = PPExposure(ctx, current);
    current = PPDepthOfField(ctx, current);
    current = PPMotionBlur(ctx, current);
    current = PPBloom(ctx, current);
    current = PPFinalize(ctx, current);
    current = PPCompose(ctx, current);
    return current;
}
} // Render
