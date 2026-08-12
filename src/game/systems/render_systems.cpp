//
// Created by William on 2025-12-26.
//

#include "render_systems.h"

#include <tracy/Tracy.hpp>

#include "scene_system.h"
#include "game/console/console.h"
#include "game/ui/ui_zindex.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/arena_vector.h"
#include "core/containers/inline_vector.h"
#include "core/math/color_helpers.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/material_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_assert.h"
#include "engine/logging/engine_log.h"
#include "game/fwd_components.h"
#include "game/components/common_components.h"
#include "game/components/debug_components.h"
#include "game/input/game_actions.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/module_mesh_component.h"
#include "game/components/render/text3d_component.h"
#include "game/components/render/light_components.h"
#include "game/components/render/local_ddgi_volume_component.h"
#include "game/components/render/reflection_probe_component.h"
#include "render/shaders/lights_interop.h"
#include "render/shaders/reflection_probe_interop.h"
#include "render/shaders/text_interop.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/render/text_component.h"
#include "game/components/common/stable_id_component.h"
#include "game/components/core_components.h"
#include "game/components/physics/physics_body_desc.h"
#include "render/types/render_types.h"


namespace Game
{
void ConnectRenderObservers(entt::registry& registry)
{
    registry.on_construct<Component::TransformComponent>().connect<&Component::TransformComponent::OnConstruct>();
    registry.on_destroy<Component::TransformComponent>().connect<&Component::TransformComponent::OnDestroy>();

    registry.on_destroy<Component::MeshRuntime>().connect<&Component::MeshRuntime::OnDestroy>();
    registry.on_destroy<Component::LightSurfaceRuntime>().connect<&Component::LightSurfaceRuntime::OnDestroy>();

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

    registry.on_destroy<Component::MeshRuntime>().disconnect<&Component::MeshRuntime::OnDestroy>();
    registry.on_destroy<Component::LightSurfaceRuntime>().disconnect<&Component::LightSurfaceRuntime::OnDestroy>();

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

    auto view = state->registry.view<Component::StaticMeshComponent, Component::MeshRuntime>();
    Core::ArenaVector<entt::entity> entitiesToRestart{&ctx->gameplayArena.Get(), view.size_hint()};

    for (const auto& [entity, smc, runtime] : view.each()) {
        if (!isHot(smc.modelId)) { continue; }
        Component::UnloadStaticMesh(state->registry, entity);
        entitiesToRestart.PushBack(entity);
    }

    for (const entt::entity entity : entitiesToRestart) {
        Component::LoadStaticMesh(state->registry.get<Component::StaticMeshComponent>(entity), state->registry, entity);
    }

    // Physics holds its own model ref (its serialized key, loaded independently), so it must also release + re-arm or the model can never drain.
    auto physicsView = state->registry.view<Component::PhysicsBodyDesc>();
    Core::ArenaVector<entt::entity> physicsToRestart{&ctx->gameplayArena.Get(), physicsView.size()};
    for (auto [entity, bodyDesc] : physicsView.each()) {
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
        if (affected) { physicsToRestart.PushBack(entity); }
    }
    for (const entt::entity entity : physicsToRestart) {
        state->registry.remove<Component::PhysicsMeshLoadingTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsMeshTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsShapeCreationTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsBodyCreationTag>(entity);
        state->assetLoad.bPendingModelResolve = true;
    }
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

    auto textView = state->registry.view<Component::TextComponent, Component::TextRuntime>();
    auto text3DView = state->registry.view<Component::Text3DComponent>();

    Core::ArenaVector<entt::entity> textEntitiesToRestart{&ctx->gameplayArena.Get(), textView.size_hint()};
    Core::ArenaVector<entt::entity> text3DEntitiesToRestart{&ctx->gameplayArena.Get(), text3DView.size()};

    for (auto [entity, textComp, runtime] : textView.each()) {
        if (!isHot(textComp.fontId)) { continue; }
        Component::UnloadTextComponent(textComp, state->registry, entity);
        textEntitiesToRestart.PushBack(entity);
    }
    for (auto [entity, text3DComp] : text3DView.each()) {
        if (!isHot(text3DComp.fontId)) { continue; }
        Component::UnloadText3DFont(state->registry, entity);
        text3DEntitiesToRestart.PushBack(entity);
    }

    for (const entt::entity entity : textEntitiesToRestart) {
        Component::LoadTextComponent(state->registry.get<Component::TextComponent>(entity), state->registry, entity);
    }
    for (const entt::entity entity : text3DEntitiesToRestart) {
        Component::LoadText3DFont(state->registry.get<Component::Text3DComponent>(entity), state->registry, entity);
    }

    // A Text3D physics collider holds its own ref to the generated mesh (keyed on the font), so it must release + re-arm too or that mesh can never drain.
    auto physicsView = state->registry.view<Component::PhysicsBodyDesc>();
    Core::ArenaVector<entt::entity> physicsToRestart{&ctx->gameplayArena.Get(), physicsView.size()};
    for (auto [entity, bodyDesc] : physicsView.each()) {
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
        if (affected) { physicsToRestart.PushBack(entity); }
    }
    for (const entt::entity entity : physicsToRestart) {
        state->registry.remove<Component::PhysicsMeshLoadingTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsMeshTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsShapeCreationTag>(entity);
        state->registry.emplace_or_replace<Component::PendingPhysicsBodyCreationTag>(entity);
        state->assetLoad.bPendingModelResolve = true;
    }
}

void TextureHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->assetLoad.pendingHotReloadTextureIds.IsEmpty()) { return; }

    for (auto hotId : state->assetLoad.pendingHotReloadTextureIds) {
        if (ctx->assetManager->IsTextureLoaded(hotId)) {
            ctx->assetManager->ReloadTexture(hotId);
        }
    }
}

void CubemapHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->assetLoad.pendingHotReloadEnvironmentMapIds.IsEmpty()) { return; }

    for (auto hotId : state->assetLoad.pendingHotReloadEnvironmentMapIds) {
        if (ctx->assetManager->IsCubemapLoaded(hotId)) {
            ctx->assetManager->ReloadCubemap(hotId);
        }
    }
}

void StaticMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::StaticMeshComponent, Component::StaticMeshLoadPendingTag>();
    if (view.size_hint() == 0) { return; }

    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const auto& [entity, meshComponent] : view.each()) {
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

    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const auto& [entity, probe] : view.each()) {
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

    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const auto& [entity, probe] : view.each()) {
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

    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size());
    for (const entt::entity entity : view) {
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
        emissive.props.alphaProperties.z = 1.0f;
        emissive.props.emissiveFactor = areaLight ? glm::vec4(areaLight->color, areaLight->intensity) : glm::vec4(sphereLight->color, sphereLight->intensity);
        const Engine::MaterialID materialID = ctx->materialManager->CreateImmutableMaterial(emissive);

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
        const Engine::InstanceStore::Range range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, &state->triLightStore, model, materialID, modelRange.offset);
        if (!range.IsValid()) {
            state->modelStore.Free(modelRange);
            resolved.PushBack(entity);
            continue;
        }
        state->modelStore.Models()[modelRange.offset] = {m, m};
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

    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (const auto& [entity, meshComponent] : view.each()) {
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        Engine::MaterialManager* materialManager = ctx->materialManager;
        Engine::InstanceStore& store = state->instanceStore;

        store.ReleaseAndFree(materialManager, &state->triLightStore, runtime->range);
        state->modelStore.Free(runtime->modelRange);

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Model ({}) is not in the asset manager after a load request.", runtime->modelHandle.index);
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            modelsWaitingThisTick++;
            continue;
        }

        const Core::HeapArray<Engine::Node>& nodes = model->modelData.nodes;
        Core::HeapArray<Engine::MeshInformation>& meshes = model->modelData.meshes;

        auto included = [&](uint32_t ordinal) -> bool {
            return !meshComponent.primitiveBlacklist.Contains(ordinal);
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
            resolved.PushBack(entity);
            continue;
        }

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
                    Engine::MaterialID materialOverride = meshComponent.GetMaterialOverride(static_cast<uint32_t>(primitive.materialIndex));
                    if (materialOverride.IsValid()) {
                        if (materialManager->DoesMutableMaterialExist(materialOverride)) {
                            matID = materialOverride;
                        }
                        else {
                            matID = materialManager->CreateImmutableMaterial(applyShaderOverrides(model->modelData.materials[primitive.materialIndex]));
                            LOG_WARN(Engine, "Mesh was resolved with a material override that does not exist in the registry.");
                        }
                    }
                    else {
                        matID = materialManager->CreateImmutableMaterial(applyShaderOverrides(model->modelData.materials[primitive.materialIndex]));
                    }
                }

                store.FillEntry(writeIndex, materialManager, &state->triLightStore, model, primitive, {
                    .material = matID,
                    .modelSlot = nodeModelSlot,
                    .originalMaterialIndex = primitive.materialIndex,
                    .sourceNodeIndex = n,
                    .modelPrimitiveOrdinal = thisOrdinal,
                    .modelSpaceTransform = nodeModelSpace,
                });
                ++writeIndex;
            }
        }

        runtime->range = range;
        runtime->modelRange = modelRange;
        state->registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
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

    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const auto& [entity, meshComponent] : view.each()) {
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

    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (const auto& [entity, meshComponent] : view.each()) {
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        Engine::MaterialManager* materialManager = ctx->materialManager;
        state->instanceStore.ReleaseAndFree(materialManager, &state->triLightStore, runtime->range);
        state->modelStore.Free(runtime->modelRange);

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Model ({}) is not in the asset manager after a load request.", runtime->modelHandle.index);
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
            resolved.PushBack(entity);
            continue;
        }

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
            matID = materialManager->CreateImmutableMaterial(applyShaderOverrides(model->modelData.materials[targetPrim->materialIndex]));
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

        materialManager->AcquireMaterial(matID);
        store[range.offset] = {
            .primitiveIndex = targetPrim->index,
            .originalMaterialIndex = targetPrim->materialIndex,
            .sourceNodeIndex = targetNode,
            .modelPrimitiveOrdinal = meshComponent.primitiveOrdinal,
            .modelSlot = modelRange.offset,
            .materialIndex = materialManager->GetMaterialIndex(matID),
            .materialID = matID,
            .blasDeviceAddress = targetPrim->blasDeviceAddress,
            .modelSpaceTransform = glm::mat4(1.0f),
        };
        runtime->range = range;
        runtime->modelRange = modelRange;
        state->registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);

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

    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (auto [entity, meshComponent] : view.each()) {
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

    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (const auto& [entity, meshComponent] : view.each()) {
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Procedural model ({}) is not in the asset manager, it should have been requested to load during scene load.", runtime->modelHandle.index);
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
        state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
        state->modelStore.Free(runtime->modelRange);
        runtime->modelRange = state->modelStore.Allocate(1);
        if (runtime->modelRange.IsValid()) {
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, &state->triLightStore, model, matID, runtime->modelRange.offset);
            state->registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
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

static void FillModuleMeshRange(Engine::EngineContext* ctx, Engine::EngineState* state, Component::MeshRuntime* runtime, Engine::StaticModel* model, const Component::ModuleMeshComponent& meshComponent)
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
        ctx->materialManager->AcquireMaterial(matID);
        store[writeIndex] = {
            .primitiveIndex = primitive.index,
            .originalMaterialIndex = primitive.materialIndex,
            .sourceNodeIndex = 0,
            .modelPrimitiveOrdinal = j,
            .modelSlot = modelRange.offset,
            .materialIndex = ctx->materialManager->GetMaterialIndex(matID),
            .materialID = matID,
            .blasDeviceAddress = primitive.blasDeviceAddress,
            .modelSpaceTransform = glm::mat4(1.0f),
        };
        ++writeIndex;
    }
    runtime->range = range;
    runtime->modelRange = modelRange;
}

void ModuleMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::ModuleMeshComponent, Component::ModuleMeshLoadPendingTag>();
    if (view.size_hint() == 0) { return; }

    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (auto [entity, meshComponent] : view.each()) {
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
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (const auto& [entity, meshComponent] : view.each()) {
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Module model ({}) is not in the asset manager.", runtime->modelHandle.index);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }

        FillModuleMeshRange(ctx, state, runtime, model, meshComponent);
        state->registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);

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

    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (auto [entity, meshComponent] : view.each()) {
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
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (const auto& [entity, meshComponent] : view.each()) {
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Spline model ({}) is not in the asset manager.", runtime->modelHandle.index);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }

        Engine::MaterialID matID = ctx->materialManager->GetDefaultMaterialID();
        if (meshComponent.material.IsValid() && ctx->materialManager->DoesMutableMaterialExist(meshComponent.material)) {
            matID = meshComponent.material;
        }
        state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime->range);
        state->modelStore.Free(runtime->modelRange);
        runtime->modelRange = state->modelStore.Allocate(1);
        if (runtime->modelRange.IsValid()) {
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, &state->triLightStore, model, matID, runtime->modelRange.offset);
            state->registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
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
    auto done = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (const auto& [entity, textComponent] : view.each()) {
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
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (const auto& [entity, textComponent] : view.each()) {
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
            runtime->range = state->instanceStore.AllocateSingleMeshRange(ctx->materialManager, &state->triLightStore, model, matID, runtime->modelRange.offset);
            state->registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
        }

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::Text3DLoadingTag>(entity);
    }
}

void MarkRenderTransformsDirty(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto transformDirtyView = state->registry.view<Component::TransformComponent, Component::DirtyTransformTag>();
    for (auto entity : transformDirtyView) {
        state->registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
    }
}

// Tick-to-tick physics deltas are tiny, so normalized lerp is visually identical to slerp without the trig
static glm::quat NlerpPose(const glm::quat& a, glm::quat b, float t)
{
    if (glm::dot(a, b) < 0.0f) { b = -b; }
    return glm::normalize(glm::lerp(a, b, t));
}

void ResolveWorldTransforms(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto& registry = state->registry;
    const float alpha = state->physics.interpolationAlpha;

    // Roots (no parent), non-physics
    for (auto [entity, local, world, dirty] : registry.view<Component::TransformComponent, Component::WorldTransformComponent, Component::MultiframeDirtyTransformComponent>(
             entt::exclude<Component::HierarchyComponent, Component::DynamicPhysicsBodyComponent>).each()) {
        world.translation = local.translation;
        world.rotation = local.rotation;
        world.scale = local.scale;
    }

    // Roots, physics (interpolated Jolt pose)
    for (auto [entity, physics, local, world] : registry.view<Component::DynamicPhysicsBodyComponent, Component::TransformComponent, Component::WorldTransformComponent>(
             entt::exclude<Component::HierarchyComponent>).each()) {
        world.translation = glm::mix(physics.previousPosition, physics.currentPosition, alpha);
        world.rotation = NlerpPose(physics.previousRotation, physics.currentRotation, alpha);
        world.scale = local.scale;
        registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
    }

    // Children in depth order. EnsureHierarchyOrder re-sorts only if a topology change flagged it dirty (cheap bool check otherwise).
    EnsureHierarchyOrder(state);
    auto orphans = Core::ArenaVector<entt::entity>(&ctx->gameplayArena.Get(), 8);
    auto hierarchy = registry.view<Component::HierarchyComponent>();
    for (auto entity : hierarchy) {
        const auto& node = hierarchy.get<Component::HierarchyComponent>(entity);

        auto& local = registry.get<Component::TransformComponent>(entity);
        auto& world = registry.get<Component::WorldTransformComponent>(entity);

        if (node.parent == entt::null || !registry.valid(node.parent) || !registry.all_of<Component::WorldTransformComponent>(node.parent)) {
            local.translation = world.translation;
            local.rotation = world.rotation;
            local.scale = world.scale;
            registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
            orphans.PushBack(entity);
            continue;
        }

        auto* physics = registry.try_get<Component::DynamicPhysicsBodyComponent>(entity);
        const bool selfDirty = physics || registry.all_of<Component::MultiframeDirtyTransformComponent>(entity);
        const bool parentDirty = registry.all_of<Component::MultiframeDirtyTransformComponent>(node.parent);
        if (!selfDirty && !parentDirty) { continue; }
        if (!registry.all_of<Component::MultiframeDirtyTransformComponent>(entity)) {
            registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
        }

        // Physics (interpolated Jolt pose)
        if (physics) {
            world.translation = glm::mix(physics->previousPosition, physics->currentPosition, alpha);
            world.rotation = NlerpPose(physics->previousRotation, physics->currentRotation, alpha);
            world.scale = local.scale;
        }
        else {
            const auto& parentWorld = registry.get<Component::WorldTransformComponent>(node.parent);
            world = Component::ComposeWorldTransform(parentWorld, local);
        }
    }

    for (entt::entity entity : orphans) {
        registry.remove<Component::HierarchyComponent>(entity);
    }
}

void UpdateUIPointerState(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    const Vec2 mousePos = state->input.mousePositionAbsolute;
    const float viewportOffsetX = static_cast<float>(ctx->windowContext.viewportOffsetX);
    const float viewportOffsetY = static_cast<float>(ctx->windowContext.viewportOffsetY);
    const bool bIsMouseDown = state->input.GetActionState(Actions::ACTION_UI_POINTER_DOWN).down;
    Clay_SetPointerState(Clay_Vector2{mousePos.x - viewportOffsetX, mousePos.y - viewportOffsetY}, bIsMouseDown);

    state->input.uiScrollAccum += state->input.GetActionState(Actions::ACTION_UI_SCROLL).axis;
}

void RenderPrepareTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;

    auto dirtyView = state->registry.view<Component::WorldTransformComponent, Component::RenderTransformComponent, Component::MultiframeDirtyTransformComponent>();
    constexpr size_t TASK_THRESHOLD = 1000;
    size_t dirtyViewCount = dirtyView.size_hint();
    if (dirtyViewCount < TASK_THRESHOLD) {
        ZoneScopedN("Serial");
        for (auto [entity, world, renderTransform, dirtyRender] : dirtyView.each()) {
            renderTransform.previousMatrix = renderTransform.modelMatrix;
            renderTransform.modelMatrix = glm::translate(GetMatrix(world), renderTransform.renderOffset) * glm::mat4_cast(renderTransform.renderRotation);
        }
    }
    else {
        ZoneScopedN("Parallel");
        auto entities = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), dirtyViewCount);
        for (entt::entity e : dirtyView) {
            entities.PushBack(e);
        }

        enki::TaskSet task(entities.Size(), [&](enki::TaskSetPartition range, uint32_t) {
            for (uint32_t i = range.start; i < range.end; ++i) {
                auto entity = entities[i];
                auto& world = dirtyView.get<Component::WorldTransformComponent>(entity);
                auto& renderTransform = dirtyView.get<Component::RenderTransformComponent>(entity);

                renderTransform.previousMatrix = renderTransform.modelMatrix;
                renderTransform.modelMatrix = glm::translate(GetMatrix(world), renderTransform.renderOffset) * glm::mat4_cast(renderTransform.renderRotation);
            }
        });
        ctx->scheduler->AddTaskSetToPipe(&task);
        ctx->scheduler->WaitforTask(&task);
    }

    // Dirty entities write their node matrices into their stable model slots
    {
        Engine::InstanceStore& store = state->instanceStore;
        Model* models = state->modelStore.Models();
        for (auto [entity, runtime, renderTransform, dirty] : state->registry.view<Component::MeshRuntime, Component::RenderTransformComponent, Component::MultiframeDirtyTransformComponent>().each()) {
            if (!runtime.range.IsValid()) { continue; }
            uint32_t lastSlot = ~0u;
            for (uint32_t i = 0; i < runtime.range.count; ++i) {
                const Engine::InstanceSource& inst = store[runtime.range.offset + i];
                if (inst.modelSlot == lastSlot) { continue; }
                lastSlot = inst.modelSlot;
                models[inst.modelSlot] = {renderTransform.modelMatrix * inst.modelSpaceTransform, renderTransform.previousMatrix * inst.modelSpaceTransform};
            }
        }
    }

    // Area light emissive quads
    for (auto [entity, light, transform, surfaceRuntime, dirty] : state->registry.view<Component::AreaLightComponent, Component::TransformComponent, Component::LightSurfaceRuntime, Component::MultiframeDirtyTransformComponent>().each()) {
        if (!surfaceRuntime.modelRange.IsValid()) { continue; }
        Model& m = state->modelStore.Models()[surfaceRuntime.modelRange.offset];
        m.prevModelMatrix = m.modelMatrix;
        m.modelMatrix = Component::ComputeAreaLightQuadMatrix(transform, light);
    }

    // Sphere light emissive meshes
    for (auto [entity, light, transform, surfaceRuntime, dirty] : state->registry.view<Component::SphereLightComponent, Component::TransformComponent, Component::LightSurfaceRuntime, Component::MultiframeDirtyTransformComponent>(entt::exclude<Component::AreaLightComponent>).each()) {
        if (!surfaceRuntime.modelRange.IsValid()) { continue; }
        Model& m = state->modelStore.Models()[surfaceRuntime.modelRange.offset];
        m.prevModelMatrix = m.modelMatrix;
        m.modelMatrix = Component::ComputeSphereLightMatrix(transform, light);
    }

    for (auto [entity, dirty] : state->registry.view<Component::MultiframeDirtyTransformComponent>().each()) {
        dirty.counter--;
        if (dirty.counter <= 0) {
            state->registry.remove<Component::MultiframeDirtyTransformComponent>(entity);
        }
    }
}

void GatherRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    auto& materialManager = ctx->materialManager; {
        ZoneScopedN("SyncMeshVisibility");
        for (auto [entity, renderFlags, runtime] : state->registry.view<Component::RenderFlagsComponent, Component::MeshRuntime>().each()) {
            runtime.visible = renderFlags.Has(Component::RenderFlagsComponent::VISIBLE);
            runtime.ddgiVisible = renderFlags.Has(Component::RenderFlagsComponent::DDGI_CONTRIBUTE);
        }
    }

    frameBuffer->mainViewFamily.primitiveInstances.Clear();
    frameBuffer->mainViewFamily.primitiveInstances.Resize(state->instanceStore.GetWatermark());


    const uint32_t modelWatermark = state->modelStore.GetWatermark();
    frameBuffer->mainViewFamily.modelMatrices.Clear();
    frameBuffer->mainViewFamily.modelMatrices.Resize(modelWatermark);
    memcpy(frameBuffer->mainViewFamily.modelMatrices.Data(), state->modelStore.Models(), modelWatermark * sizeof(Model));


    // Gather regular renderables
    {
        ZoneScopedN("MeshRuntimeGather");
        auto view = state->registry.view<Component::MeshRuntime, Component::RenderTransformComponent>(
            entt::exclude<
                Component::PortalPlaneTag,
                Component::CubemapVisualizeTag,
                Component::ProbeBakeHiddenTag
            >);

        Engine::InstanceStore& store = state->instanceStore;

        for (const auto& [entity, runtime, renderTransform] : view.each()) {
            if (!runtime.range.IsValid()) { continue; }
            if (!runtime.visible) { continue; }

            uint64_t stableId = 1234567890;
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                stableId = stable->id.id;
            }

            const uint32_t base = runtime.range.offset;
            const uint32_t count = runtime.range.count;
            for (uint32_t i = 0; i < count; ++i) {
                const Engine::InstanceSource& inst = store[base + i];
                const uint32_t slot = base + i;
                const uint32_t triBase = slot < frameBuffer->mainViewFamily.triLightBaseBySlot.Size() ? frameBuffer->mainViewFamily.triLightBaseBySlot[slot] : 0xFFFFFFFFu;
                frameBuffer->mainViewFamily.primitiveInstances[slot] = {
                    .primitiveIndex = inst.primitiveIndex,
                    .materialID = inst.materialID,
                    .materialIndex = inst.materialIndex,
                    .modelIndex = inst.modelSlot,
                    .stableId = stableId,
                    .blasDeviceAddress = inst.blasDeviceAddress,
                    .emissiveTriLightBase = triBase,
                    .ddgiVisible = runtime.ddgiVisible,
                };
            }
        }
    } {
        ZoneScopedN("LightSurfaces");
        Engine::InstanceStore& store = state->instanceStore;
        const Engine::Material* defaultMaterial = materialManager->GetMaterial(materialManager->GetDefaultMaterialID());
        Engine::Material emissiveMaterial{};
        if (defaultMaterial) {
            emissiveMaterial = *defaultMaterial; // only emissiveFactor changes per light; black albedo so only emission shows
            emissiveMaterial.props.colorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            emissiveMaterial.props.alphaProperties.z = 1.0f; // double sided
        }

        auto emitSurface = [&](entt::entity lightEntity, Component::LightSurfaceRuntime& surfaceRuntime, const glm::vec3& color, float intensity, bool draw, uint32_t lightSlot) {
            if (!draw || !defaultMaterial || !surfaceRuntime.range.IsValid()) { return; }

            emissiveMaterial.props.emissiveFactor = glm::vec4(color, intensity);
            const Engine::MaterialID materialID = Engine::HashMaterial(emissiveMaterial);
            if (materialID != surfaceRuntime.materialID) {
                materialManager->CreateImmutableMaterial(emissiveMaterial);
                materialManager->AcquireMaterial(materialID);
                materialManager->ReleaseMaterial(surfaceRuntime.materialID);
                store[surfaceRuntime.range.offset].materialID = materialID;
                store[surfaceRuntime.range.offset].materialIndex = materialManager->GetMaterialIndex(materialID);
                surfaceRuntime.materialID = materialID;
            }

            uint64_t stableId = 1234567890;
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(lightEntity)) {
                stableId = stable->id.id;
            }
            const Engine::InstanceSource& inst = store[surfaceRuntime.range.offset];

            frameBuffer->mainViewFamily.primitiveInstances[surfaceRuntime.range.offset] = {
                .primitiveIndex = inst.primitiveIndex,
                .materialID = inst.materialID,
                .materialIndex = inst.materialIndex,
                .modelIndex = inst.modelSlot,
                .stableId = stableId,
                .blasDeviceAddress = inst.blasDeviceAddress,
                .lightIndex = lightSlot,
            };
        };

        for (auto [entity, light, surfaceRuntime] : state->registry.view<Component::AreaLightComponent, Component::LightSurfaceRuntime>(entt::exclude<Component::ProbeBakeHiddenTag, Component::ProbeBakeProxyHiddenTag>).each()) {
            emitSurface(entity, surfaceRuntime, light.color, light.intensity, light.drawEmissiveSurface, light.lightSlot);
        }
        for (auto [entity, light, surfaceRuntime] : state->registry.view<Component::SphereLightComponent, Component::LightSurfaceRuntime>(entt::exclude<Component::AreaLightComponent, Component::ProbeBakeHiddenTag, Component::ProbeBakeProxyHiddenTag>).each()) {
            emitSurface(entity, surfaceRuntime, light.color, light.intensity, light.drawEmissiveSurface, light.lightSlot);
        }
    }

    // Material remap
    {
        ZoneScopedN("Material Recording");
        Core::Array<bool, Render::BINDLESS_MATERIAL_BUFFER_COUNT> seen{};
        for (auto& instance : frameBuffer->mainViewFamily.primitiveInstances) {
            if (instance.primitiveIndex == DEAD_SLOT_PRIMITIVE_INDEX) { continue; }
            if (seen[instance.materialIndex]) { continue; }
            seen[instance.materialIndex] = true;
            frameBuffer->mainViewFamily.activeMaterials.PushBack({instance.materialIndex, materialManager->GetRenderMaterial(instance.materialID)});
        }
    } {
        int32_t bestPriority = INT32_MIN;
        Render::Cubemap* bestCubemap = nullptr;
        float bestIntensity = 1.0f;
        for (auto&& [entity, sky] : state->registry.view<Component::SkyboxComponent>().each()) {
            if (!sky.envMap.IsValid() || ctx->assetManager->GetCubemapMetadata(sky.envMap) == nullptr) { continue; }
            if (!sky.handle.IsValid()) {
                sky.handle = ctx->assetManager->LoadCubemap(sky.envMap);
            }
            Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(sky.handle);
            if (cubemap && cubemap->loadState == Render::Cubemap::LoadState::Loaded && sky.priority > bestPriority) {
                bestPriority = sky.priority;
                bestCubemap = cubemap;
                bestIntensity = sky.intensity;
            }
        }
        if (bestCubemap) {
            frameBuffer->mainViewFamily.skyboxIndex = static_cast<int32_t>(bestCubemap->bindlessHandle.index);
            frameBuffer->mainViewFamily.skyboxLOD = state->lighting.skyboxLOD;
            frameBuffer->mainViewFamily.iblIntensity *= bestIntensity;
        }
    }
}

void TextFontPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::TextComponent, Component::TextRuntime, Component::TextFontPendingTag>();
    if (view.size_hint() == 0) { return; }

    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (auto [entity, textComp, runtime] : view.each()) {
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

void GatherTextRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    auto view = state->registry.view<Component::TextComponent, Component::TextRuntime, Component::RenderTransformComponent>(entt::exclude<Component::TextFontPendingTag>);

    for (const auto& [entity, textComp, runtime, renderTransform] : view.each()) {
        if (textComp.text.IsEmpty()) { continue; }

        Engine::Font* font = ctx->assetManager->GetFont(runtime.fontHandle);
        if (!font) { continue; }

        const float scale = textComp.renderSizePx / font->header.emSize;
        float lineHeightPx = font->header.lineHeight * scale;
        if (lineHeightPx <= 0.0f) { lineHeightPx = (font->header.ascender - font->header.descender) * scale; }
        if (lineHeightPx <= 0.0f) { lineHeightPx = textComp.renderSizePx; }

        const char* str = textComp.text.c_str();
        const size_t len = textComp.text.Size();
        auto advanceOf = [&](char c) -> float {
            const Engine::WGlyphInfo* g = ctx->assetManager->GetGlyph(runtime.fontHandle, static_cast<unsigned char>(c));
            return g ? g->advance * scale : textComp.renderSizePx * 0.25f;
        };

        struct TextLine
        {
            size_t start;
            size_t end;
            float width;
        };
        Core::InlineVector<TextLine, 257> lines;
        size_t lineStart = 0;
        while (lineStart <= len && !lines.IsFull()) {
            size_t hardEnd = lineStart;
            while (hardEnd < len && str[hardEnd] != '\n') { ++hardEnd; }

            size_t segStart = lineStart;
            if (textComp.wrapWidthPx > 0.0f) {
                float width = 0.0f;
                size_t lastSpace = SIZE_MAX;
                for (size_t i = segStart; i < hardEnd && !lines.IsFull(); ++i) {
                    if (str[i] == ' ') { lastSpace = i; }
                    const float adv = advanceOf(str[i]);
                    if (width + adv > textComp.wrapWidthPx && lastSpace != SIZE_MAX && lastSpace > segStart) {
                        lines.PushBack({segStart, lastSpace, 0.0f});
                        segStart = lastSpace + 1;
                        i = segStart - 1;
                        width = 0.0f;
                        lastSpace = SIZE_MAX;
                        continue;
                    }
                    width += adv;
                }
            }
            if (!lines.IsFull()) { lines.PushBack({segStart, hardEnd, 0.0f}); }
            lineStart = hardEnd + 1;
        }

        for (TextLine& line : lines) {
            float w = 0.0f;
            for (size_t i = line.start; i < line.end; ++i) { w += advanceOf(str[i]); }
            line.width = w;
        }

        float alignFactor = 0.0f;
        switch (textComp.align) {
            case Engine::Text3DAlign::Center: alignFactor = 0.5f;
                break;
            case Engine::Text3DAlign::Right: alignFactor = 1.0f;
                break;
            case Engine::Text3DAlign::Left: break;
        }

        const float blockTop = font->header.ascender * scale;
        const float blockBottom = font->header.descender * scale - static_cast<float>(lines.Size() - 1) * lineHeightPx;
        float baseY = 0.0f;
        switch (textComp.anchor) {
            case Engine::Text3DAnchor::Top: baseY = -blockTop;
                break;
            case Engine::Text3DAnchor::Center: baseY = -(blockTop + blockBottom) * 0.5f;
                break;
            case Engine::Text3DAnchor::Bottom: baseY = -blockBottom;
                break;
            case Engine::Text3DAnchor::Baseline: break;
        }

        const auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
        frameBuffer->mainViewFamily.modelMatrices.EmplaceBack(renderTransform.modelMatrix, renderTransform.previousMatrix);

        const auto drawCallIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.textInstances.Size());
        uint32_t quadCount = 0;

        for (uint32_t li = 0; li < lines.Size(); ++li) {
            float cursorX = -lines[li].width * alignFactor;
            const float penY = baseY - static_cast<float>(li) * lineHeightPx;
            for (size_t i = lines[li].start; i < lines[li].end; ++i) {
                const uint32_t codepoint = static_cast<unsigned char>(str[i]);
                const Engine::WGlyphInfo* g = ctx->assetManager->GetGlyph(runtime.fontHandle, codepoint);
                if (!g) {
                    cursorX += textComp.renderSizePx * 0.25f;
                    continue;
                }

                WorldGlyphQuad quad{};
                quad.posMin = {cursorX + g->planeLeft * scale, penY + g->planeBottom * scale};
                quad.posMax = {cursorX + g->planeRight * scale, penY + g->planeTop * scale};
                quad.uvMin = {g->uvLeft, g->uvBottom};
                quad.uvMax = {g->uvRight, g->uvTop};
                quad.color = textComp.color;
                quad.drawCallIndex = drawCallIndex;
                frameBuffer->mainViewFamily.worldGlyphQuads.PushBack(quad);
                ++quadCount;

                cursorX += g->advance * scale;
            }
        }

        if (quadCount == 0) { continue; }

        auto [matIndexRef, inserted] = frameBuffer->mainViewFamily.activeTextMaterials.TryEmplace(textComp.textMaterialId);
        if (inserted) {
            matIndexRef = static_cast<uint32_t>(frameBuffer->mainViewFamily.textMaterials.Size());
            frameBuffer->mainViewFamily.textMaterials.PushBack(ctx->materialManager->GetRenderTextMaterial(textComp.textMaterialId));
        }

        uint64_t stableId = 0;
        if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
            stableId = stable->id.id;
        }

        frameBuffer->mainViewFamily.textInstances.PushBack({
            .modelIndex = modelIndex,
            .pxRange = static_cast<float>(font->header.sdfSpread),
            .atlasBindlessIndex = font->atlasTexture.bindlessHandle.index,
            .textMaterialIndex = matIndexRef,
            .stableId = stableId,
        });
    }
}

void ApplyProbeBakeHideSet(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    entt::registry& registry = state->registry;

    for (const auto& [entity, renderFlags] : registry.view<Component::RenderFlagsComponent>().each()) {
        if (!renderFlags.Has(Component::RenderFlagsComponent::PROBE_BAKE_INCLUDE)) { registry.emplace_or_replace<Component::ProbeBakeHiddenTag>(entity); }
    }

    for (const auto& [entity, light] : registry.view<Component::AreaLightComponent>().each()) {
        if (light.bExcludeFromProbeBake) { registry.emplace_or_replace<Component::ProbeBakeHiddenTag>(entity); }
        registry.emplace_or_replace<Component::ProbeBakeProxyHiddenTag>(entity);
    }
    for (const auto& [entity, light] : registry.view<Component::SphereLightComponent>().each()) {
        if (light.bExcludeFromProbeBake) { registry.emplace_or_replace<Component::ProbeBakeHiddenTag>(entity); }
        registry.emplace_or_replace<Component::ProbeBakeProxyHiddenTag>(entity);
    }

    for (auto entity : registry.view<Component::DynamicPhysicsBodyComponent>()) {
        registry.emplace_or_replace<Component::ProbeBakeHiddenTag>(entity);
    }
    for (const auto& [entity, body] : registry.view<Component::PhysicsBodyDesc>().each()) {
        if (body.motionType != Component::PhysicsMotionType::Static) { registry.emplace_or_replace<Component::ProbeBakeHiddenTag>(entity); }
    }
}

void ClearProbeBakeHideSet(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    state->registry.clear<Component::ProbeBakeHiddenTag, Component::ProbeBakeProxyHiddenTag>();
}

void GatherLights(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    Core::ViewFamily& vf = frameBuffer->mainViewFamily;

    state->analyticLightStore.Tick(ctx->currentRenderFrame);
    state->triLightStore.Tick(ctx->currentRenderFrame);

    vf.lights.Clear();
    vf.lights.Resize(state->analyticLightStore.GetWatermark());
    vf.analyticLightCount = static_cast<uint32_t>(vf.lights.Size());

    auto areaView = state->registry.view<Component::AreaLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : areaView.each()) {
        if (light.lightSlot == Engine::AnalyticLightStore::INVALID_SLOT) { continue; }
        if (state->registry.all_of<Component::ProbeBakeHiddenTag>(entity)) { continue; }
        const glm::mat3 rot = glm::mat3_cast(transform.rotation);
        const glm::vec3 normal = rot[2];
        const glm::vec3 right = rot[0];
        const glm::vec3 up = rot[1];
        vf.lights[light.lightSlot] = LightInfo{
            .position = {transform.translation, 0.0f},
            .normal = {normal, 0.0f},
            .right = {right, light.halfWidth * transform.scale.x},
            .up = {up, light.halfHeight * transform.scale.y},
            .packedColor = Render::PackColorRGB8(light.color),
            .intensity = light.intensity,
            .range = light.range,
            .type = LIGHT_TYPE_AREA,
        };
    }

    auto sphereView = state->registry.view<Component::SphereLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : sphereView.each()) {
        if (light.lightSlot == Engine::AnalyticLightStore::INVALID_SLOT) { continue; }
        if (state->registry.all_of<Component::ProbeBakeHiddenTag>(entity)) { continue; }
        vf.lights[light.lightSlot] = LightInfo{
            .position = {transform.translation, 0.0f},
            .normal = {0.0f, 0.0f, 0.0f, 0.0f},
            .right = {0.0f, 0.0f, 0.0f, light.radius * transform.scale.x},
            .up = {0.0f, 0.0f, 0.0f, 0.0f},
            .packedColor = Render::PackColorRGB8(light.color),
            .intensity = light.intensity,
            .range = light.range,
            .type = LIGHT_TYPE_SPHERE,
        };
    }

    if (state->debug.restir.bEmissiveTriangleLights) {
        ZoneScopedN("EmissiveTriangleLights");
        auto view = state->registry.view<Component::MeshRuntime, Component::RenderTransformComponent>(
            entt::exclude<
                Component::PortalPlaneTag,
                Component::CubemapVisualizeTag,
                Component::ProbeBakeHiddenTag
            >);

        Engine::InstanceStore& store = state->instanceStore;
        vf.triLightBaseBySlot.Resize(store.GetWatermark());
        memset(vf.triLightBaseBySlot.Data(), 0xFF, vf.triLightBaseBySlot.Size() * sizeof(uint32_t));
        if (state->triLightStore.GetWatermark() > 0) {
            vf.lights.Resize(static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS) + state->triLightStore.GetWatermark());
        }
        const auto maxPerPrimitive = static_cast<uint32_t>(glm::max(state->debug.restir.emissiveTriMaxPerPrimitive, 1));
        for (const auto& [entity, runtime, renderTransform] : view.each()) {
            if (!runtime.range.IsValid() || !runtime.visible) { continue; }
            const Engine::StaticModel* model = ctx->assetManager->GetModel(runtime.modelHandle);
            if (!model || model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded || model->modelData.emissiveTriangles.IsEmpty()) { continue; }
            const Core::HeapArray<Engine::EmissiveTriangleSet>& sets = model->modelData.emissiveTriangles;

            for (uint32_t i = 0; i < runtime.range.count; ++i) {
                const uint32_t slot = runtime.range.offset + i;
                const Engine::InstanceSource& inst = store[slot];
                if (!inst.triLightRange.IsValid()) { continue; }

                const Engine::EmissiveTriangleSet* set = nullptr;
                for (const auto& candidate : sets) {
                    if (candidate.primitiveIndex == inst.primitiveIndex) {
                        set = &candidate;
                        break;
                    }
                }
                if (!set || set->verts.IsEmpty()) { continue; }

                const MaterialProperties props = ctx->materialManager->GetProperties(inst.materialID);
                const glm::vec3 emissive = glm::vec3(props.emissiveFactor);
                const float maxEmissive = glm::max(emissive.r, glm::max(emissive.g, emissive.b));
                const float intensity = props.emissiveFactor.w * maxEmissive;
                if (intensity <= 0.0f) { continue; }

                const auto setTris = static_cast<uint32_t>(set->verts.Size() / 3);
                const uint32_t gpuBase = static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS) + inst.triLightRange.offset;
                const uint32_t pushCount = glm::min(setTris, maxPerPrimitive);
                // BRDF-ray hits map via base + PrimitiveIndex(); only valid when the push covers the full BLAS triangle range
                const bool bFullPush = !set->bTruncated && pushCount == setTris;
                if (bFullPush) {
                    vf.triLightBaseBySlot[slot] = gpuBase;
                }

                const glm::vec3 color = emissive / maxEmissive;
                const uint32_t packedColor = Render::PackColorRGB8(color);
                const glm::mat4 m = renderTransform.modelMatrix * inst.modelSpaceTransform;
                const glm::mat3 m3 = glm::mat3(m);
                glm::vec3 groupMin{FLT_MAX};
                glm::vec3 groupMax{-FLT_MAX};
                float groupPower = 0.0f;
                float groupMaxRange = 0.0f;
                for (uint32_t t = 0; t < pushCount; ++t) {
                    const glm::vec3 v0 = glm::vec3(m * glm::vec4(set->verts[t * 3 + 0], 1.0f));
                    const glm::vec3 e1 = m3 * set->verts[t * 3 + 1];
                    const glm::vec3 e2 = m3 * set->verts[t * 3 + 2];
                    const glm::vec3 cr = glm::cross(e1, e2);
                    const float area = 0.5f * glm::length(cr);
                    // Degenerate triangles still occupy a slot (zero intensity) so base + PrimitiveIndex() stays aligned
                    const bool bDegenerate = area <= 1e-8f;
                    const float triRange = state->debug.restir.emissiveTriRangeMultiplier * glm::sqrt(intensity * area);
                    if (!bDegenerate) {
                        groupMin = glm::min(groupMin, glm::min(v0, glm::min(v0 + e1, v0 + e2)));
                        groupMax = glm::max(groupMax, glm::max(v0, glm::max(v0 + e1, v0 + e2)));
                        groupPower += intensity * area;
                        groupMaxRange = glm::max(groupMaxRange, triRange);
                    }
                    vf.lights[gpuBase + t] = LightInfo{
                        .position = {v0, 0.0f},
                        .normal = {bDegenerate ? glm::vec3(0.0f, 1.0f, 0.0f) : cr / (2.0f * area), 0.0f},
                        .right = {e1, 0.0f},
                        .up = {e2, 0.0f},
                        .packedColor = packedColor,
                        .intensity = bDegenerate ? 0.0f : intensity,
                        .range = triRange,
                        .type = LIGHT_TYPE_TRIANGLE,
                    };
                }

                if (groupPower > 0.0f && !vf.emissiveGroups.IsFull()) {
                    // Influence AABB
                    vf.emissiveGroups.PushBack(EmissiveGroup{
                        .aabbMin = groupMin - groupMaxRange,
                        .firstLight = gpuBase,
                        .aabbMax = groupMax + groupMaxRange,
                        .lightCount = pushCount,
                        .power = groupPower,
                    });
                }
            }
        }
    }

    //
    {
        int32_t bestPriority = INT32_MIN;
        bool found = false;
        auto dirView = state->registry.view<Component::DirectionalLightComponent, Component::TransformComponent>();
        for (const auto& [entity, light, transform] : dirView.each()) {
            if (light.priority > bestPriority) {
                bestPriority = light.priority;
                vf.directionalLight.direction = transform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
                vf.directionalLight.color = light.color;
                vf.directionalLight.intensity = light.intensity;
                vf.directionalLight.angularRadiusDegrees = light.angularRadiusDegrees;
                vf.directionalLight.bEnabled = true;
                found = true;
            }
        }
        if (!found) {
            vf.directionalLight = Core::DirectionalLight{};
        }
    }
}

void GatherReflectionProbes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    const auto& config = state->lighting.reflectionProbe;
    if (!config.bEnabled) { return; }

    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    vf.bakedDiffuseClampK = config.bakedDiffuseClampK;
    vf.bReflectionProbeBruteForce = config.bBruteForcePick;

    auto view = state->registry.view<Component::ReflectionProbeComponent, Component::WorldTransformComponent>();
    for (const auto& [entity, probe, worldTransform] : view.each()) {
        if (vf.reflectionProbes.IsFull()) { break; }
        const bool bPreview = state->debug.bProbePreview;
        if (!probe.bEnabled && !bPreview) { continue; }
        if (!probe.contentHandle.IsValid()) { continue; }
        Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(probe.contentHandle);
        if (!cubemap || cubemap->loadState != Render::Cubemap::LoadState::Loaded) { continue; }

        glm::vec3 srcTranslation = worldTransform.translation;
        glm::quat srcRotation = worldTransform.rotation;
        glm::vec3 srcScale = worldTransform.scale;
        glm::vec3 srcCaptureOffset = probe.captureOffset;
        if (probe.contentSource == Component::ReflectionProbeComponent::ContentSource::Baked) {
            if (const Engine::AssetManager::ProbeInfo* info = ctx->assetManager->GetProbeInfo(Engine::ProbeID{probe.probeId})) {
                const Engine::ProbeBakeSnapshot& snap = info->snapshot;
                srcTranslation = glm::vec3(snap.translation[0], snap.translation[1], snap.translation[2]);
                srcRotation = glm::quat(snap.rotation[0], snap.rotation[1], snap.rotation[2], snap.rotation[3]);
                srcScale = glm::vec3(snap.scale[0], snap.scale[1], snap.scale[2]);
                srcCaptureOffset = glm::vec3(snap.captureOffset[0], snap.captureOffset[1], snap.captureOffset[2]);
            }
        }

        const bool bSphere = probe.shape == Component::ReflectionProbeComponent::Shape::Sphere;
        const glm::vec3 halfExtents = bSphere
                                          ? glm::vec3(glm::max(glm::max(srcScale.x, srcScale.y), srcScale.z))
                                          : srcScale;
        const glm::mat4 world = glm::translate(glm::mat4(1.0f), srcTranslation) * glm::mat4_cast(srcRotation) * glm::scale(glm::mat4(1.0f), halfExtents);
        const glm::vec3 capturePos = srcTranslation + srcRotation * srcCaptureOffset;

        if (bPreview && !vf.probePreviews.IsFull()) {
            vf.probePreviewSettings.bActive = true;
            vf.probePreviewSettings.bIrradiance = state->debug.bProbePreviewIrradiance;
            vf.probePreviewSettings.roughness = state->debug.probePreviewRoughness;
            vf.probePreviews.PushBack(Core::ProbePreviewSphere{
                .cubemapIndex = cubemap->bindlessHandle.index,
                .position = capturePos,
            });
        }
        if (!probe.bEnabled) { continue; }

        uint32_t flags = 0u;
        if (bSphere) { flags |= REFLECTION_PROBE_FLAG_SPHERE; }
        if (probe.bParallax) { flags |= REFLECTION_PROBE_FLAG_PARALLAX; }

        vf.reflectionProbes.PushBack(ReflectionProbeGPU{
            .worldToLocal = glm::inverse(world),
            .capturePosition = {capturePos, 0.0f},
            .cubemapIndex = cubemap->bindlessHandle.index,
            .fadeMargin = probe.fadeMargin,
            .flags = flags,
            .intensity = config.intensity,
        });
    }
}

void GatherLocalDDGIVolumes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    if (!state->lighting.ddgi.bLocalVolumes) { return; }

    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    auto view = state->registry.view<Component::LocalDDGIVolumeComponent, Component::WorldTransformComponent>();
    for (const auto& [entity, volume, worldTransform] : view.each()) {
        if (vf.localDDGIVolumes.IsFull()) { break; }
        if (!volume.bEnabled) { continue; }
        vf.localDDGIVolumes.PushBack(Core::LocalDDGIVolume{
            .corner = worldTransform.translation,
            .probeSpacing = glm::max(volume.probeSpacing, 0.25f),
            .volumeId = volume.volumeId,
        });
    }
}

void GatherEditorSprites(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    if (!state->editor.bShowLightSprites) { return; }
    Core::ArenaVector<Core::Sprite>& sprites = frameBuffer->mainViewFamily.sprites;

    auto areaView = state->registry.view<Component::AreaLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : areaView.each()) {
        uint64_t stableId = 0;
        if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
            stableId = stable->id.id;
        }
        sprites.PushBack(Core::Sprite{
            .worldPosition = transform.translation,
            .pixelSize = 0.5f,
            .color = {light.color.r, light.color.g, light.color.b, 1.0f},
            .stableId = stableId,
            .textureIndex = SPRITE_AREA_LIGHT_BINDLESS_INDEX,
            .samplerIndex = ASSET_SAMPLER_NEAREST_BINDLESS_INDEX,
            .billboard = true,
        });
    }

    auto sphereView = state->registry.view<Component::SphereLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : sphereView.each()) {
        uint64_t stableId = 0;
        if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
            stableId = stable->id.id;
        }
        sprites.PushBack(Core::Sprite{
            .worldPosition = transform.translation,
            .pixelSize = 0.5f,
            .color = {light.color.r, light.color.g, light.color.b, 1.0f},
            .stableId = stableId,
            .textureIndex = SPRITE_POINT_LIGHT_BINDLESS_INDEX,
            .samplerIndex = ASSET_SAMPLER_NEAREST_BINDLESS_INDEX,
            .billboard = true,
        });
    }

    auto dirView = state->registry.view<Component::DirectionalLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : dirView.each()) {
        uint64_t stableId = 0;
        if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
            stableId = stable->id.id;
        }
        sprites.PushBack(Core::Sprite{
            .worldPosition = transform.translation,
            .pixelSize = 0.5f,
            .color = {light.color.r, light.color.g, light.color.b, 1.0f},
            .stableId = stableId,
            .textureIndex = SPRITE_DIRECTIONAL_LIGHT_BINDLESS_INDEX,
            .samplerIndex = ASSET_SAMPLER_NEAREST_BINDLESS_INDEX,
            .billboard = true,
        });
    }
}

void GatherLightDebugDraws(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    const Engine::LightDebugDrawMode mode = state->editor.lightDebugDrawMode;
    const bool bProbeDebugDraw = state->lighting.reflectionProbe.bDebugDraw;
    if (mode == Engine::LightDebugDrawMode::None && !bProbeDebugDraw) { return; }

    Core::ViewFamily& viewFamily = frameBuffer->mainViewFamily;

    auto shouldDraw = [&](entt::entity entity) {
        if (mode == Engine::LightDebugDrawMode::None) { return false; }
        return mode == Engine::LightDebugDrawMode::All || state->editor.selectedEntities.Contains(entity);
    };

    auto addHemisphereVolume = [&](const Vec3& center, const Vec3& forward, const Vec3& right, const Vec3& up, float radius, const Vec4& color) {
        if (radius <= 0.0f) { return; }

        constexpr int kRimSegments = 24;
        constexpr int kArcSegments = 8;
        constexpr float kWidth = 0.02f;
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float twoPi = 2.0f * kPi;
        constexpr float halfPi = 0.5f * kPi;

        for (int i = 0; i < kRimSegments; ++i) {
            const float a0 = (static_cast<float>(i) / kRimSegments) * twoPi;
            const float a1 = (static_cast<float>(i + 1) / kRimSegments) * twoPi;
            const Vec3 p0 = center + radius * (glm::cos(a0) * right + glm::sin(a0) * up);
            const Vec3 p1 = center + radius * (glm::cos(a1) * right + glm::sin(a1) * up);
            DEBUG_ADD_LINE(viewFamily.debugLines, {p0, p1, color, kWidth});
        }

        const Vec3 meridians[4] = {right, up, -right, -up};
        for (const Vec3& dir : meridians) {
            for (int i = 0; i < kArcSegments; ++i) {
                const float a0 = (static_cast<float>(i) / kArcSegments) * halfPi;
                const float a1 = (static_cast<float>(i + 1) / kArcSegments) * halfPi;
                const Vec3 p0 = center + radius * (glm::cos(a0) * dir + glm::sin(a0) * forward);
                const Vec3 p1 = center + radius * (glm::cos(a1) * dir + glm::sin(a1) * forward);
                DEBUG_ADD_LINE(viewFamily.debugLines, {p0, p1, color, kWidth});
            }
        }
    };

    for (const auto& [entity, light, transform] : state->registry.view<Component::AreaLightComponent, Component::TransformComponent>().each()) {
        if (!shouldDraw(entity)) { continue; }
        const Vec3 center = transform.translation;
        const Vec3 right = transform.rotation * Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 up = transform.rotation * Vec3(0.0f, 1.0f, 0.0f);
        const Vec3 forward = transform.rotation * Vec3(0.0f, 0.0f, 1.0f);
        constexpr Vec4 editColor{0.5f, 0.8f, 1.0f, 1.0f};
        constexpr Vec4 rangeColor{1.0f, 0.55f, 0.15f, 1.0f};
        DEBUG_ADD_RECT(viewFamily.debugRects, {center, light.halfWidth * transform.scale.x, light.halfHeight * transform.scale.y, right, up, editColor, 0.03f});
        DEBUG_ADD_ARROW(viewFamily.debugArrows, {center, center + forward * 0.5f, 0.08f, 0.02f, editColor, 0.01f});
        addHemisphereVolume(center, forward, right, up, light.range, rangeColor);
    }

    for (const auto& [entity, light, transform] : state->registry.view<Component::SphereLightComponent, Component::TransformComponent>().each()) {
        if (!shouldDraw(entity)) { continue; }
        constexpr Vec4 editColor{0.5f, 0.8f, 1.0f, 1.0f};
        constexpr Vec4 rangeColor{1.0f, 0.55f, 0.15f, 1.0f};
        DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {transform.translation, light.radius * transform.scale.x, editColor, 0.02f});
        if (light.range > 0.0f) {
            DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {transform.translation, light.range, rangeColor, 0.02f});
        }
    }

    for (const auto& [entity, light, transform] : state->registry.view<Component::DirectionalLightComponent, Component::TransformComponent>().each()) {
        if (!shouldDraw(entity)) { continue; }
        const Vec3 forward = transform.rotation * Vec3(0.0f, 0.0f, 1.0f);
        constexpr Vec4 dirColor{1.0f, 0.9f, 0.5f, 1.0f};
        DEBUG_ADD_ARROW(viewFamily.debugArrows, {transform.translation, transform.translation + forward * 2.0f, 0.15f, 0.04f, dirColor, 0.02f});
    }

    for (auto [entity, probe, transform] : state->registry.view<Component::ReflectionProbeComponent, Component::WorldTransformComponent>().each()) {
        if (!bProbeDebugDraw && !shouldDraw(entity)) { continue; }

        const Vec4 volumeColor = Core::Math::HashColor(probe.probeId, 0u, 0.08f, 0.84f);
        constexpr Vec4 captureColor{1.0f, 0.8f, 0.3f, 1.0f};
        const float lineWidth = state->projectConfig.reflectionProbeLineWidth;
        if (probe.shape == Component::ReflectionProbeComponent::Shape::Sphere) {
            const float radius = glm::max(glm::max(transform.scale.x, transform.scale.y), transform.scale.z);
            DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {transform.translation, radius, volumeColor, lineWidth});
        }
        else {
            DEBUG_ADD_BOX(viewFamily.debugBoxes, {transform.translation, transform.scale, transform.rotation, volumeColor, lineWidth});
            const Vec3 axes[3] = {transform.rotation * Vec3(1.0f, 0.0f, 0.0f), transform.rotation * Vec3(0.0f, 1.0f, 0.0f), transform.rotation * Vec3(0.0f, 0.0f, 1.0f)};
            for (int i = 0; i < 3; ++i) {
                const Vec3 u = axes[(i + 1) % 3] * transform.scale[(i + 1) % 3];
                const Vec3 v = axes[(i + 2) % 3] * transform.scale[(i + 2) % 3];
                for (int s = 0; s < 2; ++s) {
                    const Vec3 faceCenter = transform.translation + axes[i] * (s == 0 ? transform.scale[i] : -transform.scale[i]);
                    DEBUG_ADD_LINE(viewFamily.debugLines, {faceCenter - u - v, faceCenter + u + v, volumeColor, lineWidth});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {faceCenter - u + v, faceCenter + u - v, volumeColor, lineWidth});
                }
            }
        }
        const Vec3 capturePos = transform.translation + transform.rotation * probe.captureOffset;
        DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {capturePos, 0.1f, captureColor, lineWidth});

        // Stale baked probes
        if (probe.contentSource == Component::ReflectionProbeComponent::ContentSource::Baked) {
            const Engine::AssetManager::ProbeInfo* info = ctx->assetManager->GetProbeInfo(Engine::ProbeID{probe.probeId});
            if (info && Component::ReflectionProbeComponent::IsBakeStale(transform, probe, info->snapshot, info->resolution)) {
                constexpr Vec4 staleColor{1.0f, 0.0f, 0.0f, 1.0f};
                const Engine::ProbeBakeSnapshot& snap = info->snapshot;
                const Vec3 snapTranslation{snap.translation[0], snap.translation[1], snap.translation[2]};
                const Quat snapRotation{snap.rotation[0], snap.rotation[1], snap.rotation[2], snap.rotation[3]};
                const Vec3 snapScale{snap.scale[0], snap.scale[1], snap.scale[2]};
                if (probe.shape == Component::ReflectionProbeComponent::Shape::Sphere) {
                    const float snapRadius = glm::max(glm::max(snapScale.x, snapScale.y), snapScale.z);
                    DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {snapTranslation, snapRadius, staleColor, lineWidth});
                }
                else {
                    DEBUG_ADD_BOX(viewFamily.debugBoxes, {snapTranslation, snapScale, snapRotation, staleColor, lineWidth});
                }
            }
        }
    }
}

static const char* DenoiserModeName(Core::ReSTIRParams::DenoiserMode mode)
{
    switch (mode) {
        case Core::ReSTIRParams::DenoiserMode::None: return "None";
        case Core::ReSTIRParams::DenoiserMode::ATrous: return "ATrous";
        case Core::ReSTIRParams::DenoiserMode::ASVGF: return "ASVGF";
        case Core::ReSTIRParams::DenoiserMode::RELAX: return "RELAX";
        case Core::ReSTIRParams::DenoiserMode::ReBLUR: return "ReBLUR";
        case Core::ReSTIRParams::DenoiserMode::NRD: return "NRD";
    }
    return "None";
}

static void AppendDiffuseGIModeText(Core::InlineString<48>& out, const Engine::LightingState& lighting)
{
    if (lighting.groundTruthMode == Core::GroundTruthMode::GI || lighting.groundTruthMode == Core::GroundTruthMode::Full) {
        out.Append("Ground Truth");
        return;
    }
    if (!lighting.ddgi.bEnabled) {
        out.Append("Off");
        return;
    }
    out.Append("DDGI");
    if (lighting.ddgi.bFinalGather) {
        out.Append(" + Final Gather");
    }
}

void GatherUIRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    Clay_SetLayoutDimensions({static_cast<float>(ctx->windowContext.viewportWidth), static_cast<float>(ctx->windowContext.viewportHeight)});

    Clay_UpdateScrollContainers(true, Clay_Vector2{state->input.uiScrollAccum.x, state->input.uiScrollAccum.y}, frameBuffer->timeFrame.deltaTime);
    state->input.uiScrollAccum = {};

    Clay_BeginLayout();

#ifdef WDEBUG
    Game::Console::Draw(ctx, state);
#endif

#if WILL_EDITOR
    if (state->debug.bEnableUI) {
#endif
#if 0
        constexpr Clay_Color COLOR_LIGHT = Clay_Color{224, 215, 210, 255};
        constexpr Clay_Color COLOR_RED = Clay_Color{168, 66, 28, 255};
        constexpr Clay_Color COLOR_ORANGE = Clay_Color{225, 138, 50, 255};

        uint32_t smilingFriendImageIndex = SMILING_FRIENDS_BINDLESS_INDEX;

        constexpr Clay_Color COLOR_DARK = Clay_Color{30, 30, 40, 240};
        constexpr Clay_Color COLOR_ITEM = Clay_Color{60, 80, 120, 255};
        constexpr Clay_Color COLOR_SCROLLBAR = Clay_Color{180, 180, 200, 160};
        constexpr Clay_Color COLOR_OVERLAY = Clay_Color{255, 120, 60, 80};

        CLAY(CLAY_ID("OuterContainer"), { .layout = { .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = {250, 250, 255, 64} }) {
            CLAY(CLAY_ID("SideBar"), {
                 .layout = {.sizing = {.width = CLAY_SIZING_FIXED(300), .height = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16, .layoutDirection = CLAY_TOP_TO_BOTTOM, },
                 .backgroundColor = COLOR_LIGHT
                 }) {
                CLAY(CLAY_ID("ProfilePictureOuter"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = COLOR_RED }) {
                    CLAY(CLAY_ID("ProfilePicture"), { .layout = { .sizing = { .width = CLAY_SIZING_FIXED(60), .height = CLAY_SIZING_FIXED(60) }}, .image = { .imageData = &smilingFriendImageIndex } }) {}
                    CLAY_TEXT(CLAY_STRING("Clay - UI Library"), { .textColor = {255, 255, 255, 255}, .fontSize = 24, });
                }
                CLAY_TEXT(CLAY_STRING("WillEngine"), {.textColor = {255, 255, 255, 255}, .fontSize = 24, });

                CLAY(CLAY_ID("MainContent"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) } }, .backgroundColor = COLOR_LIGHT }) {}
            }

            // Border demo: nested boxes showing per-side widths and corner radius
            CLAY(CLAY_ID("BorderDemo"), {
                 .layout = { .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200) }, .padding = CLAY_PADDING_ALL(16), .childGap = 12, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                 .backgroundColor = {20, 20, 30, 255},
                 .border = { .color = {100, 200, 255, 255}, .width = { .left = 3, .right = 3, .top = 3, .bottom = 3 } },
                 }) {
                CLAY(CLAY_ID("BorderInner1"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                     .backgroundColor = {40, 40, 60, 255},
                     .cornerRadius = CLAY_CORNER_RADIUS(8),
                     .border = { .color = {255, 180, 50, 255}, .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 } },
                     }) {
                    CLAY_TEXT(CLAY_STRING("Rounded"), { .textColor = {220, 220, 255, 255}, .fontSize = 18 });
                }
                CLAY(CLAY_ID("BorderInner2"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                     .backgroundColor = {40, 40, 60, 255},
                     .border = { .color = {80, 255, 120, 255}, .width = { .left = 6, .right = 1, .top = 1, .bottom = 6 } },
                     }) {
                    CLAY_TEXT(CLAY_STRING("Asymmetric"), { .textColor = {220, 220, 255, 255}, .fontSize = 18 });
                }
            }

            // Rounded image demo
            CLAY(CLAY_ID("RoundedImage"), {
                 .layout = { .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(120) } },
                 .cornerRadius = CLAY_CORNER_RADIUS(20),
                 .image = { .imageData = &smilingFriendImageIndex },
                 }) {}

            // Overlay demo: a panel whose overlayColor tints all children
            CLAY(CLAY_ID("OverlayDemo"), {
                 .layout = { .sizing = { CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(200) }, .padding = CLAY_PADDING_ALL(10), .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                 .backgroundColor = {50, 50, 80, 255},
                 .overlayColor = { 80, 160, 255, 100 },
                 .border = { .color = {180, 180, 255, 200}, .width = { .left = 1, .right = 1, .top = 1, .bottom = 1 } },
                 }) {
                CLAY_TEXT(CLAY_STRING("Overlay"), { .textColor = {255, 255, 255, 255}, .fontSize = 20 });
                CLAY(CLAY_ID("OverlayItem1"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 8, 8, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                     .backgroundColor = COLOR_RED,
                     .cornerRadius = CLAY_CORNER_RADIUS(4),
                     }) {
                    CLAY_TEXT(CLAY_STRING("Red"), { .textColor = {255, 255, 255, 255}, .fontSize = 16 });
                }
                CLAY(CLAY_ID("OverlayItem2"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 8, 8, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                     .backgroundColor = COLOR_ORANGE,
                     .cornerRadius = CLAY_CORNER_RADIUS(4),
                     }) {
                    CLAY_TEXT(CLAY_STRING("Orange"), { .textColor = {255, 255, 255, 255}, .fontSize = 16 });
                }
            }

            // Scrollable list with overlay color tint and a floating scrollbar
            CLAY(CLAY_ID("ScrollDemo"), {
                 .layout = { .sizing = { .width = CLAY_SIZING_FIXED(260), .height = CLAY_SIZING_FIXED(300) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                 .backgroundColor = COLOR_DARK,
                 .overlayColor = COLOR_OVERLAY,
                 }) {
                // Clipped scroll area (generates SCISSOR_START/END)
                CLAY(CLAY_ID("ScrollList"), {
                     .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(8), .childGap = 6, .layoutDirection = CLAY_TOP_TO_BOTTOM },
                     .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
                     }) {
                    for (int32_t i = 0; i < 32; ++i) {
                        CLAY(CLAY_IDI("ScrollItem", i), {
                             .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(36) }, .padding = { 8, 8, 6, 6 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                             .backgroundColor = COLOR_ITEM,
                             .cornerRadius = CLAY_CORNER_RADIUS(4),
                             }) {
                            CLAY_TEXT(CLAY_STRING("Item"), { .textColor = {220, 220, 255, 255}, .fontSize = 18 });
                        }
                    }
                }

                // Floating scrollbar thumb
                Clay_ScrollContainerData scrollData = Clay_GetScrollContainerData(Clay_GetElementId(CLAY_STRING("ScrollList")));
                if (scrollData.found && scrollData.contentDimensions.height > scrollData.scrollContainerDimensions.height) {
                    const float trackH = scrollData.scrollContainerDimensions.height;
                    const float thumbH = (trackH / scrollData.contentDimensions.height) * trackH;
                    const float thumbY = (-scrollData.scrollPosition->y / scrollData.contentDimensions.height) * trackH;
                    CLAY(CLAY_ID("ScrollThumb"), {
                         .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(thumbH) } },
                         .backgroundColor = COLOR_SCROLLBAR,
                         .cornerRadius = CLAY_CORNER_RADIUS(3),
                         .floating = {
                         .offset = { .x = -6, .y = thumbY },
                         .parentId = Clay_GetElementId(CLAY_STRING("ScrollList")).id,
                         .zIndex = UI::ZIndex::LIST_DECORATION,
                         .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                         .attachTo = CLAY_ATTACH_TO_PARENT,
                         },
                         }) {}
                }
            }
        }
#endif

        const Core::TimeFrame& tf = frameBuffer->timeFrame;
        const auto renderFpsText = Core::InlineString<48>::Format("Render: %.0f FPS (%.2f ms)", tf.renderWallMs > 0.0f ? 1000.0f / tf.renderWallMs : 0.0f, tf.renderWallMs);
        const auto gpuText = Core::InlineString<48>::Format("GPU: %.2f ms", tf.gpuFrameMs);
        const auto gameFpsText = Core::InlineString<48>::Format("Game: %.0f FPS (%.2f ms)", tf.gameFps, tf.gameFps > 0.0f ? 1000.0f / tf.gameFps : 0.0f);
        const Clay_String renderFpsString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(renderFpsText.Size()), .chars = renderFpsText.c_str()};
        const Clay_String gpuString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(gpuText.Size()), .chars = gpuText.c_str()};
        const Clay_String gameFpsString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(gameFpsText.Size()), .chars = gameFpsText.c_str()};

        static constexpr const char* AA_MODE_NAMES[] = {"None", "SMAA", "TAA", "SMAA T2X", "Naive TAA", "Donut TAA"};
        const char* aaModeName = AA_MODE_NAMES[static_cast<int32_t>(state->lighting.aaConfig.mode)];
        const char* profileName = state->projectConfig.activeLightingProfile.IsEmpty() ? "(none)" : state->projectConfig.activeLightingProfile.c_str();
        const auto profileText = Core::InlineString<80>::Format("Profile: %s", profileName);
        const auto aaText = Core::InlineString<48>::Format("AA: %s | Denoiser: %s", aaModeName, DenoiserModeName(state->debug.restir.denoiserMode));
        Core::InlineString<48> giText("Diffuse GI: ");
        AppendDiffuseGIModeText(giText, state->lighting);

        const auto renderWidth = static_cast<uint32_t>(static_cast<float>(ctx->windowContext.viewportWidth) * state->projectConfig.resolutionScale);
        const auto renderHeight = static_cast<uint32_t>(static_cast<float>(ctx->windowContext.viewportHeight) * state->projectConfig.resolutionScale);
        const auto resText = Core::InlineString<64>::Format("Res: %ux%u (%.0f%%)", renderWidth, renderHeight, state->projectConfig.resolutionScale * 100.0f);

        const Core::PostProcessConfiguration& pp = state->lighting.postProcess;
        Core::InlineString<160> ppSummary("PP:");
        bool bAnyPPActive = false;
        auto appendPPTag = [&](bool bEnabled, const char* tag) {
            if (!bEnabled) { return; }
            ppSummary.Append(bAnyPPActive ? ", " : " ");
            ppSummary.Append(tag);
            bAnyPPActive = true;
        };
        appendPPTag(state->lighting.gtaoConfig.bEnabled, "GTAO");
        appendPPTag(pp.bExposureEnabled, "Exposure");
        appendPPTag(pp.bBloomEnabled, "Bloom");
        appendPPTag(pp.bMotionBlurEnabled, "MotionBlur");
        appendPPTag(pp.bColorGradingEnabled, "ColorGrade");
        appendPPTag(pp.bVignetteEnabled, "Vignette");
        appendPPTag(pp.bChromaticAberrationEnabled, "ChromAb");
        appendPPTag(pp.bSharpeningEnabled, "Sharpen");
        appendPPTag(pp.bPaniniEnabled, "Panini");
        appendPPTag(pp.bFilmGrainEnabled, "FilmGrain");
        appendPPTag(pp.bDitherEnabled, "Dither");
        if (!bAnyPPActive) { ppSummary.Append(" None"); }

        const Clay_String profileString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(profileText.Size()), .chars = profileText.c_str()};
        const Clay_String aaString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(aaText.Size()), .chars = aaText.c_str()};
        const Clay_String giString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(giText.Size()), .chars = giText.c_str()};
        const Clay_String resString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(resText.Size()), .chars = resText.c_str()};
        const Clay_String ppString{.isStaticallyAllocated = false, .length = static_cast<int32_t>(ppSummary.Size()), .chars = ppSummary.c_str()};

        CLAY(CLAY_ID("FpsCounter"), {
             .layout = { .sizing = { .width = CLAY_SIZING_FIXED(340) }, .padding = CLAY_PADDING_ALL(8), .childGap = 4, .layoutDirection = CLAY_TOP_TO_BOTTOM },
             .backgroundColor = {0, 0, 0, 140},
             .cornerRadius = CLAY_CORNER_RADIUS(4),
             .floating = {
             .offset = { .x = 16, .y = -16 },
             .zIndex = UI::ZIndex::HUD_OVERLAY,
             .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
             .attachTo = CLAY_ATTACH_TO_ROOT,
             },
             }) {
            CLAY_TEXT(renderFpsString, { .textColor = {255, 255, 255, 255}, .fontSize = 16 });
            CLAY_TEXT(gpuString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(gameFpsString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(profileString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(aaString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(giString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(resString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
            CLAY_TEXT(ppString, { .textColor = {200, 200, 200, 255}, .fontSize = 16 });
        }
#if WILL_EDITOR
    } // state->debug.bEnableUI
#endif

    Clay_RenderCommandArray renderCommands = Clay_EndLayout(frameBuffer->timeFrame.deltaTime);

    const auto vpWidth = static_cast<float>(ctx->windowContext.viewportWidth);
    const auto vpHeight = static_cast<float>(ctx->windowContext.viewportHeight);

    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    const Engine::Font* uiFont = ctx->assetManager->GetFont(state->uiFont);

    for (int32_t i = 0; i < renderCommands.length; ++i) {
        const Clay_RenderCommand& cmd = renderCommands.internalArray[i];

        switch (cmd.commandType) {
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                Core::UIDrawCommand dc{.type = Core::UICommandType::ScissorPush};
                dc.scissor = Core::UIScissorCommand{static_cast<int32_t>(bb.x), static_cast<int32_t>(bb.y), static_cast<uint32_t>(bb.width), static_cast<uint32_t>(bb.height)};
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            {
                vf.uiDrawList.PushBack(Core::UIDrawCommand{.type = Core::UICommandType::ScissorPop});
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START:
            {
                const Clay_Color& c = cmd.renderData.overlayColor.color;
                Core::UIDrawCommand dc{.type = Core::UICommandType::OverlayPush};
                dc.overlay = Core::UIOverlayColorCommand{.color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}};
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END:
            {
                vf.uiDrawList.PushBack(Core::UIDrawCommand{.type = Core::UICommandType::OverlayPop});
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_Color& c = cmd.renderData.rectangle.backgroundColor;
                const Clay_CornerRadius& cr = cmd.renderData.rectangle.cornerRadius;
                Core::UIDrawCommand dc{.type = Core::UICommandType::Rect};
                dc.rect = Core::UIRectDrawCall{
                    .color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f},
                    .cornerRadius = {cr.topLeft, cr.topRight, cr.bottomLeft, cr.bottomRight},
                    .pxMin = {bb.x, bb.y},
                    .pxMax = {bb.x + bb.width, bb.y + bb.height},
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_ImageRenderData& img = cmd.renderData.image;
                const uint32_t bindlessIndex = *static_cast<const uint32_t*>(img.imageData);
                const Clay_Color& tc = img.backgroundColor;
                const bool bUntinted = tc.r == 0 && tc.g == 0 && tc.b == 0 && tc.a == 0;
                const Vec4 tint = bUntinted
                                      ? Vec4{1.0f, 1.0f, 1.0f, 1.0f}
                                      : Vec4{tc.r / 255.0f, tc.g / 255.0f, tc.b / 255.0f, tc.a / 255.0f};
                const Clay_CornerRadius& cr = img.cornerRadius;
                Core::UIDrawCommand dc{.type = Core::UICommandType::Image};
                dc.image = Core::UIRenderCommandImage{
                    .pxMin = {bb.x, bb.y},
                    .pxMax = {bb.x + bb.width, bb.y + bb.height},
                    .uvMin = {0.0f, 1.0f}, // y flip: viewport Y-flip in SetupUIRender inverts V
                    .uvMax = {1.0f, 0.0f},
                    .tintColor = tint,
                    .cornerRadius = {cr.topLeft, cr.topRight, cr.bottomLeft, cr.bottomRight},
                    .imageBindlessIndex = bindlessIndex,
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER:
            {
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_BorderRenderData& bd = cmd.renderData.border;
                const Clay_Color& c = bd.color;
                Core::UIDrawCommand dc{.type = Core::UICommandType::Border};
                dc.border = Core::UIBorderDrawCall{
                    .color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f},
                    .borderWidths = {
                        static_cast<float>(bd.width.left),
                        static_cast<float>(bd.width.right),
                        static_cast<float>(bd.width.top),
                        static_cast<float>(bd.width.bottom),
                    },
                    .cornerRadius = {
                        bd.cornerRadius.topLeft,
                        bd.cornerRadius.topRight,
                        bd.cornerRadius.bottomLeft,
                        bd.cornerRadius.bottomRight,
                    },
                    .pxMin = {bb.x, bb.y},
                    .pxMax = {bb.x + bb.width, bb.y + bb.height},
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT:
            {
                if (!uiFont) { break; }
                const Clay_BoundingBox& bb = cmd.boundingBox;
                const Clay_TextRenderData& td = cmd.renderData.text;
                const float fontSize = td.fontSize;
                const float scale = fontSize / uiFont->header.emSize;
                const Vec4 color{td.textColor.r / 255.0f, td.textColor.g / 255.0f, td.textColor.b / 255.0f, td.textColor.a / 255.0f};

                const auto quadStart = static_cast<uint32_t>(vf.uiGlyphQuads.Size());
                uint32_t quadCount = 0;

                float cursorX = bb.x;
                const float baselineY = bb.y + fontSize;

                for (int32_t ci = 0; ci < td.stringContents.length; ++ci) {
                    const uint32_t cp = static_cast<unsigned char>(td.stringContents.chars[ci]);
                    const Engine::WGlyphInfo* g = ctx->assetManager->GetGlyph(state->uiFont, cp);
                    if (!g) {
                        cursorX += fontSize * 0.25f;
                        continue;
                    }

                    const float xMin = (cursorX + g->planeLeft * scale) / vpWidth * 2.0f - 1.0f;
                    const float yMax = (baselineY - g->planeBottom * scale) / vpHeight * 2.0f - 1.0f;
                    const float xMax = (cursorX + g->planeRight * scale) / vpWidth * 2.0f - 1.0f;
                    const float yMin = (baselineY - g->planeTop * scale) / vpHeight * 2.0f - 1.0f;

                    vf.uiGlyphQuads.PushBack(UIGlyphQuad{
                        .color = {color.x, color.y, color.z, color.w},
                        .posMin = {xMin, yMin},
                        .posMax = {xMax, yMax},
                        .uvMin = {g->uvLeft, g->uvBottom},
                        .uvMax = {g->uvRight, g->uvTop},
                        .uvOrigMin = {g->uvLeft, g->uvBottom},
                        .uvOrigMax = {g->uvRight, g->uvTop},
                    });
                    ++quadCount;

                    cursorX += g->advance * scale + td.letterSpacing;
                }

                if (quadCount == 0) { break; }

                Core::UIDrawCommand dc{.type = Core::UICommandType::Text};
                dc.text = Core::UITextDrawCall{
                    .quadOffset = quadStart,
                    .quadCount = quadCount,
                    .atlasBindlessIndex = uiFont->atlasTexture.bindlessHandle.index,
                    .pxRange = static_cast<float>(uiFont->header.sdfSpread),
                    .zIndex = cmd.zIndex,
                };
                vf.uiDrawList.PushBack(dc);
                break;
            }
            default: break;
        }
    }
}
}
