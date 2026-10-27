//
// Created by William on 2025-12-26.
//

#include "render_systems.h"

#include <bit>
#include <emmintrin.h>
#include <xmmintrin.h>

#include <tracy/Tracy.hpp>

#include "scene_system.h"
#include "render_systems_helpers.h"
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
#include "engine/components/fwd_components.h"
#include "engine/components/common_components.h"
#include "engine/input/engine_actions.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/module_mesh_component.h"
#include "engine/components/render/text3d_component.h"
#include "engine/components/render/light_components.h"
#include "engine/components/render/local_ddgi_volume_component.h"
#include "engine/components/render/post_process_volume_component.h"
#include "engine/components/render/reflection_probe_component.h"
#include "render/shaders/lights_interop.h"
#include "render/shaders/reflection_probe_interop.h"
#include "render/shaders/text_interop.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/static_mesh_primitive_component.h"
#include "engine/components/render/text_component.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_body_desc.h"
#include "render/types/render_types.h"
#include "render/render-view/csm_views.h"
#include "render/render-view/local_shadow_views.h"
#include "core/memory/memory_manager.h"


namespace Engine
{
void MarkRenderTransformsDirty(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    auto transformDirtyView = registry.View<Component::TransformComponent, Component::DirtyTransformRequest>();
    for (auto entity : transformDirtyView) {
        registry.Set<Component::MultiframeDirtyRequest>(entity);
    }
}

// Tick-to-tick physics deltas are tiny, so normalized lerp is visually identical to slerp without the trig
static glm::quat NlerpPose(const glm::quat& a, glm::quat b, float t)
{
    if (glm::dot(a, b) < 0.0f) { b = -b; }
    return glm::normalize(glm::lerp(a, b, t));
}

static void InterpolatedPose(const Component::DynamicPhysicsBodyRuntime& physics, float alpha, glm::vec3& outTranslation, glm::quat& outRotation)
{
    if (physics.previousPosition == physics.currentPosition && physics.previousRotation == physics.currentRotation) {
        outTranslation = physics.currentPosition;
        outRotation = physics.currentRotation;
        return;
    }
    outTranslation = glm::mix(physics.previousPosition, physics.currentPosition, alpha);
    outRotation = NlerpPose(physics.previousRotation, physics.currentRotation, alpha);
}

void ResolveWorldTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    const float alpha = state->physics.interpolationAlpha;

    // Roots (no parent), non-physics
    for (auto [entity, local, world, dirty] : registry.View<Component::TransformComponent, Engine::Mut<Component::WorldTransformRuntime>, Component::MultiframeDirtyRequest>(
             entt::exclude<Component::HierarchyComponent, Component::DynamicPhysicsBodyRuntime>).each()) {
        world.translation = local.translation;
        world.rotation = local.rotation;
        world.scale = local.scale;
    }

    // Roots, physics (interpolated Jolt pose)
    for (auto [entity, physics, local, world] : registry.View<Component::DynamicPhysicsBodyRuntime, Component::TransformComponent, Engine::Mut<Component::WorldTransformRuntime>>(
             entt::exclude<Component::HierarchyComponent>).each()) {
        glm::vec3 translation;
        glm::quat rotation;
        InterpolatedPose(physics, alpha, translation, rotation);
        if (translation == world.translation && rotation == world.rotation && local.scale == world.scale) { continue; }
        world.translation = translation;
        world.rotation = rotation;
        world.scale = local.scale;
        registry.Set<Component::MultiframeDirtyRequest>(entity);
    }

    // Children in depth order
    EnsureHierarchyOrder(state, registry);
    auto orphans = Core::ArenaVector<entt::entity>(&ctx->gameplayArena.Get(), 8);
    auto hierarchy = registry.View<Component::HierarchyComponent>();
    for (auto entity : hierarchy) {
        const auto& node = hierarchy.get<Component::HierarchyComponent>(entity);

        auto& local = registry.GetMut<Component::TransformComponent>(entity);
        auto& world = registry.GetMut<Component::WorldTransformRuntime>(entity);

        if (node.parent == entt::null || !registry.Valid(node.parent) || !registry.Has<Component::WorldTransformRuntime>(node.parent)) {
            local.translation = world.translation;
            local.rotation = world.rotation;
            local.scale = world.scale;
            registry.Set<Component::MultiframeDirtyRequest>(entity);
            orphans.PushBack(entity);
            continue;
        }

        auto* physics = registry.TryGet<Component::DynamicPhysicsBodyRuntime>(entity);
        glm::vec3 physicsTranslation{};
        glm::quat physicsRotation{};
        bool bPhysicsMoved = false;
        if (physics) {
            InterpolatedPose(*physics, alpha, physicsTranslation, physicsRotation);
            bPhysicsMoved = physicsTranslation != world.translation || physicsRotation != world.rotation || local.scale != world.scale;
        }
        const bool selfDirty = bPhysicsMoved || registry.Has<Component::MultiframeDirtyRequest>(entity);
        const bool parentDirty = !physics && registry.Has<Component::MultiframeDirtyRequest>(node.parent);
        if (!selfDirty && !parentDirty) { continue; }
        if (bPhysicsMoved || !registry.Has<Component::MultiframeDirtyRequest>(entity)) {
            registry.Set<Component::MultiframeDirtyRequest>(entity);
        }

        // Physics (interpolated Jolt pose)
        if (physics) {
            world.translation = physicsTranslation;
            world.rotation = physicsRotation;
            world.scale = local.scale;
        }
        else {
            const auto& parentWorld = registry.Get<Component::WorldTransformRuntime>(node.parent);
            world = Component::ComposeWorldTransform(parentWorld, local);
        }
    }

    for (entt::entity entity : orphans) {
        registry.Remove<Component::HierarchyComponent>(entity);
    }
}

void UpdateUIPointerState(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    const Vec2 mousePos = state->input.mousePositionAbsolute;
    const float viewportOffsetX = static_cast<float>(ctx->windowContext.viewportOffsetX);
    const float viewportOffsetY = static_cast<float>(ctx->windowContext.viewportOffsetY);
    const bool bIsMouseDown = state->input.GetActionState(Actions::ACTION_UI_POINTER_DOWN).down;
    Clay_SetPointerState(Clay_Vector2{mousePos.x - viewportOffsetX, mousePos.y - viewportOffsetY}, bIsMouseDown);

    state->input.uiScrollAccum += state->input.GetActionState(Actions::ACTION_UI_SCROLL).axis;
}

void RenderPrepareTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;

    auto dirtyView = registry.View<Component::WorldTransformRuntime, Engine::Mut<Component::RenderTransformRuntime>, Component::MultiframeDirtyRequest>();
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
        constexpr size_t TASK_BATCH = 8192;
        auto entities = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), std::min(dirtyViewCount, TASK_BATCH));

        auto dispatch = [&]() {
            if (entities.IsEmpty()) { return; }
            enki::TaskSet task(entities.Size(), [&](enki::TaskSetPartition range, uint32_t) {
                for (uint32_t i = range.start; i < range.end; ++i) {
                    auto entity = entities[i];
                    auto& world = dirtyView.get<Component::WorldTransformRuntime>(entity);
                    auto& renderTransform = dirtyView.get<Component::RenderTransformRuntime>(entity);

                    renderTransform.previousMatrix = renderTransform.modelMatrix;
                    renderTransform.modelMatrix = glm::translate(GetMatrix(world), renderTransform.renderOffset) * glm::mat4_cast(renderTransform.renderRotation);
                }
            });
            ctx->scheduler->AddTaskSetToPipe(&task);
            ctx->scheduler->WaitforTask(&task);
            entities.Clear();
        };

        for (entt::entity e : dirtyView) {
            entities.PushBack(e);
            if (entities.Size() == entities.GetCapacity()) { dispatch(); }
        }
        dispatch();
    }

    // Dirty entities write their node matrices into their stable model slots
    {
        Engine::InstanceStore& store = state->instanceStore;
        Engine::ModelStore& modelStore = state->modelStore;
        Engine::TriLightStore& triLightStore = state->triLightStore;
        for (auto [entity, runtime, renderTransform, dirty] : registry.View<Component::MeshRuntime, Component::RenderTransformRuntime, Component::MultiframeDirtyRequest>().each()) {
            if (!runtime.range.IsValid()) { continue; }
            uint32_t lastSlot = ~0u;
            for (uint32_t i = 0; i < runtime.range.count; ++i) {
                const uint32_t slot = runtime.range.offset + i;
                const Engine::InstanceSource& inst = store[slot];
                if (inst.emissiveMeshSlot != Engine::TriLightStore::INVALID_MESH_SLOT) { triLightStore.MarkDirty(inst.emissiveMeshSlot); }
                const glm::mat4 model = renderTransform.modelMatrix * inst.modelSpaceTransform;
                const float scale = glm::sqrt(glm::max(glm::dot(glm::vec3(model[0]), glm::vec3(model[0])), glm::max(glm::dot(glm::vec3(model[1]), glm::vec3(model[1])), glm::dot(glm::vec3(model[2]), glm::vec3(model[2])))));
                store.SetWorldBounds(slot, glm::vec4(glm::vec3(model * glm::vec4(glm::vec3(inst.localBounds), 1.0f)), inst.localBounds.w * scale));
                if (inst.modelSlot == lastSlot) { continue; }
                lastSlot = inst.modelSlot;
                modelStore.SetModel(inst.modelSlot, {model, renderTransform.previousMatrix * inst.modelSpaceTransform});
            }
        }
    }

    // Area light emissive quads
    for (auto [entity, light, transform, surfaceRuntime, dirty] : registry.View<Component::AreaLightComponent, Component::WorldTransformRuntime, Component::LightSurfaceRuntime, Component::MultiframeDirtyRequest>().each()) {
        if (!surfaceRuntime.modelRange.IsValid()) { continue; }
        const Model& previous = state->modelStore.GetModel(surfaceRuntime.modelRange.offset);
        state->modelStore.SetModel(surfaceRuntime.modelRange.offset, {Component::ComputeAreaLightQuadMatrix(transform, light), previous.modelMatrix});
    }

    // Sphere light emissive meshes
    for (auto [entity, light, transform, surfaceRuntime, dirty] : registry.View<Component::SphereLightComponent, Component::WorldTransformRuntime, Component::LightSurfaceRuntime, Component::MultiframeDirtyRequest>(entt::exclude<Component::AreaLightComponent>).each()) {
        if (!surfaceRuntime.modelRange.IsValid()) { continue; }
        const Model& previous = state->modelStore.GetModel(surfaceRuntime.modelRange.offset);
        state->modelStore.SetModel(surfaceRuntime.modelRange.offset, {Component::ComputeSphereLightMatrix(transform, light), previous.modelMatrix});
    }

    // Text quads
    for (auto [entity, runtime, renderTransform, dirty] : registry.View<Component::TextRuntime, Component::RenderTransformRuntime, Component::MultiframeDirtyRequest>().each()) {
        if (!runtime.modelRange.IsValid()) { continue; }
        state->modelStore.SetModel(runtime.modelRange.offset, {renderTransform.modelMatrix, renderTransform.previousMatrix});
    }

    // Analytic light payload. A bake-hidden light writes dead, so the store stays the only authority on what the GPU sees.
    {
        Engine::AnalyticLightStore& lightStore = state->analyticLightStore;
        const bool bAnyHidden = registry.View<Component::ProbeBakeHiddenFlag>().size() > 0;
        auto bHiddenLight = [&](entt::entity entity) { return bAnyHidden && registry.Has<Component::ProbeBakeHiddenFlag>(entity); };

        for (auto [entity, light, runtime, transform, dirty] : registry.View<Component::AreaLightComponent, Component::AreaLightRuntime, Component::WorldTransformRuntime, Component::MultiframeDirtyRequest>().each()) {
            if (runtime.lightSlot == Engine::AnalyticLightStore::INVALID_SLOT) { continue; }
            lightStore.SetLight(runtime.lightSlot, bHiddenLight(entity) ? LightInfo{} : Component::ComputeAreaLightInfo(transform, light));
        }

        for (auto [entity, light, runtime, transform, dirty] : registry.View<Component::SphereLightComponent, Component::SphereLightRuntime, Component::WorldTransformRuntime, Component::MultiframeDirtyRequest>().each()) {
            if (runtime.lightSlot == Engine::AnalyticLightStore::INVALID_SLOT) { continue; }
            lightStore.SetLight(runtime.lightSlot, bHiddenLight(entity) ? LightInfo{} : Component::ComputeSphereLightInfo(transform, light));
        }
    }

    for (auto [entity, dirty] : registry.View<Engine::Mut<Component::MultiframeDirtyRequest>>().each()) {
        dirty.counter--;
        if (dirty.counter <= 0) {
            registry.Remove<Component::MultiframeDirtyRequest>(entity);
        }
    }
}

void SyncLightSurfaces(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    auto& materialManager = ctx->materialManager;
    Engine::InstanceStore& store = state->instanceStore;
    Engine::Material emissiveMaterial = *materialManager->GetMaterial(materialManager->GetDefaultMaterialID());
    emissiveMaterial.props.colorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // black albedo so only emission shows
    constexpr uint32_t SURFACE_FLAGS = INSTANCE_FLAG_MOTION_BLUR | INSTANCE_FLAG_ALPHA_CUTOUT | INSTANCE_FLAG_DDGI_VISIBLE | INSTANCE_FLAG_CAMERA_MOTION_BLUR;

    const bool bAnyHideTags = registry.View<Component::ProbeBakeHiddenFlag>().size() > 0
                              || registry.View<Component::ProbeBakeProxyHiddenFlag>().size() > 0;

    auto emitSurface = [&](entt::entity lightEntity, Component::LightSurfaceRuntime& surfaceRuntime, const glm::vec3& color, float intensity, bool draw) {
        if (!surfaceRuntime.range.IsValid()) { return; }

        bool bDraw = draw;
        if (bDraw && bAnyHideTags) {
            bDraw = !registry.HasAny<Component::ProbeBakeHiddenFlag, Component::ProbeBakeProxyHiddenFlag>(lightEntity);
        }

        if (bDraw) {
            emissiveMaterial.props.emissiveFactor = glm::vec4(color, intensity);
            const Engine::MaterialID materialID = Engine::HashMaterial(emissiveMaterial);
            if (materialID != surfaceRuntime.materialID) {
                materialManager->CreateSynthesizedMaterial(emissiveMaterial);
                materialManager->AcquireMaterial(materialID);
                materialManager->ReleaseMaterial(surfaceRuntime.materialID);
                store.SetMaterial(surfaceRuntime.range.offset, materialManager, materialID);
                surfaceRuntime.materialID = materialID;
            }
        }

        const Engine::InstanceSource& src = store[surfaceRuntime.range.offset];
        if (src.bVisible != bDraw || src.flags != SURFACE_FLAGS || src.stableId != surfaceRuntime.stableId) {
            store.SetRenderState(surfaceRuntime.range, bDraw, SURFACE_FLAGS, surfaceRuntime.stableId);
        }
    };

    for (auto [entity, light, surfaceRuntime] : registry.View<Component::AreaLightComponent, Engine::Mut<Component::LightSurfaceRuntime>>().each()) {
        emitSurface(entity, surfaceRuntime, light.color, light.intensity, light.drawEmissiveSurface && light.bEnabled);
    }
    for (auto [entity, light, surfaceRuntime] : registry.View<Component::SphereLightComponent, Engine::Mut<Component::LightSurfaceRuntime>>(entt::exclude<Component::AreaLightComponent>).each()) {
        emitSurface(entity, surfaceRuntime, light.color, light.intensity, light.drawEmissiveSurface && light.bEnabled);
    }
}

void ResolveSkyboxCubemaps(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    for (auto&& [entity, sky] : registry.View<Component::SkyboxComponent>(entt::exclude<Component::SkyboxRuntime>).each()) {
        if (!sky.envMap.IsValid()) { continue; }
        if (ctx->assetManager->GetCubemapMetadata(sky.envMap) == nullptr) { continue; }
        registry.Set<Component::SkyboxRuntime>(entity, ctx->assetManager->LoadCubemap(sky.envMap));
    }
}

void GatherRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    auto& materialManager = ctx->materialManager;

#ifdef WDEBUG
    VerifyGeometryStores(state);
#endif
    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    //
    {
        ZoneScopedN("Instances");
        const Instance* storeInstances = state->instanceStore.Instances();
        vf.instanceCount = state->instanceStore.GetWatermark();
        vf.instancePayload.Clear();
        vf.instanceRuns.Clear();
        state->instanceStore.DrainDirty(static_cast<uint32_t>(ctx->currentRenderFrame), [&](uint32_t offset, uint32_t count) {
            const size_t base = vf.instancePayload.Size();
            vf.instancePayload.ResizeUninitialized(base + count);
            memcpy(vf.instancePayload.Data() + base, storeInstances + offset, count * sizeof(Instance));
            vf.instanceRuns.PushBack(Core::DirtyRun{offset, count});
        });
    }
    //
    {
        ZoneScopedN("ModelMatrices");
        const Model* storeModels = state->modelStore.Models();
        vf.modelCount = state->modelStore.GetWatermark();
        vf.modelPayload.Clear();
        vf.modelRuns.Clear();
        state->modelStore.DrainDirty(static_cast<uint32_t>(ctx->currentRenderFrame), [&](uint32_t offset, uint32_t count) {
            const size_t base = vf.modelPayload.Size();
            vf.modelPayload.ResizeUninitialized(base + count);
            memcpy(vf.modelPayload.Data() + base, storeModels + offset, count * sizeof(Model));
            vf.modelRuns.PushBack(Core::DirtyRun{offset, count});
        });
    }

    //
    {
        ZoneScopedN("Material Recording");
        uint32_t watermark = 0;
        const auto& entries = materialManager->GetActiveMaterials();
        for (uint32_t i = 0; i < static_cast<uint32_t>(Render::BINDLESS_MATERIAL_BUFFER_COUNT); ++i) {
            const Engine::MaterialEntry& entry = entries[i];
            if (!entry.handle.IsValid() || entry.refCounter <= 0) { continue; }
            watermark = i + 1u;
            vf.activeMaterials.PushBack({i, materialManager->GetRenderMaterial(entry.id)});
        }
        vf.materialCount = watermark;
        vf.materialPayload.Clear();
        vf.materialRuns.Clear();
        const Core::LightingMode lightingMode = state->lighting.lightingMode;
        Render::PipelineManager* pipelineManager = ctx->pipelineManager;
        materialManager->SetLightingMode(lightingMode);
        materialManager->DrainUploadDirty(static_cast<uint32_t>(ctx->currentRenderFrame), watermark, [&](uint32_t offset, uint32_t count) {
            const size_t base = vf.materialPayload.Size();
            vf.materialPayload.ResizeUninitialized(base + count);
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t slot = offset + i;
                const Engine::MaterialEntry& entry = entries[slot];
                MaterialProperties& props = vf.materialPayload[base + i];
                if (!entry.handle.IsValid() || entry.refCounter <= 0) {
                    props = MaterialProperties{};
                    continue;
                }
                const Engine::RenderMaterial material = materialManager->GetRenderMaterial(entry.id);
                props = material.props;
                props.shadingBucketIndex = slot;
                const StringID lightingShader = pipelineManager->ResolveLightingShaderForMode(material.lightingShader, lightingMode);
                props.lightingBucketIndex = pipelineManager->GetLightingShaderIndex(lightingShader);
                props.lightingFlags = lightingShader == "default_black"_sid ? GBUFFER1_BLACK_BIT : lightingShader == "default_unlit"_sid ? GBUFFER1_UNLIT_BIT : 0u;
            }
            vf.materialRuns.PushBack(Core::DirtyRun{offset, count});
        });
    }

    //
    {
        ZoneScopedN("Skybox Selection");
        int32_t bestPriority = INT32_MIN;
        Render::Cubemap* bestCubemap = nullptr;
        float bestIntensity = 1.0f;
        for (const auto& [entity, sky, skyRuntime] : registry.View<Component::SkyboxComponent, Component::SkyboxRuntime>().each()) {
            if (!sky.bEnabled || !skyRuntime.handle.IsValid()) { continue; }
            Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(skyRuntime.handle);
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

    //
    {
        ZoneScopedN("Volumetric Fog Selection");
        int32_t bestPriority = INT32_MIN;
        const Component::VolumetricFogComponent* bestFog = nullptr;
        for (const auto& [entity, fog] : registry.View<Component::VolumetricFogComponent>().each()) {
            if (!fog.bEnabled || fog.priority <= bestPriority) { continue; }
            bestPriority = fog.priority;
            bestFog = &fog;
        }
        if (bestFog) {
            frameBuffer->mainViewFamily.volumetricFog = {
                .albedo = bestFog->albedo,
                .density = bestFog->density,
                .heightFalloff = bestFog->heightFalloff,
                .baseHeight = bestFog->baseHeight,
                .anisotropy = bestFog->anisotropy,
                .ambientScale = bestFog->ambientScale,
                .maxDistance = bestFog->maxDistance,
                .bEnabled = true,
            };
        }
    }
}

void GatherTextRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    auto view = registry.View<Component::TextComponent,Component::TextRuntime, Component::RenderTransformRuntime>(entt::exclude<Component::TextFontRequest>);

    for (const auto& [entity, textComp, runtime, renderTransform] : view.each()) {
        if (textComp.text.IsEmpty()) { continue; }
        if (!runtime.modelRange.IsValid()) { continue; }

        Engine::Font* font = ctx->assetManager->GetFont(runtime.fontHandle);
        if (!font) { continue; }

        const float scale = textComp.scale / font->header.emSize;
        float lineHeight = font->header.lineHeight * scale;
        if (lineHeight <= 0.0f) { lineHeight = (font->header.ascender - font->header.descender) * scale; }
        if (lineHeight <= 0.0f) { lineHeight = textComp.scale; }

        const auto rmat = ctx->materialManager->GetRenderTextMaterial(textComp.textMaterialId);

        const Engine::WFontHeader& fh = font->header;
        const bool bSdf = fh.sdfUncompressedSize > 0;
        const float sdfSpreadFU = fh.sdfSpread * fh.emSize;
        const float sdfCellU = bSdf ? 1.0f / static_cast<float>(fh.sdfCols) : 0.0f;
        const float sdfCellV = bSdf ? 1.0f / static_cast<float>(fh.sdfRows) : 0.0f;

        glm::vec2 padMinEm{0.0f};
        glm::vec2 padMaxEm{0.0f};
        if (rmat.outlineColor.w > 0.0f && rmat.outlineWidth > 0.0f) {
            padMinEm = glm::vec2(rmat.outlineWidth);
            padMaxEm = glm::vec2(rmat.outlineWidth);
        }
        if (rmat.shadowColor.w > 0.0f && (rmat.shadowOffset.x != 0.0f || rmat.shadowOffset.y != 0.0f)) {
            const glm::vec2 off{rmat.shadowOffset.x, rmat.shadowOffset.y};
            const glm::vec2 soft{rmat.shadowSoftness};
            padMinEm = glm::max(padMinEm, soft - off);
            padMaxEm = glm::max(padMaxEm, soft + off);
        }
        if (bSdf) {
            padMinEm = glm::min(padMinEm, glm::vec2(fh.sdfSpread));
            padMaxEm = glm::min(padMaxEm, glm::vec2(fh.sdfSpread));
        }
        const glm::vec2 padMinFU = padMinEm * font->header.emSize;
        const glm::vec2 padMaxFU = padMaxEm * font->header.emSize;

        const char* str = textComp.text.c_str();
        const size_t len = textComp.text.Size();
        auto advanceOf = [&](char c) -> float {
            const Engine::WGlyphInfo* g = ctx->assetManager->GetGlyph(runtime.fontHandle, static_cast<unsigned char>(c));
            return g ? g->advance * scale : textComp.scale * 0.25f;
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
            if (textComp.wrapWidth > 0.0f) {
                float width = 0.0f;
                size_t lastSpace = SIZE_MAX;
                for (size_t i = segStart; i < hardEnd && !lines.IsFull(); ++i) {
                    if (str[i] == ' ') { lastSpace = i; }
                    const float adv = advanceOf(str[i]);
                    if (width + adv > textComp.wrapWidth && lastSpace != SIZE_MAX && lastSpace > segStart) {
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
        const float blockBottom = font->header.descender * scale - static_cast<float>(lines.Size() - 1) * lineHeight;
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

        const uint32_t modelIndex = runtime.modelRange.offset;

        const auto drawCallIndex = static_cast<uint32_t>(frameBuffer->mainViewFamily.textInstances.Size());
        uint32_t quadCount = 0;

        for (uint32_t li = 0; li < lines.Size(); ++li) {
            float cursorX = -lines[li].width * alignFactor;
            const float penY = baseY - static_cast<float>(li) * lineHeight;
            for (size_t i = lines[li].start; i < lines[li].end; ++i) {
                const uint32_t codepoint = static_cast<unsigned char>(str[i]);
                const Engine::WGlyphInfo* g = ctx->assetManager->GetGlyph(runtime.fontHandle, codepoint);
                if (!g) {
                    cursorX += textComp.scale * 0.25f;
                    continue;
                }

                if (g->slugTexelCount != 0) {
                    WorldGlyphQuad quad{};
                    quad.emMin = {g->planeLeft - padMinFU.x, g->planeBottom - padMinFU.y};
                    quad.emMax = {g->planeRight + padMaxFU.x, g->planeTop + padMaxFU.y};
                    quad.posMin = {cursorX + quad.emMin.x * scale, penY + quad.emMin.y * scale};
                    quad.posMax = {cursorX + quad.emMax.x * scale, penY + quad.emMax.y * scale};
                    if (bSdf) {
                        // Cell = planeBounds + spread, v runs top-down
                        const uint32_t glyphIdx = static_cast<uint32_t>(g - font->glyphs.Data());
                        const float cellU0 = static_cast<float>(glyphIdx % fh.sdfCols) * sdfCellU;
                        const float cellV0 = static_cast<float>(glyphIdx / fh.sdfCols) * sdfCellV;
                        const float ku = sdfCellU / (g->planeRight - g->planeLeft + 2.0f * sdfSpreadFU);
                        const float kv = sdfCellV / (g->planeTop - g->planeBottom + 2.0f * sdfSpreadFU);
                        quad.sdfUvScale = {ku, -kv};
                        quad.sdfUvBias = {cellU0 - (g->planeLeft - sdfSpreadFU) * ku, cellV0 + (g->planeTop + sdfSpreadFU) * kv};
                    }
                    quad.glyphTexelOffset = g->slugTexelOffset;
                    quad.color = textComp.color;
                    quad.drawCallIndex = drawCallIndex;
                    frameBuffer->mainViewFamily.worldGlyphQuads.PushBack(quad);
                    ++quadCount;
                }

                cursorX += g->advance * scale;
            }
        }

        if (quadCount == 0) { continue; }

        auto [matIndexRef, inserted] = frameBuffer->mainViewFamily.activeTextMaterials.TryEmplace(textComp.textMaterialId);
        if (inserted) {
            matIndexRef = static_cast<uint32_t>(frameBuffer->mainViewFamily.textMaterials.Size());
            frameBuffer->mainViewFamily.textMaterials.PushBack(rmat);
        }

        uint64_t stableId = 0;
        if (const auto* stable = registry.TryGet<Component::StableIdComponent>(entity)) {
            stableId = stable->id.id;
        }

        frameBuffer->mainViewFamily.textInstances.PushBack({
            .modelIndex = modelIndex,
            .fontCurveByteOffset = font->curveByteOffset,
            .textMaterialIndex = matIndexRef,
            .fontAtlasIndex = bSdf ? static_cast<uint32_t>(font->atlasBindlessHandle.index) : 0u,
            .sdfGridDims = bSdf ? (fh.sdfCols | (fh.sdfRows << 16)) : 0u,
            .stableId = stableId,
        });
    }
}

static void MarkAnalyticLightsDirty(Engine::SystemRegistry& registry)
{
    for (auto entity : registry.View<Component::AreaLightComponent>()) {
        registry.Set<Component::MultiframeDirtyRequest>(entity);
    }
    for (auto entity : registry.View<Component::SphereLightComponent>()) {
        registry.Set<Component::MultiframeDirtyRequest>(entity);
    }
}

void ApplyProbeBakeHideSet(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    for (const auto& [entity, transform] : registry.View<Component::TransformComponent>().each()) {
        if (transform.mobility != Component::Mobility::Static) { registry.Set<Component::ProbeBakeHiddenFlag>(entity); }
    }

    for (auto entity : registry.View<Component::AreaLightComponent>()) {
        registry.Set<Component::ProbeBakeProxyHiddenFlag>(entity);
    }
    for (auto entity : registry.View<Component::SphereLightComponent>()) {
        registry.Set<Component::ProbeBakeProxyHiddenFlag>(entity);
    }

    for (auto entity : registry.View<Component::DynamicPhysicsBodyRuntime>()) {
        registry.Set<Component::ProbeBakeHiddenFlag>(entity);
    }
    for (const auto& [entity, body] : registry.View<Component::PhysicsBodyDesc>().each()) {
        if (body.motionType != Component::PhysicsMotionType::Static) { registry.Set<Component::ProbeBakeHiddenFlag>(entity); }
    }

    EvaluateAllInstanceRenderStates(state, registry);
    MarkAnalyticLightsDirty(registry);
}

void ClearProbeBakeHideSet(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    registry.Clear<Component::ProbeBakeHiddenFlag, Component::ProbeBakeProxyHiddenFlag>();
    EvaluateAllInstanceRenderStates(state, registry);
    MarkAnalyticLightsDirty(registry);
}

/** Bit per active tile whose view frustum a changed caster's bounds touch. */
static uint32_t TouchedTileMask(const ShadowViewGPU* views, uint32_t activeTiles, Core::Span<const Vec4> changes)
{
    // planes[p][axis][tile], inactive tiles get a plane nothing is inside of
    alignas(16) float planes[6][4][LOCAL_SHADOW_MAX_VIEWS];
    for (uint32_t tile = 0; tile < LOCAL_SHADOW_MAX_VIEWS; ++tile) {
        const bool bActive = (activeTiles & (1u << tile)) != 0u;
        for (uint32_t p = 0; p < 6; ++p) {
            const glm::vec4 plane = bActive ? views[tile].frustum.planes[p] : glm::vec4(0.0f, 0.0f, 0.0f, -1e30f);
            for (uint32_t axis = 0; axis < 4; ++axis) {
                planes[p][axis][tile] = plane[axis];
            }
        }
    }

    uint32_t mask = 0;
    for (const Vec4& change : changes) {
        const __m128 cx = _mm_set1_ps(change.x);
        const __m128 cy = _mm_set1_ps(change.y);
        const __m128 cz = _mm_set1_ps(change.z);
        const __m128 negRadius = _mm_set1_ps(-change.w);
        for (uint32_t g = 0; g < LOCAL_SHADOW_MAX_VIEWS / 4; ++g) {
            __m128 inside = _mm_castsi128_ps(_mm_set1_epi32(-1));
            for (uint32_t p = 0; p < 6; ++p) {
                const __m128 distance = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_load_ps(planes[p][0] + 4 * g), cx), _mm_mul_ps(_mm_load_ps(planes[p][1] + 4 * g), cy)),
                                                   _mm_add_ps(_mm_mul_ps(_mm_load_ps(planes[p][2] + 4 * g), cz), _mm_load_ps(planes[p][3] + 4 * g)));
                inside = _mm_and_ps(inside, _mm_cmpge_ps(distance, negRadius));
            }
            mask |= static_cast<uint32_t>(_mm_movemask_ps(inside)) << (4 * g);
        }
    }
    return mask & activeTiles;
}

template<typename LightComponent, typename LightRuntime>
static void ResolveBakedShadow(Engine::EngineContext* ctx, Engine::EngineState* state, Core::ViewFamily& vf, const LightComponent& light, LightRuntime& runtime, bool bActive)
{
    Engine::AssetManager& assets = *ctx->assetManager;
    const LightInfo* lights = state->analyticLightStore.Lights();
    const Engine::AssetManager::ShadowMapInfo* info = light.shadowMode == Component::LightShadowMode::Baked ? assets.GetShadowMapInfo(light.shadowId) : nullptr;
    const bool bUsable = info != nullptr && runtime.lightSlot != Engine::AnalyticLightStore::INVALID_SLOT && Engine::IsShadowBakeCurrent(info->key, lights[runtime.lightSlot]);
    if (!bUsable) {
        if (runtime.bakedShadow.IsValid()) {
            assets.UnloadTextureArray(runtime.bakedShadow);
            runtime.bakedShadow = Engine::TextureArrayHandle::INVALID;
        }
        return;
    }
    if (!runtime.bakedShadow.IsValid()) {
        runtime.bakedShadow = assets.LoadShadowMap(light.shadowId);
    }
    const Render::TextureArray* map = assets.GetTextureArray(runtime.bakedShadow);
    if (bActive && map != nullptr && map->loadState == Render::TextureArray::LoadState::Loaded && !vf.bakedShadowLights.IsFull()) {
        vf.bakedShadowLights.PushBack({runtime.lightSlot, static_cast<uint32_t>(map->bindlessHandle.index), light.bMovingObjectShadows});
    }
}

void ResolveBakedShadows(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    vf.bakedShadowLights.Clear();
    const bool bActive = state->lighting.localShadows.bEnabled && state->lighting.lightingMode == Core::LightingMode::Analytic;
    for (auto [entity, light, runtime] : registry.View<Component::AreaLightComponent, Engine::Mut<Component::AreaLightRuntime>>().each()) {
        ResolveBakedShadow(ctx, state, vf, light, runtime, bActive);
    }
    for (auto [entity, light, runtime] : registry.View<Component::SphereLightComponent, Engine::Mut<Component::SphereLightRuntime>>().each()) {
        ResolveBakedShadow(ctx, state, vf, light, runtime, bActive);
    }
}

static void FindBakedLightMoverFaces(Engine::EngineContext* ctx, const Engine::InstanceStore& store, const LightInfo* lights, const Core::ViewFamily& vf, uint8_t* outFaces)
{
    const auto bakedCount = static_cast<uint32_t>(vf.bakedShadowLights.Size());
    if (bakedCount == 0) { return; }

    // spheres[group][xyzr][lane]
    const uint32_t groups = (bakedCount + 3) / 4;
    Core::Arena& arena = ctx->gameplayArena.Get();
    auto* spheres = static_cast<float*>(arena.AllocRaw(sizeof(float) * 16 * groups, 16));

    for (uint32_t i = 0; i < groups * 4; ++i) {
        float* sphere = spheres + (i / 4) * 16 + (i % 4);
        if (i >= bakedCount || !vf.bakedShadowLights[i].bMovingObjectShadows) {
            sphere[0] = sphere[4] = sphere[8] = 1e30f;
            sphere[12] = 0.0f;
            continue;
        }
        const LightInfo& light = lights[vf.bakedShadowLights[i].lightIndex];
        sphere[0] = light.position.x;
        sphere[4] = light.position.y;
        sphere[8] = light.position.z;
        sphere[12] = light.range;
    }

    const uint32_t watermark = store.GetWatermark();
    for (uint32_t slot = 0; slot < watermark; ++slot) {
        const Engine::InstanceSource& inst = store[slot];
        const Vec4& bounds = store.GetWorldBounds(slot);
        if (!inst.bVisible || bounds.w <= 0.0f || (inst.flags & INSTANCE_FLAG_BAKE_STATIC) != 0u) { continue; }

        const __m128 cx = _mm_set1_ps(bounds.x);
        const __m128 cy = _mm_set1_ps(bounds.y);
        const __m128 cz = _mm_set1_ps(bounds.z);
        const __m128 radius = _mm_set1_ps(bounds.w);
        for (uint32_t g = 0; g < groups; ++g) {
            const float* sphere = spheres + g * 16;
            const __m128 dx = _mm_sub_ps(_mm_load_ps(sphere), cx);
            const __m128 dy = _mm_sub_ps(_mm_load_ps(sphere + 4), cy);
            const __m128 dz = _mm_sub_ps(_mm_load_ps(sphere + 8), cz);
            const __m128 reach = _mm_add_ps(_mm_load_ps(sphere + 12), radius);
            const __m128 distanceSq = _mm_add_ps(_mm_add_ps(_mm_mul_ps(dx, dx), _mm_mul_ps(dy, dy)), _mm_mul_ps(dz, dz));
            uint32_t nearLights = static_cast<uint32_t>(_mm_movemask_ps(_mm_cmple_ps(distanceSq, _mm_mul_ps(reach, reach))));
            while (nearLights != 0u) {
                const uint32_t i = g * 4 + static_cast<uint32_t>(std::countr_zero(nearLights));
                nearLights &= nearLights - 1u;
                const uint32_t lightIndex = vf.bakedShadowLights[i].lightIndex;
                const LightInfo& light = lights[lightIndex];
                const glm::vec3 offset = glm::vec3(bounds) - glm::vec3(light.position);
                if (Render::IsLocalShadowSpot(light)) {
                    outFaces[lightIndex] |= Render::LocalShadowSpotTouched(light, offset, bounds.w) ? 1u : 0u;
                }
                else {
                    outFaces[lightIndex] |= static_cast<uint8_t>(Render::LocalShadowCubeFacesTouched(offset, bounds.w));
                }
            }
        }
    }
}

static void SelectLocalShadows(Engine::EngineContext* ctx, Engine::EngineState* state, Core::ViewFamily& vf, const LightInfo* lights, uint32_t lightCount)
{
    using Selection = Engine::LocalShadowSelection;

    const Engine::LightingState& lighting = state->lighting;
    Selection& selection = state->localShadowSelection;
    const Core::Span<const Vec4> changes = state->instanceStore.GetBoundsChanges();
    if (!lighting.localShadows.bEnabled || lighting.lightingMode != Core::LightingMode::Analytic) {
        selection = {};
    }
    else if (state->debug.localShadow.bFreeze) {
        selection.dirtyTiles = 0;
        selection.bWasFrozen = true;
    }
    else {
        const auto resolution = static_cast<uint32_t>(glm::clamp(lighting.localShadows.resolution, 64, 2048));
        const auto budget = static_cast<uint32_t>(glm::clamp(lighting.localShadows.viewBudget, 1, static_cast<int32_t>(LOCAL_SHADOW_MAX_VIEWS)));
        const glm::uvec2 tiles = Render::LocalShadowAtlasTiles(budget);
        const float fadeStep = lighting.localShadows.fadeSeconds > 0.0f ? state->timeFrame->deltaTime / lighting.localShadows.fadeSeconds : 1.0f;
        const bool bRedrawAll = selection.resolution != resolution || selection.budget != budget || selection.bWasFrozen || vf.bBoundsChangesOverflow;
        if (selection.resolution != resolution || selection.budget != budget) {
            for (uint32_t t = 0; t < LOCAL_SHADOW_MAX_VIEWS; ++t) {
                selection.tileOwners[t] = Selection::FREE_TILE;
            }
        }

        const Selection previous = selection;
        uint32_t previousIndices[LOCAL_SHADOW_MAX_VIEWS];
        for (uint32_t i = 0; i < previous.lightCount; ++i) {
            previousIndices[i] = previous.lights[i].lightIndex;
        }
        auto previousStrength = [&](uint32_t lightIndex) {
            for (uint32_t i = 0; i < previous.lightCount; ++i) {
                if (previous.lights[i].lightIndex == lightIndex) { return previous.lights[i].strength; }
            }
            return -1.0f;
        };

        const Core::ViewData& view = vf.mainView.currentViewData;
        const glm::mat4 cameraViewProj = view.proj * view.view;
        const Frustum camera = Render::CreateFrustum(cameraViewProj);
        uint32_t picks[LOCAL_SHADOW_MAX_VIEWS];
        uint32_t bakedMask[MAX_ANALYTIC_LIGHTS / 32] = {};
        for (const Core::BakedShadowLight& baked : vf.bakedShadowLights) {
            bakedMask[baked.lightIndex >> 5] |= 1u << (baked.lightIndex & 31u);
        }
        uint8_t moverFaces[MAX_ANALYTIC_LIGHTS] = {};
        FindBakedLightMoverFaces(ctx, state->instanceStore, lights, vf, moverFaces);
        uint32_t skipMask[MAX_ANALYTIC_LIGHTS / 32];
        memcpy(skipMask, bakedMask, sizeof(skipMask));
        for (const Core::BakedShadowLight& baked : vf.bakedShadowLights) {
            if (moverFaces[baked.lightIndex] != 0u) { skipMask[baked.lightIndex >> 5] &= ~(1u << (baked.lightIndex & 31u)); }
        }
        auto isBaked = [&](uint32_t lightIndex) { return (bakedMask[lightIndex >> 5] & (1u << (lightIndex & 31u))) != 0u; };
        const uint32_t pickCount = Render::SelectLocalShadowLights(lights, lightCount, cameraViewProj, view.cameraPos, previousIndices, previous.lightCount, budget, picks, skipMask);

        selection.lightCount = 0;
        uint32_t usedViews = 0;
        auto add = [&](uint32_t lightIndex, float strength) {
            const uint32_t faceMask = Render::LocalShadowFaceMask(lights[lightIndex], camera) & (isBaked(lightIndex) ? moverFaces[lightIndex] : 0x3Fu);
            const auto views = static_cast<uint32_t>(std::popcount(faceMask));
            if (views == 0 || usedViews + views > budget) {
                return;
            }
            selection.lights[selection.lightCount++] = Core::LocalShadowLight{lightIndex, faceMask, 0, strength, isBaked(lightIndex)};
            usedViews += views;
        };
        for (uint32_t i = 0; i < pickCount; ++i) {
            add(picks[i], glm::min(glm::max(previousStrength(picks[i]), 0.0f) + fadeStep, 1.0f));
        }
        // Dropped lights fade out on their old shadow while the budget has room.
        for (uint32_t i = 0; i < previous.lightCount; ++i) {
            const Core::LocalShadowLight& old = previous.lights[i];
            bool bPicked = false;
            for (uint32_t k = 0; k < pickCount; ++k) {
                bPicked |= picks[k] == old.lightIndex;
            }
            const float strength = old.strength - fadeStep;
            if (!bPicked && strength > 0.0f && old.lightIndex < lightCount && old.bOverlay == isBaked(old.lightIndex) && selection.lightCount < LOCAL_SHADOW_MAX_VIEWS) {
                add(old.lightIndex, strength);
            }
        }

        // A face keeps its tile while it stays active, so its depth survives; new faces take free tiles and draw.
        Core::Array<uint32_t, LOCAL_SHADOW_MAX_VIEWS> owners{};
        for (uint32_t t = 0; t < LOCAL_SHADOW_MAX_VIEWS; ++t) {
            owners[t] = Selection::FREE_TILE;
        }
        uint32_t dirty = 0;
        for (uint32_t pass = 0; pass < 2; ++pass) {
            for (uint32_t l = 0; l < selection.lightCount; ++l) {
                Core::LocalShadowLight& light = selection.lights[l];
                for (uint32_t face = 0; face < 6; ++face) {
                    if ((light.faceMask & (1u << face)) == 0u) { continue; }
                    const uint32_t key = (light.lightIndex << 4) | (light.bOverlay ? 8u : 0u) | face;
                    uint32_t tile = Selection::FREE_TILE;
                    for (uint32_t t = 0; t < budget; ++t) {
                        if (owners[t] == key) { tile = t; }
                    }
                    if (tile != Selection::FREE_TILE) { continue; }
                    for (uint32_t t = 0; t < budget && tile == Selection::FREE_TILE; ++t) {
                        const bool bKept = pass == 0 && previous.tileOwners[t] == key;
                        const bool bFree = pass == 1 && owners[t] == Selection::FREE_TILE;
                        if (bKept || bFree) { tile = t; }
                    }
                    if (tile == Selection::FREE_TILE) { continue; }
                    owners[tile] = key;
                    if (pass == 1) { dirty |= 1u << tile; }
                    light.faceTiles |= tile << (LOCAL_SHADOW_TILE_BITS * face);
                }
            }
        }

        uint32_t active = 0;
        for (uint32_t t = 0; t < budget; ++t) {
            if (owners[t] == Selection::FREE_TILE) { continue; }
            const uint32_t lightIndex = owners[t] >> 4;
            const uint32_t face = owners[t] & 7u;
            const LightInfo& info = lights[lightIndex];
            if (memcmp(&selection.tileLights[t], &info, sizeof(LightInfo)) != 0) {
                dirty |= 1u << t;
            }
            selection.views[t] = Render::BuildLocalShadowView(info, face, t, tiles, resolution);
            selection.views[t].excludedInstanceFlags = (owners[t] & 8u) != 0u ? INSTANCE_FLAG_BAKE_STATIC : 0u;
            selection.tileLights[t] = info;
            active |= 1u << t;
        }

        if (!bRedrawAll) {
            dirty |= TouchedTileMask(selection.views.Data(), active, changes);
        }
        selection.boundsChangeCount = static_cast<uint32_t>(changes.Size());
        selection.tileOwners = owners;
        selection.activeTiles = active;
        selection.dirtyTiles = bRedrawAll ? active : dirty & active;
        selection.atlasExtent = tiles * resolution;
        selection.resolution = resolution;
        selection.budget = budget;
        selection.bWasFrozen = false;
    }

    vf.localShadowLightCount = selection.lightCount;
    vf.localShadowLights = selection.lights;
    vf.localShadowViews = selection.views;
    vf.localShadowActiveTiles = selection.activeTiles;
    vf.localShadowDirtyTiles = selection.dirtyTiles;
    vf.localShadowAtlasExtent = selection.atlasExtent;
}

void GatherLights(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    Core::ViewFamily& vf = frameBuffer->mainViewFamily;

    state->analyticLightStore.Tick(ctx->currentRenderFrame);
    state->triLightStore.Tick(ctx->currentRenderFrame);

#ifdef WDEBUG
    VerifyAnalyticLightStore(state, registry);
#endif

    const uint32_t analyticWatermark = state->analyticLightStore.GetWatermark();
    const LightInfo* storeLights = state->analyticLightStore.Lights();
    vf.analyticLightCount = analyticWatermark;
    vf.lightRuns.Clear();
    vf.lightPayload.Clear();

    state->analyticLightStore.DrainDirty(static_cast<uint32_t>(ctx->currentRenderFrame), [&](uint32_t offset, uint32_t count) {
        const size_t base = vf.lightPayload.Size();
        vf.lightPayload.ResizeUninitialized(base + count);
        memcpy(vf.lightPayload.Data() + base, storeLights + offset, count * sizeof(LightInfo));
        vf.lightRuns.PushBack(Core::DirtyRun{offset, count});
    });
    const Core::Span<const Vec4> boundsChanges = state->instanceStore.GetBoundsChanges();
    vf.boundsChanges.Clear();
    vf.bBoundsChangesOverflow = boundsChanges.Size() > Core::MAX_BOUNDS_CHANGES;
    if (!vf.bBoundsChangesOverflow) {
        for (const Vec4& change : boundsChanges) {
            vf.boundsChanges.PushBack(change);
        }
    }
    SelectLocalShadows(ctx, state, vf, storeLights, analyticWatermark);
    state->instanceStore.ClearBoundsChanges();

    Engine::TriLightStore& triLightStore = state->triLightStore;
    Engine::EmissiveDebugState& emissiveDebug = state->debug.emissive;
    emissiveDebug.entries.Clear();
    emissiveDebug.bEntriesTruncated = false;
    emissiveDebug.rebuiltTriangles = 0;
    emissiveDebug.liveMeshes = 0;
    emissiveDebug.reservedInstances = triLightStore.GetReservationCount();
    emissiveDebug.meshletWatermark = triLightStore.GetMeshletWatermark();
    emissiveDebug.triLightWatermark = triLightStore.GetWatermark();
    emissiveDebug.analyticLightCount = vf.analyticLightCount;
    const bool bEmissiveCapture = emissiveDebug.bCapture;

    auto recordEmissive = [&](entt::entity entity, uint32_t slot, const Engine::InstanceSource& inst, Engine::EmissiveDispatchState dispatchState) {
        if (emissiveDebug.entries.Size() >= Engine::EmissiveDebugState::MAX_ENTRIES) {
            emissiveDebug.bEntriesTruncated = true;
            return;
        }
        const Engine::Material* material = ctx->materialManager->GetMaterial(inst.materialID);
        const Engine::TriLightStore::Reservation& reservation = triLightStore.Get(inst.emissiveMeshSlot);
        emissiveDebug.entries.PushBack(Engine::EmissiveDebugEntry{
            .entity = entity,
            .instanceSlot = slot,
            .firstLight = static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS) + reservation.range.offset,
            .triangleCount = reservation.range.count,
            .materialIndex = inst.materialIndex,
            .modelSlot = inst.modelSlot,
            .emissiveFactor = material ? material->props.emissiveFactor : glm::vec4(0.0f),
            .materialID = inst.materialID,
            .dispatchState = dispatchState,
        });
    };

    triLightStore.SetEnabled(state->debug.restir.bEmissiveTriangleLights);
    triLightStore.SetRangeMultiplier(state->debug.restir.emissiveTriRangeMultiplier);
    vf.triLightCount = triLightStore.GetWatermark();
    vf.emissiveMeshletCount = triLightStore.GetMeshletWatermark();
    vf.emissiveMeshCount = triLightStore.GetMeshWatermark();
    if (state->debug.restir.bEmissiveTriangleLights) {
        ZoneScopedN("EmissiveTriangleLights");
        Engine::InstanceStore& store = state->instanceStore;

        const uint32_t meshWatermark = triLightStore.GetMeshWatermark();
        if (ctx->frameStatus.bEmissiveLightsPipelineReady) {
            ctx->materialManager->DrainChangedDirty(static_cast<uint32_t>(Render::BINDLESS_MATERIAL_BUFFER_COUNT), [&](uint32_t offset, uint32_t count) {
                for (uint32_t g = 0; g < meshWatermark; ++g) {
                    const Engine::TriLightStore::Reservation& reservation = triLightStore.Get(g);
                    if (!reservation.bLive) { continue; }
                    const uint32_t materialIndex = store[reservation.instanceSlot].materialIndex;
                    if (materialIndex >= offset && materialIndex < offset + count) { triLightStore.MarkDirty(g); }
                }
            });

            triLightStore.DrainDirty(static_cast<uint32_t>(ctx->currentRenderFrame), [&](uint32_t meshSlot) {
                const Engine::TriLightStore::Reservation& reservation = triLightStore.Get(meshSlot);
                const bool bLive = reservation.bLive && store[reservation.instanceSlot].bVisible;
                vf.emissiveTriWork.PushBack(EmissiveTriLightWork{
                    .instanceSlot = reservation.instanceSlot,
                    .firstLight = static_cast<uint32_t>(MAX_ANALYTIC_LIGHTS) + reservation.range.offset,
                    .triangleCount = reservation.range.count,
                    .meshSlot = meshSlot,
                    .firstMeshlet = reservation.meshlets.offset,
                    .meshletCount = reservation.meshlets.count,
                    .bDead = bLive ? 0u : 1u,
                });
                emissiveDebug.rebuiltTriangles += reservation.range.count;
            });
        }

        for (uint32_t g = 0; g < meshWatermark; ++g) {
            const Engine::TriLightStore::Reservation& reservation = triLightStore.Get(g);
            if (reservation.bLive && store[reservation.instanceSlot].bVisible) { emissiveDebug.liveMeshes++; }
        }

        if (bEmissiveCapture) {
            for (const auto& [entity, runtime] : registry.View<Component::MeshRuntime>().each()) {
                if (!runtime.range.IsValid()) { continue; }
                const bool bBakeHidden = registry.Has<Component::ProbeBakeHiddenFlag>(entity);
                for (uint32_t i = 0; i < runtime.range.count; ++i) {
                    const uint32_t slot = runtime.range.offset + i;
                    const Engine::InstanceSource& inst = store[slot];
                    if (inst.emissiveMeshSlot == Engine::TriLightStore::INVALID_MESH_SLOT) { continue; }
                    Engine::EmissiveDispatchState dispatchState;
                    if (bBakeHidden) { dispatchState = Engine::EmissiveDispatchState::ProbeBakeHidden; }
                    else if (!inst.bVisible) { dispatchState = Engine::EmissiveDispatchState::EntityHidden; }
                    else { dispatchState = Engine::EmissiveDispatchState::Live; }
                    recordEmissive(entity, slot, inst, dispatchState);
                }
            }
        }
    }
    else {
        vf.triLightCount = 0;
        vf.emissiveMeshletCount = 0;
        vf.emissiveMeshCount = 0;
    }
    emissiveDebug.rebuiltMeshes = static_cast<uint32_t>(vf.emissiveTriWork.Size());
    emissiveDebug.triLightCountFed = vf.triLightCount;

    //
    {
        ZoneScopedN("DirectionalLight");
        int32_t bestPriority = INT32_MIN;
        bool found = false;
        auto dirView = registry.View<Component::DirectionalLightComponent, Component::WorldTransformRuntime>();
        for (const auto& [entity, light, transform] : dirView.each()) {
            if (light.priority > bestPriority) {
                bestPriority = light.priority;
                vf.directionalLight.direction = transform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
                vf.directionalLight.color = light.color;
                vf.directionalLight.intensity = light.intensity;
                vf.directionalLight.angularRadiusDegrees = light.angularRadiusDegrees;
                vf.directionalLight.volumetricScale = glm::max(light.volumetricScale, 0.0f);
                vf.directionalLight.bEnabled = true;
                found = true;
            }
        }
        if (!found) {
            vf.directionalLight = Core::DirectionalLight{};
        }
    }
}

void GatherReflectionProbes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    const auto& config = state->lighting.reflectionProbe;
    if (!config.bEnabled) { return; }

    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    vf.bReflectionProbeBruteForce = config.bBruteForcePick;

    auto view = registry.View<Component::ReflectionProbeComponent, Component::ReflectionProbeRuntime, Component::WorldTransformRuntime>();
    for (const auto& [entity, probe, probeRuntime, worldTransform] : view.each()) {
        if (vf.reflectionProbes.IsFull()) { break; }
        const bool bPreview = state->debug.bProbePreview;
        if (!probe.bEnabled && !bPreview) { continue; }
        if (!probeRuntime.contentHandle.IsValid()) { continue; }
        Render::Cubemap* cubemap = ctx->assetManager->GetCubemap(probeRuntime.contentHandle);
        if (!cubemap || cubemap->loadState != Render::Cubemap::LoadState::Loaded) { continue; }

        glm::vec3 srcTranslation = worldTransform.translation;
        glm::quat srcRotation = worldTransform.rotation;
        glm::vec3 srcScale = worldTransform.scale;
        glm::vec3 srcCaptureOffset = probe.captureOffset;
        float radianceScale = probe.standInIntensity;
        if (probeRuntime.contentSource == Component::ReflectionProbeComponent::ContentSource::Baked) {
            if (const Engine::AssetManager::ProbeInfo* info = ctx->assetManager->GetProbeInfo(Engine::ProbeID{probe.probeId})) {
                radianceScale = info->radianceScale;
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
                .radianceScale = radianceScale,
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
            .intensity = config.intensity * radianceScale,
        });
    }
}

void GatherLocalDDGIVolumes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    if (!state->lighting.ddgi.bLocalVolumes) { return; }

    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    auto view = registry.View<Component::LocalDDGIVolumeComponent, Component::WorldTransformRuntime>();
    for (const auto& [entity, volume, worldTransform] : view.each()) {
        if (vf.localDDGIVolumes.IsFull()) { break; }
        if (!volume.bEnabled) { continue; }
        vf.localDDGIVolumes.PushBack(Core::LocalDDGIVolume{
            .corner = Component::LocalDDGIVolumeComponent::WindowCorner(worldTransform.translation, volume.probeSpacing),
            .probeSpacing = glm::max(volume.probeSpacing, 0.25f),
            .volumeId = volume.volumeId,
        });
    }
}

void GatherFogVolumes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    const Vec3 cameraPos = vf.mainView.currentViewData.cameraPos;
    const float froxelReach = vf.volumetricFog.bEnabled ? vf.volumetricFog.maxDistance : Core::VolumetricFog{}.maxDistance;

    struct Candidate
    {
        FogVolumeGPU gpu;
        float distance;
    };
    Core::InlineVector<Candidate, VOLUMETRIC_FOG_MAX_VOLUMES> picked;
    for (const auto& [entity, volume, worldTransform] : registry.View<Component::FogVolumeComponent, Component::WorldTransformRuntime>().each()) {
        if (!volume.bEnabled) { continue; }
        const float absorption = glm::max(volume.absorption, 0.0f);
        const float scattering = glm::max(volume.scattering, 0.0f);
        if (absorption <= 0.0f && scattering <= 0.0f) { continue; }

        const Vec3 half = glm::max(glm::abs(worldTransform.scale), Vec3(1e-3f));
        const float boundRadius = glm::length(half);
        const float distance = glm::max(glm::length(worldTransform.translation - cameraPos) - boundRadius, 0.0f);
        // Absorption is per pixel at any distance; scattering lives only inside the froxel grid.
        if (absorption <= 0.0f && distance > froxelReach) { continue; }

        const bool bCylinder = volume.shape == Component::FogVolumeComponent::Shape::Cylinder;
        FogVolumeGPU gpu{};
        Vec4* rows[3] = {&gpu.worldToLocal0, &gpu.worldToLocal1, &gpu.worldToLocal2};
        for (int axis = 0; axis < 3; ++axis) {
            Vec3 basis{0.0f};
            basis[axis] = 1.0f;
            const Vec3 row = (worldTransform.rotation * basis) / half[axis];
            *rows[axis] = Vec4(row, -glm::dot(row, worldTransform.translation));
        }
        const float sideFade = glm::max(volume.edgeFade, 0.0f);
        const float heightFade = glm::max(volume.heightFade, 0.0f) / half.y;
        gpu.fade = bCylinder ? Vec4(sideFade / glm::min(half.x, half.z), heightFade, 0.0f, 0.0f) : Vec4(sideFade / half.x, heightFade, sideFade / half.z, 0.0f);
        gpu.albedoScattering = Vec4(glm::clamp(volume.albedo, Vec3(0.0f), Vec3(1.0f)), scattering);
        gpu.scales = Vec4(glm::max(volume.bottomScale, 0.0f), glm::max(volume.topScale, 0.0f), glm::max(volume.ambientScale, 0.0f), glm::max(volume.localLightScale, 0.0f));
        gpu.absorption = absorption;
        gpu.shape = bCylinder ? FOG_VOLUME_SHAPE_CYLINDER : FOG_VOLUME_SHAPE_BOX;

        if (picked.IsFull()) {
            if (distance >= picked.Back().distance) { continue; }
            picked.PopBack();
        }
        picked.PushBack({gpu, distance});
        for (size_t i = picked.Size() - 1; i > 0 && picked[i - 1].distance > picked[i].distance; --i) {
            std::swap(picked[i - 1], picked[i]);
        }
    }

    if (picked.IsEmpty()) { return; }
    for (const Candidate& c : picked) {
        vf.fogVolumes.PushBack(c.gpu);
    }
    if (!vf.volumetricFog.bEnabled) {
        vf.volumetricFog.density = 0.0f;
        vf.volumetricFog.bEnabled = true;
    }
}

void ApplyPostProcessVolumes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    Core::ViewFamily& vf = frameBuffer->mainViewFamily;
    const Vec3 cameraPos = vf.mainView.currentViewData.cameraPos;
    Core::InlineVector<Engine::ActivePostProcessVolume, Engine::DebugState::MAX_ACTIVE_POST_PROCESS_VOLUMES>& active = state->debug.postProcessVolumes;
    active.Clear();
    for (const auto& [entity, volume, worldTransform] : registry.View<Component::PostProcessVolumeComponent, Component::WorldTransformRuntime>().each()) {
        if (active.IsFull()) { break; }
        const float weight = Component::PostProcessVolumeComponent::CameraWeight(volume, worldTransform.translation, worldTransform.rotation, worldTransform.scale, cameraPos);
        if (weight <= 0.0f || volume.overrides.IsEmpty()) { continue; }
        active.PushBack({entity, volume.priority, weight});
        for (size_t i = active.Size() - 1; i > 0 && active[i - 1].priority > active[i].priority; --i) {
            std::swap(active[i - 1], active[i]);
        }
    }

    for (const Engine::ActivePostProcessVolume& a : active) {
        for (const Component::PostProcessOverride& o : registry.Get<Component::PostProcessVolumeComponent>(a.entity).overrides) {
            if (const Component::PostProcessOverrideField* field = Component::FindPostProcessOverrideField(o.field.View())) {
                Component::ApplyPostProcessOverride(*field, vf, o.value, a.weight);
            }
        }
    }
}

void GatherEditorSprites(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    const uint32_t mask = state->projectConfig.editorSpriteMask;
    Core::ArenaVector<Core::Sprite>& sprites = frameBuffer->mainViewFamily.sprites;
    auto push = [&](entt::entity entity, const Vec3& position, const Vec4& color, uint32_t textureIndex) {
        const auto* stable = registry.TryGet<Component::StableIdComponent>(entity);
        sprites.PushBack(Core::Sprite{
            .worldPosition = position,
            .pixelSize = 0.5f,
            .color = color,
            .stableId = stable != nullptr ? stable->id.id : uint64_t{0},
            .textureIndex = textureIndex,
            .samplerIndex = ASSET_SAMPLER_NEAREST_BINDLESS_INDEX,
            .billboard = true,
        });
    };

    if (mask & Engine::EDITOR_OBJECT_AREA_LIGHT) {
        for (auto [entity, light, transform] : registry.View<Component::AreaLightComponent, Component::WorldTransformRuntime>().each()) {
            push(entity, transform.translation, {light.color.r, light.color.g, light.color.b, 1.0f}, SPRITE_AREA_LIGHT_BINDLESS_INDEX);
        }
    }
    if (mask & Engine::EDITOR_OBJECT_SPHERE_LIGHT) {
        for (auto [entity, light, transform] : registry.View<Component::SphereLightComponent, Component::WorldTransformRuntime>().each()) {
            push(entity, transform.translation, {light.color.r, light.color.g, light.color.b, 1.0f}, SPRITE_POINT_LIGHT_BINDLESS_INDEX);
        }
    }
    if (mask & Engine::EDITOR_OBJECT_DIRECTIONAL_LIGHT) {
        for (auto [entity, light, transform] : registry.View<Component::DirectionalLightComponent, Component::WorldTransformRuntime>().each()) {
            push(entity, transform.translation, {light.color.r, light.color.g, light.color.b, 1.0f}, SPRITE_DIRECTIONAL_LIGHT_BINDLESS_INDEX);
        }
    }
    if (mask & Engine::EDITOR_OBJECT_REFLECTION_PROBE) {
        for (auto [entity, probe, transform] : registry.View<Component::ReflectionProbeComponent, Component::WorldTransformRuntime>().each()) {
            push(entity, transform.translation + transform.rotation * probe.captureOffset, Core::Math::HashColor(probe.probeId, 0u, 0.08f, 0.84f), SPRITE_REFLECTION_PROBE_BINDLESS_INDEX);
        }
    }
    if (mask & Engine::EDITOR_OBJECT_DDGI_VOLUME) {
        for (auto [entity, volume, transform] : registry.View<Component::LocalDDGIVolumeComponent, Component::WorldTransformRuntime>().each()) {
            push(entity, transform.translation, Core::Math::HashColor(volume.volumeId, 0u, 0.08f, 0.84f), SPRITE_DDGI_VOLUME_BINDLESS_INDEX);
        }
    }
    if (mask & Engine::EDITOR_OBJECT_POST_PROCESS_VOLUME) {
        for (auto [entity, volume, transform] : registry.View<Component::PostProcessVolumeComponent, Component::WorldTransformRuntime>().each()) {
            push(entity, transform.translation, {0.7f, 0.4f, 1.0f, 1.0f}, SPRITE_POST_PROCESS_VOLUME_BINDLESS_INDEX);
        }
    }
    if (mask & Engine::EDITOR_OBJECT_FOG_VOLUME) {
        for (auto [entity, volume, transform] : registry.View<Component::FogVolumeComponent, Component::WorldTransformRuntime>().each()) {
            push(entity, transform.translation, {0.55f, 0.75f, 0.85f, 1.0f}, SPRITE_POST_PROCESS_VOLUME_BINDLESS_INDEX);
        }
    }
}

void GatherLightDebugDraws(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    const Engine::LightDebugDrawMode mode = state->projectConfig.editorDebugDrawMode;
    const uint32_t typeMask = mode == Engine::LightDebugDrawMode::None ? 0u : state->projectConfig.editorDebugDrawMask;
    if (typeMask == 0) { return; }

    Core::ViewFamily& viewFamily = frameBuffer->mainViewFamily;

    auto shouldDraw = [&](entt::entity entity, uint32_t typeBit) {
        if (!(typeMask & typeBit)) { return false; }
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

    for (const auto& [entity, light, transform] : registry.View<Component::AreaLightComponent, Component::WorldTransformRuntime>().each()) {
        if (!shouldDraw(entity, Engine::EDITOR_OBJECT_AREA_LIGHT)) { continue; }
        const Vec3 center = transform.translation;
        const Vec3 right = transform.rotation * Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 up = transform.rotation * Vec3(0.0f, 1.0f, 0.0f);
        const Vec3 forward = transform.rotation * Vec3(0.0f, 0.0f, 1.0f);
        constexpr Vec4 editColor{0.5f, 0.8f, 1.0f, 1.0f};
        constexpr Vec4 rangeColor{1.0f, 0.55f, 0.15f, 1.0f};
        if (light.bDisk) {
            const float radius = light.halfWidth * transform.scale.x;
            constexpr int kDiskSegments = 32;
            for (int i = 0; i < kDiskSegments; ++i) {
                const float a0 = (static_cast<float>(i) / kDiskSegments) * 6.2831853f;
                const float a1 = (static_cast<float>(i + 1) / kDiskSegments) * 6.2831853f;
                const Vec3 p0 = center + radius * (glm::cos(a0) * right + glm::sin(a0) * up);
                const Vec3 p1 = center + radius * (glm::cos(a1) * right + glm::sin(a1) * up);
                DEBUG_ADD_LINE(viewFamily.debugLines, {p0, p1, editColor, 0.03f});
            }
        } else {
            DEBUG_ADD_RECT(viewFamily.debugRects, {center, light.halfWidth * transform.scale.x, light.halfHeight * transform.scale.y, right, up, editColor, 0.03f});
        }
        DEBUG_ADD_ARROW(viewFamily.debugArrows, {center, center + forward * 0.5f, 0.08f, 0.02f, editColor, 0.01f});
        if (light.coneOuterDegrees >= 90.0f) {
            addHemisphereVolume(center, forward, right, up, light.range, rangeColor);
        }
        else {
            const float depth = light.range * glm::cos(glm::radians(light.coneOuterDegrees));
            const float spread = light.range * glm::sin(glm::radians(light.coneOuterDegrees));
            const Vec3 farCenter = center + forward * depth;
            if (light.bDisk) {
                const float radius = light.halfWidth * transform.scale.x;
                constexpr int kConeSegments = 32;
                for (int i = 0; i < kConeSegments; ++i) {
                    const float a0 = (static_cast<float>(i) / kConeSegments) * 6.2831853f;
                    const float a1 = (static_cast<float>(i + 1) / kConeSegments) * 6.2831853f;
                    const Vec3 d0 = glm::cos(a0) * right + glm::sin(a0) * up;
                    const Vec3 d1 = glm::cos(a1) * right + glm::sin(a1) * up;
                    DEBUG_ADD_LINE(viewFamily.debugLines, {farCenter + (radius + spread) * d0, farCenter + (radius + spread) * d1, editColor, 0.02f});
                    if ((i % 8) == 0) { DEBUG_ADD_LINE(viewFamily.debugLines, {center + radius * d0, farCenter + (radius + spread) * d0, editColor, 0.02f}); }
                }
            }
            else {
                const float halfW = light.halfWidth * transform.scale.x;
                const float halfH = light.halfHeight * transform.scale.y;
                DEBUG_ADD_RECT(viewFamily.debugRects, {farCenter, halfW + spread, halfH + spread, right, up, editColor, 0.02f});
                for (const Vec2 corner : {Vec2(-1.0f, -1.0f), Vec2(1.0f, -1.0f), Vec2(1.0f, 1.0f), Vec2(-1.0f, 1.0f)}) {
                    const Vec3 nearCorner = center + right * (corner.x * halfW) + up * (corner.y * halfH);
                    const Vec3 farCorner = farCenter + right * (corner.x * (halfW + spread)) + up * (corner.y * (halfH + spread));
                    DEBUG_ADD_LINE(viewFamily.debugLines, {nearCorner, farCorner, editColor, 0.02f});
                }
            }
        }
    }

    for (const auto& [entity, light, transform] : registry.View<Component::SphereLightComponent, Component::WorldTransformRuntime>().each()) {
        if (!shouldDraw(entity, Engine::EDITOR_OBJECT_SPHERE_LIGHT)) { continue; }
        constexpr Vec4 editColor{0.5f, 0.8f, 1.0f, 1.0f};
        constexpr Vec4 rangeColor{1.0f, 0.55f, 0.15f, 1.0f};
        DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {transform.translation, light.radius * transform.scale.x, editColor, 0.02f});
        if (light.range > 0.0f) {
            DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {transform.translation, light.range, rangeColor, 0.02f});
        }
    }

    for (const auto& [entity, light, transform] : registry.View<Component::DirectionalLightComponent, Component::WorldTransformRuntime>().each()) {
        if (!shouldDraw(entity, Engine::EDITOR_OBJECT_DIRECTIONAL_LIGHT)) { continue; }
        const Vec3 forward = transform.rotation * Vec3(0.0f, 0.0f, 1.0f);
        constexpr Vec4 dirColor{1.0f, 0.9f, 0.5f, 1.0f};
        DEBUG_ADD_ARROW(viewFamily.debugArrows, {transform.translation, transform.translation + forward * 2.0f, 0.15f, 0.04f, dirColor, 0.02f});
    }

    for (auto [entity, volume, transform] : registry.View<Component::LocalDDGIVolumeComponent, Component::WorldTransformRuntime>().each()) {
        if (!shouldDraw(entity, Engine::EDITOR_OBJECT_DDGI_VOLUME)) { continue; }
        constexpr Vec4 disabledColor{0.45f, 0.45f, 0.45f, 1.0f};
        const Vec4 color = volume.bEnabled ? Core::Math::HashColor(volume.volumeId, 0u, 0.08f, 0.84f) : disabledColor;
        Component::LocalDDGIVolumeComponent::DrawWindow(viewFamily, transform.translation, volume.probeSpacing, color, state->projectConfig.reflectionProbeLineWidth, state->editor.selectedEntities.Contains(entity));
    }

    for (auto [entity, volume, transform] : registry.View<Component::PostProcessVolumeComponent, Component::WorldTransformRuntime>().each()) {
        if (!shouldDraw(entity, Engine::EDITOR_OBJECT_POST_PROCESS_VOLUME)) { continue; }
        Component::PostProcessVolumeComponent::DrawBounds(viewFamily, volume, transform.translation, transform.rotation, transform.scale, state->projectConfig.reflectionProbeLineWidth, state->editor.selectedEntities.Contains(entity));
    }

    for (auto [entity, volume, transform] : registry.View<Component::FogVolumeComponent, Component::WorldTransformRuntime>().each()) {
        if (!shouldDraw(entity, Engine::EDITOR_OBJECT_FOG_VOLUME)) { continue; }
        Component::FogVolumeComponent::DrawBounds(viewFamily, volume, transform.translation, transform.rotation, transform.scale, state->projectConfig.reflectionProbeLineWidth, state->editor.selectedEntities.Contains(entity));
    }

    for (auto [entity, probe, transform] : registry.View<Component::ReflectionProbeComponent, Component::WorldTransformRuntime>().each()) {
        if (!shouldDraw(entity, Engine::EDITOR_OBJECT_REFLECTION_PROBE)) { continue; }

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
            for (int i = 0; i < 3 && state->editor.selectedEntities.Contains(entity); ++i) {
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
        const auto* probeRuntime = registry.TryGet<Component::ReflectionProbeRuntime>(entity);
        if (probeRuntime && probeRuntime->contentSource == Component::ReflectionProbeComponent::ContentSource::Baked) {
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

void GatherCSMDebugDraws(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    Core::ViewFamily& viewFamily = frameBuffer->mainViewFamily;
    const Core::ViewData& view = viewFamily.mainView.currentViewData;
    if (!state->debug.csm.bDrawCascades) { return; }

    static constexpr glm::vec4 CASCADE_COLORS[CSM_MAX_CASCADES] = {{1.0f, 0.2f, 0.2f, 1.0f}, {0.2f, 1.0f, 0.2f, 1.0f}, {0.2f, 0.4f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.2f, 1.0f}};
    const Render::CSMFrame frame = Render::ComputeCSMFrame(viewFamily.csm, viewFamily.csmAnchor, viewFamily.directionalLight.direction, view.nearPlane);
    const glm::quat rotation = glm::quat_cast(glm::mat3(frame.right, frame.up, frame.toSun));
    for (uint32_t i = 0; i < frame.cascadeCount; ++i) {
        const Render::CSMCascade& cascade = frame.cascades[i];
        const glm::vec3 extents{cascade.halfExtent, cascade.halfExtent, 0.5f * cascade.depthRange};
        DEBUG_ADD_BOX(viewFamily.debugBoxes, {cascade.center, extents, rotation, CASCADE_COLORS[i], cascade.halfExtent * 0.002f});
    }
}

void GatherLocalShadowDebugDraws(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    if (!state->debug.localShadow.bDrawViews) { return; }
    Core::ViewFamily& viewFamily = frameBuffer->mainViewFamily;
    const Engine::LocalShadowSelection& selection = state->localShadowSelection;

    static constexpr glm::vec4 TILE_COLORS[] = {{1.0f, 0.2f, 0.2f, 1.0f}, {0.2f, 1.0f, 0.2f, 1.0f}, {0.2f, 0.4f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.2f, 1.0f},
                                                {1.0f, 0.2f, 1.0f, 1.0f}, {0.2f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.6f, 0.2f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    static constexpr int32_t EDGES[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    const uint32_t drawnTiles = state->debug.localShadow.bRedrawnOnly ? selection.dirtyTiles : selection.activeTiles;
    for (uint32_t v = 0; v < LOCAL_SHADOW_MAX_VIEWS; ++v) {
        if ((drawnTiles & (1u << v)) == 0u) { continue; }
        const glm::mat4 invViewProj = glm::inverse(selection.views[v].viewProj);
        glm::vec3 corners[8];
        for (int32_t c = 0; c < 8; ++c) {
            const glm::vec4 ndc((c & 1) ? 1.0f : -1.0f, (c & 2) ? 1.0f : -1.0f, (c & 4) ? 0.0f : 1.0f, 1.0f);
            const glm::vec4 world = invViewProj * ndc;
            corners[c] = glm::vec3(world) / world.w;
        }
        const glm::vec4 color = TILE_COLORS[v % 8];
        for (const auto& edge : EDGES) {
            DEBUG_ADD_LINE(viewFamily.debugLines, {corners[edge[0]], corners[edge[1]], color, 0.02f});
        }
    }
}
}
