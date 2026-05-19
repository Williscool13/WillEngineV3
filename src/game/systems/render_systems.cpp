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
#include "engine/logging/engine_log.h"
#include "game/fwd_components.h"
#include "game/components/common_components.h"
#include "game/components/debug_components.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/text_component.h"


namespace Game
{
void ConnectRenderObservers(entt::registry& registry)
{
    registry.on_destroy<Component::MeshRuntime>().connect<&Component::MeshRuntime::OnDestroy>();

    registry.on_construct<Component::StaticMeshComponent>().connect<&Component::StaticMeshComponent::OnConstruct>();
    registry.on_destroy<Component::StaticMeshComponent>().connect<&Component::StaticMeshComponent::OnDestroy>();

    registry.on_construct<Component::ProceduralMeshComponent>().connect<&Component::ProceduralMeshComponent::OnConstruct>();
    registry.on_destroy<Component::ProceduralMeshComponent>().connect<&Component::ProceduralMeshComponent::OnDestroy>();

    registry.on_construct<Component::SplineMeshComponent>().connect<&Component::SplineMeshComponent::OnConstruct>();
    registry.on_destroy<Component::SplineMeshComponent>().connect<&Component::SplineMeshComponent::OnDestroy>();

    registry.on_construct<Component::TextComponent>().connect<&Component::TextComponent::OnConstruct>();
    registry.on_destroy<Component::TextComponent>().connect<&Component::TextComponent::OnDestroy>();
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
                        matID = materialManager->CreateImmutableMaterial(model->modelData.materials[primitive.materialIndex]);
                        LOG_WARN(Engine, "Mesh was resolved with a material override that does not exist in the registry.");
                    }
                }
                else {
                    matID = materialManager->CreateImmutableMaterial(model->modelData.materials[primitive.materialIndex]);
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
    auto transformDirtyView = state->registry.view<Component::RenderTransformComponent, Component::DirtyTransformTag>();
    for (auto entity : transformDirtyView) {
        state->registry.emplace_or_replace<Component::DirtyRenderTransformComponent>(entity);
    }
}

void RenderPrepareTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;

    auto dirtyView = state->registry.view<Component::TransformComponent, Component::RenderTransformComponent, Component::DirtyRenderTransformComponent>(
        entt::exclude<Component::DynamicPhysicsBodyComponent>);
    constexpr size_t TASK_THRESHOLD = 1000;
    size_t dirtyViewCount = dirtyView.size_hint();
    if (dirtyViewCount < TASK_THRESHOLD) {
        ZoneScopedN("Serial");
        for (auto [entity, transform, renderTransform, dirtyRender] : dirtyView.each()) {
            renderTransform.previousMatrix = renderTransform.modelMatrix;
            renderTransform.modelMatrix = glm::translate(GetMatrix(transform), renderTransform.renderOffset);
            dirtyRender.counter--;
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
                renderTransform.modelMatrix = glm::translate(GetMatrix(transform), renderTransform.renderOffset);

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

        float alpha = state->physics.interpolationAlpha;
        glm::vec3 interpPos = glm::mix(physics.previousPosition, transform.translation, alpha);
        glm::quat interpRot = glm::slerp(physics.previousRotation, transform.rotation, alpha);
        renderTransform.modelMatrix = glm::translate(glm::mat4(1.0f), interpPos) * glm::mat4_cast(interpRot) * glm::scale(glm::mat4(1.0f), transform.scale) * glm::translate(
                                          glm::mat4(1.0f), renderTransform.renderOffset);
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

            for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
                auto& prim = runtime.primitives[i];
                frameBuffer->mainViewFamily.instances.PushBack({
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
        /*ZoneScopedN("PortalRenderables");
        auto portalView = state->registry.view<Component::PortalPlaneTag, Component::MeshRuntime, Component::RenderTransformComponent>();

        if (portalView.size_hint() > 0) {
            auto id = SID("portal_rendering");
            Core::CustomShaderDraw& portalDraw = frameBuffer->mainViewFamily.GetOrCreateCustomShaderDraw(id);
            portalDraw.prefix = Core::InlineString("portal_render");
            portalDraw.pipelineId = id;
            portalDraw.stencilValue = 1;


            for (auto [entity, runtime, renderTransform] : portalView.each()) {
                auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
                frameBuffer->mainViewFamily.modelMatrices.PushBack({renderTransform.modelMatrix, renderTransform.previousMatrix});

                for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
                    auto& prim = runtime.primitives[i];
                    portalDraw.instances.PushBack({
                        .primitiveIndex = prim.primitiveIndex,
                        .materialID = prim.materialID,
                        .modelIndex = modelIndex
                    });
                }
            }
        }*/
    }

    // Gather cubemap visualizations
    {
        /*ZoneScopedN("CubemapVisualizations");
        auto cubemapView = state->registry.view<Component::CubemapVisualizeTag, Component::MeshRuntime, Component::RenderTransformComponent>();

        for (auto [entity, runtime, renderTransform] : cubemapView.each()) {
            auto id = SID("cubemap_visualize");
            Core::CustomShaderDraw& cubemapVis = frameBuffer->mainViewFamily.GetOrCreateCustomShaderDraw(id);
            cubemapVis.prefix = Core::InlineString("cubemap_vis");
            cubemapVis.pipelineId = id;
            cubemapVis.pushConstantCustomData[0] = 0;
            cubemapVis.pushConstantCustomData[1] = ASSET_SAMPLER_LINEAR_BINDLESS_INDEX;
            cubemapVis.pushConstantCustomData[2] = 0;

            auto modelIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.modelMatrices.Size());
            frameBuffer->mainViewFamily.modelMatrices.PushBack({renderTransform.modelMatrix, renderTransform.previousMatrix});

            for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
                auto& prim = runtime.primitives[i];
                cubemapVis.instances.PushBack({
                    .primitiveIndex = prim.primitiveIndex,
                    .materialID = prim.materialID,
                    .modelIndex = modelIndex
                });
            }
        }*/
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
            frameBuffer->mainViewFamily.instances.PushBack({
                .primitiveIndex = runtime.primitives[0].primitiveIndex,
                .materialID = runtime.primitives[0].materialID,
                .modelIndex = modelIndex,
                .stableId = stableId,
            });
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

            frameBuffer->mainViewFamily.instances.PushBack({
                .primitiveIndex = runtime.primitives[0].primitiveIndex,
                .materialID = runtime.primitives[0].materialID,
                .modelIndex = modelIndex,
                .stableId = stableId,
            });
        }
    }

    // Material remap
    {
        ZoneScopedN("Material Recording");
        for (auto& instance : frameBuffer->mainViewFamily.instances) {
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

    for (auto [entity, textComp, runtime, renderTransform] : view.each()) {
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
}
