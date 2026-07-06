//
// Created by William on 2025-12-26.
//

#include "render_systems.h"

#include <tracy/Tracy.hpp>

#include "scene_system.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/arena_vector.h"
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
#include "game/components/render/text3d_component.h"
#include "game/components/render/light_components.h"
#include "render/shaders/lights_interop.h"
#include "render/shaders/text_interop.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/render/text_component.h"
#include "game/components/common/stable_id_component.h"
#include "game/components/core_components.h"
#include "game/components/physics/physics_body_desc.h"


namespace Game
{
void ConnectRenderObservers(entt::registry& registry)
{
    registry.on_construct<Component::TransformComponent>().connect<&Component::TransformComponent::OnConstruct>();
    registry.on_destroy<Component::TransformComponent>().connect<&Component::TransformComponent::OnDestroy>();

    registry.on_destroy<Component::MeshRuntime>().connect<&Component::MeshRuntime::OnDestroy>();

    registry.on_construct<Component::StaticMeshComponent>().connect<&Component::StaticMeshComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshComponent>().connect<&Component::StaticMeshComponent::OnDestroy>();

    registry.on_construct<Component::StaticMeshPrimitiveComponent>().connect<&Component::StaticMeshPrimitiveComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshPrimitiveComponent>().connect<&Component::StaticMeshPrimitiveComponent::OnDestroy>();

    registry.on_construct<Component::ProceduralMeshComponent>().connect<&Component::ProceduralMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ProceduralMeshComponent>().connect<&Component::ProceduralMeshComponent::OnDestroy>();

    registry.on_construct<Component::SplineMeshComponent>().connect<&Component::SplineMeshComponent::OnConstruct>();
    registry.on_destroy<Component::SplineMeshComponent>().connect<&Component::SplineMeshComponent::OnDestroy>();

    registry.on_construct<Component::TextComponent>().connect<&Component::TextComponent::OnConstruct>();
    registry.on_destroy<Component::TextComponent>().connect<&Component::TextComponent::OnDestroy>();

    registry.on_construct<Component::Text3DComponent>().connect<&Component::Text3DComponent::OnConstruct>();
    registry.on_destroy<Component::Text3DComponent>().connect<&Component::Text3DComponent::OnDestroy>();

    registry.on_construct<Component::AreaLightComponent>().connect<&Component::AreaLightComponent::OnConstruct>();
    registry.on_destroy<Component::AreaLightComponent>().connect<&Component::AreaLightComponent::OnDestroy>();

    registry.on_construct<Component::SphereLightComponent>().connect<&Component::SphereLightComponent::OnConstruct>();
    registry.on_destroy<Component::SphereLightComponent>().connect<&Component::SphereLightComponent::OnDestroy>();
}

void DisconnectRenderObservers(entt::registry& registry)
{
    registry.on_construct<Component::TransformComponent>().disconnect<&Component::TransformComponent::OnConstruct>();
    registry.on_destroy<Component::TransformComponent>().disconnect<&Component::TransformComponent::OnDestroy>();

    registry.on_destroy<Component::MeshRuntime>().disconnect<&Component::MeshRuntime::OnDestroy>();

    registry.on_construct<Component::StaticMeshComponent>().disconnect<&Component::StaticMeshComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshComponent>().disconnect<&Component::StaticMeshComponent::OnDestroy>();

    registry.on_construct<Component::StaticMeshPrimitiveComponent>().disconnect<&Component::StaticMeshPrimitiveComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshPrimitiveComponent>().disconnect<&Component::StaticMeshPrimitiveComponent::OnDestroy>();

    registry.on_construct<Component::ProceduralMeshComponent>().disconnect<&Component::ProceduralMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ProceduralMeshComponent>().disconnect<&Component::ProceduralMeshComponent::OnDestroy>();

    registry.on_construct<Component::SplineMeshComponent>().disconnect<&Component::SplineMeshComponent::OnConstruct>();
    registry.on_destroy<Component::SplineMeshComponent>().disconnect<&Component::SplineMeshComponent::OnDestroy>();

    registry.on_construct<Component::TextComponent>().disconnect<&Component::TextComponent::OnConstruct>();
    registry.on_destroy<Component::TextComponent>().disconnect<&Component::TextComponent::OnDestroy>();

    registry.on_construct<Component::Text3DComponent>().disconnect<&Component::Text3DComponent::OnConstruct>();
    registry.on_destroy<Component::Text3DComponent>().disconnect<&Component::Text3DComponent::OnDestroy>();

    registry.on_construct<Component::AreaLightComponent>().disconnect<&Component::AreaLightComponent::OnConstruct>();
    registry.on_destroy<Component::AreaLightComponent>().disconnect<&Component::AreaLightComponent::OnDestroy>();

    registry.on_construct<Component::SphereLightComponent>().disconnect<&Component::SphereLightComponent::OnConstruct>();
    registry.on_destroy<Component::SphereLightComponent>().disconnect<&Component::SphereLightComponent::OnDestroy>();
}

void ModelHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->pendingHotReloadModelIds.IsEmpty()) { return; }

    // Freeze + release refs so the model drains, then re-arm; the load resolves re-acquire the fresh version after ResolveUnloads unfreezes on reclaim.
    auto isHot = [&](Engine::ModelID id) {
        if (!ctx->assetManager->IsModelFrozen(id)) { return false; }
        for (const Engine::ModelID& hotId : state->pendingHotReloadModelIds) {
            if (id == hotId) { return true; }
        }
        return false;
    };

    // Only resident models need freezing; a non-resident one's fresh load already reads the regenerated file.
    for (const Engine::ModelID& hotId : state->pendingHotReloadModelIds) {
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
        state->bPendingModelResolve = true;
    }
}

void FontHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->pendingHotReloadFontIds.IsEmpty()) { return; }

    // The UI font is engine-held (no component owns it) so it can never drain; reject its hot-reload and require a restart.
    Engine::FontID uiFontId{};
    if (const Engine::Font* uiFont = ctx->assetManager->GetFont(state->uiFont)) { uiFontId = uiFont->fontId; }

    // Freeze + release refs so the font drains, then re-arm; the resolve systems re-acquire the fresh version after ResolveUnloads unfreezes on reclaim.
    auto isHot = [&](Engine::FontID id) {
        if (!ctx->assetManager->IsFontFrozen(id)) { return false; }
        for (const Engine::FontID& hotId : state->pendingHotReloadFontIds) {
            if (id == hotId) { return true; }
        }
        return false;
    };

    for (const Engine::FontID& hotId : state->pendingHotReloadFontIds) {
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
        state->bPendingModelResolve = true;
    }
}

void TextureHotReload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->pendingHotReloadTextureIds.IsEmpty()) { return; }

    for (auto hotId : state->pendingHotReloadTextureIds) {
        if (ctx->assetManager->IsTextureLoaded(hotId)) {
            ctx->assetManager->ReloadTexture(hotId);
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

static void FreeMeshRange(Engine::EngineContext* ctx, Engine::EngineState* state, Component::MeshRuntime* runtime)
{
    if (!runtime->range.IsValid()) { return; }
    Engine::MeshPrimitiveStore& store = state->meshPrimitiveStore;
    for (uint32_t i = 0; i < runtime->range.count; ++i) {
        ctx->materialManager->ReleaseMaterial(store[runtime->range.offset + i].materialID);
    }
    store.Free(runtime->range);
    runtime->range = {};
}

static void FillSingleMeshRange(Engine::EngineContext* ctx, Engine::EngineState* state, Component::MeshRuntime* runtime, Engine::StaticModel* model, Engine::MaterialID material)
{
    FreeMeshRange(ctx, state, runtime);

    if (model->modelData.meshes.IsEmpty()) { return; }
    Engine::MeshInformation& mesh = model->modelData.meshes[0];
    const auto count = static_cast<uint32_t>(mesh.primitiveProperties.Size());
    if (count == 0) { return; }

    Engine::MeshPrimitiveStore& store = state->meshPrimitiveStore;
    Engine::MeshPrimitiveStore::Range range = store.Allocate(count);
    if (!range.IsValid()) {
        LOG_ERROR(Game, "Static primitive store full; cannot resolve single-mesh runtime for model ({})", model->name.c_str());
        return;
    }

    uint32_t writeIndex = range.offset;
    for (uint32_t j = 0; j < count; ++j) {
        Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[j];
        store[writeIndex] = {
            .primitiveIndex = primitive.index,
            .originalMaterialIndex = -1,
            .sourceNodeIndex = 0,
            .modelPrimitiveOrdinal = j,
            .materialID = material,
            .blasDeviceAddress = primitive.blasDeviceAddress,
            .modelSpaceTransform = glm::mat4(1.0f),
        };
        ctx->materialManager->AcquireMaterial(material);
        ++writeIndex;
    }
    runtime->range = range;
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
        Engine::MeshPrimitiveStore& store = state->meshPrimitiveStore;

        if (runtime->range.IsValid()) {
            for (uint32_t i = 0; i < runtime->range.count; ++i) {
                materialManager->ReleaseMaterial(store[runtime->range.offset + i].materialID);
            }
            store.Free(runtime->range);
            runtime->range = {};
        }

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
        {
            uint32_t ordinal = 0;
            for (size_t n = 0; n < nodes.Size(); ++n) {
                const uint32_t mi = nodes[n].meshIndex;
                if (mi == ~0u || mi >= meshes.Size()) { continue; }
                const size_t nodePrimCount = meshes[mi].primitiveProperties.Size();
                for (size_t j = 0; j < nodePrimCount; ++j) {
                    if (included(ordinal)) { ++totalPrimitives; }
                    ++ordinal;
                }
            }
        }

        if (totalPrimitives == 0) {
            resolved.PushBack(entity);
            continue;
        }

        Engine::MeshPrimitiveStore::Range range = store.Allocate(totalPrimitives);
        if (!range.IsValid()) {
            LOG_ERROR(Game, "Static primitive store full; cannot flatten model ({}) needing {} primitives", model->name.c_str(), totalPrimitives);
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
        for (uint32_t n = 0; n < nodes.Size(); ++n) {
            const uint32_t mi = nodes[n].meshIndex;
            if (mi == ~0u || mi >= meshes.Size()) { continue; }

            const glm::mat4 nodeModelSpace = ComposeNodeModelSpace(nodes, n);
            Engine::MeshInformation& mesh = meshes[mi];

            for (size_t j = 0; j < mesh.primitiveProperties.Size(); ++j) {
                const uint32_t thisOrdinal = ordinal++;
                if (!included(thisOrdinal)) { continue; }

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

                store[writeIndex] = {
                    .primitiveIndex = primitive.index,
                    .originalMaterialIndex = primitive.materialIndex,
                    .sourceNodeIndex = n,
                    .modelPrimitiveOrdinal = thisOrdinal,
                    .materialID = matID,
                    .blasDeviceAddress = primitive.blasDeviceAddress,
                    .modelSpaceTransform = nodeModelSpace,
                };
                materialManager->AcquireMaterial(matID);
                ++writeIndex;
            }
        }

        runtime->range = range;
        resolved.PushBack(entity);
    }

    if (modelsWaitingThisTick > 0) {
        state->pendingModelWaitCount += modelsWaitingThisTick;
        state->modelWaitLastActivity = std::chrono::steady_clock::now();
    }
    if (state->pendingModelWaitCount > 0 && modelsWaitingThisTick == 0 && (std::chrono::steady_clock::now() - state->modelWaitLastActivity) >= std::chrono::seconds(1)) {
        LOG_TRACE(Game, "{} model(s) not yet done loading", state->pendingModelWaitCount);
        state->pendingModelWaitCount = 0;
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
        FreeMeshRange(ctx, state, runtime);

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
                if (ordinal == meshComponent.primitiveOrdinal) { targetNode = n; targetPrim = &mesh.primitiveProperties[j]; break; }
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

        Engine::MeshPrimitiveStore& store = state->meshPrimitiveStore;
        Engine::MeshPrimitiveStore::Range range = store.Allocate(1);
        if (!range.IsValid()) {
            LOG_ERROR(Game, "Static primitive store full; cannot resolve split primitive");
            resolved.PushBack(entity);
            continue;
        }

        store[range.offset] = {
            .primitiveIndex = targetPrim->index,
            .originalMaterialIndex = targetPrim->materialIndex,
            .sourceNodeIndex = targetNode,
            .modelPrimitiveOrdinal = meshComponent.primitiveOrdinal,
            .materialID = matID,
            .blasDeviceAddress = targetPrim->blasDeviceAddress,
            .modelSpaceTransform = glm::mat4(1.0f),
        };
        materialManager->AcquireMaterial(matID);
        runtime->range = range;

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
        FillSingleMeshRange(ctx, state, runtime, model, matID);

        resolved.PushBack(entity);
    }

    if (proceduralWaitingThisTick > 0) {
        state->pendingProceduralWaitCount += proceduralWaitingThisTick;
        state->proceduralWaitLastActivity = std::chrono::steady_clock::now();
    }
    if (state->pendingProceduralWaitCount > 0 && proceduralWaitingThisTick == 0 && (std::chrono::steady_clock::now() - state->proceduralWaitLastActivity) >= std::chrono::seconds(1)) {
        LOG_TRACE(Game, "{} procedural model(s) not yet done loading", state->pendingProceduralWaitCount);
        state->pendingProceduralWaitCount = 0;
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::ProceduralMeshLoadingTag>(entity);
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
        FillSingleMeshRange(ctx, state, runtime, model, matID);

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
            runtime.modelHandle = ctx->assetManager->LoadText3DModel(textComponent.fontId, textComponent.text, textComponent.depth, textComponent.flatness, textComponent.tracking, textComponent.scale, textComponent.bSmoothNormals);
            if (runtime.modelHandle.IsValid()) {
                state->registry.emplace_or_replace<Component::Text3DLoadingTag>(entity);
                state->bPendingModelResolve = true;
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
            FreeMeshRange(ctx, state, runtime);
            resolved.PushBack(entity); // nothing to resolve (e.g. empty text / no font); drop the tag
            continue;
        }

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Text3D model ({}) is not in the asset manager.", runtime->modelHandle.index);
            continue;
        }
        if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
            FreeMeshRange(ctx, state, runtime);
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
        FillSingleMeshRange(ctx, state, runtime, model, matID);

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
        world.rotation = glm::slerp(physics.previousRotation, physics.currentRotation, alpha);
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
            world.rotation = glm::slerp(physics->previousRotation, physics->currentRotation, alpha);
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

    const Vec2 scroll = state->input.GetActionState(Actions::ACTION_UI_SCROLL).axis;
    Clay_UpdateScrollContainers(true, Clay_Vector2{scroll.x, scroll.y}, state->timeFrame->deltaTime);
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

    // Area light emissive quads
    for (auto [entity, light, transform, areaLightTransform, dirty] : state->registry.view<Component::AreaLightComponent, Component::TransformComponent, Component::AreaLightTransformComponent, Component::MultiframeDirtyTransformComponent>().each()) {
        areaLightTransform.previousMatrix = areaLightTransform.modelMatrix;
        areaLightTransform.modelMatrix = Component::ComputeAreaLightQuadMatrix(transform, light);
    }

    // Sphere light emissive meshes
    for (auto [entity, light, transform, sphereLightTransform, dirty] : state->registry.view<Component::SphereLightComponent, Component::TransformComponent, Component::SphereLightTransformComponent, Component::MultiframeDirtyTransformComponent>().each()) {
        sphereLightTransform.previousMatrix = sphereLightTransform.modelMatrix;
        sphereLightTransform.modelMatrix = Component::ComputeSphereLightMatrix(transform, light);
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
    auto& materialManager = ctx->materialManager;

    {
        ZoneScopedN("SyncMeshVisibility");
        for (auto [entity, meshComponent, runtime] : state->registry.view<Component::StaticMeshComponent, Component::MeshRuntime>().each()) {
            runtime.visible = meshComponent.modelFlags.x != 0.0f;
        }
        for (auto [entity, meshComponent, runtime] : state->registry.view<Component::StaticMeshPrimitiveComponent, Component::MeshRuntime>().each()) {
            runtime.visible = meshComponent.modelFlags.x != 0.0f;
        }
        for (auto [entity, meshComponent, runtime] : state->registry.view<Component::ProceduralMeshComponent, Component::MeshRuntime>().each()) {
            runtime.visible = meshComponent.modelFlags.x != 0.0f;
        }
        for (auto [entity, meshComponent, runtime] : state->registry.view<Component::SplineMeshComponent, Component::MeshRuntime>().each()) {
            runtime.visible = meshComponent.modelFlags.x != 0.0f;
        }
        for (auto [entity, meshComponent, runtime] : state->registry.view<Component::Text3DComponent, Component::MeshRuntime>().each()) {
            runtime.visible = meshComponent.modelFlags.x != 0.0f;
        }
    }

    // Gather regular renderables
    {
        ZoneScopedN("MeshRuntimeGather");
        auto view = state->registry.view<Component::MeshRuntime, Component::RenderTransformComponent>(
            entt::exclude<
                Component::PortalPlaneTag,
                Component::CubemapVisualizeTag
            >);

        Engine::MeshPrimitiveStore& store = state->meshPrimitiveStore;

        for (const auto& [entity, runtime, renderTransform] : view.each()) {
            if (!runtime.range.IsValid()) { continue; }
            if (!runtime.visible) { continue; }

            uint64_t stableId = 1234567890;
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                stableId = stable->id.id;
            }

            uint32_t modelIndex = 0;
            uint32_t lastNode = ~0u;
            const uint32_t base = runtime.range.offset;
            const uint32_t count = runtime.range.count;
            for (uint32_t i = 0; i < count; ++i) {
                const Engine::MeshPrimitiveInstance& inst = store[base + i];
                if (inst.sourceNodeIndex != lastNode) {
                    modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
                    frameBuffer->mainViewFamily.modelMatrices.EmplaceBack(renderTransform.modelMatrix * inst.modelSpaceTransform, renderTransform.previousMatrix * inst.modelSpaceTransform);
                    lastNode = inst.sourceNodeIndex;
                }
                frameBuffer->mainViewFamily.primitiveInstances.PushBack({
                    .primitiveIndex = inst.primitiveIndex,
                    .materialID = inst.materialID,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                    .blasDeviceAddress = inst.blasDeviceAddress,
                });
            }
        }
    }

    {
        ZoneScopedN("AreaLightQuads");
        const Engine::StaticModelHandle quadHandle = state->builtinAssets.GetUnitQuad(ctx->assetManager);
        Engine::StaticModel* quadModel = ctx->assetManager->GetModel(quadHandle);
        const Engine::Material* defaultMaterial = materialManager->GetMaterial(materialManager->GetDefaultMaterialID());
        if (defaultMaterial && quadModel && quadModel->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded
            && !quadModel->modelData.meshes.IsEmpty() && !quadModel->modelData.meshes[0].primitiveProperties.IsEmpty()) {
            const uint32_t quadPrimitiveIndex = quadModel->modelData.meshes[0].primitiveProperties[0].index;
            const uint64_t quadBlasDeviceAddress = quadModel->modelData.meshes[0].primitiveProperties[0].blasDeviceAddress;

            Engine::Material emissiveMaterial = *defaultMaterial; // only emissiveFactor changes per light; black albedo so only emission shows
            emissiveMaterial.props.colorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            emissiveMaterial.props.alphaProperties.z = 1.0f; // double sided

            for (const auto& [entity, light, areaLightTransform] : state->registry.view<Component::AreaLightComponent, Component::AreaLightTransformComponent>().each()) {
                if (!light.drawEmissiveSurface) { continue; }
                const auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
                frameBuffer->mainViewFamily.modelMatrices.EmplaceBack(areaLightTransform.modelMatrix, areaLightTransform.previousMatrix);

                emissiveMaterial.props.emissiveFactor = glm::vec4(light.color, light.intensity);
                const Engine::MaterialID materialKey = Engine::HashMaterial(emissiveMaterial);

                auto [materialIndex, inserted] = frameBuffer->mainViewFamily.activeMaterials.TryEmplace(materialKey);
                if (inserted) {
                    materialIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.materials.Size());
                    frameBuffer->mainViewFamily.materials.PushBack(Engine::RenderMaterial{emissiveMaterial.props, emissiveMaterial.fragmentShader, emissiveMaterial.lightingShader});
                }

                uint64_t stableId = 1234567890;
                if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                    stableId = stable->id.id;
                }

                const uint32_t* lightIdxPtr = frameBuffer->mainViewFamily.lightEntityToIndex.Find(static_cast<uint32_t>(entity));

                frameBuffer->mainViewFamily.primitiveInstances.PushBack({
                    .primitiveIndex = quadPrimitiveIndex,
                    .materialID = materialKey,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                    .blasDeviceAddress = quadBlasDeviceAddress,
                    .lightIndex = lightIdxPtr ? *lightIdxPtr : 0xFFFFFFFFu,
                });
            }
        }
    }

    // Gather sphere light emissive meshes.
    {
        ZoneScopedN("SphereLightMeshes");
        const Engine::StaticModelHandle sphereHandle = state->builtinAssets.GetUnitSphere(ctx->assetManager);
        Engine::StaticModel* sphereModel = ctx->assetManager->GetModel(sphereHandle);
        const Engine::Material* defaultMaterial = materialManager->GetMaterial(materialManager->GetDefaultMaterialID());
        if (defaultMaterial && sphereModel && sphereModel->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded
            && !sphereModel->modelData.meshes.IsEmpty() && !sphereModel->modelData.meshes[0].primitiveProperties.IsEmpty()) {
            const uint32_t spherePrimitiveIndex = sphereModel->modelData.meshes[0].primitiveProperties[0].index;
            const uint64_t sphereBlasDeviceAddress = sphereModel->modelData.meshes[0].primitiveProperties[0].blasDeviceAddress;

            Engine::Material emissiveMaterial = *defaultMaterial;
            emissiveMaterial.props.colorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            emissiveMaterial.props.alphaProperties.z = 1.0f; // double sided

            for (const auto& [entity, light, sphereLightTransform] : state->registry.view<Component::SphereLightComponent, Component::SphereLightTransformComponent>().each()) {
                if (!light.drawEmissiveSurface) { continue; }
                const auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
                frameBuffer->mainViewFamily.modelMatrices.EmplaceBack(sphereLightTransform.modelMatrix, sphereLightTransform.previousMatrix);

                emissiveMaterial.props.emissiveFactor = glm::vec4(light.color, light.intensity);
                const Engine::MaterialID materialKey = Engine::HashMaterial(emissiveMaterial);

                auto [materialIndex, inserted] = frameBuffer->mainViewFamily.activeMaterials.TryEmplace(materialKey);
                if (inserted) {
                    materialIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.materials.Size());
                    frameBuffer->mainViewFamily.materials.PushBack(Engine::RenderMaterial{emissiveMaterial.props, emissiveMaterial.fragmentShader, emissiveMaterial.lightingShader});
                }

                uint64_t stableId = 1234567890;
                if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                    stableId = stable->id.id;
                }

                const uint32_t* lightIdxPtr = frameBuffer->mainViewFamily.lightEntityToIndex.Find(static_cast<uint32_t>(entity));
                frameBuffer->mainViewFamily.primitiveInstances.PushBack({
                    .primitiveIndex = spherePrimitiveIndex,
                    .materialID = materialKey,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                    .blasDeviceAddress = sphereBlasDeviceAddress,
                    .lightIndex = lightIdxPtr ? *lightIdxPtr : 0xFFFFFFFFu,
                });
            }
        }
    }

    // Material remap
    {
        ZoneScopedN("Material Recording");
        for (auto& instance : frameBuffer->mainViewFamily.primitiveInstances) {
            auto [val, inserted] = frameBuffer->mainViewFamily.activeMaterials.TryEmplace(instance.materialID);
            if (inserted) {
                val = frameBuffer->mainViewFamily.materials.Size();
                frameBuffer->mainViewFamily.materials.PushBack(materialManager->GetRenderMaterial(instance.materialID));
            }
        }
    }

    if (state->lighting.skybox.IsValid()) {
        Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(state->lighting.skybox);
        if (cubemap && cubemap->loadState == Render::Cubemap::LoadState::Loaded) {
            frameBuffer->mainViewFamily.skyboxIndex = state->lighting.skybox.index;
            frameBuffer->mainViewFamily.skyboxLOD = state->lighting.skyboxLOD;
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

        const auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
        frameBuffer->mainViewFamily.modelMatrices.EmplaceBack(renderTransform.modelMatrix, renderTransform.previousMatrix);

        const auto drawCallIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.textInstances.Size());
        uint32_t quadCount = 0;

        float cursorX = 0.0f;
        for (size_t i = 0; i < textComp.text.Size(); ++i) {
            const uint32_t codepoint = static_cast<unsigned char>(textComp.text.c_str()[i]);
            const Engine::WGlyphInfo* g = ctx->assetManager->GetGlyph(runtime.fontHandle, codepoint);
            if (!g) {
                cursorX += textComp.renderSizePx * 0.25f;
                continue;
            }

            WorldGlyphQuad quad{};
            quad.posMin = {cursorX + g->planeLeft * scale, g->planeBottom * scale};
            quad.posMax = {cursorX + g->planeRight * scale, g->planeTop * scale};
            quad.uvMin = {g->uvLeft, g->uvBottom};
            quad.uvMax = {g->uvRight, g->uvTop};
            quad.color = textComp.color;
            quad.drawCallIndex = drawCallIndex;
            frameBuffer->mainViewFamily.worldGlyphQuads.PushBack(quad);
            ++quadCount;

            cursorX += g->advance * scale;
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

void GatherLights(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    Core::ViewFamily& vf = frameBuffer->mainViewFamily;

    auto pointView = state->registry.view<Component::PointLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : pointView.each()) {
        if (vf.pointLights.IsFull()) { break; }
        const glm::vec3& c = light.color;
        vf.pointLights.PushBack(PointLightData{
            .positionRange = {transform.translation, light.range},
            .packedColor =
            (static_cast<uint32_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f)) |
            (static_cast<uint32_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
            (static_cast<uint32_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
            (0xFFu << 24),
            .intensity = light.intensity,
        });
    }

    auto areaView = state->registry.view<Component::AreaLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : areaView.each()) {
        if (vf.lights.IsFull()) { break; }
        vf.lightEntityToIndex[static_cast<uint32_t>(entity)] = static_cast<uint32_t>(vf.lights.Size());
        const glm::mat3 rot = glm::mat3_cast(transform.rotation);
        const glm::vec3 normal = rot[2];
        const glm::vec3 right = rot[0];
        const glm::vec3 up = rot[1];
        const glm::vec3& c = light.color;
        vf.lights.PushBack(LightInfo{
            .position = {transform.translation, 0.0f},
            .normal = {normal, 0.0f},
            .right = {right, light.halfWidth * transform.scale.x},
            .up = {up, light.halfHeight * transform.scale.y},
            .packedColor =
            (static_cast<uint32_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f)) |
            (static_cast<uint32_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
            (static_cast<uint32_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
            (0xFFu << 24),
            .intensity = light.intensity,
            .range = light.range,
            .type = LIGHT_TYPE_AREA,
        });
    }

    auto sphereView = state->registry.view<Component::SphereLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : sphereView.each()) {
        if (vf.lights.IsFull()) { break; }
        vf.lightEntityToIndex[static_cast<uint32_t>(entity)] = static_cast<uint32_t>(vf.lights.Size());
        const glm::vec3& c = light.color;
        vf.lights.PushBack(LightInfo{
            .position = {transform.translation, 0.0f},
            .normal = {0.0f, 0.0f, 0.0f, 0.0f},
            .right = {0.0f, 0.0f, 0.0f, light.radius * transform.scale.x},
            .up = {0.0f, 0.0f, 0.0f, 0.0f},
            .packedColor =
            (static_cast<uint32_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f)) |
            (static_cast<uint32_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
            (static_cast<uint32_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
            (0xFFu << 24),
            .intensity = light.intensity,
            .range = light.range,
            .type = LIGHT_TYPE_SPHERE,
        });
    }

    //
    {
        int32_t bestPriority = INT32_MIN;
        bool found = false;
        auto dirView = state->registry.view<Component::DirectionalLightComponent, Component::TransformComponent>();
        for (auto [entity, light, transform] : dirView.each()) {
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

void GatherEditorSprites(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    if (!state->editor.bShowLightSprites) { return; }
    Core::ArenaVector<Core::Sprite>& sprites = frameBuffer->mainViewFamily.sprites;

    auto pointView = state->registry.view<Component::PointLightComponent, Component::TransformComponent>();
    for (auto [entity, light, transform] : pointView.each()) {
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
    if (mode == Engine::LightDebugDrawMode::None) { return; }

    Core::ViewFamily& viewFamily = frameBuffer->mainViewFamily;

    auto shouldDraw = [&](entt::entity entity) {
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

    for (auto [entity, light, transform] : state->registry.view<Component::AreaLightComponent, Component::TransformComponent>().each()) {
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

    for (auto [entity, light, transform] : state->registry.view<Component::SphereLightComponent, Component::TransformComponent>().each()) {
        if (!shouldDraw(entity)) { continue; }
        constexpr Vec4 editColor{0.5f, 0.8f, 1.0f, 1.0f};
        constexpr Vec4 rangeColor{1.0f, 0.55f, 0.15f, 1.0f};
        DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {transform.translation, light.radius * transform.scale.x, editColor, 0.02f});
        if (light.range > 0.0f) {
            DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {transform.translation, light.range, rangeColor, 0.02f});
        }
    }

    for (auto [entity, light, transform] : state->registry.view<Component::DirectionalLightComponent, Component::TransformComponent>().each()) {
        if (!shouldDraw(entity)) { continue; }
        const Vec3 forward = transform.rotation * Vec3(0.0f, 0.0f, 1.0f);
        constexpr Vec4 dirColor{1.0f, 0.9f, 0.5f, 1.0f};
        DEBUG_ADD_ARROW(viewFamily.debugArrows, {transform.translation, transform.translation + forward * 2.0f, 0.15f, 0.04f, dirColor, 0.02f});
    }
}

void GatherUIRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    Clay_SetLayoutDimensions({static_cast<float>(ctx->windowContext.viewportWidth), static_cast<float>(ctx->windowContext.viewportHeight)});

    Clay_BeginLayout();

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
                     .zIndex = 1,
                     .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                     .attachTo = CLAY_ATTACH_TO_PARENT,
                     },
                     }) {}
            }
        }
    }

    Clay_RenderCommandArray renderCommands = Clay_EndLayout(frameBuffer->timeFrame.deltaTime);

    const float vpWidth = static_cast<float>(ctx->windowContext.viewportWidth);
    const float vpHeight = static_cast<float>(ctx->windowContext.viewportHeight);

    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    const Engine::Font* uiFont = ctx->assetManager->GetFont(state->uiFont);
    assert(uiFont);

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
