//
// Created by William on 2026-09-05.
//

#include "mcp_tools_engine.h"

#include <atomic>

#include "mcp_tool.h"
#include "core/time/frame_stamp.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/logging/engine_logger.h"
#include "engine/logging/log_category.h"
#include "platform/paths.h"
#include "render/renderer_statistics.h"
#include "render/render-graph/render_graph_resources.h"

namespace Engine::MCP
{
static ToolResult GetFrameTimings(EngineContext* ctx, EngineState*, Call& call)
{
    call.SetInt("gameFrame", static_cast<int64_t>(Core::gGameFrame.load(std::memory_order_relaxed)));
    call.SetInt("renderFrame", static_cast<int64_t>(Core::gRenderFrame.load(std::memory_order_relaxed)));

    const Core::TimeFrame t = ctx->publishedTimeFrame.Get();
    call.BeginObject("cpu");
    call.SetFloat("deltaMs", static_cast<double>(t.deltaTime) * 1000.0);
    call.SetFloat("fps", t.gameFps);
    call.SetFloat("totalTimeS", t.totalTime);
    call.SetInt("frameCount", static_cast<int64_t>(t.frameCount));
    call.End();

    if (!ctx->rendererStatistics) {
        call.SetNull("render");
        call.SetNull("gpu");
        return ToolResult::Complete;
    }
    const Render::RendererStatistics s = ctx->rendererStatistics->GetPublished();

    call.BeginObject("render");
    call.SetFloat("fps", t.renderFps);
    call.SetFloat("wallFrameMs", s.wallFrameMs);
    call.SetFloat("gpuSpanMs", s.gpuSpanMs);
    call.SetFloat("gpuTotalMs", s.gpuProfile.totalMs);
    call.End();

    call.BeginObject("gpu");
    call.BeginObject("groupsMs");
    for (uint32_t i = 0; i < Render::RENDER_CATEGORY_GROUP_COUNT; ++i) {
        call.SetFloat(Render::RENDER_CATEGORY_GROUP_NAMES[i], s.gpuProfile.groupMs[i]);
    }
    call.End();
    call.BeginObject("passesMs");
    for (uint32_t i = 0; i < Render::RENDER_CATEGORY_BIT_COUNT; ++i) {
        call.SetFloat(Render::RENDER_CATEGORY_NAMES[i], s.gpuProfile.leafMs[i]);
    }
    call.End();
    call.End();

    call.BeginObject("culling");
    call.SetInt("visibleMeshlets", s.visibleMeshletCount);
    call.SetInt("instancesCulledFrustum", s.culledInstanceFrustum);
    call.SetInt("instancesCulledContribution", s.culledInstanceContribution);
    call.SetInt("instancesCulledOcclusion", s.culledInstanceOcclusion);
    call.SetInt("meshletsCulledFrustum", s.culledMeshletFrustum);
    call.SetInt("meshletsCulledCone", s.culledMeshletCone);
    call.SetInt("meshletsCulledContribution", s.culledMeshletContribution);
    call.SetInt("meshletsCulledOcclusion", s.culledMeshletOcclusion);
    call.End();

    call.BeginObject("pipelineStats");
    call.SetInt("meshInvocations", static_cast<int64_t>(s.meshInvocations));
    call.SetInt("fragmentInvocations", static_cast<int64_t>(s.fragmentInvocations));
    call.SetInt("computeInvocations", static_cast<int64_t>(s.computeInvocations));
    call.SetInt("clippingInvocations", static_cast<int64_t>(s.clippingInvocations));
    call.SetInt("clippingPrimitives", static_cast<int64_t>(s.clippingPrimitives));
    call.End();

    return ToolResult::Complete;
}

static ToolResult GetLogInfo(EngineContext* ctx, EngineState*, Call& call)
{
    const auto logFile = Platform::GetLogPath() / "engine.log";
    call.SetString("logPath", logFile.c_str());
    call.SetBool("truncatedAtStartup", true);
    call.SetString("flushLevel", "warn");
    call.SetString("frameStampFormat", "[gameFrame|renderFrame]");

    call.BeginArray("categories");
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i) {
        call.PushString(kCategoryNames[i]);
    }
    call.End();

    call.BeginArray("gameSubCategories");
    if (ctx->engineLogger) {
        const auto names = ctx->engineLogger->GetImGuiSink()->GetGameSubCategoryNames();
        for (size_t i = 0; i < names.Size(); ++i) {
            call.PushString(names[i].c_str());
        }
    }
    call.End();

    return ToolResult::Complete;
}

void RegisterEngineTools(EngineState* state)
{
    RegisterTool(state, {
        .id = "get_frame_timings"_sid,
        .name = "get_frame_timings",
        .description = "CPU and GPU frame timing for the running engine: frame counters, per-category GPU pass times in ms, culling counters, pipeline statistics. Safe to poll.",
        .inputSchemaJson = nullptr,
        .invoke = &GetFrameTimings,
        .origin = ToolOrigin::Engine,
        .bNeedsDrain = false,
        .bLogMarkers = false,
    });

    RegisterTool(state, {
        .id = "get_log_info"_sid,
        .name = "get_log_info",
        .description = "Where the engine log file is and how to read it: path, flush policy, the frame-stamp format on every line, and the category names. The log is the return channel for side effects.",
        .inputSchemaJson = nullptr,
        .invoke = &GetLogInfo,
        .origin = ToolOrigin::Engine,
        .bNeedsDrain = false,
        .bLogMarkers = false,
    });
}
} // Engine::MCP
