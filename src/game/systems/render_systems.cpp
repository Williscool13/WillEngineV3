//
// Created by William on 2025-12-26.
//

#include "render_systems.h"

#include <tracy/Tracy.hpp>

#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/fwd_components.h"
#include "game/components/common_components.h"
#include "game/components/debug_components.h"


namespace Game
{
void ResolveStaticMeshLoads(Core::EngineContext* ctx, Engine::GameState* state)
{
    // todo: ew
    std::vector<entt::entity> resolved;

    for (auto [entity, meshComponent] : state->registry.view<Component::StaticMeshComponent, Component::StaticMeshLoadingTag>().each()) {
        Engine::MaterialManager* materialManager = ctx->materialManager;
        // Cleanup
        {
            for (size_t i = 0; i < meshComponent.primitiveCount; ++i) {
                materialManager->ReleaseMaterial(meshComponent.primitives[i].materialID);
            }
            meshComponent.primitiveCount = 0;
        }

        auto model = ctx->assetManager->GetModel(meshComponent.modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Model ({}) is not in the asset manager, it should have been requested to load during scene load.", meshComponent.modelHandle.index);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            LOG_TRACE(Game, "Model ({}) not yet done loading", model->name);
            continue;
        }

        Engine::MeshInformation& mesh = model->modelData.meshes[meshComponent.meshIndex];

        if (mesh.primitiveProperties.size() > 128) {
            LOG_WARN(Game, "Model ({}) has {} primitives, limiting to 128", model->name, mesh.primitiveProperties.size());
        }

        size_t primCount = std::min(mesh.primitiveProperties.size(), static_cast<size_t>(128));

        for (size_t j = 0; j < primCount; ++j) {
            Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[j];

            Engine::MaterialID matID;
            if (primitive.materialIndex == -1) {
                matID = materialManager->GetDefaultMaterial();
            }
            else {
                Engine::MaterialID materialOverride = meshComponent.materialOverrides[primitive.materialIndex];
                if (materialOverride.IsValid()) {
                    if (materialManager->DoesMutableMaterialExist(materialOverride)) {
                        matID = materialOverride;
                    }
                    else {
                        matID = materialManager->CreateImmutableMaterial(model->modelData.materials[primitive.materialIndex]);
                        LOG_WARN(Engine, "Mesh was resolved with a material override that does not exist in the registry.");
                    }
                }
                else {
                    matID = materialManager->CreateImmutableMaterial(model->modelData.materials[primitive.materialIndex]);
                }
            }

            meshComponent.primitives[j] = {
                .primitiveIndex = primitive.index,
                .originalMaterialIndex = primitive.materialIndex,
                .materialID = matID
            };
            materialManager->AcquireMaterial(matID);
        }
        meshComponent.primitiveCount = primCount;

        resolved.push_back(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::StaticMeshLoadingTag>(entity);
    }
}

void ResolveProceduralMeshLoads(Core::EngineContext* ctx, Engine::GameState* state)
{
    std::vector<entt::entity> resolved;

    for (auto [entity, meshComponent] : state->registry.view<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadingTag>().each()) {
        // Cleanup
        {
            if (meshComponent.bPrimitiveReady) {
                ctx->materialManager->ReleaseMaterial(meshComponent.primitive.materialID);
                meshComponent.bPrimitiveReady = false;
            }
        }

        auto model = ctx->assetManager->GetModel(meshComponent.modelHandle);
        if (!model) {
            LOG_ERROR(Game, "Procedural model ({}) is not in the asset manager, it should have been requested to load during scene load.", meshComponent.modelHandle.index);
            continue;
        }
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            LOG_TRACE(Game, "Procedural model ({}) not yet done loading", model->name);
            continue;
        }

        Engine::MeshInformation& mesh = model->modelData.meshes[0];
        Engine::PrimitiveProperty& primitive = mesh.primitiveProperties[0];

        Engine::MaterialManager* materialManager = ctx->materialManager;
        Engine::MaterialID matID = materialManager->GetDefaultMaterial();
        if (meshComponent.material.IsValid()) {
            if (materialManager->DoesMutableMaterialExist(meshComponent.material)) {
                matID = meshComponent.material;
            }
        }

        meshComponent.primitive = {
            .primitiveIndex = primitive.index,
            .originalMaterialIndex = -1,
            .materialID = matID,
        };
        materialManager->AcquireMaterial(matID);
        meshComponent.bPrimitiveReady = true;

        resolved.push_back(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::ProceduralMeshLoadingTag>(entity);
    }
}

void RenderUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    auto transformDirtyView = state->registry.view<Component::RenderTransformComponent, Component::DirtyTransformTag>();
    for (auto entity : transformDirtyView) {
        state->registry.emplace_or_replace<Component::DirtyRenderTransformComponent>(entity);
    }
}

void RenderPrepareTransforms(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;

    auto dirtyView = state->registry.view<Component::TransformComponent, Component::RenderTransformComponent, Component::DirtyRenderTransformComponent>(
        entt::exclude<Component::DynamicPhysicsBodyComponent>);
    constexpr size_t TASK_THRESHOLD = 1000;
    if (dirtyView.size_hint() < TASK_THRESHOLD) {
        ZoneScopedN("Serial");
        for (auto [entity, transform, renderTransform, dirtyRender] : dirtyView.each()) {
            renderTransform.previousMatrix = renderTransform.modelMatrix;
            renderTransform.modelMatrix = GetMatrix(transform);
            dirtyRender.counter--;
        }
    }
    else {
        ZoneScopedN("Parallel");
        std::vector entities(dirtyView.begin(), dirtyView.end());

        enki::TaskSet task(entities.size(), [&](enki::TaskSetPartition range, uint32_t) {
            for (uint32_t i = range.start; i < range.end; ++i) {
                auto entity = entities[i];
                auto& transform = dirtyView.get<Component::TransformComponent>(entity);
                auto& renderTransform = dirtyView.get<Component::RenderTransformComponent>(entity);

                renderTransform.previousMatrix = renderTransform.modelMatrix;
                renderTransform.modelMatrix = GetMatrix(transform);

                auto& dirty = dirtyView.get<Component::DirtyRenderTransformComponent>(entity);
                dirty.counter--;
            }
        });
        ctx->scheduler->AddTaskSetToPipe(&task);
        ctx->scheduler->WaitforTask(&task);
    }

    auto cleanupView = state->registry.view<Component::DirtyRenderTransformComponent>();
    for (const auto& [entity, dirty] : cleanupView.each()) {
        if (dirty.counter <= 0) {
            state->registry.remove<Component::DirtyRenderTransformComponent>(entity);
        }
    }

    // Physics always dirty until I find a better way
    auto physicsView = state->registry.view<Component::DynamicPhysicsBodyComponent, Component::TransformComponent, Component::RenderTransformComponent>();
    for (auto [entity, physics, transform, renderTransform] : physicsView.each()) {
        renderTransform.previousMatrix = renderTransform.modelMatrix;

        float alpha = state->physicsInterpolationAlpha;
        glm::vec3 interpPos = glm::mix(physics.previousPosition, transform.translation, alpha);
        glm::quat interpRot = glm::slerp(physics.previousRotation, transform.rotation, alpha);
        renderTransform.modelMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::mat4_cast(interpRot) * glm::scale(glm::mat4(1.0f), transform.scale);
    }
}

void GatherRenderables(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    auto& materialManager = ctx->materialManager;

    // Gather regular renderables
    {
        ZoneScopedN("MainSceneStaticMeshes");
        auto view = state->registry.view<Component::StaticMeshComponent, Component::RenderTransformComponent>(
            entt::exclude<
                Component::PortalPlaneTag,
                Component::CubemapVisualizeTag,
                Component::StaticMeshLoadingTag
            >);

        for (auto [entity, renderable, renderTransform] : view.each()) {
            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.size());
            frameBuffer->mainViewFamily.modelMatrices.push_back({renderTransform.modelMatrix, renderTransform.previousMatrix});

            uint64_t stableId = 1234567890;
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                stableId = stable->id.id;
            }

            for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                auto& prim = renderable.primitives[i];
                frameBuffer->mainViewFamily.mainPassInstances.push_back({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex,
                    .stableId = stableId,
                });
            }
        }
    }

    // Gather portal planes
    {
        ZoneScopedN("PortalRenderables");
        auto portalView = state->registry.view<Component::PortalPlaneTag, Component::StaticMeshComponent, Component::RenderTransformComponent>();

        if (portalView.size_hint() > 0) {
            auto& portalDraw = frameBuffer->mainViewFamily.customShaderDraws["portal_rendering"];
            if (portalDraw.instances.empty()) {
                portalDraw.pipelineId = SID("portal_rendering");
                portalDraw.stencilValue = 1;
            }

            for (auto [entity, renderable, renderTransform] : portalView.each()) {
                auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.size());
                frameBuffer->mainViewFamily.modelMatrices.push_back({renderTransform.modelMatrix, renderTransform.previousMatrix});

                for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                    auto& prim = renderable.primitives[i];
                    portalDraw.instances.push_back({
                        .primitiveIndex = prim.primitiveIndex,
                        .materialID = prim.materialID,
                        .modelIndex = modelIndex
                    });
                }
            }
        }
    }

    // Gather cubemap visualizations
    {
        ZoneScopedN("CubemapVisualizations");
        auto cubemapView = state->registry.view<Component::CubemapVisualizeTag, Component::StaticMeshComponent, Component::RenderTransformComponent>();

        for (auto [entity, renderable, renderTransform] : cubemapView.each()) {
            auto& cubemapVis = frameBuffer->mainViewFamily.customShaderDraws["cubemap_visualize"];
            if (cubemapVis.instances.empty()) {
                cubemapVis.pipelineId = SID("cubemap_visualize");
                cubemapVis.pushConstantCustomData[0] = 0;
                cubemapVis.pushConstantCustomData[1] = ASSET_SAMPLER_BINDLESS_INDEX;
                cubemapVis.pushConstantCustomData[2] = 0;
            }

            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.size());
            frameBuffer->mainViewFamily.modelMatrices.push_back({renderTransform.modelMatrix, renderTransform.previousMatrix});

            for (uint8_t i = 0; i < renderable.primitiveCount; ++i) {
                auto& prim = renderable.primitives[i];
                cubemapVis.instances.push_back({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex
                });
            }
        }
    }

    // Gather procedural meshes
    {
        ZoneScopedN("ProceduralMeshes");
        auto view = state->registry.view<Component::ProceduralMeshComponent, Component::RenderTransformComponent>(entt::exclude<Component::ProceduralMeshLoadingTag>);

        for (const auto& [entity, renderable, renderTransform] : view.each()) {
            if (!renderable.bPrimitiveReady) { continue; }

            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.size());
            frameBuffer->mainViewFamily.modelMatrices.push_back({renderTransform.modelMatrix, renderTransform.previousMatrix});

            uint64_t stableId = 1234567890;
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                stableId = stable->id.id;
            }

            frameBuffer->mainViewFamily.mainPassInstances.push_back({
                .primitiveIndex = renderable.primitive.primitiveIndex,
                .materialID = renderable.primitive.materialID,
                .modelIndex = modelIndex,
                .stableId = stableId,
            });
        }
    }

    // Material remap
    {
        ZoneScopedN("Material Remap");
        // todo revisit this, dont need this much indirection
        for (auto& instance : frameBuffer->mainViewFamily.mainPassInstances) {
            instance.gpuMaterialIndex = materialManager->GetMaterialIndex(instance.materialID);
        }

        for (auto& customDraw : frameBuffer->mainViewFamily.customShaderDraws) {
            for (auto& instance : customDraw.second.instances) {
                instance.gpuMaterialIndex = materialManager->GetMaterialIndex(instance.materialID);
            }
        }
    }

    frameBuffer->mainViewFamily.materials.resize(Render::BINDLESS_MATERIAL_BUFFER_COUNT);
    for (auto& [matID, slotIndex] : materialManager->GetIdToEntryMap()) {
        frameBuffer->mainViewFamily.materials[slotIndex] = materialManager->GetProperties(matID);
    }
}
}
