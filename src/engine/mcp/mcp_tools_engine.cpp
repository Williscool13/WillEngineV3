//
// Created by William on 2026-09-05.
//

#include "mcp_tools_engine.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "mcp_tool.h"
#include "core/containers/inline_string.h"
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
static constexpr int64_t ASSET_QUERY_DEFAULT_LIMIT = 50;
static constexpr int64_t ASSET_QUERY_MAX_LIMIT = 500;
static constexpr int64_t SCENE_QUERY_DEFAULT_LIMIT = 100;
static constexpr int64_t SCENE_QUERY_MAX_LIMIT = 1000;

struct Page
{
    int64_t offset{0};
    int64_t limit{0};
    int64_t matched{0};

    bool Accept()
    {
        const bool bEmit = matched >= offset && matched < offset + limit;
        ++matched;
        return bEmit;
    }
};

static Page ReadPage(const Call& call, const int64_t defaultLimit, const int64_t maxLimit)
{
    Page page{};
    page.offset = std::max<int64_t>(0, call.GetInt("offset", 0));
    page.limit = std::clamp<int64_t>(call.GetInt("limit", defaultLimit), 1, maxLimit);
    return page;
}

static void WritePage(Call& call, const Page& page)
{
    call.SetInt("total", page.matched);
    call.SetInt("offset", page.offset);
    call.SetInt("limit", page.limit);
}

static ToolResult GetFrameTimings(EngineContext* ctx, EngineState*, Call& call)
{
    call.SetInt("gameFrame", static_cast<int64_t>(Core::gGameFrame.load(std::memory_order_relaxed)));
    call.SetInt("renderFrame", static_cast<int64_t>(Core::gRenderFrame.load(std::memory_order_relaxed)));
    call.SetBool("screenshotInFlight", ctx->frameStatus.bScreenshotInFlight);

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
    call.SetString("mcpMarkerFormat", "mcp/<callId> begin|end frame=<gameFrame> tool=<name>");

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

enum class AssetKind : uint8_t
{
    Model,
    Texture,
    Cubemap,
    Font,
    Scene,
    Prefab,
    All,
    Invalid,
};

static AssetKind ParseAssetKind(const char* s)
{
    if (!s || !*s) { return AssetKind::All; }
    if (strcmp(s, "model") == 0) { return AssetKind::Model; }
    if (strcmp(s, "texture") == 0) { return AssetKind::Texture; }
    if (strcmp(s, "cubemap") == 0) { return AssetKind::Cubemap; }
    if (strcmp(s, "font") == 0) { return AssetKind::Font; }
    if (strcmp(s, "scene") == 0) { return AssetKind::Scene; }
    if (strcmp(s, "prefab") == 0) { return AssetKind::Prefab; }
    return AssetKind::Invalid;
}

static bool NameMatches(const Core::InlineString<128>& name, const std::string_view needle)
{
    return name.Contains(needle, Core::CaseSensitivity::Insensitive);
}

static void BeginAssetEntry(Call& call, const char* type, const uint64_t id, const char* name, const Core::Path& source, const uint64_t contentVersion, const bool bResident)
{
    call.PushObject();
    call.SetString("type", type);
    call.SetString("id", HexId(id).c_str());
    call.SetString("name", name);
    call.SetString("source", source.c_str());
    call.SetInt("contentVersion", static_cast<int64_t>(contentVersion));
    call.SetBool("resident", bResident);
}

static ToolResult QueryAssets(EngineContext* ctx, EngineState* state, Call& call)
{
    const AssetKind kind = ParseAssetKind(call.GetString("type", ""));
    if (kind == AssetKind::Invalid) {
        call.SetError("Unknown asset type; expected one of model, texture, cubemap, font, scene, prefab");
        return ToolResult::Error;
    }
    const std::string_view needle = call.GetString("nameContains", "");
    Page page = ReadPage(call, ASSET_QUERY_DEFAULT_LIMIT, ASSET_QUERY_MAX_LIMIT);
    AssetManager& am = *ctx->assetManager;

    call.BeginArray("assets");

    if (kind == AssetKind::All || kind == AssetKind::Model) {
        for (const auto kv : am.GetModelCache()) {
            if (!NameMatches(kv.value.name, needle) || !page.Accept()) { continue; }
            BeginAssetEntry(call, "model", kv.key.id, kv.value.name.c_str(), kv.value.source, kv.value.contentVersion, am.IsModelResident(kv.key));
            call.SetInt("nodeCount", kv.value.nodeCount);
            call.SetInt("meshNodeCount", kv.value.meshNodesCount);
            call.End();
        }
    }
    if (kind == AssetKind::All || kind == AssetKind::Texture) {
        for (const auto kv : am.GetTextureRegistry()) {
            if (!NameMatches(kv.value.name, needle) || !page.Accept()) { continue; }
            BeginAssetEntry(call, "texture", kv.key.id, kv.value.name.c_str(), kv.value.source, kv.value.contentVersion, am.IsTextureLoaded(kv.key));
            call.SetInt("width", kv.value.width);
            call.SetInt("height", kv.value.height);
            call.SetInt("mipCount", kv.value.mipCount);
            call.SetBool("ungenerated", kv.value.bUngenerated);
            call.End();
        }
    }
    if (kind == AssetKind::All || kind == AssetKind::Cubemap) {
        for (const auto kv : am.GetCubemapCache()) {
            if (!NameMatches(kv.value.name, needle) || !page.Accept()) { continue; }
            BeginAssetEntry(call, "cubemap", kv.key.id, kv.value.name.c_str(), kv.value.source, kv.value.contentVersion, am.IsCubemapLoaded(kv.key));
            call.SetInt("width", kv.value.width);
            call.SetInt("height", kv.value.height);
            call.SetInt("mipCount", kv.value.mipCount);
            call.End();
        }
    }
    if (kind == AssetKind::All || kind == AssetKind::Font) {
        for (const auto kv : am.GetFontCache()) {
            if (!NameMatches(kv.value.name, needle) || !page.Accept()) { continue; }
            BeginAssetEntry(call, "font", kv.key.id, kv.value.name.c_str(), kv.value.source, kv.value.contentVersion, am.IsFontResident(kv.key));
            call.End();
        }
    }
    if (kind == AssetKind::All || kind == AssetKind::Scene) {
        for (const auto kv : am.GetSceneCache()) {
            if (!NameMatches(kv.value.sceneName, needle) || !page.Accept()) { continue; }
            BeginAssetEntry(call, "scene", kv.key.id, kv.value.sceneName.c_str(), kv.value.source, kv.value.contentVersion, state->scene.currentSceneId == kv.key);
            call.SetInt("entityCount", kv.value.entityCount);
            call.End();
        }
    }
    if (kind == AssetKind::All || kind == AssetKind::Prefab) {
        for (const auto kv : am.GetPrefabCache()) {
            if (!NameMatches(kv.value.prefabName, needle) || !page.Accept()) { continue; }
            BeginAssetEntry(call, "prefab", kv.key.id, kv.value.prefabName.c_str(), kv.value.source, kv.value.contentVersion, false);
            call.SetInt("componentCount", kv.value.componentCount);
            call.End();
        }
    }

    call.End();
    WritePage(call, page);
    return ToolResult::Complete;
}

static ToolResult QueryScene(EngineContext*, EngineState* state, Call& call)
{
    Page page = ReadPage(call, SCENE_QUERY_DEFAULT_LIMIT, SCENE_QUERY_MAX_LIMIT);
    const bool bIncludeComponents = call.GetBool("includeComponents", true);
    const char* withComponent = call.GetString("withComponent", "");

    const ComponentRegistry& components = state->componentRegistry;
    const ComponentEntry* filter = nullptr;
    if (*withComponent) {
        const size_t* index = components.registryMapping.Find(StringID(Hash(withComponent, strlen(withComponent))));
        if (!index || !components.registry[*index].has) {
            call.SetError("Unknown component type; names are the registered COMPONENT_NAME strings, e.g. NameComponent");
            return ToolResult::Error;
        }
        filter = &components.registry[*index];
    }

    const entt::registry& registry = state->registry;
    int64_t aliveCount = 0;
    for ([[maybe_unused]] const entt::entity e : registry.view<entt::entity>()) {
        ++aliveCount;
    }

    call.BeginArray("entities");
    for (const auto kv : state->stableIdToEntityMap) {
        const entt::entity e = kv.value;
        if (!registry.valid(e)) { continue; }
        if (filter && !filter->has(registry, e)) { continue; }
        if (!page.Accept()) { continue; }

        call.PushObject();
        call.SetString("stableId", HexId(kv.key.id).c_str());
        call.SetInt("entity", static_cast<int64_t>(entt::to_integral(e)));
        if (bIncludeComponents) {
            call.BeginArray("components");
            for (size_t i = 0; i < components.registry.Size(); ++i) {
                const ComponentEntry& entry = components.registry[i];
                if (entry.has && entry.has(registry, e)) {
                    call.PushString(entry.name);
                }
            }
            call.End();
        }
        call.End();
    }
    call.End();

    WritePage(call, page);
    call.SetInt("entityCount", aliveCount);
    call.SetInt("stableEntityCount", static_cast<int64_t>(state->stableIdToEntityMap.Size()));
    call.SetString("sceneId", HexId(state->scene.currentSceneId.id).c_str());
    call.SetString("sceneName", state->scene.currentSceneName.c_str());
    return ToolResult::Complete;
}

static ToolResult CaptureScreenshot(EngineContext* ctx, EngineState* state, Call& call)
{
    if (ctx->frameStatus.bScreenshotInFlight || state->requests.bWantsScreenshot) {
        call.SetError("A screenshot is already in flight; poll get_frame_timings.screenshotInFlight until it clears");
        return ToolResult::Error;
    }

    const char* requested = call.GetString("path", "");
    Core::InlineString<512> path{};
    if (*requested) {
        path = Core::InlineString<512>(std::string_view(requested));
    }
    else {
        const auto filename = Core::InlineString<64>::Format("mcp_%llu.png", static_cast<unsigned long long>(Core::gGameFrame.load(std::memory_order_relaxed)));
        const Core::Path full = Platform::GetUserDataPath() / "screenshots" / filename.c_str();
        path = Core::InlineString<512>(std::string_view(full.c_str()));
    }

    state->requests.bWantsScreenshot = true;
    state->requests.screenshotPath = path;

    call.SetString("path", path.c_str());
    call.SetBool("inFlight", true);
    return ToolResult::Complete;
}

void RegisterEngineTools(EngineState* state)
{
    RegisterTool(state, {
        .id = "get_frame_timings"_sid,
        .name = "get_frame_timings",
        .description = "CPU and GPU frame timing for the running engine: frame counters, per-category GPU pass times in ms, culling counters, pipeline statistics, screenshotInFlight. Safe to poll.",
        .inputSchemaJson = nullptr,
        .invoke = &GetFrameTimings,
        .origin = Origin::Engine,
        .bNeedsDrain = false,
    });

    RegisterTool(state, {
        .id = "get_log_info"_sid,
        .name = "get_log_info",
        .description = "Where the engine log file is and how to read it: path, flush policy, the frame-stamp format on every line, the MCP call-marker format, and the category names. The log is the return channel for side effects.",
        .inputSchemaJson = nullptr,
        .invoke = &GetLogInfo,
        .origin = Origin::Engine,
        .bNeedsDrain = false,
    });

    RegisterTool(state, {
        .id = "query_assets"_sid,
        .name = "query_assets",
        .description = "Lists the assets the engine knows about (models, textures, cubemaps, fonts, scenes, prefabs) with ids, source paths, content versions and whether each is resident. Paginated.",
        .inputSchemaJson = R"({"type":"object","properties":{
            "type":{"type":"string","enum":["model","texture","cubemap","font","scene","prefab"],"description":"Omit to list every type"},
            "nameContains":{"type":"string","description":"Case-insensitive substring filter on the asset name"},
            "limit":{"type":"integer","default":50,"minimum":1,"maximum":500},
            "offset":{"type":"integer","default":0,"minimum":0}}})",
        .invoke = &QueryAssets,
        .origin = Origin::Engine,
        .bNeedsDrain = true,
    });

    RegisterTool(state, {
        .id = "query_scene"_sid,
        .name = "query_scene",
        .description = "Lists the live scene's entities by stable id with the names of the components each one carries. Paginated; filter by a component type name.",
        .inputSchemaJson = R"({"type":"object","properties":{
            "withComponent":{"type":"string","description":"Only entities carrying this component type, e.g. NameComponent"},
            "includeComponents":{"type":"boolean","default":true},
            "limit":{"type":"integer","default":100,"minimum":1,"maximum":1000},
            "offset":{"type":"integer","default":0,"minimum":0}}})",
        .invoke = &QueryScene,
        .origin = Origin::Engine,
        .bNeedsDrain = true,
    });

    RegisterTool(state, {
        .id = "capture_screenshot"_sid,
        .name = "capture_screenshot",
        .description = "Requests a PNG of the current viewport (editor UI included) and returns the path immediately. The file lands a few frames later: poll get_frame_timings.screenshotInFlight until false, then read it.",
        .inputSchemaJson = R"({"type":"object","properties":{
            "path":{"type":"string","description":"Absolute output path; omit for <UserData>/screenshots/mcp_<frame>.png"}}})",
        .invoke = &CaptureScreenshot,
        .origin = Origin::Engine,
        .bNeedsDrain = true,
    });
}
} // Engine::MCP
