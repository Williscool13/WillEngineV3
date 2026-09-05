//
// Created by William on 2026-09-05.
//

#include "game_mcp_tools.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "core/time/frame_stamp.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/mcp/mcp_tool.h"
#include "game/game_state.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/scene_components.h"
#include "engine/components/common/stable_id_component.h"
#include "game/fwd_components.h"
#include "engine/console/console.h"
#include "engine/editor/capture_shot_system.h"
#include "engine/systems/scene_system.h"

namespace Game
{
using Engine::MCP::Call;
using Engine::MCP::ToolResult;

static constexpr int32_t SETTLED_QUIET_FRAMES = 30;
static constexpr int64_t FIND_DEFAULT_LIMIT = 20;
static constexpr int64_t FIND_MAX_LIMIT = 200;
static constexpr size_t MAX_COMMAND_OUTPUT_LINES = 64;

static const char* InputContextName(const Engine::InputContext context)
{
    switch (context) {
        case Engine::InputContext::Editor: return "Editor";
        case Engine::InputContext::Menu: return "Menu";
        case Engine::InputContext::Gameplay: return "Gameplay";
        case Engine::InputContext::Console: return "Console";
        case Engine::InputContext::ProbeBake: return "ProbeBake";
    }
    return "Unknown";
}

static bool AnythingPending(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    return Engine::CountLoadingEntities(state) > 0 || ctx->assetManager->HasPendingLoads() || ctx->frameStatus.bAssetGenerationPending;
}

void TickQuietFrames(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    int32_t& quiet = ctx->GetGameState<GameState>()->quietFrames;
    if (AnythingPending(ctx, state)) {
        quiet = 0;
    }
    else if (quiet < INT32_MAX) {
        quiet++;
    }
}

static ToolResult GetEngineStatus(Engine::EngineContext* ctx, Engine::EngineState* state, Call& call)
{
    const Core::TimeFrame t = ctx->publishedTimeFrame.Get();
    const int32_t quiet = ctx->GetGameState<GameState>()->quietFrames;

    call.SetFloat("gameFps", t.gameFps);
    call.SetFloat("renderFps", t.renderFps);
    call.SetBool("settled", quiet >= SETTLED_QUIET_FRAMES);
    call.SetInt("quietFrames", quiet);
    call.SetInt("settledThresholdFrames", SETTLED_QUIET_FRAMES);
    call.SetInt("loadingEntities", static_cast<int64_t>(Engine::CountLoadingEntities(state)));
    call.SetBool("pendingAssetLoads", ctx->assetManager->HasPendingLoads());
    call.SetBool("assetGenerationPending", ctx->frameStatus.bAssetGenerationPending);
    call.SetBool("rescanResources", ctx->rescan.bResources);
    call.SetBool("screenshotInFlight", ctx->frameStatus.bScreenshotInFlight);
    call.SetString("sceneId", Engine::MCP::HexId(state->scene.currentSceneId.id).c_str());
    call.SetString("sceneName", state->scene.currentSceneName.c_str());
    call.SetInt("stableEntityCount", static_cast<int64_t>(state->stableIdToEntityMap.Size()));
    call.SetString("inputContext", InputContextName(state->inputContext));
    call.SetBool("physicsEnabled", state->physics.bEnabled);
    return ToolResult::Complete;
}

static ToolResult ExecConsoleCommand(Engine::EngineContext* ctx, Engine::EngineState* state, Call& call)
{
    const char* command = call.GetString("command", "");
    if (!*command) {
        call.SetError("Missing command");
        return ToolResult::Error;
    }

    Engine::Console::ConsoleState& console = state->console;
    const uint64_t printedBefore = console.totalPrinted;
    const bool bOk = Engine::Console::ExecuteCommand(ctx, state, command);

    const size_t printed = static_cast<size_t>(std::min<uint64_t>(console.totalPrinted - printedBefore, console.lines.Size()));
    const size_t first = console.lines.Size() - printed;
    call.SetBool("ok", bOk);
    call.BeginArray("output");
    for (size_t i = first + 1; i < console.lines.Size() && i - first - 1 < MAX_COMMAND_OUTPUT_LINES; ++i) {
        call.PushString(console.lines[i].c_str());
    }
    call.End();
    call.SetBool("outputTruncated", printed > MAX_COMMAND_OUTPUT_LINES + 1);
    if (!bOk) {
        call.SetError(Core::InlineString<256>::Format("Unknown or empty console command: %s", command).c_str());
        return ToolResult::Error;
    }
    return ToolResult::Complete;
}

static ToolResult ListConsoleCommands(Engine::EngineContext*, Engine::EngineState* state, Call& call)
{
    const char* prefix = call.GetString("prefix", "");
    const size_t prefixLen = strlen(prefix);
    call.BeginArray("commands");
    for (size_t i = 0; i < Engine::Console::GetCommandCount(state); ++i) {
        const Engine::Console::CommandInfo info = Engine::Console::GetCommandInfo(state, i);
        if (strncmp(info.name, prefix, prefixLen) != 0) { continue; }
        call.PushObject();
        call.SetString("name", info.name);
        call.SetString("help", info.help);
        call.End();
    }
    call.End();
    call.SetInt("total", static_cast<int64_t>(Engine::Console::GetCommandCount(state)));
    return ToolResult::Complete;
}

static void WriteTransform(Call& call, const char* key, const Vec3& translation, const Quat& rotation, const Vec3& scale)
{
    call.BeginObject(key);
    call.BeginArray("translation");
    call.PushFloat(translation.x);
    call.PushFloat(translation.y);
    call.PushFloat(translation.z);
    call.End();
    call.BeginArray("rotation");
    call.PushFloat(rotation.x);
    call.PushFloat(rotation.y);
    call.PushFloat(rotation.z);
    call.PushFloat(rotation.w);
    call.End();
    call.BeginArray("scale");
    call.PushFloat(scale.x);
    call.PushFloat(scale.y);
    call.PushFloat(scale.z);
    call.End();
    call.End();
}

static void WriteEntityDetail(Engine::EngineState* state, Call& call, const entt::entity entity)
{
    const entt::registry& registry = state->registry;
    const auto* stable = registry.try_get<Component::StableIdComponent>(entity);
    const auto* name = registry.try_get<Component::NameComponent>(entity);
    const auto* scene = registry.try_get<Component::SceneComponent>(entity);
    const auto* local = registry.try_get<Component::TransformComponent>(entity);
    const auto* world = registry.try_get<Component::WorldTransformComponent>(entity);
    const auto* hierarchy = registry.try_get<Component::HierarchyComponent>(entity);

    call.SetInt("entity", static_cast<int64_t>(entt::to_integral(entity)));
    if (stable) { call.SetString("stableId", Engine::MCP::HexId(stable->id.id).c_str()); } else { call.SetNull("stableId"); }
    call.SetString("name", name ? name->name.c_str() : "");
    if (scene) { call.SetString("sceneId", Engine::MCP::HexId(scene->sceneId.id).c_str()); } else { call.SetNull("sceneId"); }
    if (local) { WriteTransform(call, "localTransform", local->translation, local->rotation, local->scale); } else { call.SetNull("localTransform"); }
    if (world) { WriteTransform(call, "worldTransform", world->translation, world->rotation, world->scale); } else { call.SetNull("worldTransform"); }
    if (hierarchy && hierarchy->parentStableId.IsValid()) { call.SetString("parentStableId", Engine::MCP::HexId(hierarchy->parentStableId.id).c_str()); } else { call.SetNull("parentStableId"); }

    call.BeginArray("components");
    const Engine::ComponentRegistry& components = state->componentRegistry;
    for (size_t i = 0; i < components.registry.Size(); ++i) {
        const Engine::ComponentEntry& entry = components.registry[i];
        if (entry.has && entry.has(registry, entity)) {
            call.PushString(entry.name);
        }
    }
    call.End();
}

static entt::entity FindByStableIdArg(Engine::EngineState* state, Call& call, const char* key)
{
    uint64_t raw = 0;
    if (!Engine::MCP::ParseHexId(call.GetString(key, ""), raw)) {
        return entt::null;
    }
    const entt::entity* found = state->stableIdToEntityMap.Find(StringID(raw));
    return found && state->registry.valid(*found) ? *found : entt::null;
}

static ToolResult GetEntity(Engine::EngineContext*, Engine::EngineState* state, Call& call)
{
    const entt::entity entity = FindByStableIdArg(state, call, "stableId");
    if (entity == entt::null) {
        call.SetError("No live entity with that stableId; expected 16 hex digits as returned by query_scene");
        return ToolResult::Error;
    }
    WriteEntityDetail(state, call, entity);
    return ToolResult::Complete;
}

static ToolResult FindEntities(Engine::EngineContext*, Engine::EngineState* state, Call& call)
{
    const std::string_view needle = call.GetString("nameContains", "");
    const int64_t limit = std::clamp<int64_t>(call.GetInt("limit", FIND_DEFAULT_LIMIT), 1, FIND_MAX_LIMIT);
    int64_t matched = 0;

    call.BeginArray("entities");
    for (const auto [entity, name] : state->registry.view<Component::NameComponent>().each()) {
        if (!name.name.Contains(needle, Core::CaseSensitivity::Insensitive)) { continue; }
        if (matched++ >= limit) { continue; }
        call.PushObject();
        WriteEntityDetail(state, call, entity);
        call.End();
    }
    call.End();
    call.SetInt("total", matched);
    call.SetInt("limit", limit);
    return ToolResult::Complete;
}

static bool ResolvePrefabArg(Engine::EngineContext* ctx, const char* text, StringID& outId)
{
    uint64_t raw = 0;
    if (Engine::MCP::ParseHexId(text, raw) && ctx->assetManager->GetPrefabMetadata(StringID(raw))) {
        outId = StringID(raw);
        return true;
    }
    for (const auto kv : ctx->assetManager->GetPrefabCache()) {
        if (kv.value.prefabName == text) {
            outId = kv.key;
            return true;
        }
    }
    return false;
}

static bool ResolveModelArg(Engine::EngineContext* ctx, const char* text, Engine::ModelID& outId)
{
    uint64_t raw = 0;
    if (Engine::MCP::ParseHexId(text, raw) && ctx->assetManager->GetModelMetadata(Engine::ModelID(raw))) {
        outId = Engine::ModelID(raw);
        return true;
    }
    for (const auto kv : ctx->assetManager->GetModelCache()) {
        if (kv.value.name == text) {
            outId = kv.key;
            return true;
        }
    }
    return false;
}

static ToolResult SpawnEntity(Engine::EngineContext* ctx, Engine::EngineState* state, Call& call)
{
    const char* prefab = call.GetString("prefab", "");
    const char* model = call.GetString("model", "");
    if (*prefab && *model) {
        call.SetError("Give either prefab or model, not both");
        return ToolResult::Error;
    }
    const Vec3 position{static_cast<float>(call.GetFloat("x", 0.0)), static_cast<float>(call.GetFloat("y", 0.0)), static_cast<float>(call.GetFloat("z", 0.0))};

    entt::entity parent = entt::null;
    if (call.HasArg("parentStableId")) {
        parent = FindByStableIdArg(state, call, "parentStableId");
        if (parent == entt::null) {
            call.SetError("parentStableId does not name a live entity");
            return ToolResult::Error;
        }
    }

    entt::entity entity = entt::null;
    if (*prefab) {
        StringID prefabId{};
        if (!ResolvePrefabArg(ctx, prefab, prefabId)) {
            call.SetError("Unknown prefab; pass a name or the hex id from query_assets type=prefab");
            return ToolResult::Error;
        }
        entity = Engine::SpawnPrefab(state, ctx->assetManager, prefabId, position);
    }
    else if (*model) {
        Engine::ModelID modelId{};
        if (!ResolveModelArg(ctx, model, modelId)) {
            call.SetError("Unknown model; pass a name or the hex id from query_assets type=model");
            return ToolResult::Error;
        }
        const auto spawned = Engine::SpawnModel(ctx, state, modelId, position);
        entity = spawned.IsEmpty() ? entt::null : spawned[0];
    }
    else {
        entity = Engine::CreateSceneEntity(state);
        state->registry.get<Component::TransformComponent>(entity).translation = position;
    }

    if (entity == entt::null || !state->registry.valid(entity)) {
        call.SetError("Spawn failed; see the engine log");
        return ToolResult::Error;
    }

    const char* name = call.GetString("name", "");
    if (*name) {
        state->registry.get_or_emplace<Component::NameComponent>(entity).name = Core::InlineString<128>(std::string_view(name));
    }
    if (parent != entt::null) {
        Engine::SetParent(state, entity, parent);
    }

    WriteEntityDetail(state, call, entity);
    return ToolResult::Complete;
}

void RegisterMCPTools(Engine::EngineState* state)
{
    using Engine::MCP::RegisterTool;

    RegisterTool(state, {
        .id = "get_engine_status"_sid,
        .name = "get_engine_status",
        .description = "Readiness of the running scene: settled (no loads in flight for 30 frames), loading entity count, pending asset work, current scene, input context. Poll this after any mutation before trusting a screenshot or query.",
        .inputSchemaJson = nullptr,
        .invoke = &GetEngineStatus,
        .origin = Engine::Origin::Game,
        .bNeedsDrain = true,
    });

    RegisterTool(state, {
        .id = "exec_console_command"_sid,
        .name = "exec_console_command",
        .description = "Runs one line in the in-game developer console exactly as if typed, and returns the lines it printed. Use list_console_commands to discover commands.",
        .inputSchemaJson = R"({"type":"object","required":["command"],"properties":{"command":{"type":"string","description":"Full command line, e.g. `echo hi`"}}})",
        .invoke = &ExecConsoleCommand,
        .origin = Engine::Origin::Game,
        .bNeedsDrain = true,
    });

    RegisterTool(state, {
        .id = "list_console_commands"_sid,
        .name = "list_console_commands",
        .description = "Names and help text of every registered console command, optionally narrowed by name prefix.",
        .inputSchemaJson = R"({"type":"object","properties":{"prefix":{"type":"string"}}})",
        .invoke = &ListConsoleCommands,
        .origin = Engine::Origin::Game,
        .bNeedsDrain = true,
    });

    RegisterTool(state, {
        .id = "get_entity"_sid,
        .name = "get_entity",
        .description = "Full detail for one entity by stable id: name, scene, local and world transform, parent, component names.",
        .inputSchemaJson = R"({"type":"object","required":["stableId"],"properties":{"stableId":{"type":"string","description":"16 hex digits from query_scene"}}})",
        .invoke = &GetEntity,
        .origin = Engine::Origin::Game,
        .bNeedsDrain = true,
    });

    RegisterTool(state, {
        .id = "find_entities"_sid,
        .name = "find_entities",
        .description = "Entities whose name contains a substring (case-insensitive), with the same detail as get_entity.",
        .inputSchemaJson = R"({"type":"object","properties":{"nameContains":{"type":"string"},"limit":{"type":"integer","default":20,"minimum":1,"maximum":200}}})",
        .invoke = &FindEntities,
        .origin = Engine::Origin::Game,
        .bNeedsDrain = true,
    });

    RegisterTool(state, {
        .id = "spawn_entity"_sid,
        .name = "spawn_entity",
        .description = "Spawns into the current scene: a prefab, a whole model, or an empty entity when neither is given. Returns the new entity's detail immediately; meshes and physics stream in over later frames, so poll get_engine_status.settled.",
        .inputSchemaJson = R"({"type":"object","properties":{
            "prefab":{"type":"string","description":"Prefab name or hex id"},
            "model":{"type":"string","description":"Model name or hex id"},
            "name":{"type":"string","description":"Override the entity name"},
            "x":{"type":"number","default":0},"y":{"type":"number","default":0},"z":{"type":"number","default":0},
            "parentStableId":{"type":"string","description":"Parent under this entity, keeping world pose"}}})",
        .invoke = &SpawnEntity,
        .origin = Engine::Origin::Game,
        .bNeedsDrain = true,
    });
}
} // Game
