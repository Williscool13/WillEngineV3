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
        .deltaTime = deltaTime,
        .frameNumber = frameNumber,
        .pipelines = pipelineManager,
    };

    StringID current = ctx.targets.colorOutput;
    current = PPExposure(ctx, current);
    current = PPBloom(ctx, current);
    current = PPSharpening(ctx, current);
    current = PPTonemap(ctx, current);
    // current = PPMotionBlur(ctx, current); // disabled: motion vector format change
    current = PPColorGrading(ctx, current);
    current = PPVignetteAberration(ctx, current);
    current = PPPanini(ctx, current);
    current = PPFilmGrain(ctx, current);
    current = PPDither(ctx, current);
    return current;
}
} // Render
