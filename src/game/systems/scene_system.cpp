//
// Created by William on 2026-02-26.
//

#include "scene_system.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <chrono>
#include <limits>

#include <glm/gtx/matrix_decompose.hpp>

#include "core/containers/arena_array.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/scene/scene.h"
#include "engine/resources/prefab/prefab_format.h"
#include "engine/resources/scene/scene_format.h"
#include "engine/serialization/text_parse.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/components/camera_components.h"
#include "game/components/common_components.h"
#include "game/components/common/stable_id_component.h"
#include "game/components/core_components.h"
#include "game/components/editor_components.h"
#include "game/components/gameplay/player_spawn_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/scene_components.h"
#include "game/components/physics/physics_components.h"
#include "game/gameplay/player/physics_player_controller.h"
#include "game/game_state.h"
#include "game/systems/physics_system.h"
#include "platform/file_utils.h"
#include "platform/paths.h"

namespace Game
{
Engine::Scene SaveScene(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, StringID sceneId, std::string_view sceneName)
{
    ZoneScoped;
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    Core::TlsfAllocator* alloc = &ctx->memoryManager->General();

    Engine::Scene outScene(alloc);
    Engine::TextWriter w(outScene.content);

    w.Key("scene_id", sceneId.id);
    w.KeyStr("scene_name", sceneName);

    auto view = registry.view<Component::SceneComponent>();
    const StringID prefabTypeId = TypeSID<Component::PrefabInstanceComponent>();

    auto shouldSerialize = [&](entt::entity entity) {
        return view.get<Component::SceneComponent>(entity).sceneId == sceneId && !registry.all_of<Component::DoNotSerializeTag>(entity);
    };

    for (auto entity : view) {
        if (shouldSerialize(entity)) { outScene.entityCount++; }
    }
    if (outScene.entityCount == 0) {
        return outScene;
    }
    w.Count("entities", outScene.entityCount);

    // Cache prefab bodies per-prefab so we only read each file once
    Core::InlineMap<StringID, Engine::WPrefabData, 512> prefabCache;

    Core::Vector<std::byte> fragment(alloc, Core::AllocTag::AssetManager);
    for (auto entity : view) {
        if (!shouldSerialize(entity)) {
            continue;
        }

        const auto* prefabInst = registry.try_get<Component::PrefabInstanceComponent>(entity);

        const Engine::WPrefabData* prefabRef = nullptr;
        if (prefabInst) {
            auto cacheIt = prefabCache.Find(prefabInst->prefabId);
            if (!cacheIt) {
                if (const auto* meta = assetManager->GetPrefabMetadata(prefabInst->prefabId)) {
                    auto prefabData = Engine::ReadWPrefab(meta->source.c_str(), alloc);
                    if (prefabData) {
                        cacheIt = &prefabCache.Emplace(prefabInst->prefabId, std::move(*prefabData));
                    }
                }
            }
            if (cacheIt) {
                prefabRef = cacheIt;
            }
        }

        w.BeginBlock("entity");
        for (Engine::ComponentEntry& entry : componentRegistry.registry) {
            if (!entry.has(registry, entity)) {
                continue;
            }

            fragment.Clear();
            Engine::TextWriter fw(fragment);
            entry.serialize(registry, entity, fw);
            const std::string_view fragView(reinterpret_cast<const char*>(fragment.Data()), fragment.Size());

            if (prefabRef && entry.typeId != prefabTypeId) {
                auto s = Core::ShortString::Format("%llu", entry.typeId.id);
                const Engine::TextReader prefabFrag = prefabRef->Body().Block(s.c_str());
                if (prefabFrag.IsValid() && prefabFrag.Span() == fragView) {
                    continue;
                }
            }

            w.BeginBlock(entry.typeId.id);
            w.Raw(fragment.Data(), fragment.Size());
            w.EndBlock();
        }
        w.EndBlock();
    }

    return outScene;
}

static constexpr size_t MAX_PROFILED_COMPONENT_TYPES = 128;

static void LogDeserializeBreakdown(const Engine::ComponentRegistry& componentRegistry, uint32_t entityCount, uint64_t createNanos, const uint64_t* typeNanos, const uint32_t* typeHits)
{
    const size_t typeCount = std::min(componentRegistry.registry.Size(), MAX_PROFILED_COMPONENT_TYPES);

    size_t order[MAX_PROFILED_COMPONENT_TYPES];
    size_t used = 0;
    uint64_t total = 0;
    uint32_t blocks = 0;
    for (size_t i = 0; i < typeCount; ++i) {
        total += typeNanos[i];
        blocks += typeHits[i];
        if (typeHits[i] > 0) { order[used++] = i; }
    }
    std::sort(order, order + used, [&](size_t a, size_t b) { return typeNanos[a] > typeNanos[b]; });

    LOG_INFO(Game, "Scene deserialize: {} entities, {} component blocks, {:.2f}ms in deserializers, {:.2f}ms in registry.create",
             entityCount, blocks, static_cast<double>(total) / 1e6, static_cast<double>(createNanos) / 1e6);
    for (size_t i = 0; i < used; ++i) {
        const size_t t = order[i];
        LOG_INFO(Game, "  {:<34} {:>8} x {:>9.3f}ms", componentRegistry.registry[t].name, typeHits[t], static_cast<double>(typeNanos[t]) / 1e6);
    }
}

StringID LoadScene(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, const Engine::TextReader& scene)
{
    ZoneScoped;
    auto sceneId = StringID(scene.U64("scene_id"));

    uint64_t typeNanos[MAX_PROFILED_COMPONENT_TYPES]{};
    uint32_t typeHits[MAX_PROFILED_COMPONENT_TYPES]{};
    uint32_t entityCount = 0;
    uint64_t createNanos = 0;

    scene.ForEachRecord("entities", [&](const Engine::TextReader& entityReader) {
        const auto createStart = std::chrono::steady_clock::now();
        auto entity = registry.create();
        createNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - createStart).count();
        ++entityCount;

        entityReader.ForEachBlock([&](std::string_view opener, const Engine::TextReader& compReader) {
            uint64_t typeId = 0;
            std::from_chars(opener.data(), opener.data() + opener.size(), typeId);
            const size_t* index = componentRegistry.registryMapping.Find(StringID{typeId});
            if (index == nullptr) {
                LOG_WARN(Game, "Scene component key {} matches no registered component; dropped on load", typeId);
                return;
            }

            const auto start = std::chrono::steady_clock::now();
            componentRegistry.registry[*index].deserialize(registry, entity, compReader);
            if (*index < MAX_PROFILED_COMPONENT_TYPES) {
                typeNanos[*index] += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
                typeHits[*index]++;
            }
        });
        registry.emplace<Component::SceneComponent>(entity, sceneId);
    });

    LogDeserializeBreakdown(componentRegistry, entityCount, createNanos, typeNanos, typeHits);
    return sceneId;
}

Core::InlineVector<Engine::Scene, 8> SerializeAll(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, Core::Span<Engine::RuntimeSceneMetadata> loadedScenes)
{
    ZoneScoped;
    Core::InlineVector<Engine::Scene, 8> snapshots;
    for (int i = 0; i < loadedScenes.Size(); ++i) {
        auto& meta = loadedScenes[i];
        snapshots.PushBack(SaveScene(componentRegistry, registry, assetManager, meta.sceneId, meta.sceneId.ToString()));
        LOG_INFO(Game, "Saved scene snapshot '{}'", meta.sceneId.ToString());
    }
    return snapshots;
}

void DeserializeAll(Engine::EngineState* state, Core::Span<Engine::Scene> snapshots)
{
    ZoneScoped;
    for (Engine::Scene& scene : snapshots) {
        StringID loadedId = LoadScene(state->componentRegistry, state->registry, Engine::TextReader(scene.content.Data(), scene.content.Size()));
        {
            ZoneScopedN("SortOrder");
            uint64_t maxSortOrder = 0;
            auto sortView = state->registry.view<Component::SceneComponent, Component::StableIdComponent>();
            for (auto entity : sortView) {
                if (sortView.get<Component::SceneComponent>(entity).sceneId == loadedId) {
                    maxSortOrder = std::max(maxSortOrder, sortView.get<Component::StableIdComponent>(entity).sortOrder);
                }
            }
            // For those without a valid sort order
            for (auto entity : sortView) {
                if (sortView.get<Component::SceneComponent>(entity).sceneId == loadedId) {
                    auto& stable = sortView.get<Component::StableIdComponent>(entity);
                    if (stable.sortOrder == 0) {
                        maxSortOrder += 1;
                        stable.sortOrder = maxSortOrder;
                    }
                }
            }
        }
        state->editor.loadedScenes.PushBack({loadedId});
        LOG_INFO(Game, "Loaded scene snapshot '{}'", loadedId.ToString());
    }

    auto* ctx = state->registry.ctx().get<Engine::EngineContext*>();
    ResolvePrefabLoads(state, ctx->assetManager);
    ResolveHierarchyLinks(state);
}

void UnloadScenes(Engine::EngineState* state, Core::Span<StringID> scenes)
{
    ZoneScoped;
    for (const StringID sceneId : scenes) {
        UnloadScene(state, sceneId);
        LOG_INFO(Game, "Unloaded scene '{}'", sceneId.ToString());
    }
}

void UnloadScene(Engine::EngineState* state, StringID sceneId)
{
    ZoneScoped;
    auto* ctx = state->registry.ctx().get<Engine::EngineContext*>();
    auto view = state->registry.view<Component::SceneComponent>();

    auto size = view.size();
    Core::ArenaVector<entt::entity> toDestroy{&ctx->gameplayArena.Get(), size};
    for (auto entity : view) {
        if (view.get<Component::SceneComponent>(entity).sceneId == sceneId) {
            toDestroy.PushBack(entity);
        }
    }

    {
        ZoneScopedN("Destroy");
        for (entt::entity entity : toDestroy) {
            state->registry.destroy(entity);
        }
    }

    state->editor.selectedEntities.Clear();
    state->editor.loadedScenes.RemoveFirstIf([sceneId](const Engine::RuntimeSceneMetadata& m) {
        return m.sceneId == sceneId;
    });
    state->editor.modifiedScenes.RemoveFirst(sceneId);
}

void SaveSceneToFile(StringID sceneID, std::string_view sceneName, Engine::EngineState* state, Engine::AssetManager* assetManager, Engine::EngineContext* ctx)
{
    const auto& sceneCache = assetManager->GetSceneCache();
    Core::Path path;

    auto it = sceneCache.Find(sceneID);
    if (it && !it->source.IsEmpty()) {
        path = it->source;
    }
    else {
        auto stem = Core::InlineString<128>(sceneName);
        std::ranges::transform(stem.buf, stem.buf + stem.len, stem.buf, tolower);
        std::ranges::replace(stem.buf, stem.buf + stem.len, ' ', '_');
        stem.Append(".wscene");
        path = Platform::GetAssetPath() / "scenes" / stem.c_str();
        assert(path.Extension() == ".wscene");
    }

    Engine::Scene s = SaveScene(state->componentRegistry, state->registry, assetManager, sceneID, sceneName); {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            Engine::TextWriter camWriter(s.content);
            camWriter.BeginBlock("editor_camera");
            camWriter.Key("translation", transform.translation);
            camWriter.Key("rotation", transform.rotation);
            camWriter.EndBlock();
        }
    }

    uint64_t contentVersion = 1;
    if (path.Exists()) {
        if (auto existing = Engine::ReadWSceneHeader(path)) {
            contentVersion = existing->contentVersion + 1;
        }
    }

    Engine::WSceneHeader sceneHeader{};
    sceneHeader.sceneId = sceneID.id;
    sceneHeader.contentVersion = contentVersion;
    const auto nameLen = std::min(sceneName.size(), Engine::WSCENE_NAME_LENGTH - 1);
    memcpy(sceneHeader.name, sceneName.data(), nameLen);
    sceneHeader.name[nameLen] = '\0';
    sceneHeader.entityCount = s.entityCount;

    Core::Vector<std::byte> out(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    Engine::WriteWSceneHeader(out, sceneHeader);
    out.Append(s.content.Data(), s.content.Data() + s.content.Size());
    if (!Platform::WriteFile(path, out.Data(), out.Size())) {
        LOG_ERROR(Game, "Failed to write scene file '{}'", path.c_str());
        return;
    }

    assetManager->UpdateSceneCachePath(sceneID, path, sceneHeader.entityCount);

    LOG_INFO(Game, "Saved scene '{}' to '{}'", sceneName, path.c_str());
}

LoadSceneResult LoadSceneFromFile(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID sceneId)
{
    ZoneScoped;
    if (std::ranges::any_of(state->editor.loadedScenes, [&](const auto& m) { return m.sceneId == sceneId; })) {
        LOG_WARN(Game, "Scene '{}' is already loaded", sceneId.ToString());
        return {false, sceneId, {}};
    }

    const auto& sceneCache = assetManager->GetSceneCache();
    const auto* it = sceneCache.Find(sceneId);
    if (!it) {
        LOG_ERROR(Game, "Scene ID not found in registry");
        return {false, sceneId, {}};
    }

    const Core::InlineString<128> sceneName = it->sceneName;
    const Core::Path& path = it->source;
    Platform::ScopedFileMapping map(path);
    if (!map.data) {
        LOG_ERROR(Game, "Failed to open scene file '{}'", path.c_str());
        return {false, sceneId, sceneName};
    }

    auto header = Engine::ReadWSceneHeader(map.data, map.size);
    if (!header) {
        LOG_ERROR(Game, "Failed to read scene header from '{}'", path.c_str());
        return {false, sceneId, sceneName};
    }

    const Engine::TextReader sceneReader(map.data + header->dataOffset, map.size - header->dataOffset);
    StringID loadedId = LoadScene(state->componentRegistry, state->registry, sceneReader);
    assert(loadedId == sceneId && "Scene ID in file does not match registry key, file was likely saved with a mismatched ID");
    uint64_t maxSortOrder = 0; {
        ZoneScopedN("SortOrder");
        auto sortView = state->registry.view<Component::SceneComponent, Component::StableIdComponent>();
        for (auto entity : sortView) {
            if (sortView.get<Component::SceneComponent>(entity).sceneId == loadedId) {
                maxSortOrder = std::max(maxSortOrder, sortView.get<Component::StableIdComponent>(entity).sortOrder);
            }
        }
        // For those without a valid sort order
        for (auto entity : sortView) {
            if (sortView.get<Component::SceneComponent>(entity).sceneId == loadedId) {
                auto& stable = sortView.get<Component::StableIdComponent>(entity);
                if (stable.sortOrder == 0) {
                    maxSortOrder += 1;
                    stable.sortOrder = maxSortOrder;
                }
            }
        }
    }
    state->editor.loadedScenes.PushBack({loadedId});
    state->editor.modifiedScenes.RemoveFirst(loadedId);

    ResolvePrefabLoads(state, assetManager);
    ResolveHierarchyLinks(state);

    const Engine::TextReader camReader = sceneReader.Block("editor_camera");
    if (camReader.IsValid()) {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            transform.translation = camReader.Vec3("translation", transform.translation);
            transform.rotation = camReader.Quat("rotation", transform.rotation);
        }
    }

    LOG_INFO(Game, "Loaded scene '{}' from '{}'", sceneId.ToString(), path.c_str());
    return {true, sceneId, sceneName};
}

bool SaveSceneSlot(Engine::EngineState* state, int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= Engine::MAX_SCENE_SLOTS) { return false; }
    if (!state->scene.currentSceneId) {
        LOG_WARN(Game, "Scene slot {}: no current scene to bind", slotIndex + 1);
        return false;
    }

    Engine::SceneSlot& slot = state->projectConfig.sceneSlots[slotIndex];
    slot.sceneId = state->scene.currentSceneId;
    slot.sceneName = state->scene.currentSceneName;
    Engine::WriteProjectConfig(state->projectConfig, state->allocator);

    LOG_INFO(Game, "Scene slot {} bound to '{}'", slotIndex + 1, slot.sceneName.c_str());
    return true;
}

bool LoadSceneSlot(Engine::EngineContext* ctx, Engine::EngineState* state, int slotIndex)
{
    ZoneScoped;
    if (slotIndex < 0 || slotIndex >= Engine::MAX_SCENE_SLOTS) { return false; }

    const Engine::SceneSlot& slot = state->projectConfig.sceneSlots[slotIndex];
    if (!slot.sceneId) {
        LOG_WARN(Game, "Scene slot {} is empty", slotIndex + 1);
        return false;
    }
    if (!ctx->assetManager->GetSceneCache().Find(slot.sceneId)) {
        LOG_WARN(Game, "Scene slot {} points at '{}', which is no longer registered", slotIndex + 1, slot.sceneName.c_str());
        return false;
    }

    const bool bWasPlaying = IsPlaying(state);
    if (bWasPlaying) {
        PlayStop(ctx, state);
    }

    Core::InlineVector<StringID, 8> scenesToUnload;
    for (const Engine::RuntimeSceneMetadata& scene : state->editor.loadedScenes) {
        scenesToUnload.PushBack(scene.sceneId);
    }
    UnloadScenes(state, scenesToUnload);

    const LoadSceneResult result = LoadSceneFromFile(state, ctx->assetManager, slot.sceneId);
    if (result.bSuccess) {
        state->scene.currentSceneId = result.sceneId;
        state->scene.currentSceneName = result.sceneName;
    }

    if (bWasPlaying) {
        PlayStart(ctx, state);
    }
    return result.bSuccess;
}

Core::ArenaVector<entt::entity> SpawnModel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::ModelID modelId, const glm::vec3& offset)
{
    ZoneScoped;
    const Engine::AssetManager::CachedModelMetadata* cached = ctx->assetManager->GetModelMetadata(modelId);
    if (!cached) {
        LOG_ERROR(Game, "SpawnModel: modelId {} not in asset registry", modelId.id);
        return {};
    }

    auto& registry = state->registry;
    auto spawned = Core::ArenaVector<entt::entity>(&ctx->gameplayArena.Get(), 1);

    entt::entity entity = CreateSceneEntity(state);

    if (cached->name.Size() > 0) {
        registry.get<Component::NameComponent>(entity).name = Core::InlineString<256>(cached->name.c_str());
    }

    auto& transform = registry.get<Component::TransformComponent>(entity);
    transform.translation = offset;

    Component::StaticMeshComponent meshComp{};
    meshComp.modelId = modelId;
    registry.emplace<Component::StaticMeshComponent>(entity, std::move(meshComp));

    spawned.PushBack(entity);
    state->bHierarchyOrderDirty = true;
    return spawned;
}

entt::entity SplitOffMeshPrimitive(Engine::EngineState* state, entt::entity parent, uint32_t primitiveOrdinal, const glm::mat4& nodeModelSpace)
{
    auto& registry = state->registry;
    auto& parentComp = registry.get<Component::StaticMeshComponent>(parent);

    entt::entity child = CreateSceneEntity(state);
    registry.get<Component::NameComponent>(child).name = Core::InlineString<256>::Format("Primitive %u", primitiveOrdinal);

    glm::vec3 scale, translation, skew;
    glm::quat rotation;
    glm::vec4 perspective;
    glm::decompose(nodeModelSpace, scale, rotation, translation, skew, perspective);
    auto& childTransform = registry.get<Component::TransformComponent>(child);
    childTransform.translation = translation;
    childTransform.rotation = rotation;
    childTransform.scale = scale;

    auto& hierarchy = registry.emplace<Component::HierarchyComponent>(child);
    hierarchy.parent = parent;
    hierarchy.parentStableId = registry.get<Component::StableIdComponent>(parent).id;

    Component::StaticMeshPrimitiveComponent childMesh{};
    childMesh.modelId = parentComp.modelId;
    childMesh.primitiveOrdinal = primitiveOrdinal;
    registry.emplace<Component::StaticMeshPrimitiveComponent>(child, childMesh);
    if (auto* parentFlags = registry.try_get<Component::RenderFlagsComponent>(parent)) {
        registry.emplace_or_replace<Component::RenderFlagsComponent>(child, *parentFlags);
    }

    if (!parentComp.primitiveBlacklist.Contains(primitiveOrdinal)) { parentComp.primitiveBlacklist.PushBack(primitiveOrdinal); }
    registry.emplace_or_replace<Component::StaticMeshLoadingTag>(parent);
    state->assetLoad.bPendingModelResolve = true;

    state->bHierarchyOrderDirty = true;
    return child;
}

uint64_t HighestSortOrderInScene(entt::registry& registry, StringID sceneId)
{
    uint64_t maxSortOrder = 0;
    auto view = registry.view<Component::SceneComponent, Component::StableIdComponent>();
    for (auto entity : view) {
        if (view.get<Component::SceneComponent>(entity).sceneId == sceneId) {
            maxSortOrder = std::max(maxSortOrder, view.get<Component::StableIdComponent>(entity).sortOrder);
        }
    }
    return maxSortOrder;
}

Core::InlineString<256> GenerateIncrementedName(entt::registry& registry, StringID sceneId, const Core::InlineString<256>& sourceName)
{
    const std::string_view src = sourceName.View();

    size_t digitsStart = src.size();
    while (digitsStart > 0 && std::isdigit(static_cast<unsigned char>(src[digitsStart - 1]))) {
        --digitsStart;
    }
    const std::string_view base = src.substr(0, digitsStart);

    uint32_t highest = 0;
    auto view = registry.view<Component::SceneComponent, Component::NameComponent>();
    for (auto entity : view) {
        if (view.get<Component::SceneComponent>(entity).sceneId != sceneId) { continue; }

        std::string_view name = view.get<Component::NameComponent>(entity).name.View();
        if (name.size() < base.size() || name.substr(0, base.size()) != base) { continue; }

        std::string_view rest = name.substr(base.size());
        if (rest.empty()) { continue; }

        bool allDigits = true;
        for (const char c : rest) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                allDigits = false;
                break;
            }
        }
        if (!allDigits) { continue; }

        uint32_t index = 0;
        for (const char c : rest) { index = index * 10 + static_cast<uint32_t>(c - '0'); }
        highest = std::max(highest, index);
    }

    return Core::InlineString<256>::Format("%.*s%u", static_cast<int>(base.size()), base.data(), highest + 1);
}

entt::entity CreateSceneEntity(Engine::EngineState* state)
{
    entt::entity newEntity = state->registry.create();
    state->registry.emplace<Component::TransformComponent>(newEntity);
    state->registry.emplace<Component::SceneComponent>(newEntity, state->scene.currentSceneId);
    state->registry.emplace<Component::StableIdComponent>(newEntity);
    state->registry.get<Component::StableIdComponent>(newEntity).sortOrder = HighestSortOrderInScene(state->registry, state->scene.currentSceneId) + 1;
    state->registry.emplace<Component::EntityFolderComponent>(newEntity);
    static int32_t runningNameTally = 0;
    auto newName = fmt::format("New Entity {}", runningNameTally++);
    state->registry.emplace<Component::NameComponent>(newEntity, Core::InlineString<256>(newName.c_str()));
    LOG_TRACE(Game, "Created new entity {}", entt::to_integral(newEntity));
    return newEntity;
}

static uint16_t HierarchyDepth(const entt::registry& registry, entt::entity entity)
{
    uint16_t depth = 0;
    const auto* node = registry.try_get<Component::HierarchyComponent>(entity);
    while (node && node->parent != entt::null && registry.valid(node->parent)) {
        node = registry.try_get<Component::HierarchyComponent>(node->parent);
        if (++depth >= 1024) { break; }
    }
    return depth;
}

static void RefreshHierarchyOrder(entt::registry& registry)
{
    ZoneScoped;
    for (auto [entity, node] : registry.view<Component::HierarchyComponent>().each()) {
        node.depth = HierarchyDepth(registry, entity);
    }
    registry.sort<Component::HierarchyComponent>(
        [](const Component::HierarchyComponent& a, const Component::HierarchyComponent& b) { return a.depth < b.depth; },
        entt::insertion_sort{});
}

void EnsureHierarchyOrder(Engine::EngineState* state)
{
    if (!state->bHierarchyOrderDirty) { return; }
    RefreshHierarchyOrder(state->registry);
    state->bHierarchyOrderDirty = false;
}

void ResolveHierarchyLinks(Engine::EngineState* state)
{
    ZoneScoped;
    auto& registry = state->registry;
    for (auto [entity, node] : registry.view<Component::HierarchyComponent>().each()) {
        const auto* found = node.parentStableId.IsValid() ? state->stableIdToEntityMap.Find(node.parentStableId) : nullptr;
        node.parent = found ? *found : entt::null;
    }
    state->bHierarchyOrderDirty = true;
}

void SetParent(Engine::EngineState* state, entt::entity child, entt::entity parent)
{
    ZoneScoped;
    auto& registry = state->registry;
    if (child == parent || !registry.valid(child) || !registry.valid(parent)) { return; }

    // Parent must not already be a descendant of child.
    for (entt::entity e = parent; e != entt::null;) {
        if (e == child) { return; }
        const auto* ancestor = registry.try_get<Component::HierarchyComponent>(e);
        e = (ancestor && registry.valid(ancestor->parent)) ? ancestor->parent : entt::null;
    }

    const Transform parentWorld = Component::ComputeWorldTransform(registry, parent);
    const Transform childWorld = Component::ComputeWorldTransform(registry, child);

    auto& node = registry.get_or_emplace<Component::HierarchyComponent>(child);
    node.parent = parent;
    node.parentStableId = registry.get<Component::StableIdComponent>(parent).id;

    // Keep the child's world pose fixed.
    if (!registry.all_of<Component::DynamicPhysicsBodyComponent>(child)) {
        registry.get<Component::TransformComponent>(child) = Component::ComposeLocalFromWorld(parentWorld, childWorld);
        registry.emplace_or_replace<Component::DirtyTransformTag>(child);
    }

    state->bHierarchyOrderDirty = true;
}

void ClearParent(Engine::EngineState* state, entt::entity child)
{
    ZoneScoped;
    auto& registry = state->registry;
    if (!registry.valid(child) || !registry.all_of<Component::HierarchyComponent>(child)) { return; }

    const Transform childWorld = Component::ComputeWorldTransform(registry, child);
    registry.remove<Component::HierarchyComponent>(child);

    if (!registry.all_of<Component::DynamicPhysicsBodyComponent>(child)) {
        auto& local = registry.get<Component::TransformComponent>(child);
        local.translation = childWorld.translation;
        local.rotation = childWorld.rotation;
        local.scale = childWorld.scale;
        registry.emplace_or_replace<Component::DirtyTransformTag>(child);
    }

    state->bHierarchyOrderDirty = true;
}

void SetWorldTransform(Engine::EngineState* state, entt::entity entity, const Transform& world)
{
    ZoneScoped;
    auto& registry = state->registry;
    if (!registry.valid(entity) || !registry.all_of<Component::TransformComponent>(entity)) { return; }

    const auto* node = registry.try_get<Component::HierarchyComponent>(entity);
    if (node && registry.valid(node->parent)) {
        // The parent's cached world already encodes the chain above it; reading it is O(1) and keeps both on the same one-frame-behind clock.
        const Transform parentWorld = registry.get<Component::WorldTransformComponent>(node->parent);
        registry.get<Component::TransformComponent>(entity) = Component::ComposeLocalFromWorld(parentWorld, world);
    }
    else {
        registry.get<Component::TransformComponent>(entity) = world;
    }
    registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
}

void SaveEntityAsPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, Engine::EngineContext* ctx, entt::entity entity, std::string_view prefabName)
{
    // todo: Fix this to:
    //  Compare all existing prefabs and check their components. If their component fields are precisely the same as the src prefab, then replace it with new
    Core::Vector<std::byte> body(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    Engine::TextWriter w(body);
    uint32_t componentCount = 0;

    for (Engine::ComponentEntry& entry : state->componentRegistry.registry) {
        if (entry.has(state->registry, entity)) {
            w.BeginBlock(entry.typeId.id);
            entry.serialize(state->registry, entity, w);
            w.EndBlock();
            componentCount++;
        }
    }

    Core::Path path;
    uint64_t prefabId = 0;
    bool isNewPrefab = true;

    auto* prefabInstance = state->registry.try_get<Component::PrefabInstanceComponent>(entity);
    if (prefabInstance) {
        const auto* meta = assetManager->GetPrefabMetadata(StringID{prefabInstance->prefabId});
        if (meta) {
            path = meta->source;
            prefabId = prefabInstance->prefabId.id;
            isNewPrefab = false;
        }
    }

    if (isNewPrefab) {
        Core::InlineString s{prefabName};
        s.ToLower();
        s.Replace(' ', '_');
        s.Append(".wprefab");
        path = Platform::GetAssetPath() / "prefabs" / s.c_str();
        prefabId = state->rng();
    }

    uint64_t contentVersion = 1;
    if (path.Exists()) {
        if (auto existing = Engine::ReadWPrefabHeader(path)) {
            contentVersion = existing->contentVersion + 1;
        }
    }

    Engine::WPrefabHeader header{};
    header.prefabId = prefabId;
    header.contentVersion = contentVersion;
    const auto nameLen = std::min(prefabName.size(), Engine::WPREFAB_NAME_LENGTH - 1);
    memcpy(header.name, prefabName.data(), nameLen);
    header.name[nameLen] = '\0';
    header.componentCount = componentCount;

    Core::Vector<std::byte> out(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    Engine::WriteWPrefabHeader(out, header);
    out.Append(body.Data(), body.Data() + body.Size());
    if (!Platform::WriteFile(path, out.Data(), out.Size())) {
        LOG_ERROR(Game, "Failed to write prefab file '{}'", path.c_str());
        return;
    }

    state->registry.emplace_or_replace<Component::PrefabInstanceComponent>(entity, StringID{prefabId});

    if (isNewPrefab) {
        ctx->rescan.bResources = true;
    }
    LOG_INFO(Game, "Saved prefab '{}' to '{}'", prefabName, path.c_str());
}

entt::entity SpawnPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID prefabId, const glm::vec3& spawnPosition)
{
    const auto* meta = assetManager->GetPrefabMetadata(prefabId);
    if (!meta) {
        LOG_ERROR(Game, "Prefab '{}' not found in registry", prefabId.ToString());
        return entt::null;
    }

    auto* ctx = state->registry.ctx().get<Engine::EngineContext*>();
    auto prefabData = Engine::ReadWPrefab(meta->source.c_str(), &ctx->memoryManager->AssetsScratch());
    if (!prefabData) {
        LOG_ERROR(Game, "Failed to read prefab file '{}'", meta->source.c_str());
        return entt::null;
    }

    entt::entity entity = state->registry.create();
    prefabData->Body().ForEachBlock([&](std::string_view opener, const Engine::TextReader& compReader) {
        uint64_t typeId = 0;
        std::from_chars(opener.data(), opener.data() + opener.size(), typeId);
        const size_t* index = state->componentRegistry.registryMapping.Find(StringID{typeId});
        if (index == nullptr) {
            LOG_WARN(Game, "Prefab component key {} matches no registered component; dropped on spawn", typeId);
            return;
        }
        state->componentRegistry.registry[*index].deserialize(state->registry, entity, compReader);
    });

    if (auto* transform = state->registry.try_get<Component::TransformComponent>(entity)) {
        transform->translation = spawnPosition;
    }

    state->registry.emplace<Component::SceneComponent>(entity, state->scene.currentSceneId);
    state->registry.emplace_or_replace<Component::PrefabInstanceComponent>(entity, prefabId);

    if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
        stable->sortOrder = HighestSortOrderInScene(state->registry, state->scene.currentSceneId) + 1;
    }

    LOG_INFO(Game, "Spawned prefab '{}' as entity {}", meta->prefabName.c_str(), entt::to_integral(entity));
    return entity;
}

void ResolvePrefabLoads(Engine::EngineState* state, Engine::AssetManager* assetManager)
{
    ZoneScoped;
    auto* ctx = state->registry.ctx().get<Engine::EngineContext*>();

    // Cache prefab bodies so each file is read once even with many instances
    Core::InlineMap<StringID, Engine::WPrefabData, 512> prefabCache;

    auto view = state->registry.view<Component::PrefabInstanceComponent>();
    for (auto entity : view) {
        auto& prefabInst = view.get<Component::PrefabInstanceComponent>(entity);

        const auto* meta = assetManager->GetPrefabMetadata(prefabInst.prefabId);
        if (!meta) {
            LOG_WARN(Game, "Prefab '{}' not found for entity {}, skipping", prefabInst.prefabId.ToString(), entt::to_integral(entity));
            continue;
        }

        auto cacheIt = prefabCache.Find(prefabInst.prefabId);
        if (cacheIt == nullptr) {
            auto prefabData = Engine::ReadWPrefab(meta->source.c_str(), &ctx->memoryManager->AssetsScratch());
            if (!prefabData) {
                LOG_WARN(Game, "Failed to read prefab file '{}'", meta->source.c_str());
                continue;
            }
            cacheIt = &prefabCache.Emplace(prefabInst.prefabId, std::move(*prefabData));
        }

        cacheIt->Body().ForEachBlock([&](std::string_view opener, const Engine::TextReader& compReader) {
            uint64_t typeId = 0;
            std::from_chars(opener.data(), opener.data() + opener.size(), typeId);
            const size_t* index = state->componentRegistry.registryMapping.Find(StringID{typeId});
            if (index == nullptr) {
                LOG_WARN(Game, "Prefab component key {} matches no registered component; skipped", typeId);
                return;
            }

            Engine::ComponentEntry& entry = state->componentRegistry.registry[*index];
            if (!entry.has(state->registry, entity)) {
                entry.deserialize(state->registry, entity, compReader);
            }
        });
    }
}

void PlayStart(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped; {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            state->editor.pieCameraTranslation = transform.translation;
            state->editor.pieCameraRotation = transform.rotation;
        }
    }

    state->editor.pieSnapshot = SerializeAll(state->componentRegistry, state->registry, ctx->assetManager, state->editor.loadedScenes); {
        auto view = state->registry.view<Component::PrefabInstanceComponent>();
        if (view.size() > 0) {
            auto masterPrefabs = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size());
            for (auto entity : view) {
                if (view.get<Component::PrefabInstanceComponent>(entity).bMasterPrefab) {
                    masterPrefabs.PushBack(entity);
                }
            }
            for (auto entity : masterPrefabs) {
                state->registry.destroy(entity);
            }
        }
    }

    state->inputContext = Engine::InputContext::Gameplay;
    ctx->setCursorHiddenFn(true);
    state->editor.selectedEntities.Clear();

    glm::vec3 spawnPosition{0.0f, 3.0f, 0.0f}; {
        int32_t bestPriority = std::numeric_limits<int32_t>::min();
        auto spawnView = state->registry.view<Component::PlayerSpawnComponent, Component::TransformComponent>();
        for (auto entity : spawnView) {
            auto& spawn = spawnView.get<Component::PlayerSpawnComponent>(entity);
            if (spawn.priority > bestPriority) {
                bestPriority = spawn.priority;
                spawnPosition = spawnView.get<Component::TransformComponent>(entity).translation + spawn.offset;
            }
        }
    }

    ctx->GetGameState<GameState>()->playerController.Initialize(state, ctx, spawnPosition);
}

void PlayStop(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    PhysicsPlayerController& playerController = ctx->GetGameState<GameState>()->playerController;
    if (playerController.GetCharacter()) {
        playerController.Shutdown(ctx->physicsSystem);
    }

    Core::InlineVector<StringID, 8> scenesToUnload;
    for (Engine::RuntimeSceneMetadata scene : state->editor.loadedScenes) {
        scenesToUnload.PushBack(scene.sceneId);
    }
    UnloadScenes(state, scenesToUnload);
    DeserializeAll(state, state->editor.pieSnapshot);
    state->editor.pieSnapshot = {};

    state->inputContext = Engine::InputContext::Editor;
    ctx->setCursorHiddenFn(false); {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            transform.translation = state->editor.pieCameraTranslation;
            transform.rotation = state->editor.pieCameraRotation;
        }
    }
}
} // Game
