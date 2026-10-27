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
static bool AuthoredMaterialExists(const Engine::Registry& registry, const MaterialManager* materialManager, entt::entity entity, MaterialID material)
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

void ModelHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->assetLoad.pendingHotReloadModelIds.IsEmpty()) { return; }

    // Freeze + release refs so the model drains, then re-arm; the load resolves re-acquire the fresh version after ResolveUnloads unfreezes on reclaim.
    auto isHot = [&](Engine::ModelID id) {
        if (!ctx->assetManager->IsModelFrozen(id)) { return false; }
        for (const Engine::ModelID& hotId : state->assetLoad.pendingHotReloadModelIds) {
            if (id == hotId) { return true; }
        }
        return false;
    };

    // Only resident models need freezing; a non-resident one's fresh load already reads the regenerated file.
    for (const Engine::ModelID& hotId : state->assetLoad.pendingHotReloadModelIds) {
        if (ctx->assetManager->IsModelResident(hotId)) {
            ctx->assetManager->FreezeModel(hotId);
        }
    }

    for (auto [entity, smc] : state->registry.View<Engine::Mut<Component::StaticMeshComponent>>().each()) {
        if (!state->registry.Has<Component::MeshRuntime>(entity)) { continue; }
        if (!isHot(smc.modelId)) { continue; }
        Component::UnloadStaticMesh(state->registry, entity);
        Component::LoadStaticMesh(smc, state->registry, entity);
    }

    // Physics holds its own model ref (its serialized key, loaded independently), so it must also release + re-arm or the model can never drain.
    for (auto [entity, bodyDesc, runtime] : state->registry.View<Component::PhysicsBodyDesc, Engine::Mut<Component::PhysicsShapeRuntime>>().each()) {
        bool affected = false;
        for (size_t i = 0; i < bodyDesc.shapes.Size(); ++i) {
            const auto* collider = std::get_if<Component::ColliderShape>(&bodyDesc.shapes[i].geometry);
            if (collider && collider->meshSourceModelId.IsValid() && isHot(collider->meshSourceModelId) && runtime.colliders[i].IsValid()) {
                ctx->assetManager->UnloadCollider(runtime.colliders[i]);
                runtime.colliders[i] = {};
                affected = true;
            }
        }
        if (!affected) { continue; }

        state->registry.Remove<Component::PhysicsMeshLoadingFlag>(entity);
        state->registry.Set<Component::PhysicsMeshRequest>(entity);
        state->registry.Set<Component::PhysicsShapeCreationRequest>(entity);
        state->registry.Set<Component::PhysicsBodyCreationRequest>(entity);
    }

    state->assetLoad.pendingHotReloadModelIds.Clear();
}

void FontHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->assetLoad.pendingHotReloadFontIds.IsEmpty()) { return; }

    // The UI font is engine-held (no component owns it) so it can never drain; reject its hot-reload and require a restart.
    Engine::FontID uiFontId{};
    if (const Engine::Font* uiFont = ctx->assetManager->GetFont(state->uiFont)) { uiFontId = uiFont->fontId; }

    // Freeze + release refs so the font drains, then re-arm; the resolve systems re-acquire the fresh version after ResolveUnloads unfreezes on reclaim.
    auto isHot = [&](Engine::FontID id) {
        if (!ctx->assetManager->IsFontFrozen(id)) { return false; }
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
        if (ctx->assetManager->IsFontResident(hotId)) {
            ctx->assetManager->FreezeFont(hotId);
        }
    }

    for (auto [entity, textComp, runtime] : state->registry.View<Engine::Mut<Component::TextComponent>, Component::TextRuntime>().each()) {
        if (!isHot(textComp.fontId)) { continue; }
        Component::UnloadTextComponent(textComp, state->registry, entity);
        Component::LoadTextComponent(textComp, state->registry, entity);
    }
    for (auto [entity, text3DComp] : state->registry.View<Engine::Mut<Component::Text3DComponent>>().each()) {
        if (!isHot(text3DComp.fontId)) { continue; }
        Component::UnloadText3DFont(state->registry, entity);
        Component::LoadText3DFont(text3DComp, state->registry, entity);
    }

    // A Text3D physics collider holds its own ref to the generated mesh (keyed on the font), so it must release + re-arm too or that mesh can never drain.
    for (auto [entity, bodyDesc, runtime] : state->registry.View<Component::PhysicsBodyDesc, Engine::Mut<Component::PhysicsShapeRuntime>>().each()) {
        bool affected = false;
        for (size_t i = 0; i < bodyDesc.shapes.Size(); ++i) {
            const auto* collider = std::get_if<Component::ColliderShape>(&bodyDesc.shapes[i].geometry);
            if (collider && collider->text3DSource.IsValid() && isHot(collider->text3DSource.fontId) && runtime.colliders[i].IsValid()) {
                ctx->assetManager->UnloadCollider(runtime.colliders[i]);
                runtime.colliders[i] = {};
                affected = true;
            }
        }
        if (!affected) { continue; }

        state->registry.Remove<Component::PhysicsMeshLoadingFlag>(entity);
        state->registry.Set<Component::PhysicsMeshRequest>(entity);
        state->registry.Set<Component::PhysicsShapeCreationRequest>(entity);
        state->registry.Set<Component::PhysicsBodyCreationRequest>(entity);
    }

    state->assetLoad.pendingHotReloadFontIds.Clear();
}

void TextureHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->assetLoad.pendingHotReloadTextureIds.IsEmpty()) { return; }

    for (auto hotId : state->assetLoad.pendingHotReloadTextureIds) {
        if (ctx->assetManager->IsTextureLoaded(hotId) && ctx->assetManager->ReloadTexture(hotId)) {
            ctx->materialManager->RebindTexture(hotId, ctx->assetManager->GetTextureBindlessIndex(hotId));
        }
    }

    state->assetLoad.pendingHotReloadTextureIds.Clear();
}

void CubemapHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->assetLoad.pendingHotReloadEnvironmentMapIds.IsEmpty()) { return; }

    for (auto hotId : state->assetLoad.pendingHotReloadEnvironmentMapIds) {
        if (ctx->assetManager->IsCubemapLoaded(hotId)) {
            ctx->assetManager->ReloadCubemap(hotId);
        }
    }

    state->assetLoad.pendingHotReloadEnvironmentMapIds.Clear();
}

static void StageModel(Engine::EngineContext* ctx, Component::MeshRuntime& runtime, Engine::StaticModelHandle handle)
{
    if (runtime.pendingModelHandle.IsValid()) { ctx->assetManager->UnloadModel(runtime.pendingModelHandle); }
    runtime.pendingModelHandle = Engine::StaticModelHandle::INVALID;
    if (runtime.modelHandle.IsValid()) { runtime.pendingModelHandle = handle; }
    else { runtime.modelHandle = handle; }
}

static Engine::StaticModelHandle TargetModel(const Component::MeshRuntime& runtime)
{
    return runtime.pendingModelHandle.IsValid() ? runtime.pendingModelHandle : runtime.modelHandle;
}

static bool DropPendingModel(Engine::EngineContext* ctx, Component::MeshRuntime& runtime)
{
    if (!runtime.pendingModelHandle.IsValid()) { return false; }
    ctx->assetManager->UnloadModel(runtime.pendingModelHandle);
    runtime.pendingModelHandle = Engine::StaticModelHandle::INVALID;
    return true;
}

static void PromotePendingModel(Engine::EngineContext* ctx, Component::MeshRuntime& runtime)
{
    if (!runtime.pendingModelHandle.IsValid()) { return; }
    if (runtime.modelHandle.IsValid()) { ctx->assetManager->UnloadModel(runtime.modelHandle); }
    runtime.modelHandle = runtime.pendingModelHandle;
    runtime.pendingModelHandle = Engine::StaticModelHandle::INVALID;
}

void StaticMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::StaticMeshComponent, Component::StaticMeshLoadRequest>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        if (ctx->assetManager->IsModelFrozen(meshComponent.modelId)) { continue; } // stay pending while frozen
        const Engine::StaticModelHandle handle = ctx->assetManager->LoadModel(meshComponent.modelId);
        if (!handle.IsValid()) { continue; }
        StageModel(ctx, state->registry.GetOrAdd<Component::MeshRuntime>(entity), handle);
        started.PushBack(entity);
    }
    for (const entt::entity entity : started) {
        state->registry.Remove<Component::StaticMeshLoadRequest>(entity);
        state->registry.Set<Component::StaticMeshLoadingFlag>(entity);
    }
}

void ReflectionProbePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::ReflectionProbeComponent, Engine::Mut<Component::ReflectionProbeRuntime>, Component::ReflectionProbeLoadRequest>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, probe, runtime] : view.each()) {
        if (started.Size() >= budget) { break; }
        if (ctx->assetManager->GetProbeInfo(Engine::ProbeID{probe.probeId})) {
            runtime.contentHandle = ctx->assetManager->LoadProbe(Engine::ProbeID{probe.probeId});
            runtime.contentSource = Component::ReflectionProbeComponent::ContentSource::Baked;
        }
        else if (probe.standInEnvMap.IsValid()) {
            runtime.contentHandle = ctx->assetManager->LoadCubemap(probe.standInEnvMap);
            runtime.contentSource = Component::ReflectionProbeComponent::ContentSource::StandIn;
        }
        else {
            runtime.contentSource = Component::ReflectionProbeComponent::ContentSource::None;
            state->registry.Remove<Component::ReflectionProbeLoadRequest>(entity);
            continue;
        }
        if (runtime.contentHandle.IsValid()) { started.PushBack(entity); }
    }
    for (const entt::entity entity : started) {
        state->registry.Remove<Component::ReflectionProbeLoadRequest>(entity);
        state->registry.Set<Component::ReflectionProbeLoadingFlag>(entity);
    }
}

void ReflectionProbeBakeUpgrade(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    struct ClaimedProbeId
    {
        uint64_t id;
        entt::entity owner;
    };
    Core::InlineVector<ClaimedProbeId, 64> claimedProbeIds{};
    for (auto [entity, probe, runtime] : state->registry.View<Component::ReflectionProbeComponent, Component::ReflectionProbeRuntime>().each()) {
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
    for (auto [entity, probe, runtime] : state->registry.View<Engine::Mut<Component::ReflectionProbeComponent>, Engine::Mut<Component::ReflectionProbeRuntime>>().each()) {
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
                ctx->assetManager->UnloadCubemap(runtime.contentHandle);
                runtime.contentHandle = Engine::CubemapHandle::INVALID;
            }
            runtime.contentSource = Component::ReflectionProbeComponent::ContentSource::None;
            state->registry.Remove<Component::ReflectionProbeLoadingFlag>(entity);
            state->registry.Set<Component::ReflectionProbeLoadRequest>(entity);
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

    for (auto [entity, probe, runtime] : state->registry.View<Component::ReflectionProbeComponent, Engine::Mut<Component::ReflectionProbeRuntime>>().each()) {
        if (runtime.contentSource == Component::ReflectionProbeComponent::ContentSource::Baked) { continue; }
        if (!ctx->assetManager->GetProbeInfo(Engine::ProbeID{probe.probeId})) { continue; }
        if (runtime.contentHandle.IsValid()) {
            ctx->assetManager->UnloadCubemap(runtime.contentHandle);
            runtime.contentHandle = Engine::CubemapHandle::INVALID;
        }
        state->registry.Remove<Component::ReflectionProbeLoadingFlag>(entity);
        state->registry.Set<Component::ReflectionProbeLoadRequest>(entity);
    }
}

void ReflectionProbeLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::ReflectionProbeRuntime, Component::ReflectionProbeLoadingFlag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, runtime] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(runtime.contentHandle);
        if (!cubemap) {
            resolved.PushBack(entity);
            continue;
        }
        if (cubemap->loadState == Render::Cubemap::LoadState::Loading) { continue; }
        resolved.PushBack(entity);
    }
    for (const entt::entity entity : resolved) {
        state->registry.Remove<Component::ReflectionProbeLoadingFlag>(entity);
    }
}

static bool HasEmissiveLightFlag(const Engine::Registry& registry, entt::entity entity)
{
    const auto* renderFlags = registry.TryGet<Component::RenderFlagsComponent>(entity);
    return renderFlags && renderFlags->bEmissiveLight;
}

static bool IsBakeStatic(const Engine::Registry& registry, entt::entity entity, const Component::RenderFlagsComponent& renderFlags)
{
    const auto* transform = registry.TryGet<Component::TransformComponent>(entity);
    if (!transform || transform->mobility != Component::Mobility::Static || registry.Has<Component::DynamicPhysicsBodyRuntime>(entity)) { return false; }
    const auto* body = registry.TryGet<Component::PhysicsBodyDesc>(entity);
    return body == nullptr || body->motionType == Component::PhysicsMotionType::Static;
}

static uint32_t InstanceFlagsFrom(const Engine::Registry& registry, entt::entity entity, const Component::RenderFlagsComponent& renderFlags)
{
    return (renderFlags.bMotionBlur ? INSTANCE_FLAG_MOTION_BLUR : 0u)
           | (renderFlags.bAlphaCutout ? INSTANCE_FLAG_ALPHA_CUTOUT : 0u)
           | (renderFlags.bDdgiContribute ? INSTANCE_FLAG_DDGI_VISIBLE : 0u)
           | (renderFlags.bCameraMotionBlur ? INSTANCE_FLAG_CAMERA_MOTION_BLUR : 0u)
           | (IsBakeStatic(registry, entity, renderFlags) ? INSTANCE_FLAG_BAKE_STATIC : 0u);
}

void EvaluateInstanceRenderState(Engine::EngineState* state, entt::entity entity)
{
    auto* runtime = state->registry.TryGet<Component::MeshRuntime>(entity);
    const auto* renderFlags = state->registry.TryGet<Component::RenderFlagsComponent>(entity);
    if (!runtime || !renderFlags) { return; }

    if (!runtime->range.IsValid()) { return; }

    const bool bVisible = renderFlags->bVisible &&!state->registry.Has<Component::ProbeBakeHiddenFlag>(entity);
    const uint32_t flags = InstanceFlagsFrom(state->registry, entity, *renderFlags);

    const Engine::InstanceSource& src = state->instanceStore[runtime->range.offset];
    if (src.bVisible == bVisible && src.flags == flags && src.stableId == runtime->stableId) { return; }
    state->instanceStore.SetRenderState(runtime->range, bVisible, flags, runtime->stableId);
}

void EvaluateAllInstanceRenderStates(Engine::EngineState* state)
{
    ZoneScoped;
    Engine::InstanceStore& store = state->instanceStore;
    const bool bAnyHideTags = state->registry.View<Component::ProbeBakeHiddenFlag>().size() > 0;

    for (auto [entity, renderFlags, runtime] : state->registry.View<Component::RenderFlagsComponent, Component::MeshRuntime>().each()) {
        if (!runtime.range.IsValid()) { continue; }

        bool bVisible = renderFlags.bVisible;
        if (bVisible && bAnyHideTags) {
            bVisible = !state->registry.Has<Component::ProbeBakeHiddenFlag>(entity);
        }
        const uint32_t flags = InstanceFlagsFrom(state->registry, entity, renderFlags);

        const Engine::InstanceSource& src = store[runtime.range.offset];
        if (src.bVisible == bVisible && src.flags == flags && src.stableId == runtime.stableId) { continue; }
        store.SetRenderState(runtime.range, bVisible, flags, runtime.stableId);
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

void LightSurfaceResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::LightSurfaceRequest>();
    if (view.size() == 0) { return; }

    const Engine::Material* defaultMaterial = ctx->materialManager->GetMaterial(ctx->materialManager->GetDefaultMaterialID());
    if (!defaultMaterial) { return; }

    const size_t budget = std::min(view.size(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const entt::entity entity : view) {
        if (resolved.Size() >= budget) { break; }
        const auto* areaLight = state->registry.TryGet<Component::AreaLightComponent>(entity);
        const auto* sphereLight = state->registry.TryGet<Component::SphereLightComponent>(entity);
        if (!areaLight && !sphereLight) {
            resolved.PushBack(entity);
            continue;
        }

        const Engine::StaticModelHandle handle = areaLight
            ? (areaLight->bDisk ? state->builtinAssets.GetUnitDisk(ctx->assetManager) : state->builtinAssets.GetUnitQuad(ctx->assetManager))
            : state->builtinAssets.GetUnitSphere(ctx->assetManager);
        Engine::StaticModel* model = ctx->assetManager->GetModel(handle);
        if (!model || model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) { continue; }

        Engine::Material emissive = *defaultMaterial; // black albedo so only emission shows
        emissive.props.colorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        emissive.props.emissiveFactor = areaLight ? glm::vec4(areaLight->color, areaLight->intensity) : glm::vec4(sphereLight->color, sphereLight->intensity);
        const Engine::MaterialID materialID = ctx->materialManager->CreateSynthesizedMaterial(emissive);

        glm::mat4 m(1.0f);
        if (state->registry.Has<Component::TransformComponent>(entity)) {
            const Transform world = Component::ComputeWorldTransform(state->registry, entity);
            m = areaLight ? Component::ComputeAreaLightQuadMatrix(world, *areaLight) : Component::ComputeSphereLightMatrix(world, *sphereLight);
        }

        state->registry.Remove<Component::LightSurfaceRuntime>(entity); // frees any stale ranges via OnDestroy
        Engine::ModelStore::Range modelRange = state->modelStore.Allocate(1);
        if (!modelRange.IsValid()) {
            resolved.PushBack(entity);
            continue;
        }
        const Engine::InstanceStore::Range range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, model, materialID, modelRange.offset, false);
        if (!range.IsValid()) {
            state->modelStore.Free(modelRange);
            resolved.PushBack(entity);
            continue;
        }
        state->modelStore.SetModel(modelRange.offset, {m, m});
        state->instanceStore.SetLightIndex(range.offset, Component::FindLightSlot(state->registry, entity));
        state->registry.Add<Component::LightSurfaceRuntime>(entity, range, modelRange, materialID);
        resolved.PushBack(entity);
    }

    for (const entt::entity entity : resolved) {
        state->registry.Remove<Component::LightSurfaceRequest>(entity);
    }
}

void StaticMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::StaticMeshComponent, Component::StaticMeshLoadingFlag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    int32_t modelsWaitingThisTick{0};

    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.TryGetMut<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        Engine::MaterialManager* materialManager = ctx->materialManager;
        Engine::InstanceStore& store = state->instanceStore;

        auto releaseExisting = [&] {
            store.ReleaseAndFree(materialManager,runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(TargetModel(*runtime));
        if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            if (!model) { LOG_ERROR(Engine, "Model ({}) is not in the asset manager after a load request.", TargetModel(*runtime).index); }
            if (!DropPendingModel(ctx, *runtime)) { releaseExisting(); }
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            modelsWaitingThisTick++;
            continue;
        }
        PromotePendingModel(ctx, *runtime);

        const Core::HeapArray<Engine::Node>& nodes = model->modelData.nodes;
        Core::HeapArray<Engine::MeshInformation>& meshes = model->modelData.meshes;

        const auto* overrides = state->registry.TryGet<Component::StaticMeshOverridesComponent>(entity);

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
            resolved.PushBack(entity);
            continue;
        }

        releaseExisting();

        Engine::ModelStore::Range modelRange = state->modelStore.Allocate(contributingNodes);
        if (!modelRange.IsValid()) {
            resolved.PushBack(entity);
            continue;
        }

        Engine::InstanceStore::Range range = store.Allocate(totalPrimitives);
        if (!range.IsValid()) {
            LOG_ERROR(Engine, "Instance store full; cannot flatten model ({}) needing {} primitives", model->name.c_str(), totalPrimitives);
            state->modelStore.Free(modelRange);
            resolved.PushBack(entity);
            continue;
        }

        auto applyShaderOverrides = [&](Engine::Material mat) -> Engine::Material {
            if (meshComponent.shadingShaderOverride) { mat.fragmentShader = meshComponent.shadingShaderOverride; }
            if (meshComponent.lightingShaderOverride) { mat.lightingShader = meshComponent.lightingShaderOverride; }
            return mat;
        };

        const bool bEmissiveLight = HasEmissiveLightFlag(state->registry, entity);
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
                    if (AuthoredMaterialExists(state->registry, materialManager, entity, materialOverride)) {
                        matID = materialOverride;
                    }
                    else {
                        matID = materialManager->CreateSynthesizedMaterial(applyShaderOverrides(model->modelData.materials[primitive.materialIndex]));
                    }
                }

                store.FillEntry(writeIndex, materialManager,model, primitive, {
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
        EvaluateInstanceRenderState(state, entity);
        state->registry.Set<Component::MultiframeDirtyRequest>(entity);
        resolved.PushBack(entity);
    }

    if (modelsWaitingThisTick > 0) {
        state->assetLoad.pendingModelWaitCount += modelsWaitingThisTick;
        state->assetLoad.modelWaitLastActivity = std::chrono::steady_clock::now();
    }
    if (state->assetLoad.pendingModelWaitCount > 0 && modelsWaitingThisTick == 0 && (std::chrono::steady_clock::now() - state->assetLoad.modelWaitLastActivity) >= std::chrono::seconds(1)) {
        LOG_TRACE(Engine, "{} model(s) not yet done loading", state->assetLoad.pendingModelWaitCount);
        state->assetLoad.pendingModelWaitCount = 0;
    }

    for (const auto entity : resolved) {
        state->registry.Remove<Component::StaticMeshLoadingFlag>(entity);
    }
}

void StaticMeshPrimitivePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadRequest>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        if (ctx->assetManager->IsModelFrozen(meshComponent.modelId)) { continue; }
        const Engine::StaticModelHandle handle = ctx->assetManager->LoadModel(meshComponent.modelId);
        if (!handle.IsValid()) { continue; }
        StageModel(ctx, state->registry.GetOrAdd<Component::MeshRuntime>(entity), handle);
        started.PushBack(entity);
    }
    for (const entt::entity entity : started) {
        state->registry.Remove<Component::StaticMeshPrimitiveLoadRequest>(entity);
        state->registry.Set<Component::StaticMeshPrimitiveLoadingFlag>(entity);
    }
}

void StaticMeshPrimitiveLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadingFlag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) { return; }

    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.TryGetMut<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        Engine::MaterialManager* materialManager = ctx->materialManager;

        auto releaseExisting = [&] {
            state->instanceStore.ReleaseAndFree(materialManager,runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(TargetModel(*runtime));
        if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            if (!model) { LOG_ERROR(Engine, "Model ({}) is not in the asset manager after a load request.", TargetModel(*runtime).index); }
            if (!DropPendingModel(ctx, *runtime)) { releaseExisting(); }
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }
        PromotePendingModel(ctx, *runtime);

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
            resolved.PushBack(entity);
            continue;
        }

        releaseExisting();

        auto applyShaderOverrides = [&](Engine::Material mat) -> Engine::Material {
            if (meshComponent.shadingShaderOverride) { mat.fragmentShader = meshComponent.shadingShaderOverride; }
            if (meshComponent.lightingShaderOverride) { mat.lightingShader = meshComponent.lightingShaderOverride; }
            return mat;
        };

        Engine::MaterialID matID;
        if (AuthoredMaterialExists(state->registry, materialManager, entity, meshComponent.materialOverride)) {
            matID = meshComponent.materialOverride;
        }
        else if (targetPrim->materialIndex == -1) {
            matID = materialManager->GetDefaultMaterialID();
        }
        else {
            matID = materialManager->CreateSynthesizedMaterial(applyShaderOverrides(model->modelData.materials[targetPrim->materialIndex]));
        }

        Engine::InstanceStore& store = state->instanceStore;
        Engine::ModelStore::Range modelRange = state->modelStore.Allocate(1);
        if (!modelRange.IsValid()) {
            resolved.PushBack(entity);
            continue;
        }
        Engine::InstanceStore::Range range = store.Allocate(1);
        if (!range.IsValid()) {
            LOG_ERROR(Engine, "Instance store full; cannot resolve split primitive");
            state->modelStore.Free(modelRange);
            resolved.PushBack(entity);
            continue;
        }

        store.FillEntry(range.offset, materialManager,model, *targetPrim, {
                            .material = matID,
                            .modelSlot = modelRange.offset,
                            .materialSlot = targetPrim->materialIndex,
                            .sourceNodeIndex = targetNode,
                            .modelPrimitiveOrdinal = meshComponent.primitiveOrdinal,
                            .bEmissiveLight = HasEmissiveLightFlag(state->registry, entity),
                        });
        runtime->range = range;
        runtime->modelRange = modelRange;
        EvaluateInstanceRenderState(state, entity);
        state->registry.Set<Component::MultiframeDirtyRequest>(entity);

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.Remove<Component::StaticMeshPrimitiveLoadingFlag>(entity);
    }
}

void ProceduralMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadRequest>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        const Engine::StaticModelHandle handle = ctx->assetManager->LoadProceduralModel(meshComponent.params, meshComponent.repeat);
        if (!handle.IsValid()) { continue; }
        StageModel(ctx, state->registry.GetOrAdd<Component::MeshRuntime>(entity), handle);
        started.PushBack(entity);
    }
    for (const entt::entity entity : started) {
        state->registry.Remove<Component::ProceduralMeshLoadRequest>(entity);
        state->registry.Set<Component::ProceduralMeshLoadingFlag>(entity);
    }
}

void ProceduralMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadingFlag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }

    int32_t proceduralWaitingThisTick{0};

    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.TryGetMut<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto releaseExisting = [&] {
            state->instanceStore.ReleaseAndFree(ctx->materialManager,runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(TargetModel(*runtime));
        if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            if (!model) { LOG_ERROR(Engine, "Procedural model ({}) is not in the asset manager, it should have been requested to load during scene load.", TargetModel(*runtime).index); }
            if (!DropPendingModel(ctx, *runtime)) { releaseExisting(); }
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            proceduralWaitingThisTick++;
            continue;
        }
        PromotePendingModel(ctx, *runtime);

        Engine::MaterialID matID = ctx->materialManager->GetDefaultMaterialID();
        if (AuthoredMaterialExists(state->registry, ctx->materialManager, entity, meshComponent.material)) {
            matID = meshComponent.material;
        }
        releaseExisting();
        runtime->modelRange = state->modelStore.Allocate(1);
        if (runtime->modelRange.IsValid()) {
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager,model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(state->registry, entity));
            EvaluateInstanceRenderState(state, entity);
            state->registry.Set<Component::MultiframeDirtyRequest>(entity);
        }

        resolved.PushBack(entity);
    }

    if (proceduralWaitingThisTick > 0) {
        state->assetLoad.pendingProceduralWaitCount += proceduralWaitingThisTick;
        state->assetLoad.proceduralWaitLastActivity = std::chrono::steady_clock::now();
    }
    if (state->assetLoad.pendingProceduralWaitCount > 0 && proceduralWaitingThisTick == 0 && (std::chrono::steady_clock::now() - state->assetLoad.proceduralWaitLastActivity) >= std::chrono::seconds(1)) {
        LOG_TRACE(Engine, "{} procedural model(s) not yet done loading", state->assetLoad.pendingProceduralWaitCount);
        state->assetLoad.pendingProceduralWaitCount = 0;
    }

    for (const auto entity : resolved) {
        state->registry.Remove<Component::ProceduralMeshLoadingFlag>(entity);
    }
}

static void FillModuleMeshRange(Engine::EngineContext* ctx, Engine::EngineState* state, Component::MeshRuntime* runtime, Engine::StaticModel* model, const Component::ModuleMeshComponent& meshComponent, bool bEmissiveLight)
{
    state->instanceStore.ReleaseAndFree(ctx->materialManager,runtime->range);
    state->modelStore.Free(runtime->modelRange);

    if (model->modelData.meshes.IsEmpty()) { return; }
    Engine::MeshInformation& mesh = model->modelData.meshes[0];
    const auto count = static_cast<uint32_t>(mesh.primitiveProperties.Size());
    if (count == 0) { return; }

    Engine::ModelStore::Range modelRange = state->modelStore.Allocate(1);
    if (!modelRange.IsValid()) { return; }

    Engine::InstanceStore& store = state->instanceStore;
    Engine::InstanceStore::Range range = store.Allocate(count);
    if (!range.IsValid()) {
        LOG_ERROR(Engine, "Instance store full; cannot resolve module runtime for model ({})", model->name.c_str());
        state->modelStore.Free(modelRange);
        return;
    }

    uint32_t writeIndex = range.offset;
    for (uint32_t j = 0; j < count; ++j) {
        Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[j];
        // materialIndex carries the module slot (see FinalizeGeometryGroups)
        Engine::MaterialID matID = ctx->materialManager->GetDefaultMaterialID();
        if (primitive.materialIndex >= 0 && primitive.materialIndex < Engine::MAX_MODULE_SLOTS) {
            const Engine::MaterialID slotMat = meshComponent.slotMaterials[primitive.materialIndex];
            if (slotMat.IsValid() && ctx->materialManager->DoesMutableMaterialExist(slotMat)) {
                matID = slotMat;
            }
        }
        store.FillEntry(writeIndex, ctx->materialManager,model, primitive, {
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

void ModuleMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::ModuleMeshComponent, Component::ModuleMeshLoadRequest>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        if (meshComponent.params.parts.IsEmpty()) { continue; }
        const Engine::StaticModelHandle handle = ctx->assetManager->LoadModuleModel(meshComponent.params);
        if (!handle.IsValid()) { continue; }
        StageModel(ctx, state->registry.GetOrAdd<Component::MeshRuntime>(entity), handle);
        started.PushBack(entity);
    }
    for (const entt::entity entity : started) {
        state->registry.Remove<Component::ModuleMeshLoadRequest>(entity);
        state->registry.Set<Component::ModuleMeshLoadingFlag>(entity);
    }
}

void ModuleMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::ModuleMeshComponent, Component::ModuleMeshLoadingFlag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.TryGetMut<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto releaseExisting = [&] {
            state->instanceStore.ReleaseAndFree(ctx->materialManager,runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(TargetModel(*runtime));
        if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            if (!model) { LOG_ERROR(Engine, "Module model ({}) is not in the asset manager.", TargetModel(*runtime).index); }
            if (!DropPendingModel(ctx, *runtime)) { releaseExisting(); }
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }
        PromotePendingModel(ctx, *runtime);

        FillModuleMeshRange(ctx, state, runtime, model, meshComponent, HasEmissiveLightFlag(state->registry, entity));
        EvaluateInstanceRenderState(state, entity);
        state->registry.Set<Component::MultiframeDirtyRequest>(entity);

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.Remove<Component::ModuleMeshLoadingFlag>(entity);
    }
}

/** A railing with no lanes and no posts generates nothing; cross planks need two lanes. */
static bool SplineHasGeometry(const Engine::SplineParams& params)
{
    return !params.railing.bEnabled || !params.railing.lanes.IsEmpty() || params.railing.bPosts;
}

void SplineMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::SplineMeshComponent, Component::SplineMeshLoadRequest>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    auto emptied = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, meshComponent] : view.each()) {
        if (started.Size() + emptied.Size() >= budget) { break; }
        const Engine::SplineParams params = Component::ToSplineParams(meshComponent);
        if (!SplineHasGeometry(params)) {
            emptied.PushBack(entity);
            continue;
        }
        const Engine::StaticModelHandle handle = ctx->assetManager->LoadSplineModel(params);
        if (!handle.IsValid()) { continue; }
        StageModel(ctx, state->registry.GetOrAdd<Component::MeshRuntime>(entity), handle);
        started.PushBack(entity);
    }
    for (const entt::entity entity : started) {
        state->registry.Remove<Component::SplineMeshLoadRequest>(entity);
        state->registry.Set<Component::SplineMeshLoadingFlag>(entity);
    }
    for (const entt::entity entity : emptied) {
        state->registry.Remove<Component::SplineMeshLoadRequest>(entity);
        state->registry.Remove<Component::MeshRuntime>(entity);
    }
}

void SplineMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::SplineMeshComponent, Component::SplineMeshLoadingFlag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.TryGetMut<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto releaseExisting = [&] {
            state->instanceStore.ReleaseAndFree(ctx->materialManager,runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(TargetModel(*runtime));
        if (!model || model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            if (!model) { LOG_ERROR(Engine, "Spline model ({}) is not in the asset manager.", TargetModel(*runtime).index); }
            if (!DropPendingModel(ctx, *runtime)) { releaseExisting(); }
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }
        PromotePendingModel(ctx, *runtime);

        Engine::MaterialID matID = ctx->materialManager->GetDefaultMaterialID();
        if (AuthoredMaterialExists(state->registry, ctx->materialManager, entity, meshComponent.material)) {
            matID = meshComponent.material;
        }
        releaseExisting();
        runtime->modelRange = state->modelStore.Allocate(1);
        if (runtime->modelRange.IsValid()) {
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager,model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(state->registry, entity));
            EvaluateInstanceRenderState(state, entity);
            state->registry.Set<Component::MultiframeDirtyRequest>(entity);
        }

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.Remove<Component::SplineMeshLoadingFlag>(entity);
    }
}

void Text3DGeneratePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::Text3DComponent, Component::Text3DGenerateRequest>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto done = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, textComponent] : view.each()) {
        if (done.Size() >= budget) { break; }
        // Stay pending while the font is missing or frozen (e.g. mid hot-reload drain). The generated mesh takes its own font ref, so we hold none here.
        if (!textComponent.fontId.IsValid() || ctx->assetManager->IsFontFrozen(textComponent.fontId)) { continue; }

        const Engine::StaticModelHandle handle = textComponent.text.Size() > 0
            ? ctx->assetManager->LoadText3DModel(textComponent.fontId, textComponent.text, textComponent.depth, textComponent.flatness, textComponent.tracking, textComponent.scale, textComponent.bSmoothNormals, textComponent.align, textComponent.anchor, textComponent.wrapWidth, textComponent.bendRadius)
            : Engine::StaticModelHandle::INVALID;
        if (handle.IsValid()) {
            StageModel(ctx, state->registry.GetOrAdd<Component::MeshRuntime>(entity), handle);
            state->registry.Set<Component::Text3DLoadingFlag>(entity);
        }
        else {
            state->registry.Remove<Component::MeshRuntime>(entity);
        }
        done.PushBack(entity);
    }
    for (const entt::entity entity : done) {
        state->registry.Remove<Component::Text3DGenerateRequest>(entity);
    }
}

void Text3DLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::Text3DComponent, Component::Text3DLoadingFlag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, textComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.TryGetMut<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        if (!TargetModel(*runtime).IsValid()) {
            state->instanceStore.ReleaseAndFree(ctx->materialManager,runtime->range);
            state->modelStore.Free(runtime->modelRange);
            resolved.PushBack(entity); // nothing to resolve (e.g. empty text / no font); drop the tag
            continue;
        }

        auto model = ctx->assetManager->GetModel(TargetModel(*runtime));
        if (!model) {
            LOG_ERROR(Engine, "Text3D model ({}) is not in the asset manager.", TargetModel(*runtime).index);
            continue;
        }
        if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            if (!DropPendingModel(ctx, *runtime)) {
                state->instanceStore.ReleaseAndFree(ctx->materialManager,runtime->range);
                state->modelStore.Free(runtime->modelRange);
            }
            resolved.PushBack(entity); // generation failed (e.g. empty/whitespace text); stop waiting so editing unlocks
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }
        PromotePendingModel(ctx, *runtime);

        Engine::MaterialID matID = ctx->materialManager->GetDefaultMaterialID();
        if (AuthoredMaterialExists(state->registry, ctx->materialManager, entity, textComponent.material)) {
            matID = textComponent.material;
        }
        state->instanceStore.ReleaseAndFree(ctx->materialManager,runtime->range);
        state->modelStore.Free(runtime->modelRange);
        runtime->modelRange = state->modelStore.Allocate(1);
        if (runtime->modelRange.IsValid()) {
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager,model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(state->registry, entity));
            EvaluateInstanceRenderState(state, entity);
            state->registry.Set<Component::MultiframeDirtyRequest>(entity);
        }

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.Remove<Component::Text3DLoadingFlag>(entity);
    }
}

void TextFontPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.View<Component::TextComponent, Engine::Mut<Component::TextRuntime>, Component::TextFontRequest>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, textComp, runtime] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        // Load here (not at construct) so the freeze can hold it pending.
        if (!runtime.fontHandle.IsValid()) {
            if (!textComp.fontId.IsValid() || ctx->assetManager->IsFontFrozen(textComp.fontId)) { continue; }
            runtime.fontHandle = ctx->assetManager->LoadFont(textComp.fontId);
            if (!runtime.fontHandle.IsValid()) { continue; }
        }

        Engine::Font* font = ctx->assetManager->GetFont(runtime.fontHandle);
        if (!font) { continue; }
        if (font->loadState == Engine::Font::LoadState::FailedToLoad) {
            resolved.PushBack(entity);
            continue;
        }
        if (font->loadState != Engine::Font::LoadState::Loaded) { continue; }

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.Remove<Component::TextFontRequest>(entity);
    }
}
} // Engine
