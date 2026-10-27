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
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/scene_components.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/static_mesh_primitive_component.h"
#include "engine/components/render/text3d_component.h"
#include "engine/material_manager.h"
#include "game/fwd_components.h"
#include "engine/console/console.h"
#include "engine/editor/playtest_system.h"
#include "engine/systems/scene_system.h"
#include "engine/components/physics/physics_body_component.h"
#include "physics/physics_world.h"

namespace Game
{
using Engine::MCP::Call;
using Engine::MCP::ToolResult;

static constexpr int64_t FIND_DEFAULT_LIMIT = 20;
static constexpr int64_t FIND_MAX_LIMIT = 200;
static constexpr size_t MAX_COMMAND_OUTPUT_LINES = 64;

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

static void WriteAuthoredMaterial(Engine::EngineContext* ctx, Engine::EngineState* state, Call& call, const entt::entity entity)
{
    const Engine::Registry& registry = state->registry;
    Engine::MaterialID id{};
    if (const auto* c = registry.TryGet<Component::ProceduralMeshComponent>(entity)) { id = c->material; }
    else if (const auto* c = registry.TryGet<Component::SplineMeshComponent>(entity)) { id = c->material; }
    else if (const auto* c = registry.TryGet<Component::Text3DComponent>(entity)) { id = c->material; }
    else if (const auto* c = registry.TryGet<Component::StaticMeshPrimitiveComponent>(entity)) { id = c->materialOverride; }
    if (!id.IsValid()) {
        call.SetNull("material");
        return;
    }
    const Engine::Material* material = ctx->materialManager->GetMaterial(id);
    call.BeginObject("material");
    call.SetString("id", Engine::MCP::HexId(id.id).c_str());
    if (material) { call.SetString("name", material->name.c_str()); } else { call.SetNull("name"); }
    call.SetBool("missing", material == nullptr);
    call.End();
}

static void WriteVec3(Call& call, const char* key, const Vec3& v)
{
    call.BeginArray(key);
    call.PushFloat(v.x);
    call.PushFloat(v.y);
    call.PushFloat(v.z);
    call.End();
}

static void WritePhysicsState(Engine::EngineContext* ctx, Engine::EngineState* state, Call& call, const entt::entity entity)
{
    const auto* body = Component::FindBody(state->registry, entity);
    if (!body) {
        call.SetNull("physics");
        return;
    }
    const Physics::PhysicsWorld* physics = ctx->physicsWorld;
    const Physics::BodyState bodyState = physics->GetBodyState(body->bodyID);

    call.BeginObject("physics");
    const char* MOTION_TYPES[] = {"Static", "Kinematic", "Dynamic"};
    call.SetString("motionType", MOTION_TYPES[static_cast<uint32_t>(bodyState.motionType)]);
    call.SetBool("awake", bodyState.bAwake);
    call.SetInt("objectLayer", static_cast<int64_t>(bodyState.objectLayer));
    WriteVec3(call, "linearVelocity", bodyState.linearVelocity);
    WriteVec3(call, "angularVelocity", bodyState.angularVelocity);

    constexpr size_t MAX_TOUCHING = 16;
    call.BeginArray("touching");
    for (const Physics::BodyTouch& touch : physics->QueryTouching(&ctx->gameplayArena.Get(), body->bodyID, 0.01f, MAX_TOUCHING)) {
        call.PushObject();
        const entt::entity other = Component::EntityFromBodyUserData(state->registry, physics->GetUserData(touch.body));
        const auto* otherStable = other != entt::null ? state->registry.TryGet<Component::StableIdComponent>(other) : nullptr;
        const auto* otherName = other != entt::null ? state->registry.TryGet<Component::NameComponent>(other) : nullptr;
        if (otherStable) { call.SetString("stableId", Engine::MCP::HexId(otherStable->id.id).c_str()); } else { call.SetNull("stableId"); }
        call.SetString("name", otherName ? otherName->name.c_str() : "");
        call.SetBool("awake", physics->IsAwake(touch.body));
        call.SetInt("contacts", touch.contacts);
        call.SetInt("subShapes", static_cast<int64_t>(touch.subShapes));
        call.SetFloat("maxPenetration", touch.maxPenetration);
        call.End();
    }
    call.End();
    call.End();
}

static void WriteEntityDetail(Engine::EngineContext* ctx, Engine::EngineState* state, Call& call, const entt::entity entity)
{
    const Engine::Registry& registry = state->registry;
    const auto* stable = registry.TryGet<Component::StableIdComponent>(entity);
    const auto* name = registry.TryGet<Component::NameComponent>(entity);
    const auto* scene = registry.TryGet<Component::SceneRuntime>(entity);
    const auto* local = registry.TryGet<Component::TransformComponent>(entity);
    const auto* world = registry.TryGet<Component::WorldTransformRuntime>(entity);
    const auto* hierarchy = registry.TryGet<Component::HierarchyComponent>(entity);

    call.SetInt("entity", static_cast<int64_t>(entt::to_integral(entity)));
    if (stable) { call.SetString("stableId", Engine::MCP::HexId(stable->id.id).c_str()); } else { call.SetNull("stableId"); }
    call.SetString("name", name ? name->name.c_str() : "");
    if (scene) { call.SetString("sceneId", Engine::MCP::HexId(scene->sceneId.id).c_str()); } else { call.SetNull("sceneId"); }
    if (local) { WriteTransform(call, "localTransform", local->translation, local->rotation, local->scale); } else { call.SetNull("localTransform"); }
    if (world) { WriteTransform(call, "worldTransform", world->translation, world->rotation, world->scale); } else { call.SetNull("worldTransform"); }
    if (hierarchy && hierarchy->parentStableId.IsValid()) { call.SetString("parentStableId", Engine::MCP::HexId(hierarchy->parentStableId.id).c_str()); } else { call.SetNull("parentStableId"); }
    WriteAuthoredMaterial(ctx, state, call, entity);
    WritePhysicsState(ctx, state, call, entity);

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
    return Engine::FindEntity(state, StringID(raw));
}

static ToolResult GetEntity(Engine::EngineContext* ctx, Engine::EngineState* state, Call& call)
{
    const entt::entity entity = FindByStableIdArg(state, call, "stableId");
    if (entity == entt::null) {
        call.SetError("No live entity with that stableId; expected 16 hex digits as returned by query_scene");
        return ToolResult::Error;
    }
    WriteEntityDetail(ctx, state, call, entity);
    return ToolResult::Complete;
}

static ToolResult FindEntities(Engine::EngineContext* ctx, Engine::EngineState* state, Call& call)
{
    const std::string_view needle = call.GetString("nameContains", "");
    const int64_t limit = std::clamp<int64_t>(call.GetInt("limit", FIND_DEFAULT_LIMIT), 1, FIND_MAX_LIMIT);
    int64_t matched = 0;

    call.BeginArray("entities");
    for (const auto [entity, name] : state->registry.View<Component::NameComponent>().each()) {
        if (!name.name.Contains(needle, Core::CaseSensitivity::Insensitive)) { continue; }
        if (matched++ >= limit) { continue; }
        call.PushObject();
        WriteEntityDetail(ctx, state, call, entity);
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
        state->registry.GetMut<Component::TransformComponent>(entity).translation = position;
    }

    if (entity == entt::null || !state->registry.Valid(entity)) {
        call.SetError("Spawn failed; see the engine log");
        return ToolResult::Error;
    }

    const char* name = call.GetString("name", "");
    if (*name) {
        state->registry.GetOrAdd<Component::NameComponent>(entity).name = Core::InlineString<128>(std::string_view(name));
    }
    if (parent != entt::null) {
        Engine::SetParent(state, entity, parent);
    }

    WriteEntityDetail(ctx, state, call, entity);
    return ToolResult::Complete;
}

void RegisterMCPTools(Engine::EngineState* state)
{
    using Engine::MCP::RegisterTool;

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
        .description = "Full detail for one entity by stable id: name, scene, local and world transform, parent, component names, and for a physics body its motion type, "
                       "awake state, velocities and every body within 1 cm of it (contacts, distinct sub-shapes touched, deepest penetration).",
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
