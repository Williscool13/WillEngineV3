//
// Created by William on 2025-12-26.
//

#include "render_systems.h"

#include <tracy/Tracy.hpp>

#include "scene_system.h"
#include "render_systems_helpers.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/inline_vector.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/material_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/components/fwd_components.h"
#include "engine/components/common_components.h"
#include "engine/components/physics/physics_shape_runtime.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/module_mesh_component.h"
#include "engine/components/render/text3d_component.h"
#include "engine/components/render/light_components.h"
#include "engine/components/render/local_ddgi_volume_component.h"
#include "engine/components/render/reflection_probe_component.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/static_mesh_primitive_component.h"
#include "engine/components/render/text_component.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/components/physics/physics_components.h"
#include "render/types/render_types.h"


namespace Engine
{
static bool AuthoredMaterialExists(const Engine::SystemRegistry& registry, const MaterialManager* materialManager, entt::entity entity, MaterialID material)
{
    if (!material.IsValid()) { return false; }
    if (materialManager->DoesMutableMaterialExist(material)) { return true; }
    const auto* name = registry.TryGet<Component::NameComponent>(entity);
    LOG_WARN(Engine, "'{}' references material {:016x}, which does not exist; drawing the fallback material", name != nullptr ? name->name.c_str() : "(unnamed)", material.id);
    return false;
}

void ConnectRenderObservers(Engine::Registry& registry)
{
    registry.ConnectConstruct<Component::TransformComponent, &Component::TransformComponent::OnConstruct>();
    registry.ConnectDestroy<Component::TransformComponent, &Component::TransformComponent::OnDestroy>();

    registry.ConnectConstruct<Component::MeshRuntime, &Component::MeshRuntime::OnConstruct>();
    registry.ConnectDestroy<Component::MeshRuntime, &Component::MeshRuntime::OnDestroy>();
    registry.ConnectConstruct<Component::LightSurfaceRuntime, &Component::LightSurfaceRuntime::OnConstruct>();
    registry.ConnectDestroy<Component::LightSurfaceRuntime, &Component::LightSurfaceRuntime::OnDestroy>();
    registry.ConnectDestroy<Component::TextRuntime, &Component::TextRuntime::OnDestroy>();

    registry.ConnectConstruct<Component::StaticMeshComponent, &Component::StaticMeshComponent::OnConstruct>();
    registry.ConnectDestroy<Component::StaticMeshComponent, &Component::StaticMeshComponent::OnDestroy>();

    registry.ConnectConstruct<Component::StaticMeshPrimitiveComponent, &Component::StaticMeshPrimitiveComponent::OnConstruct>();
    registry.ConnectDestroy<Component::StaticMeshPrimitiveComponent, &Component::StaticMeshPrimitiveComponent::OnDestroy>();

    registry.ConnectConstruct<Component::ProceduralMeshComponent, &Component::ProceduralMeshComponent::OnConstruct>();
    registry.ConnectDestroy<Component::ProceduralMeshComponent, &Component::ProceduralMeshComponent::OnDestroy>();

    registry.ConnectConstruct<Component::SplineMeshComponent, &Component::SplineMeshComponent::OnConstruct>();
    registry.ConnectDestroy<Component::SplineMeshComponent, &Component::SplineMeshComponent::OnDestroy>();

    registry.ConnectConstruct<Component::ModuleMeshComponent, &Component::ModuleMeshComponent::OnConstruct>();
    registry.ConnectDestroy<Component::ModuleMeshComponent, &Component::ModuleMeshComponent::OnDestroy>();

    registry.ConnectConstruct<Component::TextComponent, &Component::TextComponent::OnConstruct>();
    registry.ConnectDestroy<Component::TextComponent, &Component::TextComponent::OnDestroy>();

    registry.ConnectConstruct<Component::Text3DComponent, &Component::Text3DComponent::OnConstruct>();
    registry.ConnectDestroy<Component::Text3DComponent, &Component::Text3DComponent::OnDestroy>();

    registry.ConnectConstruct<Component::AreaLightComponent, &Component::AreaLightComponent::OnConstruct>();
    registry.ConnectDestroy<Component::AreaLightComponent, &Component::AreaLightComponent::OnDestroy>();
    registry.ConnectDestroy<Component::AreaLightRuntime, &Component::AreaLightRuntime::OnDestroy>();

    registry.ConnectConstruct<Component::SphereLightComponent, &Component::SphereLightComponent::OnConstruct>();
    registry.ConnectDestroy<Component::SphereLightComponent, &Component::SphereLightComponent::OnDestroy>();
    registry.ConnectDestroy<Component::SphereLightRuntime, &Component::SphereLightRuntime::OnDestroy>();

    registry.ConnectConstruct<Component::ReflectionProbeComponent, &Component::ReflectionProbeComponent::OnConstruct>();
    registry.ConnectDestroy<Component::ReflectionProbeComponent, &Component::ReflectionProbeComponent::OnDestroy>();
    registry.ConnectDestroy<Component::ReflectionProbeRuntime, &Component::ReflectionProbeRuntime::OnDestroy>();

    registry.ConnectConstruct<Component::LocalDDGIVolumeComponent, &Component::LocalDDGIVolumeComponent::OnConstruct>();

    registry.ConnectDestroy<Component::SkyboxComponent, &Component::SkyboxComponent::OnDestroy>();
    registry.ConnectDestroy<Component::SkyboxRuntime, &Component::SkyboxRuntime::OnDestroy>();
}

void DisconnectRenderObservers(Engine::Registry& registry)
{
    registry.DisconnectConstruct<Component::TransformComponent, &Component::TransformComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::TransformComponent, &Component::TransformComponent::OnDestroy>();

    registry.DisconnectConstruct<Component::MeshRuntime, &Component::MeshRuntime::OnConstruct>();
    registry.DisconnectDestroy<Component::MeshRuntime, &Component::MeshRuntime::OnDestroy>();
    registry.DisconnectConstruct<Component::LightSurfaceRuntime, &Component::LightSurfaceRuntime::OnConstruct>();
    registry.DisconnectDestroy<Component::LightSurfaceRuntime, &Component::LightSurfaceRuntime::OnDestroy>();
    registry.DisconnectDestroy<Component::TextRuntime, &Component::TextRuntime::OnDestroy>();

    registry.DisconnectConstruct<Component::StaticMeshComponent, &Component::StaticMeshComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::StaticMeshComponent, &Component::StaticMeshComponent::OnDestroy>();

    registry.DisconnectConstruct<Component::StaticMeshPrimitiveComponent, &Component::StaticMeshPrimitiveComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::StaticMeshPrimitiveComponent, &Component::StaticMeshPrimitiveComponent::OnDestroy>();

    registry.DisconnectConstruct<Component::ProceduralMeshComponent, &Component::ProceduralMeshComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::ProceduralMeshComponent, &Component::ProceduralMeshComponent::OnDestroy>();

    registry.DisconnectConstruct<Component::SplineMeshComponent, &Component::SplineMeshComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::SplineMeshComponent, &Component::SplineMeshComponent::OnDestroy>();

    registry.DisconnectConstruct<Component::ModuleMeshComponent, &Component::ModuleMeshComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::ModuleMeshComponent, &Component::ModuleMeshComponent::OnDestroy>();

    registry.DisconnectConstruct<Component::TextComponent, &Component::TextComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::TextComponent, &Component::TextComponent::OnDestroy>();

    registry.DisconnectConstruct<Component::Text3DComponent, &Component::Text3DComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::Text3DComponent, &Component::Text3DComponent::OnDestroy>();

    registry.DisconnectConstruct<Component::AreaLightComponent, &Component::AreaLightComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::AreaLightComponent, &Component::AreaLightComponent::OnDestroy>();
    registry.DisconnectDestroy<Component::AreaLightRuntime, &Component::AreaLightRuntime::OnDestroy>();

    registry.DisconnectConstruct<Component::SphereLightComponent, &Component::SphereLightComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::SphereLightComponent, &Component::SphereLightComponent::OnDestroy>();
    registry.DisconnectDestroy<Component::SphereLightRuntime, &Component::SphereLightRuntime::OnDestroy>();

    registry.DisconnectConstruct<Component::ReflectionProbeComponent, &Component::ReflectionProbeComponent::OnConstruct>();
    registry.DisconnectDestroy<Component::ReflectionProbeComponent, &Component::ReflectionProbeComponent::OnDestroy>();
    registry.DisconnectDestroy<Component::ReflectionProbeRuntime, &Component::ReflectionProbeRuntime::OnDestroy>();

    registry.DisconnectConstruct<Component::LocalDDGIVolumeComponent, &Component::LocalDDGIVolumeComponent::OnConstruct>();

    registry.DisconnectDestroy<Component::SkyboxComponent, &Component::SkyboxComponent::OnDestroy>();
    registry.DisconnectDestroy<Component::SkyboxRuntime, &Component::SkyboxRuntime::OnDestroy>();
}

void ModelHotReload(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    if (state->assetLoad.pendingHotReloadModelIds.IsEmpty()) { return; }

    // Freeze + release refs so the model drains, then re-arm; the load resolves re-acquire the fresh version after ResolveUnloads unfreezes on reclaim.
    auto isHot = [&](Engine::ModelID id) {
        if (!registry.Assets().IsModelFrozen(id)) { return false; }
        for (const Engine::ModelID& hotId : state->assetLoad.pendingHotReloadModelIds) {
            if (id == hotId) { return true; }
        }
        return false;
    };

    // Only resident models need freezing; a non-resident one's fresh load already reads the regenerated file.
    for (const Engine::ModelID& hotId : state->assetLoad.pendingHotReloadModelIds) {
        if (registry.Assets().IsModelResident(hotId)) {
            registry.ServiceMut<Engine::AssetManager>().FreezeModel(hotId);
        }
    }

    for (auto [entity, smc] : registry.View<Engine::Mut<Component::StaticMeshComponent>>().each()) {
        if (!registry.Has<Component::MeshRuntime>(entity)) { continue; }
        if (!isHot(smc.modelId)) { continue; }
        Component::UnloadStaticMesh(registry, entity);
        Component::LoadStaticMesh(smc, registry, entity);
    }

    // Physics holds its own model ref (its serialized key, loaded independently), so it must also release + re-arm or the model can never drain.
    for (auto [entity, bodyDesc, runtime] : registry.View<Component::PhysicsBodyDesc, Engine::Mut<Component::PhysicsShapeRuntime>>().each()) {
        bool affected = false;
        for (size_t i = 0; i < bodyDesc.shapes.Size(); ++i) {
            const auto* collider = std::get_if<Component::ColliderShape>(&bodyDesc.shapes[i].geometry);
            if (collider && collider->meshSourceModelId.IsValid() && isHot(collider->meshSourceModelId) && runtime.colliders[i].IsValid()) {
                registry.Assets().UnloadCollider(runtime.colliders[i]);
                runtime.colliders[i] = {};
                affected = true;
            }
        }
        if (!affected) { continue; }

        registry.Remove<Component::PhysicsMeshLoadingFlag>(entity);
        registry.Set<Component::PhysicsMeshRequest>(entity);
        registry.Set<Component::PhysicsShapeCreationRequest>(entity);
        registry.Set<Component::PhysicsBodyCreationRequest>(entity);
    }

    state->assetLoad.pendingHotReloadModelIds.Clear();
}

void FontHotReload(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    if (state->assetLoad.pendingHotReloadFontIds.IsEmpty()) { return; }

    // The UI font is engine-held (no component owns it) so it can never drain; reject its hot-reload and require a restart.
    Engine::FontID uiFontId{};
    if (const Engine::Font* uiFont = registry.Assets().GetFont(state->uiFont)) { uiFontId = uiFont->fontId; }

    // Freeze + release refs so the font drains, then re-arm; the resolve systems re-acquire the fresh version after ResolveUnloads unfreezes on reclaim.
    auto isHot = [&](Engine::FontID id) {
        if (!registry.Assets().IsFontFrozen(id)) { return false; }
        for (const Engine::FontID& hotId : state->assetLoad.pendingHotReloadFontIds) {
            if (id == hotId) { return true; }
        }
        return false;
    };

    for (const Engine::FontID& hotId : state->assetLoad.pendingHotReloadFontIds) {
        if (uiFontId.IsValid() && hotId == uiFontId) {
            LOG_WARN(Engine, "UI font hot-reload ignored; restart required to apply changes to the UI font.");
            continue;
        }
        if (registry.Assets().IsFontResident(hotId)) {
            registry.ServiceMut<Engine::AssetManager>().FreezeFont(hotId);
        }
    }

    for (auto [entity, textComp, runtime] : registry.View<Engine::Mut<Component::TextComponent>, Component::TextRuntime>().each()) {
        if (!isHot(textComp.fontId)) { continue; }
        Component::UnloadTextComponent(textComp, registry, entity);
        Component::LoadTextComponent(textComp, registry, entity);
    }
    for (auto [entity, text3DComp] : registry.View<Engine::Mut<Component::Text3DComponent>>().each()) {
        if (!isHot(text3DComp.fontId)) { continue; }
        Component::UnloadText3DFont(registry, entity);
        Component::LoadText3DFont(text3DComp, registry, entity);
    }

    // A Text3D physics collider holds its own ref to the generated mesh (keyed on the font), so it must release + re-arm too or that mesh can never drain.
    for (auto [entity, bodyDesc, runtime] : registry.View<Component::PhysicsBodyDesc, Engine::Mut<Component::PhysicsShapeRuntime>>().each()) {
        bool affected = false;
        for (size_t i = 0; i < bodyDesc.shapes.Size(); ++i) {
            const auto* collider = std::get_if<Component::ColliderShape>(&bodyDesc.shapes[i].geometry);
            if (collider && collider->text3DSource.IsValid() && isHot(collider->text3DSource.fontId) && runtime.colliders[i].IsValid()) {
                registry.Assets().UnloadCollider(runtime.colliders[i]);
                runtime.colliders[i] = {};
                affected = true;
            }
        }
        if (!affected) { continue; }

        registry.Remove<Component::PhysicsMeshLoadingFlag>(entity);
        registry.Set<Component::PhysicsMeshRequest>(entity);
        registry.Set<Component::PhysicsShapeCreationRequest>(entity);
        registry.Set<Component::PhysicsBodyCreationRequest>(entity);
    }

    state->assetLoad.pendingHotReloadFontIds.Clear();
}

void TextureHotReload(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    if (state->assetLoad.pendingHotReloadTextureIds.IsEmpty()) { return; }

    for (auto hotId : state->assetLoad.pendingHotReloadTextureIds) {
        if (registry.Assets().IsTextureLoaded(hotId) && registry.ServiceMut<Engine::AssetManager>().ReloadTexture(hotId)) {
            registry.ServiceMut<Engine::MaterialManager>().RebindTexture(hotId, registry.Assets().GetTextureBindlessIndex(hotId));
        }
    }

    state->assetLoad.pendingHotReloadTextureIds.Clear();
}

void CubemapHotReload(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    if (state->assetLoad.pendingHotReloadEnvironmentMapIds.IsEmpty()) { return; }

    for (auto hotId : state->assetLoad.pendingHotReloadEnvironmentMapIds) {
        if (registry.Assets().IsCubemapLoaded(hotId)) {
            registry.ServiceMut<Engine::AssetManager>().ReloadCubemap(hotId);
        }
    }

    state->assetLoad.pendingHotReloadEnvironmentMapIds.Clear();
}

static void StageModel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Component::MeshRuntime& runtime, Engine::StaticModelHandle handle)
{
    if (runtime.pendingModelHandle.IsValid()) { registry.Assets().UnloadModel(runtime.pendingModelHandle); }
    runtime.pendingModelHandle = Engine::StaticModelHandle::INVALID;
    if (runtime.modelHandle.IsValid()) { runtime.pendingModelHandle = handle; }
    else { runtime.modelHandle = handle; }
}

static void StageModel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity entity, Engine::StaticModelHandle handle)
{
    if (auto* existing = registry.TryGetMut<Component::MeshRuntime>(entity)) {
        StageModel(ctx, state, registry, *existing, handle);
        return;
    }
    Component::MeshRuntime runtime{};
    StageModel(ctx, state, registry, runtime, handle);
    registry.Set<Component::MeshRuntime>(entity, runtime);
}

static Engine::StaticModelHandle TargetModel(const Component::MeshRuntime& runtime)
{
    return runtime.pendingModelHandle.IsValid() ? runtime.pendingModelHandle : runtime.modelHandle;
}

static bool DropPendingModel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Component::MeshRuntime& runtime)
{
    if (!runtime.pendingModelHandle.IsValid()) { return false; }
    registry.Assets().UnloadModel(runtime.pendingModelHandle);
    runtime.pendingModelHandle = Engine::StaticModelHandle::INVALID;
    return true;
}

static void PromotePendingModel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Component::MeshRuntime& runtime)
{
    if (!runtime.pendingModelHandle.IsValid()) { return; }
    if (runtime.modelHandle.IsValid()) { registry.Assets().UnloadModel(runtime.modelHandle); }
    runtime.modelHandle = runtime.pendingModelHandle;
    runtime.pendingModelHandle = Engine::StaticModelHandle::INVALID;
}

/** Records Apply for each entity whose target model finished loading (or failed, or is gone); returns how many are still loading. */
template<typename TComponent, typename TLoadingFlag, auto Apply>
static int32_t PollModelResolves(Engine::SystemRegistry& registry)
{
    int32_t waiting = 0;
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const auto& [entity, component, runtime] : registry.View<TComponent, TLoadingFlag, Component::MeshRuntime>().each()) {
        if (budget == 0) { break; }
        const Engine::StaticModel* model = registry.Assets().GetModel(TargetModel(runtime));
        if (model && model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded && model->modelLoadState != Engine::StaticModel::ModelLoadState::FailedToLoad) {
            ++waiting;
            continue;
        }
        registry.RunExclusive<Apply>(entity);
        --budget;
    }
    return waiting;
}

struct ModelKickoff
{
    entt::entity entity{entt::null};
    Engine::StaticModelHandle handle{Engine::StaticModelHandle::INVALID};
};

/** The kickoff system took the model ref; this stages it, or gives it back if the entity went away in the same wave. */
template<typename TComponent, typename TRequest, typename TLoadingFlag>
static void ApplyModelKickoff(Engine::SystemRegistry& registry, const ModelKickoff& kickoff)
{
    if (!registry.Valid(kickoff.entity) || !registry.Has<TComponent>(kickoff.entity)) {
        registry.Assets().UnloadModel(kickoff.handle);
        return;
    }
    StageModel(registry.Context(), registry.State(), registry, kickoff.entity, kickoff.handle);
    registry.Remove<TRequest>(kickoff.entity);
    registry.Set<TLoadingFlag>(kickoff.entity);
}

template<typename TRequest>
static void ApplyMeshCleared(Engine::SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity)) { return; }
    registry.Remove<TRequest>(entity);
    registry.Remove<Component::MeshRuntime>(entity);
}

void StaticMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const auto& [entity, meshComponent] : registry.View<Component::StaticMeshComponent, Component::StaticMeshLoadRequest>().each()) {
        if (budget == 0) { break; }
        if (registry.Assets().IsModelFrozen(meshComponent.modelId)) { continue; } // stay pending while frozen
        const Engine::StaticModelHandle handle = registry.Assets().LoadModel(meshComponent.modelId);
        if (!handle.IsValid()) { continue; }
        registry.RunExclusive<&ApplyModelKickoff<Component::StaticMeshComponent, Component::StaticMeshLoadRequest, Component::StaticMeshLoadingFlag>>(ModelKickoff{entity, handle});
        --budget;
    }
}

struct ReflectionProbeKickoff
{
    entt::entity entity{entt::null};
    Engine::CubemapHandle handle{Engine::CubemapHandle::INVALID};
    Component::ReflectionProbeComponent::ContentSource source{Component::ReflectionProbeComponent::ContentSource::None};
};

static void ApplyReflectionProbeKickoff(Engine::SystemRegistry& registry, const ReflectionProbeKickoff& kickoff)
{
    auto* runtime = registry.Valid(kickoff.entity) ? registry.TryGetMut<Component::ReflectionProbeRuntime>(kickoff.entity) : nullptr;
    if (!runtime) {
        if (kickoff.handle.IsValid()) { registry.Assets().UnloadCubemap(kickoff.handle); }
        return;
    }
    runtime->contentSource = kickoff.source;
    if (kickoff.source == Component::ReflectionProbeComponent::ContentSource::None) {
        registry.Remove<Component::ReflectionProbeLoadRequest>(kickoff.entity);
        return;
    }
    runtime->contentHandle = kickoff.handle;
    if (!kickoff.handle.IsValid()) { return; }
    registry.Remove<Component::ReflectionProbeLoadRequest>(kickoff.entity);
    registry.Set<Component::ReflectionProbeLoadingFlag>(kickoff.entity);
}

void ReflectionProbePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const auto& [entity, probe, runtime] : registry.View<Component::ReflectionProbeComponent, Component::ReflectionProbeRuntime, Component::ReflectionProbeLoadRequest>().each()) {
        if (budget == 0) { break; }
        ReflectionProbeKickoff kickoff{.entity = entity};
        if (registry.Assets().GetProbeInfo(Engine::ProbeID{probe.probeId})) {
            kickoff.handle = registry.Assets().LoadProbe(Engine::ProbeID{probe.probeId});
            kickoff.source = Component::ReflectionProbeComponent::ContentSource::Baked;
        }
        else if (probe.standInEnvMap.IsValid()) {
            kickoff.handle = registry.Assets().LoadCubemap(probe.standInEnvMap);
            kickoff.source = Component::ReflectionProbeComponent::ContentSource::StandIn;
        }
        registry.RunExclusive<&ApplyReflectionProbeKickoff>(kickoff);
        --budget;
    }
}

void ReflectionProbeBakeUpgrade(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    struct ClaimedProbeId
    {
        uint64_t id;
        entt::entity owner;
    };
    Core::InlineVector<ClaimedProbeId, 64> claimedProbeIds{};
    for (auto [entity, probe, runtime] : registry.View<Component::ReflectionProbeComponent, Component::ReflectionProbeRuntime>().each()) {
        if (runtime.contentSource != Component::ReflectionProbeComponent::ContentSource::Baked || probe.probeId == 0) { continue; }
        bool bTaken = false;
        for (const ClaimedProbeId& claimed : claimedProbeIds) {
            if (claimed.id == probe.probeId) {
                bTaken = true;
                break;
            }
        }
        if (!bTaken && !claimedProbeIds.IsFull()) { claimedProbeIds.PushBack({probe.probeId, entity}); }
    }
    for (auto [entity, probe, runtime] : registry.View<Engine::Mut<Component::ReflectionProbeComponent>, Engine::Mut<Component::ReflectionProbeRuntime>>().each()) {
        bool bCollision = probe.probeId == 0;
        for (const ClaimedProbeId& claimed : claimedProbeIds) {
            if (claimed.id == probe.probeId) {
                bCollision = claimed.owner != entity;
                break;
            }
        }
        if (bCollision) {
            uint64_t fresh = state->rng();
            bool bUnique = false;
            while (!bUnique) {
                bUnique = fresh != 0;
                for (const ClaimedProbeId& claimed : claimedProbeIds) {
                    if (claimed.id == fresh) {
                        bUnique = false;
                        break;
                    }
                }
                if (!bUnique) { fresh = state->rng(); }
            }
            probe.probeId = fresh;
            if (runtime.contentHandle.IsValid()) {
                registry.Assets().UnloadCubemap(runtime.contentHandle);
                runtime.contentHandle = Engine::CubemapHandle::INVALID;
            }
            runtime.contentSource = Component::ReflectionProbeComponent::ContentSource::None;
            registry.Remove<Component::ReflectionProbeLoadingFlag>(entity);
            registry.Set<Component::ReflectionProbeLoadRequest>(entity);
        }
        bool bTaken = false;
        for (const ClaimedProbeId& claimed : claimedProbeIds) {
            if (claimed.id == probe.probeId) {
                bTaken = true;
                break;
            }
        }
        if (!bTaken && !claimedProbeIds.IsFull()) { claimedProbeIds.PushBack({probe.probeId, entity}); }
    }

    for (auto [entity, probe, runtime] : registry.View<Component::ReflectionProbeComponent, Engine::Mut<Component::ReflectionProbeRuntime>>().each()) {
        if (runtime.contentSource == Component::ReflectionProbeComponent::ContentSource::Baked) { continue; }
        if (!registry.Assets().GetProbeInfo(Engine::ProbeID{probe.probeId})) { continue; }
        if (runtime.contentHandle.IsValid()) {
            registry.Assets().UnloadCubemap(runtime.contentHandle);
            runtime.contentHandle = Engine::CubemapHandle::INVALID;
        }
        registry.Remove<Component::ReflectionProbeLoadingFlag>(entity);
        registry.Set<Component::ReflectionProbeLoadRequest>(entity);
    }
}

void ReflectionProbeLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    auto view = registry.View<Component::ReflectionProbeRuntime, Component::ReflectionProbeLoadingFlag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, runtime] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        const Render::Cubemap* cubemap = registry.Assets().GetCubemap(runtime.contentHandle);
        if (!cubemap) {
            resolved.PushBack(entity);
            continue;
        }
        if (cubemap->loadState == Render::Cubemap::LoadState::Loading) { continue; }
        resolved.PushBack(entity);
    }
    for (const entt::entity entity : resolved) {
        registry.Remove<Component::ReflectionProbeLoadingFlag>(entity);
    }
}

static bool HasEmissiveLightFlag(const Engine::SystemRegistry& registry, entt::entity entity)
{
    const auto* renderFlags = registry.TryGet<Component::RenderFlagsComponent>(entity);
    return renderFlags && renderFlags->bEmissiveLight;
}

static bool IsBakeStatic(const Engine::SystemRegistry& registry, entt::entity entity, const Component::RenderFlagsComponent& renderFlags)
{
    const auto* transform = registry.TryGet<Component::TransformComponent>(entity);
    if (!transform || transform->mobility != Component::Mobility::Static || registry.Has<Component::DynamicPhysicsBodyRuntime>(entity)) { return false; }
    const auto* body = registry.TryGet<Component::PhysicsBodyDesc>(entity);
    return body == nullptr || body->motionType == Component::PhysicsMotionType::Static;
}

static uint32_t InstanceFlagsFrom(const Engine::SystemRegistry& registry, entt::entity entity, const Component::RenderFlagsComponent& renderFlags)
{
    return (renderFlags.bMotionBlur ? INSTANCE_FLAG_MOTION_BLUR : 0u)
           | (renderFlags.bAlphaCutout ? INSTANCE_FLAG_ALPHA_CUTOUT : 0u)
           | (renderFlags.bDdgiContribute ? INSTANCE_FLAG_DDGI_VISIBLE : 0u)
           | (renderFlags.bCameraMotionBlur ? INSTANCE_FLAG_CAMERA_MOTION_BLUR : 0u)
           | (IsBakeStatic(registry, entity, renderFlags) ? INSTANCE_FLAG_BAKE_STATIC : 0u);
}

void EvaluateInstanceRenderState(Engine::EngineState* state, entt::entity entity, Engine::SystemRegistry& registry)
{
    auto* runtime = registry.TryGet<Component::MeshRuntime>(entity);
    const auto* renderFlags = registry.TryGet<Component::RenderFlagsComponent>(entity);
    if (!runtime || !renderFlags) { return; }

    if (!runtime->range.IsValid()) { return; }

    const bool bVisible = renderFlags->bVisible &&!registry.Has<Component::ProbeBakeHiddenFlag>(entity);
    const uint32_t flags = InstanceFlagsFrom(registry, entity, *renderFlags);

    const Engine::InstanceSource& src = registry.ServiceMut<Engine::InstanceStore>()[runtime->range.offset];
    if (src.bVisible == bVisible && src.flags == flags && src.stableId == runtime->stableId) { return; }
    registry.ServiceMut<Engine::InstanceStore>().SetRenderState(runtime->range, &registry.ServiceMut<Engine::TriLightStore>(), bVisible, flags, runtime->stableId);
}

void EvaluateAllInstanceRenderStates(Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    Engine::InstanceStore& store = registry.ServiceMut<Engine::InstanceStore>();
    const bool bAnyHideTags = registry.View<Component::ProbeBakeHiddenFlag>().size() > 0;

    for (auto [entity, renderFlags, runtime] : registry.View<Component::RenderFlagsComponent, Component::MeshRuntime>().each()) {
        if (!runtime.range.IsValid()) { continue; }

        bool bVisible = renderFlags.bVisible;
        if (bVisible && bAnyHideTags) {
            bVisible = !registry.Has<Component::ProbeBakeHiddenFlag>(entity);
        }
        const uint32_t flags = InstanceFlagsFrom(registry, entity, renderFlags);

        const Engine::InstanceSource& src = store[runtime.range.offset];
        if (src.bVisible == bVisible && src.flags == flags && src.stableId == runtime.stableId) { continue; }
        store.SetRenderState(runtime.range, &registry.ServiceMut<Engine::TriLightStore>(), bVisible, flags, runtime.stableId);
    }
}

static glm::mat4 ComposeNodeModelSpace(const Core::HeapArray<Engine::Node>& nodes, uint32_t nodeIndex)
{
    auto local = [](const Engine::Node& n) -> glm::mat4 {
        return glm::translate(glm::mat4(1.0f), n.localTranslation) * glm::mat4_cast(n.localRotation) * glm::scale(glm::mat4(1.0f), n.localScale);
    };
    glm::mat4 m = local(nodes[nodeIndex]);
    uint32_t parent = nodes[nodeIndex].parent;
    while (parent != ~0u) {
        m = local(nodes[parent]) * m;
        parent = nodes[parent].parent;
    }
    return m;
}

static void ApplyLightSurfaceResolve(Engine::SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity) || !registry.Has<Component::LightSurfaceRequest>(entity)) { return; }
    Engine::EngineState* state = registry.State();

    const Engine::Material* defaultMaterial = registry.Service<Engine::MaterialManager>().GetMaterial(registry.Service<Engine::MaterialManager>().GetDefaultMaterialID());
    if (!defaultMaterial) { return; }

    const auto* areaLight = registry.TryGet<Component::AreaLightComponent>(entity);
    const auto* sphereLight = registry.TryGet<Component::SphereLightComponent>(entity);
    if (!areaLight && !sphereLight) {
        registry.Remove<Component::LightSurfaceRequest>(entity);
        return;
    }

    const Engine::StaticModelHandle handle = areaLight
        ? (areaLight->bDisk ? state->builtinAssets.GetUnitDisk(&registry.Assets()) : state->builtinAssets.GetUnitQuad(&registry.Assets()))
        : state->builtinAssets.GetUnitSphere(&registry.Assets());
    Engine::StaticModel* model = registry.Assets().GetModel(handle);
    if (!model || model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) { return; }

    Engine::Material emissive = *defaultMaterial; // black albedo so only emission shows
    emissive.props.colorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    emissive.props.emissiveFactor = areaLight ? glm::vec4(areaLight->color, areaLight->intensity) : glm::vec4(sphereLight->color, sphereLight->intensity);
    const Engine::MaterialID materialID = registry.ServiceMut<Engine::MaterialManager>().CreateSynthesizedMaterial(emissive);

    glm::mat4 m(1.0f);
    if (registry.Has<Component::TransformComponent>(entity)) {
        const Transform world = Component::ComputeWorldTransform(registry, entity);
        m = areaLight ? Component::ComputeAreaLightQuadMatrix(world, *areaLight) : Component::ComputeSphereLightMatrix(world, *sphereLight);
    }

    registry.Remove<Component::LightSurfaceRequest>(entity);
    registry.Remove<Component::LightSurfaceRuntime>(entity); // frees any stale ranges via OnDestroy
    Engine::ModelStore::Range modelRange = registry.ServiceMut<Engine::ModelStore>().Allocate(1);
    if (!modelRange.IsValid()) { return; }
    const Engine::InstanceStore::Range range = registry.ServiceMut<Engine::InstanceStore>().AllocateSingleMeshRange(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), model, materialID, modelRange.offset, false);
    if (!range.IsValid()) {
        registry.ServiceMut<Engine::ModelStore>().Free(modelRange);
        return;
    }
    registry.ServiceMut<Engine::ModelStore>().SetModel(modelRange.offset, {m, m});
    registry.ServiceMut<Engine::InstanceStore>().SetLightIndex(range.offset, &registry.ServiceMut<Engine::TriLightStore>(), Component::FindLightSlot(registry, entity));
    registry.Set<Component::LightSurfaceRuntime>(entity, range, modelRange, materialID);
}

void LightSurfaceResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const entt::entity entity : registry.View<Component::LightSurfaceRequest>()) {
        if (budget == 0) { break; }
        registry.RunExclusive<&ApplyLightSurfaceResolve>(entity);
        --budget;
    }
}

static void ApplyStaticMeshResolve(Engine::SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity)) { return; }
    const auto* meshComponentPtr = registry.TryGet<Component::StaticMeshComponent>(entity);
    auto* runtime = registry.TryGetMut<Component::MeshRuntime>(entity);
    if (!meshComponentPtr || !runtime) { return; }
    const Component::StaticMeshComponent& meshComponent = *meshComponentPtr;
    Engine::EngineContext* ctx = registry.Context();
    Engine::EngineState* state = registry.State();

    Engine::MaterialManager* materialManager = &registry.ServiceMut<Engine::MaterialManager>();
    Engine::InstanceStore& store = registry.ServiceMut<Engine::InstanceStore>();

    auto releaseExisting = [&] {
        store.ReleaseAndFree(materialManager, &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
        registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);
    };

    auto model = registry.Assets().GetModel(TargetModel(*runtime));
    if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
        if (!model) { LOG_ERROR(Engine, "Model ({}) is not in the asset manager after a load request.", TargetModel(*runtime).index); }
        if (!DropPendingModel(ctx, state, registry, *runtime)) { releaseExisting(); }
        registry.Remove<Component::StaticMeshLoadingFlag>(entity);
        return;
    }
    if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
        return;
    }
    PromotePendingModel(ctx, state, registry, *runtime);

    const Core::HeapArray<Engine::Node>& nodes = model->modelData.nodes;
    Core::HeapArray<Engine::MeshInformation>& meshes = model->modelData.meshes;

    const auto* overrides = registry.TryGet<Component::StaticMeshOverridesComponent>(entity);

    auto included = [&](uint32_t ordinal) -> bool {
        return !overrides || !overrides->primitiveBlacklist.Contains(ordinal);
    };

    const uint32_t rootNode = meshComponent.nodeIndex < nodes.Size() ? meshComponent.nodeIndex : ~0u;
    auto inSubtree = [&](uint32_t n) -> bool {
        for (uint32_t i = n; i != ~0u; i = nodes[i].parent) {
            if (i == rootNode) { return true; }
        }
        return rootNode == ~0u;
    };
    const glm::mat4 rootInverse = rootNode != ~0u ? glm::inverse(ComposeNodeModelSpace(nodes, rootNode)) : glm::mat4(1.0f);

    uint32_t totalPrimitives = 0;
    uint32_t contributingNodes = 0;

    //
    {
        uint32_t ordinal = 0;
        for (uint32_t n = 0; n < nodes.Size(); ++n) {
            const uint32_t mi = nodes[n].meshIndex;
            if (mi == ~0u || mi >= meshes.Size()) { continue; }
            const size_t nodePrimCount = meshes[mi].primitiveProperties.Size();
            const bool bNodeIn = inSubtree(n);
            uint32_t nodeIncluded = 0;
            for (size_t j = 0; j < nodePrimCount; ++j) {
                if (bNodeIn && included(ordinal)) {
                    ++totalPrimitives;
                    ++nodeIncluded;
                }
                ++ordinal;
            }
            if (nodeIncluded > 0) { ++contributingNodes; }
        }
    }

    if (totalPrimitives == 0) {
        releaseExisting();
        registry.Remove<Component::StaticMeshLoadingFlag>(entity);
        return;
    }

    releaseExisting();

    Engine::ModelStore::Range modelRange = registry.ServiceMut<Engine::ModelStore>().Allocate(contributingNodes);
    if (!modelRange.IsValid()) {
        registry.Remove<Component::StaticMeshLoadingFlag>(entity);
        return;
    }

    Engine::InstanceStore::Range range = store.Allocate(totalPrimitives);
    if (!range.IsValid()) {
        LOG_ERROR(Engine, "Instance store full; cannot flatten model ({}) needing {} primitives", model->name.c_str(), totalPrimitives);
        registry.ServiceMut<Engine::ModelStore>().Free(modelRange);
        registry.Remove<Component::StaticMeshLoadingFlag>(entity);
        return;
    }

    auto applyShaderOverrides = [&](Engine::Material mat) -> Engine::Material {
        if (meshComponent.shadingShaderOverride) { mat.fragmentShader = meshComponent.shadingShaderOverride; }
        if (meshComponent.lightingShaderOverride) { mat.lightingShader = meshComponent.lightingShaderOverride; }
        return mat;
    };

    const bool bEmissiveLight = HasEmissiveLightFlag(registry, entity);
    uint32_t writeIndex = range.offset;
    uint32_t ordinal = 0;
    uint32_t nodeModelSlot = modelRange.offset - 1;
    for (uint32_t n = 0; n < nodes.Size(); ++n) {
        const uint32_t mi = nodes[n].meshIndex;
        if (mi == ~0u || mi >= meshes.Size()) { continue; }

        const glm::mat4 nodeModelSpace = rootInverse * ComposeNodeModelSpace(nodes, n);
        Engine::MeshInformation& mesh = meshes[mi];
        const bool bNodeIn = inSubtree(n);

        bool bNodeSlotAssigned = false;
        for (size_t j = 0; j < mesh.primitiveProperties.Size(); ++j) {
            const uint32_t thisOrdinal = ordinal++;
            if (!bNodeIn || !included(thisOrdinal)) { continue; }
            if (!bNodeSlotAssigned) {
                ++nodeModelSlot;
                bNodeSlotAssigned = true;
            }

            Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[j];

            Engine::MaterialID matID;
            if (primitive.materialIndex == -1) {
                matID = materialManager->GetDefaultMaterialID();
            }
            else {
                Engine::MaterialID materialOverride = overrides ? overrides->GetMaterialOverride(static_cast<uint32_t>(primitive.materialIndex)) : Engine::MaterialID::INVALID;
                if (AuthoredMaterialExists(registry, materialManager, entity, materialOverride)) {
                    matID = materialOverride;
                }
                else {
                    matID = materialManager->CreateSynthesizedMaterial(applyShaderOverrides(model->modelData.materials[primitive.materialIndex]));
                }
            }

            store.FillEntry(writeIndex, materialManager, &registry.ServiceMut<Engine::TriLightStore>(), model, primitive, {
                                .material = matID,
                                .modelSlot = nodeModelSlot,
                                .materialSlot = primitive.materialIndex,
                                .sourceNodeIndex = n,
                                .modelPrimitiveOrdinal = thisOrdinal,
                                .modelSpaceTransform = nodeModelSpace,
                                .bEmissiveLight = bEmissiveLight,
                            });
            ++writeIndex;
        }
    }

    runtime->range = range;
    runtime->modelRange = modelRange;
    EvaluateInstanceRenderState(state, entity, registry);
    registry.Set<Component::MultiframeDirtyRequest>(entity);
    registry.Remove<Component::StaticMeshLoadingFlag>(entity);
}

void StaticMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    const int32_t modelsWaitingThisTick = PollModelResolves<Component::StaticMeshComponent, Component::StaticMeshLoadingFlag, &ApplyStaticMeshResolve>(registry);

    if (modelsWaitingThisTick > 0) {
        state->assetLoad.pendingModelWaitCount += modelsWaitingThisTick;
        state->assetLoad.modelWaitLastActivity = std::chrono::steady_clock::now();
    }
    if (state->assetLoad.pendingModelWaitCount > 0 && modelsWaitingThisTick == 0 && (std::chrono::steady_clock::now() - state->assetLoad.modelWaitLastActivity) >= std::chrono::seconds(1)) {
        LOG_TRACE(Engine, "{} model(s) not yet done loading", state->assetLoad.pendingModelWaitCount);
        state->assetLoad.pendingModelWaitCount = 0;
    }
}

void StaticMeshPrimitivePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const auto& [entity, meshComponent] : registry.View<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadRequest>().each()) {
        if (budget == 0) { break; }
        if (registry.Assets().IsModelFrozen(meshComponent.modelId)) { continue; }
        const Engine::StaticModelHandle handle = registry.Assets().LoadModel(meshComponent.modelId);
        if (!handle.IsValid()) { continue; }
        registry.RunExclusive<&ApplyModelKickoff<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadRequest, Component::StaticMeshPrimitiveLoadingFlag>>(ModelKickoff{entity, handle});
        --budget;
    }
}

static void ApplyStaticMeshPrimitiveResolve(Engine::SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity)) { return; }
    const auto* meshComponentPtr = registry.TryGet<Component::StaticMeshPrimitiveComponent>(entity);
    auto* runtime = registry.TryGetMut<Component::MeshRuntime>(entity);
    if (!meshComponentPtr || !runtime) { return; }
    const Component::StaticMeshPrimitiveComponent& meshComponent = *meshComponentPtr;
    Engine::EngineContext* ctx = registry.Context();
    Engine::EngineState* state = registry.State();

    Engine::MaterialManager* materialManager = &registry.ServiceMut<Engine::MaterialManager>();

    auto releaseExisting = [&] {
        registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(materialManager, &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
        registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);
    };

    auto model = registry.Assets().GetModel(TargetModel(*runtime));
    if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
        if (!model) { LOG_ERROR(Engine, "Model ({}) is not in the asset manager after a load request.", TargetModel(*runtime).index); }
        if (!DropPendingModel(ctx, state, registry, *runtime)) { releaseExisting(); }
        registry.Remove<Component::StaticMeshPrimitiveLoadingFlag>(entity);
        return;
    }
    if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
        return;
    }
    PromotePendingModel(ctx, state, registry, *runtime);

    const Core::HeapArray<Engine::Node>& nodes = model->modelData.nodes;
    Core::HeapArray<Engine::MeshInformation>& meshes = model->modelData.meshes;

    uint32_t ordinal = 0;
    uint32_t targetNode = ~0u;
    Engine::PrimitiveProperty* targetPrim = nullptr;
    for (uint32_t n = 0; n < nodes.Size() && !targetPrim; ++n) {
        const uint32_t mi = nodes[n].meshIndex;
        if (mi == ~0u || mi >= meshes.Size()) { continue; }
        Engine::MeshInformation& mesh = meshes[mi];
        for (size_t j = 0; j < mesh.primitiveProperties.Size(); ++j) {
            if (ordinal == meshComponent.primitiveOrdinal) {
                targetNode = n;
                targetPrim = &mesh.primitiveProperties[j];
                break;
            }
            ++ordinal;
        }
    }
    if (!targetPrim) {
        LOG_WARN(Engine, "StaticMeshPrimitive ordinal {} out of range for model ({})", meshComponent.primitiveOrdinal, model->name.c_str());
        releaseExisting();
        registry.Remove<Component::StaticMeshPrimitiveLoadingFlag>(entity);
        return;
    }

    releaseExisting();

    auto applyShaderOverrides = [&](Engine::Material mat) -> Engine::Material {
        if (meshComponent.shadingShaderOverride) { mat.fragmentShader = meshComponent.shadingShaderOverride; }
        if (meshComponent.lightingShaderOverride) { mat.lightingShader = meshComponent.lightingShaderOverride; }
        return mat;
    };

    Engine::MaterialID matID;
    if (AuthoredMaterialExists(registry, materialManager, entity, meshComponent.materialOverride)) {
        matID = meshComponent.materialOverride;
    }
    else if (targetPrim->materialIndex == -1) {
        matID = materialManager->GetDefaultMaterialID();
    }
    else {
        matID = materialManager->CreateSynthesizedMaterial(applyShaderOverrides(model->modelData.materials[targetPrim->materialIndex]));
    }

    Engine::InstanceStore& store = registry.ServiceMut<Engine::InstanceStore>();
    Engine::ModelStore::Range modelRange = registry.ServiceMut<Engine::ModelStore>().Allocate(1);
    if (!modelRange.IsValid()) {
        registry.Remove<Component::StaticMeshPrimitiveLoadingFlag>(entity);
        return;
    }
    Engine::InstanceStore::Range range = store.Allocate(1);
    if (!range.IsValid()) {
        LOG_ERROR(Engine, "Instance store full; cannot resolve split primitive");
        registry.ServiceMut<Engine::ModelStore>().Free(modelRange);
        registry.Remove<Component::StaticMeshPrimitiveLoadingFlag>(entity);
        return;
    }

    store.FillEntry(range.offset, materialManager, &registry.ServiceMut<Engine::TriLightStore>(), model, *targetPrim, {
                        .material = matID,
                        .modelSlot = modelRange.offset,
                        .materialSlot = targetPrim->materialIndex,
                        .sourceNodeIndex = targetNode,
                        .modelPrimitiveOrdinal = meshComponent.primitiveOrdinal,
                        .bEmissiveLight = HasEmissiveLightFlag(registry, entity),
                    });
    runtime->range = range;
    runtime->modelRange = modelRange;
    EvaluateInstanceRenderState(state, entity, registry);
    registry.Set<Component::MultiframeDirtyRequest>(entity);

    registry.Remove<Component::StaticMeshPrimitiveLoadingFlag>(entity);
}

void StaticMeshPrimitiveLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    PollModelResolves<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadingFlag, &ApplyStaticMeshPrimitiveResolve>(registry);
}

void ProceduralMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const auto& [entity, meshComponent] : registry.View<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadRequest>().each()) {
        if (budget == 0) { break; }
        const Engine::StaticModelHandle handle = registry.Assets().LoadProceduralModel(meshComponent.params, meshComponent.repeat);
        if (!handle.IsValid()) { continue; }
        registry.RunExclusive<&ApplyModelKickoff<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadRequest, Component::ProceduralMeshLoadingFlag>>(ModelKickoff{entity, handle});
        --budget;
    }
}

static void ApplyProceduralMeshResolve(Engine::SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity)) { return; }
    const auto* meshComponentPtr = registry.TryGet<Component::ProceduralMeshComponent>(entity);
    auto* runtime = registry.TryGetMut<Component::MeshRuntime>(entity);
    if (!meshComponentPtr || !runtime) { return; }
    const Component::ProceduralMeshComponent& meshComponent = *meshComponentPtr;
    Engine::EngineContext* ctx = registry.Context();
    Engine::EngineState* state = registry.State();

    auto releaseExisting = [&] {
        registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
        registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);
    };

    auto model = registry.Assets().GetModel(TargetModel(*runtime));
    if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
        if (!model) { LOG_ERROR(Engine, "Procedural model ({}) is not in the asset manager, it should have been requested to load during scene load.", TargetModel(*runtime).index); }
        if (!DropPendingModel(ctx, state, registry, *runtime)) { releaseExisting(); }
        registry.Remove<Component::ProceduralMeshLoadingFlag>(entity);
        return;
    }
    if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
        return;
    }
    PromotePendingModel(ctx, state, registry, *runtime);

    Engine::MaterialID matID = registry.Service<Engine::MaterialManager>().GetDefaultMaterialID();
    if (AuthoredMaterialExists(registry, &registry.Service<Engine::MaterialManager>(), entity, meshComponent.material)) {
        matID = meshComponent.material;
    }
    releaseExisting();
    runtime->modelRange = registry.ServiceMut<Engine::ModelStore>().Allocate(1);
    if (runtime->modelRange.IsValid()) {
        runtime->range = registry.ServiceMut<Engine::InstanceStore>().AllocateSingleMeshRange(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(registry, entity));
        EvaluateInstanceRenderState(state, entity, registry);
        registry.Set<Component::MultiframeDirtyRequest>(entity);
    }

    registry.Remove<Component::ProceduralMeshLoadingFlag>(entity);
}

void ProceduralMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    const int32_t proceduralWaitingThisTick = PollModelResolves<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadingFlag, &ApplyProceduralMeshResolve>(registry);

    if (proceduralWaitingThisTick > 0) {
        state->assetLoad.pendingProceduralWaitCount += proceduralWaitingThisTick;
        state->assetLoad.proceduralWaitLastActivity = std::chrono::steady_clock::now();
    }
    if (state->assetLoad.pendingProceduralWaitCount > 0 && proceduralWaitingThisTick == 0 && (std::chrono::steady_clock::now() - state->assetLoad.proceduralWaitLastActivity) >= std::chrono::seconds(1)) {
        LOG_TRACE(Engine, "{} procedural model(s) not yet done loading", state->assetLoad.pendingProceduralWaitCount);
        state->assetLoad.pendingProceduralWaitCount = 0;
    }
}

static void FillModuleMeshRange(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Component::MeshRuntime* runtime, Engine::StaticModel* model, const Component::ModuleMeshComponent& meshComponent, bool bEmissiveLight)
{
    registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
    registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);

    if (model->modelData.meshes.IsEmpty()) { return; }
    Engine::MeshInformation& mesh = model->modelData.meshes[0];
    const auto count = static_cast<uint32_t>(mesh.primitiveProperties.Size());
    if (count == 0) { return; }

    Engine::ModelStore::Range modelRange = registry.ServiceMut<Engine::ModelStore>().Allocate(1);
    if (!modelRange.IsValid()) { return; }

    Engine::InstanceStore& store = registry.ServiceMut<Engine::InstanceStore>();
    Engine::InstanceStore::Range range = store.Allocate(count);
    if (!range.IsValid()) {
        LOG_ERROR(Engine, "Instance store full; cannot resolve module runtime for model ({})", model->name.c_str());
        registry.ServiceMut<Engine::ModelStore>().Free(modelRange);
        return;
    }

    uint32_t writeIndex = range.offset;
    for (uint32_t j = 0; j < count; ++j) {
        Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[j];
        // materialIndex carries the module slot (see FinalizeGeometryGroups)
        Engine::MaterialID matID = registry.Service<Engine::MaterialManager>().GetDefaultMaterialID();
        if (primitive.materialIndex >= 0 && primitive.materialIndex < Engine::MAX_MODULE_SLOTS) {
            const Engine::MaterialID slotMat = meshComponent.slotMaterials[primitive.materialIndex];
            if (slotMat.IsValid() && registry.Service<Engine::MaterialManager>().DoesMutableMaterialExist(slotMat)) {
                matID = slotMat;
            }
        }
        store.FillEntry(writeIndex, &registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), model, primitive, {
                            .material = matID,
                            .modelSlot = modelRange.offset,
                            .materialSlot = primitive.materialIndex,
                            .sourceNodeIndex = 0,
                            .modelPrimitiveOrdinal = j,
                            .bEmissiveLight = bEmissiveLight,
                        });
        ++writeIndex;
    }
    runtime->range = range;
    runtime->modelRange = modelRange;
}

void ModuleMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const auto& [entity, meshComponent] : registry.View<Component::ModuleMeshComponent, Component::ModuleMeshLoadRequest>().each()) {
        if (budget == 0) { break; }
        if (meshComponent.params.parts.IsEmpty()) { continue; }
        const Engine::StaticModelHandle handle = registry.Assets().LoadModuleModel(meshComponent.params);
        if (!handle.IsValid()) { continue; }
        registry.RunExclusive<&ApplyModelKickoff<Component::ModuleMeshComponent, Component::ModuleMeshLoadRequest, Component::ModuleMeshLoadingFlag>>(ModelKickoff{entity, handle});
        --budget;
    }
}

static void ApplyModuleMeshResolve(Engine::SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity)) { return; }
    const auto* meshComponentPtr = registry.TryGet<Component::ModuleMeshComponent>(entity);
    auto* runtime = registry.TryGetMut<Component::MeshRuntime>(entity);
    if (!meshComponentPtr || !runtime) { return; }
    const Component::ModuleMeshComponent& meshComponent = *meshComponentPtr;
    Engine::EngineContext* ctx = registry.Context();
    Engine::EngineState* state = registry.State();

    auto releaseExisting = [&] {
        registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
        registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);
    };

    auto model = registry.Assets().GetModel(TargetModel(*runtime));
    if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
        if (!model) { LOG_ERROR(Engine, "Module model ({}) is not in the asset manager.", TargetModel(*runtime).index); }
        if (!DropPendingModel(ctx, state, registry, *runtime)) { releaseExisting(); }
        registry.Remove<Component::ModuleMeshLoadingFlag>(entity);
        return;
    }
    if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
        return;
    }
    PromotePendingModel(ctx, state, registry, *runtime);

    FillModuleMeshRange(ctx, state, registry, runtime, model, meshComponent, HasEmissiveLightFlag(registry, entity));
    EvaluateInstanceRenderState(state, entity, registry);
    registry.Set<Component::MultiframeDirtyRequest>(entity);

    registry.Remove<Component::ModuleMeshLoadingFlag>(entity);
}

void ModuleMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    PollModelResolves<Component::ModuleMeshComponent, Component::ModuleMeshLoadingFlag, &ApplyModuleMeshResolve>(registry);
}

/** A railing with no lanes and no posts generates nothing; cross planks need two lanes. */
static bool SplineHasGeometry(const Engine::SplineParams& params)
{
    return !params.railing.bEnabled || !params.railing.lanes.IsEmpty() || params.railing.bPosts;
}

void SplineMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const auto& [entity, meshComponent] : registry.View<Component::SplineMeshComponent, Component::SplineMeshLoadRequest>().each()) {
        if (budget == 0) { break; }
        const Engine::SplineParams params = Component::ToSplineParams(meshComponent);
        if (!SplineHasGeometry(params)) {
            registry.RunExclusive<&ApplyMeshCleared<Component::SplineMeshLoadRequest>>(entity);
            --budget;
            continue;
        }
        const Engine::StaticModelHandle handle = registry.Assets().LoadSplineModel(params);
        if (!handle.IsValid()) { continue; }
        registry.RunExclusive<&ApplyModelKickoff<Component::SplineMeshComponent, Component::SplineMeshLoadRequest, Component::SplineMeshLoadingFlag>>(ModelKickoff{entity, handle});
        --budget;
    }
}

static void ApplySplineMeshResolve(Engine::SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity)) { return; }
    const auto* meshComponentPtr = registry.TryGet<Component::SplineMeshComponent>(entity);
    auto* runtime = registry.TryGetMut<Component::MeshRuntime>(entity);
    if (!meshComponentPtr || !runtime) { return; }
    const Component::SplineMeshComponent& meshComponent = *meshComponentPtr;
    Engine::EngineContext* ctx = registry.Context();
    Engine::EngineState* state = registry.State();

    auto releaseExisting = [&] {
        registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
        registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);
    };

    auto model = registry.Assets().GetModel(TargetModel(*runtime));
    if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
        if (!model) { LOG_ERROR(Engine, "Spline model ({}) is not in the asset manager.", TargetModel(*runtime).index); }
        if (!DropPendingModel(ctx, state, registry, *runtime)) { releaseExisting(); }
        registry.Remove<Component::SplineMeshLoadingFlag>(entity);
        return;
    }
    if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
        return;
    }
    PromotePendingModel(ctx, state, registry, *runtime);

    Engine::MaterialID matID = registry.Service<Engine::MaterialManager>().GetDefaultMaterialID();
    if (AuthoredMaterialExists(registry, &registry.Service<Engine::MaterialManager>(), entity, meshComponent.material)) {
        matID = meshComponent.material;
    }
    releaseExisting();
    runtime->modelRange = registry.ServiceMut<Engine::ModelStore>().Allocate(1);
    if (runtime->modelRange.IsValid()) {
        runtime->range = registry.ServiceMut<Engine::InstanceStore>().AllocateSingleMeshRange(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(registry, entity));
        EvaluateInstanceRenderState(state, entity, registry);
        registry.Set<Component::MultiframeDirtyRequest>(entity);
    }

    registry.Remove<Component::SplineMeshLoadingFlag>(entity);
}

void SplineMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    PollModelResolves<Component::SplineMeshComponent, Component::SplineMeshLoadingFlag, &ApplySplineMeshResolve>(registry);
}

void Text3DGeneratePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    size_t budget = Engine::MAX_ASSET_RESOLVES_PER_TICK;
    for (const auto& [entity, textComponent] : registry.View<Component::Text3DComponent, Component::Text3DGenerateRequest>().each()) {
        if (budget == 0) { break; }
        // Stay pending while the font is missing or frozen (e.g. mid hot-reload drain). The generated mesh takes its own font ref, so we hold none here.
        if (!textComponent.fontId.IsValid() || registry.Assets().IsFontFrozen(textComponent.fontId)) { continue; }
        const Engine::StaticModelHandle handle = textComponent.text.Size() > 0
            ? registry.Assets().LoadText3DModel(textComponent.fontId, textComponent.text, textComponent.depth, textComponent.flatness, textComponent.tracking, textComponent.scale, textComponent.bSmoothNormals, textComponent.align, textComponent.anchor, textComponent.wrapWidth, textComponent.bendRadius)
            : Engine::StaticModelHandle::INVALID;
        if (handle.IsValid()) {
            registry.RunExclusive<&ApplyModelKickoff<Component::Text3DComponent, Component::Text3DGenerateRequest, Component::Text3DLoadingFlag>>(ModelKickoff{entity, handle});
        }
        else {
            registry.RunExclusive<&ApplyMeshCleared<Component::Text3DGenerateRequest>>(entity);
        }
        --budget;
    }
}

static void ApplyText3DResolve(Engine::SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity)) { return; }
    const auto* textComponentPtr = registry.TryGet<Component::Text3DComponent>(entity);
    auto* runtime = registry.TryGetMut<Component::MeshRuntime>(entity);
    if (!textComponentPtr || !runtime) { return; }
    const Component::Text3DComponent& textComponent = *textComponentPtr;
    Engine::EngineContext* ctx = registry.Context();
    Engine::EngineState* state = registry.State();

    if (!TargetModel(*runtime).IsValid()) {
        registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
        registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);
        registry.Remove<Component::Text3DLoadingFlag>(entity); // nothing to resolve (e.g. empty text / no font); drop the tag
        return;
    }

    auto model = registry.Assets().GetModel(TargetModel(*runtime));
    if (!model) {
        LOG_ERROR(Engine, "Text3D model ({}) is not in the asset manager.", TargetModel(*runtime).index);
        return;
    }
    if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
        if (!DropPendingModel(ctx, state, registry, *runtime)) {
            registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
            registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);
        }
        registry.Remove<Component::Text3DLoadingFlag>(entity); // generation failed (e.g. empty/whitespace text); stop waiting so editing unlocks
        return;
    }
    if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
        return;
    }
    PromotePendingModel(ctx, state, registry, *runtime);

    Engine::MaterialID matID = registry.Service<Engine::MaterialManager>().GetDefaultMaterialID();
    if (AuthoredMaterialExists(registry, &registry.Service<Engine::MaterialManager>(), entity, textComponent.material)) {
        matID = textComponent.material;
    }
    registry.ServiceMut<Engine::InstanceStore>().ReleaseAndFree(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), runtime->range);
    registry.ServiceMut<Engine::ModelStore>().Free(runtime->modelRange);
    runtime->modelRange = registry.ServiceMut<Engine::ModelStore>().Allocate(1);
    if (runtime->modelRange.IsValid()) {
        runtime->range = registry.ServiceMut<Engine::InstanceStore>().AllocateSingleMeshRange(&registry.ServiceMut<Engine::MaterialManager>(), &registry.ServiceMut<Engine::TriLightStore>(), model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(registry, entity));
        EvaluateInstanceRenderState(state, entity, registry);
        registry.Set<Component::MultiframeDirtyRequest>(entity);
    }

    registry.Remove<Component::Text3DLoadingFlag>(entity);
}

void Text3DLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    PollModelResolves<Component::Text3DComponent, Component::Text3DLoadingFlag, &ApplyText3DResolve>(registry);
}

struct TextFontKickoff
{
    entt::entity entity{entt::null};
    Engine::FontHandle handle{Engine::FontHandle::INVALID};
};

static void ApplyTextFontKickoff(Engine::SystemRegistry& registry, const TextFontKickoff& kickoff)
{
    auto* runtime = registry.Valid(kickoff.entity) ? registry.TryGetMut<Component::TextRuntime>(kickoff.entity) : nullptr;
    if (!runtime || runtime->fontHandle.IsValid()) {
        registry.Assets().UnloadFont(kickoff.handle);
        return;
    }
    runtime->fontHandle = kickoff.handle;
}

void TextFontPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    auto view = registry.View<Component::TextComponent, Component::TextRuntime, Component::TextFontRequest>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, textComp, runtime] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        // Load here (not at construct) so the freeze can hold it pending.
        if (!runtime.fontHandle.IsValid()) {
            if (!textComp.fontId.IsValid() || registry.Assets().IsFontFrozen(textComp.fontId)) { continue; }
            const Engine::FontHandle handle = registry.Assets().LoadFont(textComp.fontId);
            if (handle.IsValid()) { registry.RunExclusive<&ApplyTextFontKickoff>(TextFontKickoff{entity, handle}); }
            continue;
        }

        const Engine::Font* font = registry.Assets().GetFont(runtime.fontHandle);
        if (!font) { continue; }
        if (font->loadState == Engine::Font::LoadState::FailedToLoad) {
            resolved.PushBack(entity);
            continue;
        }
        if (font->loadState != Engine::Font::LoadState::Loaded) { continue; }

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        registry.Remove<Component::TextFontRequest>(entity);
    }
}
} // Engine
