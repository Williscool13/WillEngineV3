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
#include "core/containers/arena_fixed_vector.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/scene/scene.h"
#include "engine/resources/prefab/prefab_format.h"
#include "engine/resources/scene/scene_format.h"
#include "engine/serialization/text_parse.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/components/camera_components.h"
#include "engine/components/common_components.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/components/core_components.h"
#include "engine/components/editor_components.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/static_mesh_primitive_component.h"
#include "engine/components/scene_components.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/components/physics/physics_components.h"
#include "engine/systems/physics_system.h"
#include "engine/systems/runtime_save.h"
#include "engine/editor/mobility_assign.h"
#include "platform/file_utils.h"
#include "platform/paths.h"

namespace Engine
{
Engine::Scene SaveScene(Engine::ComponentRegistry& componentRegistry, Engine::Registry& registry, Engine::AssetManager* assetManager, StringID sceneId, SceneSaveFilter filter)
{
    ZoneScoped;
    auto* ctx = registry.Context();
    Core::TlsfAllocator* alloc = &ctx->memoryManager->General();

    Engine::Scene outScene(alloc);
    Engine::TextWriter w(outScene.content);

    w.Key("scene_id", sceneId.id);

    auto view = registry.View<Component::SceneRuntime>();
    const StringID prefabTypeId = TypeSID<Component::PrefabInstanceComponent>();

    const bool bSpawned = filter == SceneSaveFilter::Spawned;
    auto shouldSerialize = [&](entt::entity entity) {
        if (view.get<Component::SceneRuntime>(entity).sceneId != sceneId) { return false; }
        if (registry.Has<Component::DoNotSerializeTag>(entity) != bSpawned) { return false; }
        return !bSpawned || !registry.Has<Component::DoNotRuntimeSaveTag>(entity);
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

        const auto* prefabInst = registry.TryGet<Component::PrefabInstanceComponent>(entity);

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
        if (bSpawned) {
            for (const Engine::RuntimeStateEntry& entry : componentRegistry.runtimeState) {
                if (!entry.has(registry, entity)) { continue; }
                w.BeginBlock(entry.typeId.id);
                entry.serialize(registry, entity, w);
                w.EndBlock();
            }
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

    LOG_INFO(Engine, "Scene deserialize: {} entities, {} component blocks, {:.2f}ms in deserializers, {:.2f}ms in registry.create",
             entityCount, blocks, static_cast<double>(total) / 1e6, static_cast<double>(createNanos) / 1e6);
    for (size_t i = 0; i < used; ++i) {
        const size_t t = order[i];
        LOG_INFO(Engine, "  {:<34} {:>8} x {:>9.3f}ms", componentRegistry.registry[t].name, typeHits[t], static_cast<double>(typeNanos[t]) / 1e6);
    }
}

StringID LoadScene(Engine::ComponentRegistry& componentRegistry, Engine::Registry& registry, const Engine::TextReader& scene)
{
    ZoneScoped;
    auto sceneId = StringID(scene.U64("scene_id"));

    uint64_t typeNanos[MAX_PROFILED_COMPONENT_TYPES]{};
    uint32_t typeHits[MAX_PROFILED_COMPONENT_TYPES]{};
    uint32_t entityCount = 0;
    uint64_t createNanos = 0;

    scene.ForEachRecord("entities", [&](const Engine::TextReader& entityReader) {
        const auto createStart = std::chrono::steady_clock::now();
        auto entity = registry.Create();
        createNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - createStart).count();
        ++entityCount;

        entityReader.ForEachBlock([&](std::string_view opener, const Engine::TextReader& compReader) {
            uint64_t typeId = 0;
            std::from_chars(opener.data(), opener.data() + opener.size(), typeId);
            const size_t* index = componentRegistry.registryMapping.Find(StringID{typeId});
            if (index == nullptr) {
                if (const size_t* stateIndex = componentRegistry.runtimeStateMapping.Find(StringID{typeId})) {
                    componentRegistry.runtimeState[*stateIndex].deserialize(registry, entity, compReader);
                    return;
                }
                LOG_WARN(Engine, "Scene component key {} matches no registered component; dropped on load", typeId);
                return;
            }

            const auto start = std::chrono::steady_clock::now();
            componentRegistry.registry[*index].deserialize(registry, entity, compReader);
            if (*index < MAX_PROFILED_COMPONENT_TYPES) {
                typeNanos[*index] += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
                typeHits[*index]++;
            }
        });
        registry.Add<Component::SceneRuntime>(entity, sceneId);
    });

    LogDeserializeBreakdown(componentRegistry, entityCount, createNanos, typeNanos, typeHits);
    return sceneId;
}

Core::InlineVector<Engine::Scene, 8> SerializeAll(Engine::ComponentRegistry& componentRegistry, Engine::Registry& registry, Engine::AssetManager* assetManager, Core::Span<Engine::RuntimeSceneMetadata> loadedScenes)
{
    ZoneScoped;
    Core::InlineVector<Engine::Scene, 8> snapshots;
    for (int i = 0; i < loadedScenes.Size(); ++i) {
        auto& meta = loadedScenes[i];
        snapshots.PushBack(SaveScene(componentRegistry, registry, assetManager, meta.sceneId));
        LOG_INFO(Engine, "Saved scene snapshot '{}'", meta.sceneId.ToString());
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
            auto sortView = state->registry.View<Component::SceneRuntime, Engine::Mut<Component::StableIdComponent>>();
            for (auto entity : sortView) {
                if (sortView.get<Component::SceneRuntime>(entity).sceneId == loadedId) {
                    maxSortOrder = std::max(maxSortOrder, sortView.get<Component::StableIdComponent>(entity).sortOrder);
                }
            }
            // For those without a valid sort order
            for (auto entity : sortView) {
                if (sortView.get<Component::SceneRuntime>(entity).sceneId == loadedId) {
                    auto& stable = sortView.get<Component::StableIdComponent>(entity);
                    if (stable.sortOrder == 0) {
                        maxSortOrder += 1;
                        stable.sortOrder = maxSortOrder;
                    }
                }
            }
        }
        state->editor.loadedScenes.PushBack({loadedId});
        LOG_INFO(Engine, "Loaded scene snapshot '{}'", loadedId.ToString());
    }

    auto* ctx = state->registry.Context();
    ResolvePrefabLoads(state, ctx->assetManager);
    ResolveHierarchyLinks(state);
}

void UnloadScenes(Engine::EngineState* state, Core::Span<StringID> scenes)
{
    ZoneScoped;
    for (const StringID sceneId : scenes) {
        UnloadScene(state, sceneId);
        LOG_INFO(Engine, "Unloaded scene '{}'", sceneId.ToString());
    }
}

void UnloadScene(Engine::EngineState* state, StringID sceneId)
{
    ZoneScoped;
    auto* ctx = state->registry.Context();
    auto view = state->registry.View<Component::SceneRuntime>();

    auto size = view.size();
    Core::ArenaVector<entt::entity> toDestroy{&ctx->gameplayArena.Get(), size};
    for (auto entity : view) {
        if (view.get<Component::SceneRuntime>(entity).sceneId == sceneId) {
            toDestroy.PushBack(entity);
        }
    }

    {
        ZoneScopedN("Destroy");
        for (entt::entity entity : toDestroy) {
            state->registry.Destroy(entity);
        }
    }

    state->editor.selectedEntities.Clear();
    state->editor.loadedScenes.RemoveFirstIf([sceneId](const Engine::RuntimeSceneMetadata& m) {
        return m.sceneId == sceneId;
    });
    state->editor.modifiedScenes.RemoveFirst(sceneId);

    if (const auto* meta = ctx->assetManager->GetSceneMetadata(sceneId); meta && meta->bUnsaved) {
        ctx->rescan.bScenes = true;
    }
}

void SaveSceneToFile(StringID sceneID, Engine::EngineState* state, Engine::AssetManager* assetManager, Engine::EngineContext* ctx)
{
    if (!ctx->bGameLoaded) {
        LOG_WARN(Engine, "Scene save refused: game.dll is not loaded, game components would be lost");
        return;
    }

    const auto* it = assetManager->GetSceneCache().Find(sceneID);
    if (!it) {
        LOG_ERROR(Engine, "Scene save refused: {} is not registered", sceneID.ToString());
        return;
    }
    const Core::Path path = it->source;
    assert(path.Extension() == ".wscene");

    Engine::Scene s = SaveScene(state->componentRegistry, state->registry, assetManager, sceneID); {
        auto camView = state->registry.View<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.Get<Component::TransformComponent>(camEntity);
            Engine::TextWriter camWriter(s.content);
            camWriter.BeginBlock("editor_camera");
            camWriter.Key("translation", transform.translation);
            camWriter.Key("rotation", transform.rotation);
            camWriter.EndBlock();
        }
    }

    uint64_t contentVersion = 1;
    if (Platform::FileExists(path)) {
        if (auto existing = Engine::ReadWSceneHeader(path)) {
            contentVersion = existing->contentVersion + 1;
        }
    }

    Engine::WSceneHeader sceneHeader{};
    sceneHeader.sceneId = sceneID.id;
    sceneHeader.contentVersion = contentVersion;
    sceneHeader.entityCount = s.entityCount;

    Core::Vector<std::byte> out(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    Engine::WriteWSceneHeader(out, sceneHeader);
    out.Append(s.content.Data(), s.content.Data() + s.content.Size());
    if (!Platform::WriteFile(path, out.Data(), out.Size())) {
        LOG_ERROR(Engine, "Failed to write scene file '{}'", path.c_str());
        return;
    }

    assetManager->UpdateSceneCachePath(sceneID, path, sceneHeader.entityCount);

    LOG_INFO(Engine, "Saved scene '{}' to '{}'", it->sceneName.c_str(), path.c_str());
}

LoadSceneResult LoadSceneFromFile(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID sceneId)
{
    ZoneScoped;
    if (std::ranges::any_of(state->editor.loadedScenes, [&](const auto& m) { return m.sceneId == sceneId; })) {
        LOG_WARN(Engine, "Scene '{}' is already loaded", sceneId.ToString());
        return {false, sceneId, {}};
    }

    const auto& sceneCache = assetManager->GetSceneCache();
    const auto* it = sceneCache.Find(sceneId);
    if (!it) {
        LOG_ERROR(Engine, "Scene ID not found in registry");
        return {false, sceneId, {}};
    }

    const Core::InlineString<128> sceneName = it->sceneName;
    const Core::Path& path = it->source;
    Platform::ScopedFileMapping map(path);
    if (!map.data) {
        LOG_ERROR(Engine, "Failed to open scene file '{}'", path.c_str());
        return {false, sceneId, sceneName};
    }

    auto header = Engine::ReadWSceneHeader(map.data, map.size);
    if (!header) {
        LOG_ERROR(Engine, "Failed to read scene header from '{}'", path.c_str());
        return {false, sceneId, sceneName};
    }

    const Engine::TextReader sceneReader(map.data + header->dataOffset, map.size - header->dataOffset);
    StringID loadedId = LoadScene(state->componentRegistry, state->registry, sceneReader);
    assert(loadedId == sceneId && "Scene ID in file does not match registry key, file was likely saved with a mismatched ID");
    uint64_t maxSortOrder = 0; {
        ZoneScopedN("SortOrder");
        auto sortView = state->registry.View<Component::SceneRuntime, Engine::Mut<Component::StableIdComponent>>();
        for (auto entity : sortView) {
            if (sortView.get<Component::SceneRuntime>(entity).sceneId == loadedId) {
                maxSortOrder = std::max(maxSortOrder, sortView.get<Component::StableIdComponent>(entity).sortOrder);
            }
        }
        // For those without a valid sort order
        for (auto entity : sortView) {
            if (sortView.get<Component::SceneRuntime>(entity).sceneId == loadedId) {
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
        auto camView = state->registry.View<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            auto& transform = state->registry.GetMut<Component::TransformComponent>(camEntity);
            transform.translation = camReader.Vec3("translation", transform.translation);
            transform.rotation = camReader.Quat("rotation", transform.rotation);
        }
    }

    LOG_INFO(Engine, "Loaded scene '{}' from '{}'", sceneId.ToString(), path.c_str());
    return {true, sceneId, sceneName};
}

bool SaveSceneSlot(Engine::EngineState* state, int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= Engine::MAX_SCENE_SLOTS) { return false; }
    if (!state->scene.currentSceneId) {
        LOG_WARN(Engine, "Scene slot {}: no current scene to bind", slotIndex + 1);
        return false;
    }

    Engine::SceneSlot& slot = state->projectConfig.sceneSlots[slotIndex];
    slot.sceneId = state->scene.currentSceneId;
    Engine::WriteProjectConfig(state->projectConfig, state->allocator);

    LOG_INFO(Engine, "Scene slot {} bound to '{}'", slotIndex + 1, state->scene.currentSceneName.c_str());
    return true;
}

bool LoadSceneSlot(Engine::EngineContext* ctx, Engine::EngineState* state, int slotIndex)
{
    ZoneScoped;
    if (slotIndex < 0 || slotIndex >= Engine::MAX_SCENE_SLOTS) { return false; }

    const Engine::SceneSlot& slot = state->projectConfig.sceneSlots[slotIndex];
    if (!slot.sceneId) {
        LOG_WARN(Engine, "Scene slot {} is empty", slotIndex + 1);
        return false;
    }
    if (!ctx->assetManager->GetSceneCache().Find(slot.sceneId)) {
        LOG_WARN(Engine, "Scene slot {} points at {}, which is no longer registered", slotIndex + 1, slot.sceneId.ToString());
        return false;
    }
    return LoadSceneExclusive(ctx, state, slot.sceneId);
}

bool LoadSceneExclusive(Engine::EngineContext* ctx, Engine::EngineState* state, StringID sceneId)
{
    ZoneScoped;
    if (!ctx->assetManager->GetSceneCache().Find(sceneId)) { return false; }

    const bool bWasPlaying = IsPlaying(state);
    const bool bWasSimulating = IsSimulating(state);
    if (bWasPlaying) {
        PlayStop(ctx, state);
    }
    if (bWasSimulating) {
        SimulateStop(ctx, state);
    }

    Core::InlineVector<StringID, 8> scenesToUnload;
    for (const Engine::RuntimeSceneMetadata& scene : state->editor.loadedScenes) {
        scenesToUnload.PushBack(scene.sceneId);
    }
    UnloadScenes(state, scenesToUnload);

    const LoadSceneResult result = LoadSceneFromFile(state, ctx->assetManager, sceneId);
    if (result.bSuccess) {
        state->scene.currentSceneId = result.sceneId;
        state->scene.currentSceneName = result.sceneName;
    }

    if (bWasPlaying) {
        PlayStart(ctx, state, state->bRunUsesRuntimeScenes);
    }
    if (bWasSimulating) {
        SimulateStart(ctx, state, state->bRunUsesRuntimeScenes);
    }
    return result.bSuccess;
}

Core::ArenaVector<entt::entity> SpawnModel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::ModelID modelId, const glm::vec3& offset)
{
    ZoneScoped;
    const Engine::AssetManager::CachedModelMetadata* cached = ctx->assetManager->GetModelMetadata(modelId);
    if (!cached) {
        LOG_ERROR(Engine, "SpawnModel: modelId {} not in asset registry", modelId.id);
        return {};
    }

    auto& registry = state->registry;
    auto spawned = Core::ArenaVector<entt::entity>(&ctx->gameplayArena.Get(), 1);

    entt::entity entity = CreateSceneEntity(state);

    if (cached->name.Size() > 0) {
        registry.GetMut<Component::NameComponent>(entity).name = cached->name;
    }

    auto& transform = registry.GetMut<Component::TransformComponent>(entity);
    transform.translation = offset;

    Component::StaticMeshComponent meshComp{};
    meshComp.modelId = modelId;
    registry.Add<Component::StaticMeshComponent>(entity, std::move(meshComp));

    spawned.PushBack(entity);
    state->bHierarchyOrderDirty = true;
    return spawned;
}

entt::entity SplitOffMeshPrimitive(Engine::EngineState* state, entt::entity parent, uint32_t primitiveOrdinal, const glm::mat4& nodeModelSpace)
{
    auto& registry = state->registry;
    const auto& parentComp = registry.Get<Component::StaticMeshComponent>(parent);

    entt::entity child = CreateSceneEntity(state);
    registry.GetMut<Component::NameComponent>(child).name = Core::InlineString<128>::Format("Primitive %u", primitiveOrdinal);

    glm::vec3 scale, translation, skew;
    glm::quat rotation;
    glm::vec4 perspective;
    glm::decompose(nodeModelSpace, scale, rotation, translation, skew, perspective);
    auto& childTransform = registry.GetMut<Component::TransformComponent>(child);
    childTransform.translation = translation;
    childTransform.rotation = rotation;
    childTransform.scale = scale;

    auto& hierarchy = registry.Add<Component::HierarchyComponent>(child);
    hierarchy.parent = parent;
    hierarchy.parentStableId = registry.Get<Component::StableIdComponent>(parent).id;

    Component::StaticMeshPrimitiveComponent childMesh{};
    childMesh.modelId = parentComp.modelId;
    childMesh.primitiveOrdinal = primitiveOrdinal;
    registry.Add<Component::StaticMeshPrimitiveComponent>(child, childMesh);
    if (auto* parentFlags = registry.TryGet<Component::RenderFlagsComponent>(parent)) {
        registry.Set<Component::RenderFlagsComponent>(child, *parentFlags);
    }

    auto& parentOverrides = registry.GetOrAdd<Component::StaticMeshOverridesComponent>(parent);
    if (!parentOverrides.primitiveBlacklist.Contains(primitiveOrdinal) && parentOverrides.primitiveBlacklist.Size() < Component::StaticMeshOverridesComponent::MaxBlacklist) {
        parentOverrides.primitiveBlacklist.PushBack(primitiveOrdinal);
    }
    registry.Set<Component::StaticMeshLoadingFlag>(parent);

    state->bHierarchyOrderDirty = true;
    return child;
}

uint32_t SplitAllMeshPrimitives(Engine::EngineContext* ctx, Engine::EngineState* state, entt::entity parent)
{
    auto& registry = state->registry;
    const auto* runtime = registry.TryGet<Component::MeshRuntime>(parent);
    if (!runtime || runtime->range.count == 0) {
        return 0;
    }
    const Engine::StaticModel* model = ctx->assetManager->GetModel(runtime->modelHandle);
    if (!model) {
        return 0;
    }
    const auto& nodes = model->modelData.nodes;
    const auto& meshes = model->modelData.meshes;
    const Engine::ModelID modelId = registry.Get<Component::StaticMeshComponent>(parent).modelId;
    const Component::RenderFlagsComponent parentFlags = registry.GetOrAdd<Component::RenderFlagsComponent>(parent);
    const Engine::InstanceStore::Range range = runtime->range;

    auto nodeEntities = Core::ArenaFixedVector<entt::entity>(&ctx->editorArena.Get(), std::max(nodes.Size(), size_t{1}));
    for (size_t n = 0; n < nodes.Size(); ++n) {
        nodeEntities.EmplaceBack(entt::entity{entt::null});
    }

    auto attach = [&](entt::entity child, entt::entity newParent) {
        auto& hierarchy = registry.Add<Component::HierarchyComponent>(child);
        hierarchy.parent = newParent;
        hierarchy.parentStableId = registry.Get<Component::StableIdComponent>(newParent).id;
    };

    auto ensureNodeEntity = [&](uint32_t nodeIndex) -> entt::entity {
        Core::InlineVector<uint32_t, 64> chain;
        uint32_t walk = nodeIndex;
        while (walk != ~0u && walk < nodes.Size() && nodeEntities[walk] == entt::null && chain.Size() < 64) {
            chain.PushBack(walk);
            walk = nodes[walk].parent;
        }
        entt::entity above = (walk != ~0u && walk < nodes.Size() && nodeEntities[walk] != entt::null) ? nodeEntities[walk] : parent;
        while (chain.Size() > 0) {
            const uint32_t n = chain.Back();
            chain.PopBack();
            entt::entity nodeEntity = CreateSceneEntity(state);
            registry.GetMut<Component::NameComponent>(nodeEntity).name = nodes[n].name.Size() > 0 ? Core::InlineString<128>::Format("%s", nodes[n].name.c_str()) : Core::InlineString<128>::Format("Node %u", n);
            auto& nodeTransform = registry.GetMut<Component::TransformComponent>(nodeEntity);
            nodeTransform.translation = nodes[n].localTranslation;
            nodeTransform.rotation = nodes[n].localRotation;
            nodeTransform.scale = nodes[n].localScale;
            attach(nodeEntity, above);
            nodeEntities[n] = nodeEntity;
            above = nodeEntity;
        }
        return above;
    };

    for (uint32_t i = 0; i < range.count; ++i) {
        const Engine::InstanceSource source = state->instanceStore[range.offset + i];
        if (source.sourceNodeIndex >= nodes.Size()) {
            continue;
        }
        const Engine::Material* material = ctx->materialManager->GetMaterial(source.materialID);
        const bool bEmissive = material && material->props.emissiveFactor.w * glm::max(material->props.emissiveFactor.x, glm::max(material->props.emissiveFactor.y, material->props.emissiveFactor.z)) > 0.0f;

        const entt::entity nodeEntity = ensureNodeEntity(source.sourceNodeIndex);
        const uint32_t meshIndex = nodes[source.sourceNodeIndex].meshIndex;
        const bool bSinglePrimitive = meshIndex < meshes.Size() && meshes[meshIndex].primitiveProperties.Size() == 1;

        entt::entity child = nodeEntity;
        if (!bSinglePrimitive) {
            child = CreateSceneEntity(state);
            registry.GetMut<Component::NameComponent>(child).name = Core::InlineString<128>::Format(bEmissive ? "Primitive %u Emissive" : "Primitive %u", source.modelPrimitiveOrdinal);
            attach(child, nodeEntity);
        }
        else if (bEmissive) {
            auto& name = registry.GetMut<Component::NameComponent>(child).name;
            name = Core::InlineString<128>::Format("%s Emissive", name.c_str());
        }

        Component::StaticMeshPrimitiveComponent childMesh{};
        childMesh.modelId = modelId;
        childMesh.primitiveOrdinal = source.modelPrimitiveOrdinal;
        if (material && !material->bSynthesized) {
            childMesh.materialOverride = source.materialID;
        }
        registry.Add<Component::StaticMeshPrimitiveComponent>(child, childMesh);

        Component::RenderFlagsComponent childFlags = parentFlags;
        childFlags.bEmissiveLight = bEmissive;
        registry.Set<Component::RenderFlagsComponent>(child, childFlags);
    }

    state->bHierarchyOrderDirty = true;
    return range.count;
}

uint64_t HighestSortOrderInScene(Engine::Registry& registry, StringID sceneId)
{
    uint64_t maxSortOrder = 0;
    auto view = registry.View<Component::SceneRuntime, Component::StableIdComponent>();
    for (auto entity : view) {
        if (view.get<Component::SceneRuntime>(entity).sceneId == sceneId) {
            maxSortOrder = std::max(maxSortOrder, view.get<Component::StableIdComponent>(entity).sortOrder);
        }
    }
    return maxSortOrder;
}

Core::InlineString<128> GenerateIncrementedName(Engine::Registry& registry, StringID sceneId, const Core::InlineString<128>& sourceName)
{
    const std::string_view src = sourceName.View();

    size_t digitsStart = src.size();
    while (digitsStart > 0 && std::isdigit(static_cast<unsigned char>(src[digitsStart - 1]))) {
        --digitsStart;
    }
    const std::string_view base = src.substr(0, digitsStart);

    uint32_t highest = 0;
    auto view = registry.View<Component::SceneRuntime, Component::NameComponent>();
    for (auto entity : view) {
        if (view.get<Component::SceneRuntime>(entity).sceneId != sceneId) { continue; }

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

    return Core::InlineString<128>::Format("%.*s%u", static_cast<int>(base.size()), base.data(), highest + 1);
}

entt::entity CreateSceneEntity(Engine::EngineState* state)
{
    entt::entity newEntity = state->registry.Create();
    state->registry.Add<Component::TransformComponent>(newEntity);
    state->registry.Add<Component::SceneRuntime>(newEntity, state->scene.currentSceneId);
    state->registry.Add<Component::StableIdComponent>(newEntity);
    state->registry.GetMut<Component::StableIdComponent>(newEntity).sortOrder = HighestSortOrderInScene(state->registry, state->scene.currentSceneId) + 1;
    state->registry.Add<Component::EntityFolderComponent>(newEntity);
    static int32_t runningNameTally = 0;
    auto newName = fmt::format("New Entity {}", runningNameTally++);
    state->registry.Add<Component::NameComponent>(newEntity, Core::InlineString<128>(newName.c_str()));
    LOG_TRACE(Engine, "Created new entity {}", entt::to_integral(newEntity));
    return newEntity;
}

entt::entity FindEntity(const Engine::EngineState* state, StringID stableId)
{
    if (!stableId.IsValid()) { return entt::null; }
    const entt::entity* entity = state->stableIdToEntityMap.Find(stableId);
    return entity && state->registry.Valid(*entity) ? *entity : entt::null;
}

static uint16_t HierarchyDepth(const Engine::Registry& registry, entt::entity entity)
{
    uint16_t depth = 0;
    const auto* node = registry.TryGet<Component::HierarchyComponent>(entity);
    while (node && node->parent != entt::null && registry.Valid(node->parent)) {
        node = registry.TryGet<Component::HierarchyComponent>(node->parent);
        if (++depth >= 1024) { break; }
    }
    return depth;
}

static void RefreshHierarchyOrder(Engine::Registry& registry)
{
    ZoneScoped;
    for (auto [entity, node] : registry.View<Engine::Mut<Component::HierarchyComponent>>().each()) {
        node.depth = HierarchyDepth(registry, entity);
    }
    registry.Sort<Component::HierarchyComponent>(
        [](const Component::HierarchyComponent& a, const Component::HierarchyComponent& b) { return a.depth < b.depth; },
        entt::insertion_sort{});
}

void EnsureHierarchyOrder(Engine::EngineState* state)
{
    if (!state->bHierarchyOrderDirty) { return; }
    RefreshHierarchyOrder(state->registry);
    state->bHierarchyOrderDirty = false;
}

void PropagateDirtyTransforms(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto& registry = state->registry;
    if (registry.View<Component::DirtyTransformRequest>().empty()) { return; }

    const bool bRunning = IsRunning(state);
    if (bRunning) {
        RejectStaticTransformWrites(state);
    }
    EnsureHierarchyOrder(state);
    for (auto [entity, node] : registry.View<Component::HierarchyComponent>().each()) {
        if (!registry.Valid(node.parent) || !registry.Has<Component::DirtyTransformRequest>(node.parent)) { continue; }
        if (bRunning && registry.Has<Component::DynamicPhysicsBodyRuntime>(entity)) { continue; }
        registry.Set<Component::DirtyTransformRequest>(entity);
    }
}

uint32_t FindStaticBodiesUnderMovers(Engine::EngineState* state, Core::Span<entt::entity> out)
{
    auto& registry = state->registry;
    uint32_t count = 0;
    for (auto [entity, desc, node] : registry.View<Component::PhysicsBodyDesc, Component::HierarchyComponent>().each()) {
        if (desc.motionType != Component::PhysicsMotionType::Static) { continue; }
        entt::entity ancestor = node.parent;
        for (int guard = 0; registry.Valid(ancestor) && guard < 1024; ++guard) {
            if (const auto* ancestorDesc = registry.TryGet<Component::PhysicsBodyDesc>(ancestor)) {
                if (ancestorDesc->motionType != Component::PhysicsMotionType::Static) {
                    if (count < out.Size()) { out[count] = entity; }
                    ++count;
                }
                break;
            }
            const auto* h = registry.TryGet<Component::HierarchyComponent>(ancestor);
            ancestor = h ? h->parent : entt::null;
        }
    }
    return count;
}

void ResolveHierarchyLinks(Engine::EngineState* state)
{
    ZoneScoped;
    auto& registry = state->registry;
    for (auto [entity, node] : registry.View<Engine::Mut<Component::HierarchyComponent>>().each()) {
        node.parent = FindEntity(state, node.parentStableId);
    }
    state->bHierarchyOrderDirty = true;
}

void SetParent(Engine::EngineState* state, entt::entity child, entt::entity parent)
{
    ZoneScoped;
    auto& registry = state->registry;
    if (child == parent || !registry.Valid(child) || !registry.Valid(parent)) { return; }

    // Parent must not already be a descendant of child.
    for (entt::entity e = parent; e != entt::null;) {
        if (e == child) { return; }
        const auto* ancestor = registry.TryGet<Component::HierarchyComponent>(e);
        e = (ancestor && registry.Valid(ancestor->parent)) ? ancestor->parent : entt::null;
    }

    const Transform parentWorld = Component::ComputeWorldTransform(registry, parent);
    const Transform childWorld = Component::ComputeWorldTransform(registry, child);

    auto& node = registry.GetOrAdd<Component::HierarchyComponent>(child);
    node.parent = parent;
    node.parentStableId = registry.Get<Component::StableIdComponent>(parent).id;

    // Keep the child's world pose fixed.
    if (!registry.Has<Component::DynamicPhysicsBodyRuntime>(child)) {
        registry.GetMut<Component::TransformComponent>(child) = Component::ComposeLocalFromWorld(parentWorld, childWorld);
        registry.Set<Component::DirtyTransformRequest>(child);
    }

    state->bHierarchyOrderDirty = true;
}

void ClearParent(Engine::EngineState* state, entt::entity child)
{
    ZoneScoped;
    auto& registry = state->registry;
    if (!registry.Valid(child) || !registry.Has<Component::HierarchyComponent>(child)) { return; }

    const Transform childWorld = Component::ComputeWorldTransform(registry, child);
    registry.Remove<Component::HierarchyComponent>(child);

    if (!registry.Has<Component::DynamicPhysicsBodyRuntime>(child)) {
        auto& local = registry.GetMut<Component::TransformComponent>(child);
        local.translation = childWorld.translation;
        local.rotation = childWorld.rotation;
        local.scale = childWorld.scale;
        registry.Set<Component::DirtyTransformRequest>(child);
    }

    state->bHierarchyOrderDirty = true;
}

void SetWorldTransform(Engine::EngineState* state, entt::entity entity, const Transform& world)
{
    ZoneScoped;
    auto& registry = state->registry;
    if (!registry.Valid(entity) || !registry.Has<Component::TransformComponent>(entity)) { return; }

    const auto* node = registry.TryGet<Component::HierarchyComponent>(entity);
    if (node && registry.Valid(node->parent)) {
        // Cached parent world keeps both on the same one-frame-behind clock.
        const Transform parentWorld = registry.Get<Component::WorldTransformRuntime>(node->parent);
        registry.GetMut<Component::TransformComponent>(entity) = Component::ComposeLocalFromWorld(parentWorld, world);
    }
    else {
        registry.GetMut<Component::TransformComponent>(entity) = world;
    }
    registry.Set<Component::DirtyTransformRequest>(entity);
}

static Engine::ComponentEntry* FindPrefabComponentEntry(Engine::ComponentRegistry& componentRegistry, std::string_view opener)
{
    uint64_t typeId = 0;
    std::from_chars(opener.data(), opener.data() + opener.size(), typeId);
    const size_t* index = componentRegistry.registryMapping.Find(StringID{typeId});
    if (index == nullptr) {
        LOG_WARN(Engine, "Prefab component key {} matches no registered component; skipped", typeId);
        return nullptr;
    }
    Engine::ComponentEntry& entry = componentRegistry.registry[*index];
    return entry.bInstanceOnly ? nullptr : &entry;
}

static void ApplyPrefabBody(Engine::EngineState* state, const Engine::WPrefabData& prefab, entt::entity entity, bool bOnlyMissing)
{
    prefab.Body().ForEachBlock([&](std::string_view opener, const Engine::TextReader& compReader) {
        Engine::ComponentEntry* entry = FindPrefabComponentEntry(state->componentRegistry, opener);
        if (!entry || (bOnlyMissing && entry->has(state->registry, entity))) { return; }
        entry->deserialize(state->registry, entity, compReader);
    });
}

static uint32_t WritePrefabBody(Engine::EngineState* state, entt::entity entity, Engine::TextWriter& w)
{
    auto* transform = state->registry.TryGetMut<Component::TransformComponent>(entity);
    const glm::vec3 translation = transform ? transform->translation : glm::vec3(0.0f);
    if (transform) { transform->translation = glm::vec3(0.0f); }

    uint32_t componentCount = 0;
    for (Engine::ComponentEntry& entry : state->componentRegistry.registry) {
        if (entry.bInstanceOnly || !entry.has(state->registry, entity)) { continue; }
        w.BeginBlock(entry.typeId.id);
        entry.serialize(state->registry, entity, w);
        w.EndBlock();
        componentCount++;
    }

    if (transform) { transform->translation = translation; }
    return componentCount;
}

static bool WritePrefabFile(Engine::EngineContext* ctx, const Core::Path& path, uint64_t prefabId, std::string_view prefabName, const Core::Vector<std::byte>& body, uint32_t componentCount)
{
    uint64_t contentVersion = 1;
    if (Platform::FileExists(path)) {
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
        LOG_ERROR(Engine, "Failed to write prefab file '{}'", path.c_str());
        return false;
    }
    return true;
}

void SaveEntityAsPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, Engine::EngineContext* ctx, entt::entity entity, std::string_view prefabName)
{
    if (!ctx->bGameLoaded) {
        LOG_WARN(Engine, "Prefab save refused: game.dll is not loaded, game components would be lost");
        return;
    }
    if (state->registry.Has<Component::PrefabInstanceComponent>(entity) || entity == state->editor.editingPrefabEntity) {
        LOG_WARN(Engine, "Prefab save refused: the entity already belongs to a prefab, edit that prefab instead");
        return;
    }

    Core::InlineString s{prefabName};
    s.ToLower();
    s.Replace(' ', '_');
    s.Append(".wprefab");
    const Core::Path path = Platform::GetAssetPath() / "prefabs" / s.c_str();
    if (Platform::FileExists(path)) {
        LOG_WARN(Engine, "Prefab save refused: '{}' already exists", path.c_str());
        return;
    }

    Core::Vector<std::byte> body(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    Engine::TextWriter w(body);
    const uint32_t componentCount = WritePrefabBody(state, entity, w);
    const uint64_t prefabId = state->rng();
    if (!WritePrefabFile(ctx, path, prefabId, prefabName, body, componentCount)) { return; }

    state->registry.Set<Component::PrefabInstanceComponent>(entity, StringID{prefabId});
    ctx->rescan.bResources = true;
    LOG_INFO(Engine, "Saved prefab '{}' to '{}'", prefabName, path.c_str());
}

entt::entity SpawnPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID prefabId, const glm::vec3& spawnPosition)
{
    const auto* meta = assetManager->GetPrefabMetadata(prefabId);
    if (!meta) {
        LOG_ERROR(Engine, "Prefab '{}' not found in registry", prefabId.ToString());
        return entt::null;
    }

    auto* ctx = state->registry.Context();
    auto prefabData = Engine::ReadWPrefab(meta->source.c_str(), &ctx->memoryManager->AssetsScratch());
    if (!prefabData) {
        LOG_ERROR(Engine, "Failed to read prefab file '{}'", meta->source.c_str());
        return entt::null;
    }

    const entt::entity entity = CreateSceneEntity(state);
    ApplyPrefabBody(state, *prefabData, entity, false);
    state->registry.GetMut<Component::TransformComponent>(entity).translation = spawnPosition;
    state->registry.Set<Component::PrefabInstanceComponent>(entity, prefabId);

    LOG_TRACE(Engine, "Spawned prefab '{}' as entity {}", meta->prefabName.c_str(), entt::to_integral(entity));
    return entity;
}

void ResolvePrefabLoads(Engine::EngineState* state, Engine::AssetManager* assetManager)
{
    ZoneScoped;
    auto* ctx = state->registry.Context();

    // Cache prefab bodies so each file is read once even with many instances
    Core::InlineMap<StringID, Engine::WPrefabData, 512> prefabCache;

    auto view = state->registry.View<Component::PrefabInstanceComponent>();
    for (auto entity : view) {
        auto& prefabInst = view.get<Component::PrefabInstanceComponent>(entity);

        const auto* meta = assetManager->GetPrefabMetadata(prefabInst.prefabId);
        if (!meta) {
            LOG_WARN(Engine, "Prefab '{}' not found for entity {}, skipping", prefabInst.prefabId.ToString(), entt::to_integral(entity));
            continue;
        }

        auto cacheIt = prefabCache.Find(prefabInst.prefabId);
        if (cacheIt == nullptr) {
            auto prefabData = Engine::ReadWPrefab(meta->source.c_str(), &ctx->memoryManager->AssetsScratch());
            if (!prefabData) {
                LOG_WARN(Engine, "Failed to read prefab file '{}'", meta->source.c_str());
                continue;
            }
            cacheIt = &prefabCache.Emplace(prefabInst.prefabId, std::move(*prefabData));
        }

        ApplyPrefabBody(state, *cacheIt, entity, true);
    }
}

bool IsEditingPrefab(Engine::EngineState* state)
{
    if (state->editor.editingPrefabEntity != entt::null && !state->registry.Valid(state->editor.editingPrefabEntity)) {
        state->editor.editingPrefabEntity = entt::null;
        state->editor.editingPrefabId = {};
    }
    return state->editor.editingPrefabEntity != entt::null;
}

entt::entity BeginPrefabEdit(Engine::EngineContext* ctx, Engine::EngineState* state, StringID prefabId, const glm::vec3& position)
{
    if (!ctx->bGameLoaded || IsRunning(state) || IsEditingPrefab(state)) {
        LOG_WARN(Engine, "Prefab edit refused: needs the game loaded, edit mode, and no other prefab edit open");
        return entt::null;
    }
    const auto* meta = ctx->assetManager->GetPrefabMetadata(prefabId);
    if (!meta) {
        LOG_ERROR(Engine, "Prefab '{}' not found in registry", prefabId.ToString());
        return entt::null;
    }
    auto prefabData = Engine::ReadWPrefab(meta->source.c_str(), &ctx->memoryManager->AssetsScratch());
    if (!prefabData) {
        LOG_ERROR(Engine, "Failed to read prefab file '{}'", meta->source.c_str());
        return entt::null;
    }

    const entt::entity entity = CreateSceneEntity(state);
    state->registry.Add<Component::DoNotSerializeTag>(entity);
    ApplyPrefabBody(state, *prefabData, entity, false);
    state->registry.GetMut<Component::TransformComponent>(entity).translation = position;

    state->editor.editingPrefabId = prefabId;
    state->editor.editingPrefabEntity = entity;
    LOG_INFO(Engine, "Editing prefab '{}'", meta->prefabName.c_str());
    return entity;
}

void StopPrefabEdit(Engine::EngineState* state)
{
    if (!IsEditingPrefab(state)) { return; }
    const entt::entity entity = state->editor.editingPrefabEntity;
    state->editor.selectedEntities.RemoveFirst(entity);
    state->registry.Destroy(entity);
    state->editor.editingPrefabEntity = entt::null;
    state->editor.editingPrefabId = {};
}

/** Pushes the old -> new prefab change into one instance; components it overrode, added or removed are left alone. */
static bool PropagatePrefabChange(Engine::EngineState* state, entt::entity entity, const Engine::TextReader& oldBody, const Engine::TextReader& newBody,
                                  Core::Vector<std::byte>& fragment)
{
    bool bChanged = false;
    newBody.ForEachBlock([&](std::string_view opener, const Engine::TextReader& after) {
        Engine::ComponentEntry* entry = FindPrefabComponentEntry(state->componentRegistry, opener);
        if (!entry) { return; }
        const Engine::TextReader before = oldBody.Block(Core::ShortString(opener).c_str());
        if (entry->has(state->registry, entity)) {
            if (before.IsValid() && entry->propagate && entry->propagate(state->registry, entity, before, after)) {
                if (entry->typeId == TypeSID<Component::TransformComponent>()) {
                    state->registry.Set<Component::DirtyTransformRequest>(entity);
                }
                bChanged = true;
            }
        }
        else if (!before.IsValid()) {
            entry->deserialize(state->registry, entity, after);
            bChanged = true;
        }
    });
    oldBody.ForEachBlock([&](std::string_view opener, const Engine::TextReader& before) {
        Engine::ComponentEntry* entry = FindPrefabComponentEntry(state->componentRegistry, opener);
        if (!entry || newBody.Block(Core::ShortString(opener).c_str()).IsValid() || !entry->has(state->registry, entity)) { return; }
        fragment.Clear();
        Engine::TextWriter fw(fragment);
        entry->serialize(state->registry, entity, fw);
        if (std::string_view(reinterpret_cast<const char*>(fragment.Data()), fragment.Size()) == before.Span()) {
            entry->remove(state->registry, entity);
            bChanged = true;
        }
    });
    return bChanged;
}

void ApplyPrefabEdit(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (!IsEditingPrefab(state)) { return; }
    const StringID prefabId = state->editor.editingPrefabId;
    const auto* meta = ctx->assetManager->GetPrefabMetadata(prefabId);
    if (!meta) {
        LOG_ERROR(Engine, "Prefab '{}' not found in registry", prefabId.ToString());
        return;
    }
    const Core::Path path = meta->source;
    const Core::InlineString<128> prefabName = meta->prefabName;

    auto oldData = Engine::ReadWPrefab(path.c_str(), &ctx->memoryManager->AssetsScratch());
    if (!oldData) {
        LOG_ERROR(Engine, "Failed to read prefab file '{}'", path.c_str());
        return;
    }

    Core::Vector<std::byte> body(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    Engine::TextWriter w(body);
    const uint32_t componentCount = WritePrefabBody(state, state->editor.editingPrefabEntity, w);
    if (!WritePrefabFile(ctx, path, prefabId.id, prefabName.c_str(), body, componentCount)) { return; }

    const Engine::TextReader newBody(body.Data(), body.Size());
    Core::Vector<std::byte> fragment(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    uint32_t updated = 0;
    auto view = state->registry.View<Component::PrefabInstanceComponent>();
    for (auto entity : view) {
        if (view.get<Component::PrefabInstanceComponent>(entity).prefabId != prefabId) { continue; }
        if (!PropagatePrefabChange(state, entity, oldData->Body(), newBody, fragment)) { continue; }
        ++updated;
        if (const auto* sc = state->registry.TryGet<Component::SceneRuntime>(entity); sc && !state->editor.modifiedScenes.Contains(sc->sceneId)) {
            state->editor.modifiedScenes.PushBack(sc->sceneId);
        }
    }

    LOG_INFO(Engine, "Applied prefab '{}', {} loaded instances updated", prefabName.c_str(), updated);
}

static void BeginPIESession(Engine::EngineContext* ctx, Engine::EngineState* state, bool bUseRuntimeScenes)
{
    entt::entity stuck[16];
    const uint32_t stuckCount = FindStaticBodiesUnderMovers(state, stuck);
    for (uint32_t i = 0; i < std::min(stuckCount, static_cast<uint32_t>(std::size(stuck))); ++i) {
        const auto* name = state->registry.TryGet<Component::NameComponent>(stuck[i]);
        LOG_WARN(Physics, "Static body '{}' is under a kinematic or dynamic body and will not follow it; make it kinematic", name ? name->name.c_str() : "Unnamed");
    }

#if WILL_EDITOR
    state->editor.pieSnapshot = SerializeAll(state->componentRegistry, state->registry, ctx->assetManager, state->editor.loadedScenes);
#endif

    LockStaticTransforms(state);
    state->runStartTime = state->timeFrame->totalTime;
    state->bRunUsesRuntimeScenes = bUseRuntimeScenes;
    if (bUseRuntimeScenes) {
        ApplyRuntimeScenes(ctx, state);
    }
}

static void RestorePIESnapshot(Engine::EngineState* state)
{
#if WILL_EDITOR
    Core::InlineVector<StringID, 8> scenesToUnload;
    for (Engine::RuntimeSceneMetadata scene : state->editor.loadedScenes) {
        scenesToUnload.PushBack(scene.sceneId);
    }
    UnloadScenes(state, scenesToUnload);
    DeserializeAll(state, state->editor.pieSnapshot);
    state->editor.pieSnapshot = {};
#endif
}

void PlayStart(Engine::EngineContext* ctx, Engine::EngineState* state, bool bUseRuntimeScenes)
{
    ZoneScoped;
    if (!ctx->bGameLoaded) {
        LOG_WARN(Engine, "Play refused: game.dll is not loaded");
        return;
    }
    if (IsEditingPrefab(state)) {
        LOG_WARN(Engine, "Play refused: stop editing the prefab first");
        return;
    }
    if (IsSimulating(state)) {
        SimulateStop(ctx, state);
    }

    {
        auto camView = state->registry.View<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.Get<Component::TransformComponent>(camEntity);
            state->editor.pieCameraTranslation = transform.translation;
            state->editor.pieCameraRotation = transform.rotation;
        }
    }

    BeginPIESession(ctx, state, bUseRuntimeScenes);

    state->runMode = Engine::RunMode::Play;
    state->inputContext = Engine::InputContext::Gameplay;
    ctx->setCursorHiddenFn(true);
    state->editor.selectedEntities.Clear();

    if (ctx->playStartFn) {
        ctx->   playStartFn(ctx, state);
    }
}

void PlayStop(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    if (ctx->playStopFn) {
        ctx->playStopFn(ctx, state);
    }

    RestorePIESnapshot(state);

    state->runMode = Engine::RunMode::Edit;
    state->inputContext = Engine::InputContext::Editor;
    ctx->setCursorHiddenFn(false); {
        auto camView = state->registry.View<Component::EditorCameraTag, Component::CameraRuntime, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            auto& transform = state->registry.GetMut<Component::TransformComponent>(camEntity);
            transform.translation = state->editor.pieCameraTranslation;
            transform.rotation = state->editor.pieCameraRotation;
            state->requests.RequestRenderReset(Core::RenderReset::Cut);
        }
    }
}

void SimulateStart(Engine::EngineContext* ctx, Engine::EngineState* state, bool bUseRuntimeScenes)
{
    ZoneScoped;
    if (!ctx->bGameLoaded) {
        LOG_WARN(Engine, "Simulate refused: game.dll is not loaded");
        return;
    }
    if (IsRunning(state)) { return; }
    if (IsEditingPrefab(state)) {
        LOG_WARN(Engine, "Simulate refused: stop editing the prefab first");
        return;
    }

    BeginPIESession(ctx, state, bUseRuntimeScenes);
    for (size_t i = state->editor.selectedEntities.Size(); i-- > 0;) {
        if (!state->registry.Valid(state->editor.selectedEntities[i])) {
            state->editor.selectedEntities.RemoveAt(i);
        }
    }
    state->runMode = Engine::RunMode::Simulate;
}

void SimulateStop(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    if (!IsSimulating(state)) { return; }

    Core::InlineVector<StringID, 64> selectedIds;
    for (entt::entity e : state->editor.selectedEntities) {
        if (const auto* stable = state->registry.TryGet<Component::StableIdComponent>(e); stable && !selectedIds.IsFull()) {
            selectedIds.PushBack(stable->id);
        }
    }

    RestorePIESnapshot(state);
    state->runMode = Engine::RunMode::Edit;

    state->editor.selectedEntities.Clear();
    for (const StringID id : selectedIds) {
        if (const entt::entity e = FindEntity(state, id); e != entt::null) {
            state->editor.selectedEntities.PushBack(e);
        }
    }
}
} // Engine
