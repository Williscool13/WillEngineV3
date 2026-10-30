//
// Created by William on 2026-10-25.
//

#include "runtime_save.h"

#include <charconv>
#include <chrono>

#include <tracy/Tracy.hpp>

#include "core/containers/inline_map.h"
#include "core/hash/xxh3.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/scene/scene_format.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/systems/command_queue.h"
#include "engine/systems/scene_system.h"
#include "platform/file_utils.h"

namespace Engine
{
static constexpr uint32_t RUNTIME_SCENE_VERSION = 1;

struct CachedRuntimeSceneStatus
{
    RuntimeSceneStatus status;
    std::chrono::steady_clock::time_point refreshed;
};

static Core::InlineMap<StringID, CachedRuntimeSceneStatus, 16> gRuntimeSceneStatus;

static void WriteClock(const EngineState* state, TextWriter& w)
{
    w.Key("runSeconds", state->timeFrame->totalTime - state->runStartTime);
    for (int i = 0; i < 4; ++i) {
        w.Key(Core::InlineString<8>::Format("rng%d", i).c_str(), state->rng.GetState(i));
    }
}

static void ReadClock(EngineState* state, const TextReader& r)
{
    for (int i = 0; i < 4; ++i) {
        state->rng.SetState(i, r.U64(Core::InlineString<8>::Format("rng%d", i).c_str()));
    }
    state->runStartTime = state->timeFrame->totalTime - r.Float("runSeconds");
}

static bool HasRuntimeState(const ComponentRegistry& componentRegistry, const Engine::SystemRegistry& registry, entt::entity e)
{
    for (const RuntimeStateEntry& entry : componentRegistry.runtimeState) {
        if (entry.has(registry, e)) { return true; }
    }
    return false;
}

/** Skips name (harmless) and transform (moves at runtime). */
static uint64_t AuthoredHash(EngineState* state, entt::entity e, Core::Vector<std::byte>& scratch, Engine::SystemRegistry& registry)
{
    scratch.Clear();
    TextWriter w(scratch);
    for (const ComponentEntry& entry : state->componentRegistry.registry) {
        if (entry.typeId == TypeSID<Component::NameComponent>() || entry.typeId == TypeSID<Component::TransformComponent>() || !entry.has(registry, e)) { continue; }
        w.BeginBlock(entry.typeId.id);
        entry.serialize(registry, e, w);
        w.EndBlock();
    }
    return Hash(scratch.Data(), scratch.Size());
}

/** Empty for an unsaved scene. */
static Core::Path RuntimeSceneFile(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, StringID sceneId)
{
    const auto* sceneMeta = registry.Assets().GetSceneMetadata(sceneId);
    return sceneMeta && !sceneMeta->bUnsaved ? RuntimeScenePath(sceneMeta->source) : Core::Path{};
}

/** In the run-start snapshot but no longer alive. */
static Core::ArenaVector<uint64_t> DestroyedAuthoredEntities(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, StringID sceneId)
{
#if WILL_EDITOR
    for (const Scene& snapshot : state->editor.pieSnapshot) {
        const TextReader reader(snapshot.content.Data(), snapshot.content.Size());
        if (StringID{reader.U64("scene_id")} != sceneId) { continue; }
        Core::ArenaVector<uint64_t> destroyed{&ctx->gameplayArena.Get(), snapshot.entityCount};
        const Core::ShortString stableKey = Core::ShortString::Format("%llu", static_cast<unsigned long long>(TypeSID<Component::StableIdComponent>().id));
        reader.ForEachRecord("entities", [&](const TextReader& entityReader) {
            const uint64_t stableId = entityReader.Block(stableKey.c_str()).U64("id");
            if (stableId != 0 && FindEntity(state, StringID{stableId}, registry) == entt::null) {
                destroyed.PushBack(stableId);
            }
        });
        return destroyed;
    }
#endif
    return Core::ArenaVector<uint64_t>{&ctx->gameplayArena.Get(), 0};
}

static void WriteRuntimeScene(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, StringID sceneId, TextWriter& w)
{
    Core::Vector<std::byte> scratch(&ctx->memoryManager->General(), Core::AllocTag::AssetManager);
    auto view = registry.View<Component::SceneRuntime, Component::StableIdComponent>(entt::exclude<Component::DoNotSerializeTag, Component::DoNotRuntimeSaveTag>);
    Core::ArenaVector<entt::entity> overrides{&ctx->gameplayArena.Get(), view.size_hint()};
    for (const entt::entity e : view) {
        if (view.get<Component::SceneRuntime>(e).sceneId == sceneId && HasRuntimeState(state->componentRegistry, registry, e)) {
            overrides.PushBack(e);
        }
    }

    w.Count("overrides", static_cast<uint32_t>(overrides.Size()));
    for (const entt::entity e : overrides) {
        w.BeginBlock("entity");
        w.Key("stable_id", view.get<Component::StableIdComponent>(e).id.id);
        w.Key("authored_hash", AuthoredHash(state, e, scratch, registry));
        if (const auto* transform = registry.TryGet<Component::TransformComponent>(e); transform && transform->mobility == Component::Mobility::Movable) {
            w.Key("translation", transform->translation);
            w.Key("rotation", transform->rotation);
            w.Key("scale", transform->scale);
        }
        for (const RuntimeStateEntry& entry : state->componentRegistry.runtimeState) {
            if (!entry.has(registry, e)) { continue; }
            w.BeginBlock(entry.typeId.id);
            entry.serialize(registry, e, w);
            w.EndBlock();
        }
        w.EndBlock();
    }

    const Core::ArenaVector<uint64_t> destroyed = DestroyedAuthoredEntities(ctx, state, registry, sceneId);
    w.Count("destroyed", static_cast<uint32_t>(destroyed.Size()));
    for (const uint64_t stableId : destroyed) {
        w.BeginBlock("entity");
        w.Key("stable_id", stableId);
        w.EndBlock();
    }

    const Scene spawned = SaveScene(ctx, state, registry, sceneId, SceneSaveFilter::Spawned);
    w.BeginBlock("spawned");
    w.Raw(spawned.content.Data(), spawned.content.Size());
    w.EndBlock();
}

/** The caller resolves prefabs and hierarchy after. */
static void ApplyRuntimeScene(EngineState* state, const TextReader& r, Engine::SystemRegistry& registry)
{
    r.ForEachRecord("overrides", [&](const TextReader& entityReader) {
        const entt::entity e = FindEntity(state, StringID{entityReader.U64("stable_id")}, registry);
        if (e == entt::null) { return; }
        if (auto* transform = registry.TryGetMut<Component::TransformComponent>(e); transform && entityReader.Has("translation")) {
            transform->translation = entityReader.Vec3("translation");
            transform->rotation = entityReader.Quat("rotation");
            transform->scale = entityReader.Vec3("scale", glm::vec3(1.0f));
            registry.Set<Component::DirtyTransformRequest>(e);
        }
        entityReader.ForEachBlock([&](std::string_view opener, const TextReader& block) {
            uint64_t typeId = 0;
            std::from_chars(opener.data(), opener.data() + opener.size(), typeId);
            if (const size_t* index = state->componentRegistry.runtimeStateMapping.Find(StringID{typeId})) {
                state->componentRegistry.runtimeState[*index].deserialize(registry, e, block);
            }
        });
    });
    r.ForEachRecord("destroyed", [&](const TextReader& entityReader) {
        if (const entt::entity e = FindEntity(state, StringID{entityReader.U64("stable_id")}, registry); e != entt::null) {
            registry.PushCommand({.type = CommandType::DestroyEntity, .entity = e});
        }
    });
    LoadScene(state->componentRegistry, registry, r.Block("spawned"));
}

static RuntimeSceneStatus ComputeRuntimeSceneStatus(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, StringID sceneId, const Platform::ScopedFileMapping& map)
{
    RuntimeSceneStatus status{.bExists = true};
    const TextReader reader(map.data, map.size);
    if (!map.data || reader.UInt("wruntimescene") != RUNTIME_SCENE_VERSION || StringID{reader.U64("scene_id")} != sceneId) {
        status.bUnreadable = true;
        return status;
    }

    Core::Vector<std::byte> scratch(&ctx->memoryManager->General(), Core::AllocTag::AssetManager);
    reader.ForEachRecord("overrides", [&](const TextReader& r) {
        const entt::entity e = FindEntity(state, StringID{r.U64("stable_id")}, registry);
        if (e == entt::null) {
            ++status.missing;
            return;
        }
        if (AuthoredHash(state, e, scratch, registry) != r.U64("authored_hash") && status.changed++ == 0) {
            const auto* name = registry.TryGet<Component::NameComponent>(e);
            status.firstChanged = name ? name->name : Core::InlineString<128>("Unnamed");
        }
    });
    return status;
}

bool SaveRuntimeScene(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, StringID sceneId)
{
    ZoneScoped;
    if (!IsRunning(state)) {
        LOG_WARN(Engine, "Runtime scene save refused: only while playing or simulating");
        return false;
    }
    const Core::Path path = RuntimeSceneFile(ctx, state, registry, sceneId);
    if (path.IsEmpty()) {
        LOG_WARN(Engine, "Runtime scene save refused: {} has no scene file", sceneId.ToString());
        return false;
    }

    Core::Vector<std::byte> out(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    TextWriter w(out);
    w.Key("wruntimescene", RUNTIME_SCENE_VERSION);
    w.Key("scene_id", sceneId.id);
    WriteClock(state, w);
    WriteRuntimeScene(ctx, state, registry, sceneId, w);

    gRuntimeSceneStatus.Remove(sceneId);
    if (!Platform::WriteFile(path, out.Data(), out.Size())) {
        LOG_ERROR(Engine, "Failed to write runtime scene '{}'", path.c_str());
        return false;
    }
    LOG_INFO(Engine, "Saved runtime scene '{}'", path.c_str());
    return true;
}

bool DeleteRuntimeScene(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, StringID sceneId)
{
    const Core::Path path = RuntimeSceneFile(ctx, state, registry, sceneId);
    gRuntimeSceneStatus.Remove(sceneId);
    if (path.IsEmpty() || !Platform::FileExists(path) || !Platform::MoveToRecycleBin(path)) {
        LOG_ERROR(Engine, "Failed to delete runtime scene '{}'", path.c_str());
        return false;
    }
    LOG_INFO(Engine, "Deleted runtime scene '{}'", path.c_str());
    return true;
}

const RuntimeSceneStatus& GetRuntimeSceneStatus(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry, StringID sceneId)
{
    constexpr auto REFRESH = std::chrono::milliseconds(500);
    const auto now = std::chrono::steady_clock::now();
    if (const CachedRuntimeSceneStatus* cached = gRuntimeSceneStatus.Find(sceneId); cached && now - cached->refreshed < REFRESH) {
        return cached->status;
    }
    if (gRuntimeSceneStatus.Size() == gRuntimeSceneStatus.GetCapacity()) {
        gRuntimeSceneStatus.Clear();
    }
    RuntimeSceneStatus status{};
    if (const Core::Path path = RuntimeSceneFile(ctx, state, registry, sceneId); !path.IsEmpty() && Platform::FileExists(path)) {
        status = ComputeRuntimeSceneStatus(ctx, state, registry, sceneId, Platform::ScopedFileMapping(path));
    }
    return gRuntimeSceneStatus.Emplace(sceneId, CachedRuntimeSceneStatus{status, now}).status;
}

void ApplyRuntimeScenes(EngineContext* ctx, EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    bool bApplied = false;
    for (const RuntimeSceneMetadata& meta : state->editor.loadedScenes) {
        const Core::Path path = RuntimeSceneFile(ctx, state, registry, meta.sceneId);
        if (path.IsEmpty() || !Platform::FileExists(path)) { continue; }

        const Platform::ScopedFileMapping map(path);
        const RuntimeSceneStatus status = ComputeRuntimeSceneStatus(ctx, state, registry, meta.sceneId, map);
        if (status.bUnreadable) {
            LOG_ERROR(Engine, "Runtime scene '{}' is unreadable, for another scene, or an unknown version", path.c_str());
            continue;
        }
        if (status.IsOutdated()) {
            LOG_WARN(Engine, "Runtime scene '{}' is outdated: {} entities gone, {} changed (first '{}'); resave it", path.c_str(), status.missing, status.changed,
                     status.firstChanged.c_str());
        }

        const TextReader reader(map.data, map.size);
        ApplyRuntimeScene(state, reader, registry);
        ReadClock(state, reader);
        bApplied = true;
        LOG_INFO(Engine, "Applied runtime scene '{}'", path.c_str());
    }

    if (bApplied) {
        ResolvePrefabLoads(ctx, state, registry);
        ResolveHierarchyLinks(state, registry);
        state->requests.bTeleportPhysics = true;
        state->requests.RequestRenderReset(Core::RenderReset::Cut);
    }
}
} // Engine
