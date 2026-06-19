//
// Created by William on 2025-12-26.
//

#include "render_systems.h"

#include <tracy/Tracy.hpp>

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
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/light_components.h"
#include "render/shaders/lights_interop.h"
#include "render/shaders/text_interop.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/text_component.h"
#include "game/components/common/stable_id_component.h"
#include "game/components/core_components.h"


namespace Game
{
void ConnectRenderObservers(entt::registry& registry)
{
    registry.on_construct<Component::TransformComponent>().connect<&Component::TransformComponent::OnConstruct>();
    registry.on_destroy<Component::TransformComponent>().connect<&Component::TransformComponent::OnDestroy>();

    registry.on_destroy<Component::MeshRuntime>().connect<&Component::MeshRuntime::OnDestroy>();

    registry.on_construct<Component::StaticMeshComponent>().connect<&Component::StaticMeshComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshComponent>().connect<&Component::StaticMeshComponent::OnDestroy>();

    registry.on_construct<Component::ProceduralMeshComponent>().connect<&Component::ProceduralMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ProceduralMeshComponent>().connect<&Component::ProceduralMeshComponent::OnDestroy>();

    registry.on_construct<Component::SplineMeshComponent>().connect<&Component::SplineMeshComponent::OnConstruct>();
    registry.on_destroy<Component::SplineMeshComponent>().connect<&Component::SplineMeshComponent::OnDestroy>();

    registry.on_construct<Component::TextComponent>().connect<&Component::TextComponent::OnConstruct>();
    registry.on_destroy<Component::TextComponent>().connect<&Component::TextComponent::OnDestroy>();

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

    registry.on_construct<Component::ProceduralMeshComponent>().disconnect<&Component::ProceduralMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ProceduralMeshComponent>().disconnect<&Component::ProceduralMeshComponent::OnDestroy>();

    registry.on_construct<Component::SplineMeshComponent>().disconnect<&Component::SplineMeshComponent::OnConstruct>();
    registry.on_destroy<Component::SplineMeshComponent>().disconnect<&Component::SplineMeshComponent::OnDestroy>();

    registry.on_construct<Component::TextComponent>().disconnect<&Component::TextComponent::OnConstruct>();
    registry.on_destroy<Component::TextComponent>().disconnect<&Component::TextComponent::OnDestroy>();

    registry.on_construct<Component::AreaLightComponent>().disconnect<&Component::AreaLightComponent::OnConstruct>();
    registry.on_destroy<Component::AreaLightComponent>().disconnect<&Component::AreaLightComponent::OnDestroy>();

    registry.on_construct<Component::SphereLightComponent>().disconnect<&Component::SphereLightComponent::OnConstruct>();
    registry.on_destroy<Component::SphereLightComponent>().disconnect<&Component::SphereLightComponent::OnDestroy>();
}

void ResolveModelHotReloads(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->pendingHotReloadModelIds.IsEmpty()) { return; }

    auto view = state->registry.view<Component::StaticMeshComponent, Component::MeshRuntime>();
    if (view.size_hint() == 0) { return; }

    Core::ArenaVector<entt::entity> entitiesToReload{&ctx->gameplayArena.Get(), view.size_hint()};

    for (const auto& [entity, smc, runtime] : view.each()) {
        for (const Engine::ModelID& hotId : state->pendingHotReloadModelIds) {
            if (smc.modelId != hotId) { continue; }
            Component::UnloadStaticMesh(smc, state->registry, entity);
            entitiesToReload.PushBack(entity);
            break;
        }
    }

    if (entitiesToReload.IsEmpty()) { return; }

    for (const Engine::ModelID& hotId : state->pendingHotReloadModelIds) {
        ctx->assetManager->EvictModel(hotId);
    }

    for (const entt::entity entity : entitiesToReload) {
        auto& smc = state->registry.get<Component::StaticMeshComponent>(entity);
        Component::LoadStaticMesh(smc, state->registry, entity);
    }
}

void ResolveFontHotReloads(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->pendingHotReloadFontIds.IsEmpty()) { return; }

    auto view = state->registry.view<Component::TextComponent, Component::TextRuntime>();
    if (view.size_hint() == 0) { return; }

    Core::ArenaVector<entt::entity> entitiesToReload{&ctx->gameplayArena.Get(), view.size_hint()};

    for (auto [entity, textComp, runtime] : view.each()) {
        for (const Engine::FontID& hotId : state->pendingHotReloadFontIds) {
            if (textComp.fontId != hotId) { continue; }
            Component::UnloadTextComponent(textComp, state->registry, entity);
            entitiesToReload.PushBack(entity);
            break;
        }
    }

    if (state->pendingHotReloadFontIds.IsEmpty()) { return; }

    for (const Engine::FontID& hotId : state->pendingHotReloadFontIds) {
        ctx->assetManager->EvictFont(hotId);
    }

    for (const entt::entity entity : entitiesToReload) {
        auto& textComp = state->registry.get<Component::TextComponent>(entity);
        Component::LoadTextComponent(textComp, state->registry, entity);
    }
}

void ResolveTextureHotReloads(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->pendingHotReloadTextureIds.IsEmpty()) { return; }

    for (auto hotId : state->pendingHotReloadTextureIds) {
        if (ctx->assetManager->IsTextureLoaded(hotId)) {
            ctx->assetManager->ReloadTexture(hotId);
        }
    }
}

void ResolveStaticMeshLoads(Engine::EngineContext* ctx, Engine::EngineState* state)
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
        // Cleanup
        for (size_t i = 0; i < runtime->primitives.Size(); ++i) {
            materialManager->ReleaseMaterial(runtime->primitives[i].materialID);
        }
        runtime->primitives.Clear();

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Model ({}) is not in the asset manager, it should have been requested to load during scene load.", runtime->modelHandle.index);
            resolved.PushBack(entity);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            modelsWaitingThisTick++;
            continue;
        }

        Engine::MeshInformation& mesh = model->modelData.meshes[meshComponent.meshIndex];

        if (mesh.primitiveProperties.Size() > Component::MeshRuntime::MaxPrimitives) {
            LOG_WARN(Game, "Model ({}) has {} primitives, limiting to {}", model->name.c_str(), mesh.primitiveProperties.Size(), Component::MeshRuntime::MaxPrimitives);
        }

        size_t primCount = std::min(mesh.primitiveProperties.Size(), Component::MeshRuntime::MaxPrimitives);

        auto applyShaderOverrides = [&](Engine::Material mat) -> Engine::Material {
            if (meshComponent.shadingShaderOverride) { mat.fragmentShader = meshComponent.shadingShaderOverride; }
            if (meshComponent.lightingShaderOverride) { mat.lightingShader = meshComponent.lightingShaderOverride; }
            return mat;
        };

        for (size_t j = 0; j < primCount; ++j) {
            Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[j];

            Engine::MaterialID matID;
            if (primitive.materialIndex == -1) {
                matID = materialManager->GetDefaultMaterialID();
            }
            else {
                Engine::MaterialID materialOverride = meshComponent.materialOverrides[primitive.materialIndex];
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

            runtime->primitives.PushBack({
                .primitiveIndex = primitive.index,
                .originalMaterialIndex = primitive.materialIndex,
                .materialID = matID
            });
            materialManager->AcquireMaterial(matID);
        }

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

void ResolveProceduralMeshLoads(Engine::EngineContext* ctx, Engine::EngineState* state)
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

        // Cleanup
        for (size_t i = 0; i < runtime->primitives.Size(); ++i) {
            ctx->materialManager->ReleaseMaterial(runtime->primitives[i].materialID);
        }
        runtime->primitives.Clear();

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Procedural model ({}) is not in the asset manager, it should have been requested to load during scene load.", runtime->modelHandle.index);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            proceduralWaitingThisTick++;
            continue;
        }

        Engine::MeshInformation& mesh = model->modelData.meshes[0];
        Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[0];

        Engine::MaterialManager* materialManager = ctx->materialManager;
        Engine::MaterialID matID = materialManager->GetDefaultMaterialID();
        if (meshComponent.material.IsValid()) {
            if (materialManager->DoesMutableMaterialExist(meshComponent.material)) {
                matID = meshComponent.material;
            }
        }

        runtime->primitives.PushBack({
            .primitiveIndex = primitive.index,
            .originalMaterialIndex = -1,
            .materialID = matID,
        });
        materialManager->AcquireMaterial(matID);

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

void ResolveSplineMeshLoads(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::SplineMeshComponent, Component::SplineMeshLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (auto [entity, meshComponent] : view.each()) {
        auto* runtime = state->registry.try_get<Component::MeshRuntime>(entity);
        if (!runtime) continue;

        for (size_t i = 0; i < runtime->primitives.Size(); ++i) {
            ctx->materialManager->ReleaseMaterial(runtime->primitives[i].materialID);
        }
        runtime->primitives.Clear();

        auto model = ctx->assetManager->GetModel(runtime->modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Spline model ({}) is not in the asset manager.", runtime->modelHandle.index);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            continue;
        }

        Engine::MeshInformation& mesh = model->modelData.meshes[0];
        Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[0];

        Engine::MaterialManager* materialManager = ctx->materialManager;
        Engine::MaterialID matID = materialManager->GetDefaultMaterialID();
        if (meshComponent.material.IsValid()) {
            if (materialManager->DoesMutableMaterialExist(meshComponent.material)) {
                matID = meshComponent.material;
            }
        }

        runtime->primitives.PushBack({
            .primitiveIndex = primitive.index,
            .originalMaterialIndex = -1,
            .materialID = matID,
        });
        materialManager->AcquireMaterial(matID);

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::SplineMeshLoadingTag>(entity);
    }
}

void MarkRenderTransformsDirty(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto transformDirtyView = state->registry.view<Component::TransformComponent, Component::DirtyTransformTag>();
    for (auto entity : transformDirtyView) {
        state->registry.emplace_or_replace<Component::MultiframeDirtyTransformComponent>(entity);
    }
}

void RenderPrepareTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;

    auto dirtyView = state->registry.view<Component::TransformComponent, Component::RenderTransformComponent, Component::MultiframeDirtyTransformComponent>(
        entt::exclude<Component::DynamicPhysicsBodyComponent>);
    constexpr size_t TASK_THRESHOLD = 1000;
    size_t dirtyViewCount = dirtyView.size_hint();
    if (dirtyViewCount < TASK_THRESHOLD) {
        ZoneScopedN("Serial");
        for (auto [entity, transform, renderTransform, dirtyRender] : dirtyView.each()) {
            renderTransform.previousMatrix = renderTransform.modelMatrix;
            renderTransform.modelMatrix = glm::translate(GetMatrix(transform), renderTransform.renderOffset) * glm::mat4_cast(renderTransform.renderRotation);
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
                auto& transform = dirtyView.get<Component::TransformComponent>(entity);
                auto& renderTransform = dirtyView.get<Component::RenderTransformComponent>(entity);

                renderTransform.previousMatrix = renderTransform.modelMatrix;
                renderTransform.modelMatrix = glm::translate(GetMatrix(transform), renderTransform.renderOffset) * glm::mat4_cast(renderTransform.renderRotation);
            }
        });
        ctx->scheduler->AddTaskSetToPipe(&task);
        ctx->scheduler->WaitforTask(&task);
    }

    // Physics always dirty until I find a better way
    auto physicsView = state->registry.view<Component::DynamicPhysicsBodyComponent, Component::TransformComponent, Component::RenderTransformComponent>();
    for (auto [entity, physics, transform, renderTransform] : physicsView.each()) {
        renderTransform.previousMatrix = renderTransform.modelMatrix;

        float alpha = state->physics.interpolationAlpha;
        glm::vec3 interpPos = glm::mix(physics.previousPosition, transform.translation, alpha);
        glm::quat interpRot = glm::slerp(physics.previousRotation, transform.rotation, alpha);
        renderTransform.modelMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::mat4_cast(interpRot) * glm::scale(glm::mat4(1.0f), transform.scale) * glm::translate(glm::mat4(1.0f), renderTransform.renderOffset) * glm::mat4_cast(renderTransform.renderRotation);
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

    // Gather regular renderables
    {
        ZoneScopedN("MainSceneStaticMeshes");
        auto view = state->registry.view<Component::MeshRuntime, Component::StaticMeshComponent, Component::RenderTransformComponent>(
            entt::exclude<
                Component::PortalPlaneTag,
                Component::CubemapVisualizeTag,
                Component::StaticMeshLoadingTag
            >);

        for (auto [entity, runtime, renderable, renderTransform] : view.each()) {
            if (runtime.primitives.IsEmpty()) { continue; }
            if (renderable.modelFlags.x == 0.0f) { continue; }
            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
            frameBuffer->mainViewFamily.modelMatrices.EmplaceBack(renderTransform.modelMatrix, renderTransform.previousMatrix);

            uint64_t stableId = 1234567890;
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                stableId = stable->id.id;
            }

            Engine::StaticModel* model = ctx->assetManager->GetModel(runtime.modelHandle);
            ENGINE_ASSERT(Game, model, "Loaded entity references a model that is not in the asset manager");
            const Engine::MeshInformation& mesh = model->modelData.meshes[renderable.meshIndex];

            const uint32_t primitiveInstanceBase = static_cast<uint32_t>(frameBuffer->mainViewFamily.primitiveInstances.Size());
            for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
                const auto& prim = runtime.primitives[i];
                frameBuffer->mainViewFamily.primitiveInstances.PushBack({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                    .blasDeviceAddress = mesh.primitiveProperties[i].blasDeviceAddress,
                });
            }
        }
    }

    // Gather procedural meshes
    {
        ZoneScopedN("ProceduralMeshes");
        auto view = state->registry.view<Component::MeshRuntime, Component::ProceduralMeshComponent, Component::RenderTransformComponent>(entt::exclude<Component::ProceduralMeshLoadingTag>);

        for (const auto& [entity, runtime, renderable, renderTransform] : view.each()) {
            if (runtime.primitives.IsEmpty()) { continue; }
            if (renderable.modelFlags.x == 0.0f) { continue; }

            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
            frameBuffer->mainViewFamily.modelMatrices.EmplaceBack(renderTransform.modelMatrix, renderTransform.previousMatrix);

            uint64_t stableId = 1234567890;
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                stableId = stable->id.id;
            }

            Engine::StaticModel* model = ctx->assetManager->GetModel(runtime.modelHandle);
            ENGINE_ASSERT(Game, model, "Loaded entity references a model that is not in the asset manager");
            const Engine::MeshInformation& mesh = model->modelData.meshes[0];

            for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
                const auto& prim = runtime.primitives[i];
                frameBuffer->mainViewFamily.primitiveInstances.PushBack({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                    .blasDeviceAddress = mesh.primitiveProperties[i].blasDeviceAddress,
                });
            }
        }
    }

    // Gather spline meshes
    {
        ZoneScopedN("SplineMeshes");
        auto view = state->registry.view<Component::MeshRuntime, Component::SplineMeshComponent, Component::RenderTransformComponent>(entt::exclude<Component::SplineMeshLoadingTag>);
        for (const auto& [entity, runtime, renderable, renderTransform] : view.each()) {
            if (runtime.primitives.IsEmpty()) { continue; }
            if (renderable.modelFlags.x == 0.0f) { continue; }

            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
            frameBuffer->mainViewFamily.modelMatrices.EmplaceBack(renderTransform.modelMatrix, renderTransform.previousMatrix);

            uint64_t stableId = 1234567890;
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                stableId = stable->id.id;
            }

            Engine::StaticModel* model = ctx->assetManager->GetModel(runtime.modelHandle);
            ENGINE_ASSERT(Game, model, "Loaded entity references a model that is not in the asset manager");
            const Engine::MeshInformation& mesh = model->modelData.meshes[0];

            for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
                const auto& prim = runtime.primitives[i];
                frameBuffer->mainViewFamily.primitiveInstances.PushBack({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                    .blasDeviceAddress = mesh.primitiveProperties[i].blasDeviceAddress,
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

                frameBuffer->mainViewFamily.primitiveInstances.PushBack({
                    .primitiveIndex = quadPrimitiveIndex,
                    .materialID = materialKey,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                    .blasDeviceAddress = 0,
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

                frameBuffer->mainViewFamily.primitiveInstances.PushBack({
                    .primitiveIndex = spherePrimitiveIndex,
                    .materialID = materialKey,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                    .blasDeviceAddress = 0,
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

void ResolveTextLoads(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto view = state->registry.view<Component::TextRuntime, Component::TextLoadingTag>();
    if (view.size_hint() == 0) { return; }

    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (auto [entity, runtime] : view.each()) {
        if (!runtime.fontHandle.IsValid()) { continue; }

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
        state->registry.remove<Component::TextLoadingTag>(entity);
    }
}

void GatherTextRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    auto view = state->registry.view<Component::TextComponent, Component::TextRuntime, Component::RenderTransformComponent>(entt::exclude<Component::TextLoadingTag>);

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

    const Vec2 mousePos = state->inputFrame->mousePositionAbsolute;
    const bool bIsMouseDown = state->inputFrame->GetMouse(MouseButton::LMB).down;
    const float viewportOffsetX = static_cast<float>(ctx->windowContext.viewportOffsetX);
    const float viewportOffsetY = static_cast<float>(ctx->windowContext.viewportOffsetY);
    Clay_SetPointerState(Clay_Vector2{mousePos.x - viewportOffsetX, mousePos.y - viewportOffsetY}, bIsMouseDown);

    const Vec2 mouseWheelDelta = state->inputFrame->mouseWheelDelta;
    Clay_UpdateScrollContainers(true, Clay_Vector2{mouseWheelDelta.x, mouseWheelDelta.y}, state->timeFrame->deltaTime);

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
