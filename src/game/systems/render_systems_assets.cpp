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
#include "game/fwd_components.h"
#include "game/components/common_components.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/module_mesh_component.h"
#include "game/components/render/text3d_component.h"
#include "game/components/render/light_components.h"
#include "game/components/render/local_ddgi_volume_component.h"
#include "game/components/render/reflection_probe_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/render/text_component.h"
#include "game/components/core_components.h"
#include "game/components/physics/physics_body_desc.h"
#include "render/types/render_types.h"


namespace Game
{
void ConnectRenderObservers(entt::registry& registry)
{
    registry.on_construct<Component::TransformComponent>().connect<&Component::TransformComponent::OnConstruct>();
    registry.on_destroy<Component::TransformComponent>().connect<&Component::TransformComponent::OnDestroy>();

    registry.on_construct<Component::MeshRuntime>().connect<&Component::MeshRuntime::OnConstruct>();
    registry.on_destroy<Component::MeshRuntime>().connect<&Component::MeshRuntime::OnDestroy>();
    registry.on_construct<Component::LightSurfaceRuntime>().connect<&Component::LightSurfaceRuntime::OnConstruct>();
    registry.on_destroy<Component::LightSurfaceRuntime>().connect<&Component::LightSurfaceRuntime::OnDestroy>();
    registry.on_destroy<Component::TextRuntime>().connect<&Component::TextRuntime::OnDestroy>();

    registry.on_construct<Component::StaticMeshComponent>().connect<&Component::StaticMeshComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshComponent>().connect<&Component::StaticMeshComponent::OnDestroy>();

    registry.on_construct<Component::StaticMeshPrimitiveComponent>().connect<&Component::StaticMeshPrimitiveComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshPrimitiveComponent>().connect<&Component::StaticMeshPrimitiveComponent::OnDestroy>();

    registry.on_construct<Component::ProceduralMeshComponent>().connect<&Component::ProceduralMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ProceduralMeshComponent>().connect<&Component::ProceduralMeshComponent::OnDestroy>();

    registry.on_construct<Component::SplineMeshComponent>().connect<&Component::SplineMeshComponent::OnConstruct>();
    registry.on_destroy<Component::SplineMeshComponent>().connect<&Component::SplineMeshComponent::OnDestroy>();

    registry.on_construct<Component::ModuleMeshComponent>().connect<&Component::ModuleMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ModuleMeshComponent>().connect<&Component::ModuleMeshComponent::OnDestroy>();

    registry.on_construct<Component::TextComponent>().connect<&Component::TextComponent::OnConstruct>();
    registry.on_destroy<Component::TextComponent>().connect<&Component::TextComponent::OnDestroy>();

    registry.on_construct<Component::Text3DComponent>().connect<&Component::Text3DComponent::OnConstruct>();
    registry.on_destroy<Component::Text3DComponent>().connect<&Component::Text3DComponent::OnDestroy>();

    registry.on_construct<Component::AreaLightComponent>().connect<&Component::AreaLightComponent::OnConstruct>();
    registry.on_destroy<Component::AreaLightComponent>().connect<&Component::AreaLightComponent::OnDestroy>();

    registry.on_construct<Component::SphereLightComponent>().connect<&Component::SphereLightComponent::OnConstruct>();
    registry.on_destroy<Component::SphereLightComponent>().connect<&Component::SphereLightComponent::OnDestroy>();

    registry.on_construct<Component::ReflectionProbeComponent>().connect<&Component::ReflectionProbeComponent::OnConstruct>();
    registry.on_destroy<Component::ReflectionProbeComponent>().connect<&Component::ReflectionProbeComponent::OnDestroy>();

    registry.on_construct<Component::LocalDDGIVolumeComponent>().connect<&Component::LocalDDGIVolumeComponent::OnConstruct>();

    registry.on_construct<Component::SkyboxComponent>().connect<&Component::SkyboxComponent::OnConstruct>();
    registry.on_destroy<Component::SkyboxComponent>().connect<&Component::SkyboxComponent::OnDestroy>();
}

void DisconnectRenderObservers(entt::registry& registry)
{
    registry.on_construct<Component::TransformComponent>().disconnect<&Component::TransformComponent::OnConstruct>();
    registry.on_destroy<Component::TransformComponent>().disconnect<&Component::TransformComponent::OnDestroy>();

    registry.on_construct<Component::MeshRuntime>().disconnect<&Component::MeshRuntime::OnConstruct>();
    registry.on_destroy<Component::MeshRuntime>().disconnect<&Component::MeshRuntime::OnDestroy>();
    registry.on_construct<Component::LightSurfaceRuntime>().disconnect<&Component::LightSurfaceRuntime::OnConstruct>();
    registry.on_destroy<Component::LightSurfaceRuntime>().disconnect<&Component::LightSurfaceRuntime::OnDestroy>();
    registry.on_destroy<Component::TextRuntime>().disconnect<&Component::TextRuntime::OnDestroy>();

    registry.on_construct<Component::StaticMeshComponent>().disconnect<&Component::StaticMeshComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshComponent>().disconnect<&Component::StaticMeshComponent::OnDestroy>();

    registry.on_construct<Component::StaticMeshPrimitiveComponent>().disconnect<&Component::StaticMeshPrimitiveComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshPrimitiveComponent>().disconnect<&Component::StaticMeshPrimitiveComponent::OnDestroy>();

    registry.on_construct<Component::ProceduralMeshComponent>().disconnect<&Component::ProceduralMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ProceduralMeshComponent>().disconnect<&Component::ProceduralMeshComponent::OnDestroy>();

    registry.on_construct<Component::SplineMeshComponent>().disconnect<&Component::SplineMeshComponent::OnConstruct>();
    registry.on_destroy<Component::SplineMeshComponent>().disconnect<&Component::SplineMeshComponent::OnDestroy>();

    registry.on_construct<Component::ModuleMeshComponent>().disconnect<&Component::ModuleMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ModuleMeshComponent>().disconnect<&Component::ModuleMeshComponent::OnDestroy>();

    registry.on_construct<Component::TextComponent>().disconnect<&Component::TextComponent::OnConstruct>();
    registry.on_destroy<Component::TextComponent>().disconnect<&Component::TextComponent::OnDestroy>();

    registry.on_construct<Component::Text3DComponent>().disconnect<&Component::Text3DComponent::OnConstruct>();
    registry.on_destroy<Component::Text3DComponent>().disconnect<&Component::Text3DComponent::OnDestroy>();

    registry.on_construct<Component::AreaLightComponent>().disconnect<&Component::AreaLightComponent::OnConstruct>();
    registry.on_destroy<Component::AreaLightComponent>().disconnect<&Component::AreaLightComponent::OnDestroy>();

    registry.on_construct<Component::SphereLightComponent>().disconnect<&Component::SphereLightComponent::OnConstruct>();
    registry.on_destroy<Component::SphereLightComponent>().disconnect<&Component::SphereLightComponent::OnDestroy>();

    registry.on_construct<Component::ReflectionProbeComponent>().disconnect<&Component::ReflectionProbeComponent::OnConstruct>();
    registry.on_destroy<Component::ReflectionProbeComponent>().disconnect<&Component::ReflectionProbeComponent::OnDestroy>();

    registry.on_construct<Component::LocalDDGIVolumeComponent>().disconnect<&Component::LocalDDGIVolumeComponent::OnConstruct>();

    registry.on_construct<Component::SkyboxComponent>().disconnect<&Component::SkyboxComponent::OnConstruct>();
    registry.on_destroy<Component::SkyboxComponent>().disconnect<&Component::SkyboxComponent::OnDestroy>();
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

    for (auto [entity, smc] : state->registry.view<Component::StaticMeshComponent>().each()) {
        if (!state->registry.all_of<Component::MeshRuntime>(entity)) { continue; }
        if (!isHot(smc.modelId)) { continue; }
        Component::UnloadStaticMesh(state->registry, entity);
        Component::LoadStaticMesh(smc, state->registry, entity);
    }

    // Physics holds its own model ref (its serialized key, loaded independently), so it must also release + re-arm or the model can never drain.
    for (auto [entity, bodyDesc] : state->registry.view<Component::PhysicsBodyDesc>().each()) {
        bool affected = false;
        for (auto& shape : bodyDesc.shapes) {
            if (shape.meshSourceModelId.IsValid() && isHot(shape.meshSourceModelId)) {
                if (shape.colliderHandle.IsValid()) {
                    ctx->assetManager->UnloadCollider(shape.colliderHandle);
                    shape.colliderHandle = {};
                    affected = true;
                }
            }
        }
        if (!affected) { continue; }

        state->registry.remove<Component::PhysicsMeshLoadingTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsMeshTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsShapeCreationTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsBodyCreationTag>(entity);
        state->assetLoad.bPendingModelResolve = true;
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
            LOG_WARN(Game, "UI font hot-reload ignored; restart required to apply changes to the UI font.");
            continue;
        }
        if (ctx->assetManager->IsFontResident(hotId)) {
            ctx->assetManager->FreezeFont(hotId);
        }
    }

    for (auto [entity, textComp, runtime] : state->registry.view<Component::TextComponent, Component::TextRuntime>().each()) {
        if (!isHot(textComp.fontId)) { continue; }
        Component::UnloadTextComponent(textComp, state->registry, entity);
        Component::LoadTextComponent(textComp, state->registry, entity);
    }
    for (auto [entity, text3DComp] : state->registry.view<Component::Text3DComponent>().each()) {
        if (!isHot(text3DComp.fontId)) { continue; }
        Component::UnloadText3DFont(state->registry, entity);
        Component::LoadText3DFont(text3DComp, state->registry, entity);
    }

    // A Text3D physics collider holds its own ref to the generated mesh (keyed on the font), so it must release + re-arm too or that mesh can never drain.
    for (auto [entity, bodyDesc] : state->registry.view<Component::PhysicsBodyDesc>().each()) {
        bool affected = false;
        for (auto& shape : bodyDesc.shapes) {
            if (shape.text3DSource.IsValid() && isHot(shape.text3DSource.fontId)) {
                if (shape.colliderHandle.IsValid()) {
                    ctx->assetManager->UnloadCollider(shape.colliderHandle);
                    shape.colliderHandle = {};
                    affected = true;
                }
            }
        }
        if (!affected) { continue; }

        state->registry.remove<Component::PhysicsMeshLoadingTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsMeshTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsShapeCreationTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsBodyCreationTag>(entity);
        state->assetLoad.bPendingModelResolve = true;
    }

    state->assetLoad.pendingHotReloadFontIds.Clear();
}

void TextureHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->assetLoad.pendingHotReloadTextureIds.IsEmpty()) { return; }

    for (auto hotId : state->assetLoad.pendingHotReloadTextureIds) {
        if (ctx->assetManager->IsTextureLoaded(hotId)) {
            ctx->assetManager->ReloadTexture(hotId);
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

void StaticMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::StaticMeshComponent, Component::StaticMeshLoadPendingTag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        if (ctx->assetManager->IsModelFrozen(meshComponent.modelId)) { continue; } // stay pending while frozen
        auto& runtime = state->registry.get_or_emplace<Component::MeshRuntime>(entity);
        runtime.modelHandle = ctx->assetManager->LoadModel(meshComponent.modelId);
        if (runtime.modelHandle.IsValid()) { started.PushBack(entity); }
    }
    for (const entt::entity entity : started) {
        state->registry.remove<Component::StaticMeshLoadPendingTag>(entity);
        state->registry.emplace_or_replace<Component::StaticMeshLoadingTag>(entity);
    }
}

void ReflectionProbePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::ReflectionProbeComponent, Component::ReflectionProbeLoadPendingTag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, probe] : view.each()) {
        if (started.Size() >= budget) { break; }
        if (ctx->assetManager->GetProbeInfo(Engine::ProbeID{probe.probeId})) {
            probe.contentHandle = ctx->assetManager->LoadProbe(Engine::ProbeID{probe.probeId});
            probe.contentSource = Component::ReflectionProbeComponent::ContentSource::Baked;
        }
        else if (probe.standInEnvMap.IsValid()) {
            probe.contentHandle = ctx->assetManager->LoadCubemap(probe.standInEnvMap);
            probe.contentSource = Component::ReflectionProbeComponent::ContentSource::StandIn;
        }
        else {
            probe.contentSource = Component::ReflectionProbeComponent::ContentSource::None;
            state->registry.remove<Component::ReflectionProbeLoadPendingTag>(entity);
            continue;
        }
        if (probe.contentHandle.IsValid()) { started.PushBack(entity); }
    }
    for (const entt::entity entity : started) {
        state->registry.remove<Component::ReflectionProbeLoadPendingTag>(entity);
        state->registry.emplace_or_replace<Component::ReflectionProbeLoadingTag>(entity);
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
    for (auto [entity, probe] : state->registry.view<Component::ReflectionProbeComponent>().each()) {
        if (probe.contentSource != Component::ReflectionProbeComponent::ContentSource::Baked || probe.probeId == 0) { continue; }
        bool bTaken = false;
        for (const ClaimedProbeId& claimed : claimedProbeIds) {
            if (claimed.id == probe.probeId) {
                bTaken = true;
                break;
            }
        }
        if (!bTaken && !claimedProbeIds.IsFull()) { claimedProbeIds.PushBack({probe.probeId, entity}); }
    }
    for (auto [entity, probe] : state->registry.view<Component::ReflectionProbeComponent>().each()) {
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
            if (probe.contentHandle.IsValid()) {
                ctx->assetManager->UnloadCubemap(probe.contentHandle);
                probe.contentHandle = Engine::CubemapHandle::INVALID;
            }
            probe.contentSource = Component::ReflectionProbeComponent::ContentSource::None;
            state->registry.remove<Component::ReflectionProbeLoadingTag>(entity);
            state->registry.emplace_or_replace<Component::ReflectionProbeLoadPendingTag>(entity);
            state->assetLoad.bPendingModelResolve |= true;
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

    for (auto [entity, probe] : state->registry.view<Component::ReflectionProbeComponent>().each()) {
        if (probe.contentSource == Component::ReflectionProbeComponent::ContentSource::Baked) { continue; }
        if (!ctx->assetManager->GetProbeInfo(Engine::ProbeID{probe.probeId})) { continue; }
        if (probe.contentHandle.IsValid()) {
            ctx->assetManager->UnloadCubemap(probe.contentHandle);
            probe.contentHandle = Engine::CubemapHandle::INVALID;
        }
        state->registry.remove<Component::ReflectionProbeLoadingTag>(entity);
        state->registry.emplace_or_replace<Component::ReflectionProbeLoadPendingTag>(entity);
        state->assetLoad.bPendingModelResolve |= true;
    }
}

void ReflectionProbeLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::ReflectionProbeComponent, Component::ReflectionProbeLoadingTag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, probe] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(probe.contentHandle);
        if (!cubemap) {
            resolved.PushBack(entity);
            continue;
        }
        if (cubemap->loadState == Render::Cubemap::LoadState::Loading) { continue; }
        resolved.PushBack(entity);
    }
    for (const entt::entity entity : resolved) {
        state->registry.remove<Component::ReflectionProbeLoadingTag>(entity);
    }
}

static bool HasEmissiveLightFlag(const entt::registry& registry, entt::entity entity)
{
    const auto* renderFlags = registry.try_get<Component::RenderFlagsComponent>(entity);
    return renderFlags && renderFlags->Has(Component::RenderFlagsComponent::EMISSIVE_LIGHT);
}

static uint32_t InstanceFlagsFrom(const Component::RenderFlagsComponent& renderFlags)
{
    return (renderFlags.Has(Component::RenderFlagsComponent::MOTION_BLUR) ? INSTANCE_FLAG_MOTION_BLUR : 0u)
           | (renderFlags.Has(Component::RenderFlagsComponent::ALPHA_CUTOUT) ? INSTANCE_FLAG_ALPHA_CUTOUT : 0u)
           | (renderFlags.Has(Component::RenderFlagsComponent::DDGI_CONTRIBUTE) ? INSTANCE_FLAG_DDGI_VISIBLE : 0u);
}

void EvaluateInstanceRenderState(Engine::EngineState* state, entt::entity entity)
{
    auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
    const auto* renderFlags = state->registry.try_get<Component::RenderFlagsComponent>(entity);
    if (!runtime || !renderFlags) { return; }

    if (!runtime->range.IsValid()) { return; }

    const bool bVisible = renderFlags->Has(Component::RenderFlagsComponent::VISIBLE) && !state->registry.all_of<Component::ProbeBakeHiddenTag>(entity);
    const uint32_t flags = InstanceFlagsFrom(*renderFlags);

    const Engine::InstanceSource& src = state->instanceStore[runtime->range.offset];
    if (src.bVisible == bVisible && src.flags == flags && src.stableId == runtime->stableId) { return; }
    state->instanceStore.SetRenderState(runtime->range, bVisible, flags, runtime->stableId);
}

void EvaluateAllInstanceRenderStates(Engine::EngineState* state)
{
    ZoneScoped;
    Engine::InstanceStore& store = state->instanceStore;
    const bool bAnyHideTags = state->registry.view<Component::ProbeBakeHiddenTag>().size() > 0;

    for (auto [entity, renderFlags, runtime] : state->registry.view<Component::RenderFlagsComponent, Component::MeshRuntime>().each()) {
        if (!runtime.range.IsValid()) { continue; }

        bool bVisible = renderFlags.Has(Component::RenderFlagsComponent::VISIBLE);
        if (bVisible && bAnyHideTags) {
            bVisible = !state->registry.all_of<Component::ProbeBakeHiddenTag>(entity);
        }
        const uint32_t flags = InstanceFlagsFrom(renderFlags);

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
    auto view = state->registry.view<Component::LightSurfacePendingTag>();
    if (view.size() == 0) { return; }

    const Engine::Material* defaultMaterial = ctx->materialManager->GetMaterial(ctx->materialManager->GetDefaultMaterialID());
    if (!defaultMaterial) { return; }

    const size_t budget = std::min(view.size(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const entt::entity entity : view) {
        if (resolved.Size() >= budget) { break; }
        const auto* areaLight = state->registry.try_get<Component::AreaLightComponent>(entity);
        const auto* sphereLight = state->registry.try_get<Component::SphereLightComponent>(entity);
        if (!areaLight && !sphereLight) {
            resolved.PushBack(entity);
            continue;
        }

        const Engine::StaticModelHandle handle = areaLight ? state->builtinAssets.GetUnitQuad(ctx->assetManager) : state->builtinAssets.GetUnitSphere(ctx->assetManager);
        Engine::StaticModel* model = ctx->assetManager->GetModel(handle);
        if (!model || model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) { continue; }

        Engine::Material emissive = *defaultMaterial; // black albedo so only emission shows
        emissive.props.colorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        emissive.props.emissiveFactor = areaLight ? glm::vec4(areaLight->color, areaLight->intensity) : glm::vec4(sphereLight->color, sphereLight->intensity);
        const Engine::MaterialID materialID = ctx->materialManager->CreateSynthesizedMaterial(emissive);

        const auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
        glm::mat4 m(1.0f);
        if (transform) {
            m = areaLight ? Component::ComputeAreaLightQuadMatrix(*transform, *areaLight) : Component::ComputeSphereLightMatrix(*transform, *sphereLight);
        }

        state->registry.remove<Component::LightSurfaceRuntime>(entity); // frees any stale ranges via OnDestroy
        Engine::ModelStore::Range modelRange = state->modelStore.Allocate(1);
        if (!modelRange.IsValid()) {
            resolved.PushBack(entity);
            continue;
        }
        const Engine::InstanceStore::Range range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, nullptr, model, materialID, modelRange.offset, false);
        if (!range.IsValid()) {
            state->modelStore.Free(modelRange);
            resolved.PushBack(entity);
            continue;
        }
        state->modelStore.SetModel(modelRange.offset, {m, m});
        state->instanceStore.SetLightIndex(range.offset, areaLight ? areaLight->lightSlot : sphereLight->lightSlot);
        state->registry.emplace<Component::LightSurfaceRuntime>(entity, range, modelRange, materialID);
        resolved.PushBack(entity);
    }

    for (const entt::entity entity : resolved) {
        state->registry.remove<Component::LightSurfacePendingTag>(entity);
    }
}

void StaticMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::StaticMeshComponent, Component::StaticMeshLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    int32_t modelsWaitingThisTick{0};

    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        Engine::MaterialManager* materialManager = ctx->materialManager;
        Engine::InstanceStore& store = state->instanceStore;

        auto releaseExisting = [&] {
            store.ReleaseAndFree(materialManager, &state->triLightStore, runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Model ({}) is not in the asset manager after a load request.", runtime->modelHandle.index);
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            modelsWaitingThisTick++;
            continue;
        }

        const Core::HeapArray<Engine::Node>& nodes = model->modelData.nodes;
        Core::HeapArray<Engine::MeshInformation>& meshes = model->modelData.meshes;

        const auto* overrides = state->registry.try_get<Component::StaticMeshOverridesComponent>(entity);

        auto included = [&](uint32_t ordinal) -> bool {
            return !overrides || !overrides->primitiveBlacklist.Contains(ordinal);
        };

        uint32_t totalPrimitives = 0;
        uint32_t contributingNodes = 0;

        //
        {
            uint32_t ordinal = 0;
            for (size_t n = 0; n < nodes.Size(); ++n) {
                const uint32_t mi = nodes[n].meshIndex;
                if (mi == ~0u || mi >= meshes.Size()) { continue; }
                const size_t nodePrimCount = meshes[mi].primitiveProperties.Size();
                uint32_t nodeIncluded = 0;
                for (size_t j = 0; j < nodePrimCount; ++j) {
                    if (included(ordinal)) {
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
            LOG_ERROR(Game, "Instance store full; cannot flatten model ({}) needing {} primitives", model->name.c_str(), totalPrimitives);
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

            const glm::mat4 nodeModelSpace = ComposeNodeModelSpace(nodes, n);
            Engine::MeshInformation& mesh = meshes[mi];

            bool bNodeSlotAssigned = false;
            for (size_t j = 0; j < mesh.primitiveProperties.Size(); ++j) {
                const uint32_t thisOrdinal = ordinal++;
                if (!included(thisOrdinal)) { continue; }
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
                    if (materialOverride.IsValid()) {
                        if (materialManager->DoesMutableMaterialExist(materialOverride)) {
                            matID = materialOverride;
                        }
                        else {
                            matID = materialManager->CreateSynthesizedMaterial(applyShaderOverrides(model->modelData.materials[primitive.materialIndex]));
                            LOG_WARN(Engine, "Mesh was resolved with a material override that does not exist in the registry.");
                        }
                    }
                    else {
                        matID = materialManager->CreateSynthesizedMaterial(applyShaderOverrides(model->modelData.materials[primitive.materialIndex]));
                    }
                }

                store.FillEntry(writeIndex, materialManager, &state->triLightStore, model, primitive, {
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
        state->registry.emplace_or_replace<Component::MultiframeDirtyComponent>(entity);
        resolved.PushBack(entity);
    }

    if (modelsWaitingThisTick > 0) {
        state->assetLoad.pendingModelWaitCount += modelsWaitingThisTick;
        state->assetLoad.modelWaitLastActivity = std::chrono::steady_clock::now();
    }
    if (state->assetLoad.pendingModelWaitCount > 0 && modelsWaitingThisTick == 0 && (std::chrono::steady_clock::now() - state->assetLoad.modelWaitLastActivity) >= std::chrono::seconds(1)) {
        LOG_TRACE(Game, "{} model(s) not yet done loading", state->assetLoad.pendingModelWaitCount);
        state->assetLoad.pendingModelWaitCount = 0;
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::StaticMeshLoadingTag>(entity);
    }
}

void StaticMeshPrimitivePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadPendingTag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        if (ctx->assetManager->IsModelFrozen(meshComponent.modelId)) { continue; }
        auto& runtime = state->registry.get_or_emplace<Component::MeshRuntime>(entity);
        runtime.modelHandle = ctx->assetManager->LoadModel(meshComponent.modelId);
        if (runtime.modelHandle.IsValid()) { started.PushBack(entity); }
    }
    for (const entt::entity entity : started) {
        state->registry.remove<Component::StaticMeshPrimitiveLoadPendingTag>(entity);
        state->registry.emplace_or_replace<Component::StaticMeshPrimitiveLoadingTag>(entity);
    }
}

void StaticMeshPrimitiveLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) { return; }

    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        Engine::MaterialManager* materialManager = ctx->materialManager;

        auto releaseExisting = [&] {
            state->instanceStore.ReleaseAndFree(materialManager, &state->triLightStore, runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Model ({}) is not in the asset manager after a load request.", runtime->modelHandle.index);
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }

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
            LOG_WARN(Game, "StaticMeshPrimitive ordinal {} out of range for model ({})", meshComponent.primitiveOrdinal, model->name.c_str());
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
        if (meshComponent.materialOverride.IsValid() && materialManager->DoesMutableMaterialExist(meshComponent.materialOverride)) {
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
            LOG_ERROR(Game, "Instance store full; cannot resolve split primitive");
            state->modelStore.Free(modelRange);
            resolved.PushBack(entity);
            continue;
        }

        store.FillEntry(range.offset, materialManager, &state->triLightStore, model, *targetPrim, {
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
        state->registry.emplace_or_replace<Component::MultiframeDirtyComponent>(entity);

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::StaticMeshPrimitiveLoadingTag>(entity);
    }
}

void ProceduralMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadPendingTag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        auto& runtime = state->registry.get_or_emplace<Component::MeshRuntime>(entity);
        runtime.modelHandle = ctx->assetManager->LoadProceduralModel(meshComponent.params);
        if (runtime.modelHandle.IsValid()) { started.PushBack(entity); }
    }
    for (const entt::entity entity : started) {
        state->registry.remove<Component::ProceduralMeshLoadPendingTag>(entity);
        state->registry.emplace_or_replace<Component::ProceduralMeshLoadingTag>(entity);
    }
}

void ProceduralMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }

    int32_t proceduralWaitingThisTick{0};

    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto releaseExisting = [&] {
            state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Procedural model ({}) is not in the asset manager, it should have been requested to load during scene load.", runtime->modelHandle.index);
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            proceduralWaitingThisTick++;
            continue;
        }

        Engine::MaterialID matID = ctx->materialManager->GetDefaultMaterialID();
        if (meshComponent.material.IsValid() && ctx->materialManager->DoesMutableMaterialExist(meshComponent.material)) {
            matID = meshComponent.material;
        }
        releaseExisting();
        runtime->modelRange = state->modelStore.Allocate(1);
        if (runtime->modelRange.IsValid()) {
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, &state->triLightStore, model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(state->registry, entity));
            EvaluateInstanceRenderState(state, entity);
            state->registry.emplace_or_replace<Component::MultiframeDirtyComponent>(entity);
        }

        resolved.PushBack(entity);
    }

    if (proceduralWaitingThisTick > 0) {
        state->assetLoad.pendingProceduralWaitCount += proceduralWaitingThisTick;
        state->assetLoad.proceduralWaitLastActivity = std::chrono::steady_clock::now();
    }
    if (state->assetLoad.pendingProceduralWaitCount > 0 && proceduralWaitingThisTick == 0 && (std::chrono::steady_clock::now() - state->assetLoad.proceduralWaitLastActivity) >= std::chrono::seconds(1)) {
        LOG_TRACE(Game, "{} procedural model(s) not yet done loading", state->assetLoad.pendingProceduralWaitCount);
        state->assetLoad.pendingProceduralWaitCount = 0;
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::ProceduralMeshLoadingTag>(entity);
    }
}

static void FillModuleMeshRange(Engine::EngineContext* ctx, Engine::EngineState* state, Component::MeshRuntime* runtime, Engine::StaticModel* model, const Component::ModuleMeshComponent& meshComponent, bool bEmissiveLight)
{
    state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
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
        LOG_ERROR(Game, "Instance store full; cannot resolve module runtime for model ({})", model->name.c_str());
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
        store.FillEntry(writeIndex, ctx->materialManager, &state->triLightStore, model, primitive, {
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
    auto view = state->registry.view<Component::ModuleMeshComponent, Component::ModuleMeshLoadPendingTag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        if (meshComponent.params.parts.IsEmpty()) { continue; }
        auto& runtime = state->registry.get_or_emplace<Component::MeshRuntime>(entity);
        runtime.modelHandle = ctx->assetManager->LoadModuleModel(meshComponent.params);
        if (runtime.modelHandle.IsValid()) { started.PushBack(entity); }
    }
    for (const entt::entity entity : started) {
        state->registry.remove<Component::ModuleMeshLoadPendingTag>(entity);
        state->registry.emplace_or_replace<Component::ModuleMeshLoadingTag>(entity);
    }
}

void ModuleMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::ModuleMeshComponent, Component::ModuleMeshLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto releaseExisting = [&] {
            state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Module model ({}) is not in the asset manager.", runtime->modelHandle.index);
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }

        FillModuleMeshRange(ctx, state, runtime, model, meshComponent, HasEmissiveLightFlag(state->registry, entity));
        EvaluateInstanceRenderState(state, entity);
        state->registry.emplace_or_replace<Component::MultiframeDirtyComponent>(entity);

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::ModuleMeshLoadingTag>(entity);
    }
}

void SplineMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::SplineMeshComponent, Component::SplineMeshLoadPendingTag>();
    if (view.size_hint() == 0) { return; }

    const size_t budget = std::min(view.size_hint(), Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, meshComponent] : view.each()) {
        if (started.Size() >= budget) { break; }
        auto& runtime = state->registry.get_or_emplace<Component::MeshRuntime>(entity);
        runtime.modelHandle = ctx->assetManager->LoadSplineModel(Component::ToSplineParams(meshComponent));
        if (runtime.modelHandle.IsValid()) { started.PushBack(entity); }
    }
    for (const entt::entity entity : started) {
        state->registry.remove<Component::SplineMeshLoadPendingTag>(entity);
        state->registry.emplace_or_replace<Component::SplineMeshLoadingTag>(entity);
    }
}

void SplineMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::SplineMeshComponent, Component::SplineMeshLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, meshComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto releaseExisting = [&] {
            state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
            state->modelStore.Free(runtime->modelRange);
        };

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Spline model ({}) is not in the asset manager.", runtime->modelHandle.index);
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            releaseExisting();
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }

        Engine::MaterialID matID = ctx->materialManager->GetDefaultMaterialID();
        if (meshComponent.material.IsValid() && ctx->materialManager->DoesMutableMaterialExist(meshComponent.material)) {
            matID = meshComponent.material;
        }
        releaseExisting();
        runtime->modelRange = state->modelStore.Allocate(1);
        if (runtime->modelRange.IsValid()) {
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, &state->triLightStore, model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(state->registry, entity));
            EvaluateInstanceRenderState(state, entity);
            state->registry.emplace_or_replace<Component::MultiframeDirtyComponent>(entity);
        }

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::SplineMeshLoadingTag>(entity);
    }
}

void Text3DGeneratePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::Text3DComponent, Component::Text3DGeneratePendingTag>();
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

        state->registry.remove<Component::MeshRuntime>(entity);

        if (textComponent.text.Size() > 0) {
            auto& runtime = state->registry.get_or_emplace<Component::MeshRuntime>(entity);
            runtime.modelHandle = ctx->assetManager->LoadText3DModel(textComponent.fontId, textComponent.text, textComponent.depth, textComponent.flatness, textComponent.tracking, textComponent.scale, textComponent.bSmoothNormals, textComponent.align, textComponent.anchor, textComponent.wrapWidth, textComponent.bendRadius);
            if (runtime.modelHandle.IsValid()) {
                state->registry.emplace_or_replace<Component::Text3DLoadingTag>(entity);
                state->assetLoad.bPendingModelResolve = true;
            }
        }
        done.PushBack(entity);
    }
    for (const entt::entity entity : done) {
        state->registry.remove<Component::Text3DGeneratePendingTag>(entity);
    }
}

void Text3DLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::Text3DComponent, Component::Text3DLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (const auto& [entity, textComponent] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        if (!runtime->modelHandle.IsValid()) {
            state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
            state->modelStore.Free(runtime->modelRange);
            resolved.PushBack(entity); // nothing to resolve (e.g. empty text / no font); drop the tag
            continue;
        }

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Text3D model ({}) is not in the asset manager.", runtime->modelHandle.index);
            continue;
        }
        if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
            state->modelStore.Free(runtime->modelRange);
            resolved.PushBack(entity); // generation failed (e.g. empty/whitespace text); stop waiting so editing unlocks
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }

        Engine::MaterialID matID = ctx->materialManager->GetDefaultMaterialID();
        if (textComponent.material.IsValid() && ctx->materialManager->DoesMutableMaterialExist(textComponent.material)) {
            matID = textComponent.material;
        }
        state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
        state->modelStore.Free(runtime->modelRange);
        runtime->modelRange = state->modelStore.Allocate(1);
        if (runtime->modelRange.IsValid()) {
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, &state->triLightStore, model, matID, runtime->modelRange.offset, HasEmissiveLightFlag(state->registry, entity));
            EvaluateInstanceRenderState(state, entity);
            state->registry.emplace_or_replace<Component::MultiframeDirtyComponent>(entity);
        }

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::Text3DLoadingTag>(entity);
    }
}

void TextFontPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::TextComponent, Component::TextRuntime, Component::TextFontPendingTag>();
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
        state->registry.remove<Component::TextFontPendingTag>(entity);
    }
}
} // Game
