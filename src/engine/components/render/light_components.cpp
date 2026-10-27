//
// Created by William on 2026-05-23.
//

#include "light_components.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include "engine/components/component_editor.h"
#include "engine/editor/editor_gizmo_helpers.h"
#include "engine/editor/editor_widgets.h"
#include "engine/components/core_components.h"
#include "engine/components/render_components.h"
#include "engine/include/engine_context.h"
#include "engine/input/engine_actions.h"
#include "engine/asset_manager.h"
#include "render/interface/render_interface.h"
#include "render/types/render_types.h"

namespace Engine
{
static constexpr float LIGHT_PI = 3.14159265358979f;

static Vec3 EditorLightScale(Engine::SystemRegistry& registry, entt::entity entity)
{
    const auto* world = registry.TryGet<Component::WorldTransformRuntime>(entity);
    return world ? world->scale : Vec3{1.0f, 1.0f, 1.0f};
}

static float AreaLightLumensPerNit(const Component::AreaLightComponent& light, const Vec3& scale)
{
    const float halfWidth = light.halfWidth * scale.x;
    const float area = light.bDisk ? LIGHT_PI * halfWidth * halfWidth : 4.0f * halfWidth * light.halfHeight * scale.y;
    if (light.bNormalizeCone) { return LIGHT_PI * area; }
    const float sinOuter = glm::sin(glm::radians(light.coneOuterDegrees));
    return LIGHT_PI * area * sinOuter * sinOuter;
}

// A sharper beam edge frays under camera motion (ReSTIR has no reusable neighbour or history samples)
static constexpr float AREA_LIGHT_MIN_CONE_DEGREES = Component::AreaLightComponent::MIN_CONE_OUTER_DEGREES;
static constexpr double AREA_LIGHT_CONE_MIN_WIDTH = 1e-6; // LIGHT_CONE_MIN_WIDTH

/** 1 / LightConeIntegral (pbr_functions.slang), from the same float cosines the shader compares against. */
static float AreaLightConeScale(float cosInner, float cosOuter)
{
    const double cO = cosOuter;
    const double w = glm::max(static_cast<double>(cosInner) - cO, AREA_LIGHT_CONE_MIN_WIDTH);
    const double cI = glm::min(cO + w, 1.0);
    const double integral = glm::max((1.0 - cI) * (1.0 + cI) + w * (cO + 0.7 * w), 1e-12);
    return static_cast<float>(1.0 / integral);
}

static float SphereLightLumensPerNit(const Component::SphereLightComponent& light, const Vec3& scale)
{
    const float radius = light.radius * scale.x;
    return 4.0f * LIGHT_PI * LIGHT_PI * radius * radius;
}

template<typename C>
static void EditLightIntensity(EditContext& edit, const char* label, float C::* member, const Widgets::LightIntensityOpts& opts)
{
    float v = edit.Get<C>().*member;
    if (Widgets::DragLightIntensity(label, &v, opts)) {
        edit.PreviewSet(member, v);
    }
    EditWidgets::CommitOnRelease<C>(edit, false);
}

static void ClaimShadowId(Engine::SystemRegistry& registry, entt::entity entity, Component::LightShadowMode mode, uint64_t& shadowId)
{
    if (mode != Component::LightShadowMode::Baked) { return; }
    auto* state = registry.State();
    auto isTaken = [&](uint64_t id) {
        for (const auto [other, light] : registry.View<Component::AreaLightComponent>().each()) {
            if (other != entity && light.shadowId == id) { return true; }
        }
        for (const auto [other, light] : registry.View<Component::SphereLightComponent>().each()) {
            if (other != entity && light.shadowId == id) { return true; }
        }
        return false;
    };
    while (shadowId == 0 || isTaken(shadowId)) {
        shadowId = state->rng();
    }
}

void Component::AreaLightComponent::OnEditPreview(Engine::SystemRegistry& registry, entt::entity entity)
{
    registry.Set<MultiframeDirtyRequest>(entity);
}

void Component::AreaLightComponent::OnEditCommit(Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& light = registry.GetMut<AreaLightComponent>(entity);
    ClaimShadowId(registry, entity, light.shadowMode, light.shadowId);
    registry.Set<LightSurfaceRequest>(entity);
    registry.Set<MultiframeDirtyRequest>(entity);
}

void Component::SphereLightComponent::OnEditPreview(Engine::SystemRegistry& registry, entt::entity entity)
{
    registry.Set<MultiframeDirtyRequest>(entity);
}

void Component::SphereLightComponent::OnEditCommit(Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& light = registry.GetMut<SphereLightComponent>(entity);
    ClaimShadowId(registry, entity, light.shadowMode, light.shadowId);
    registry.Set<MultiframeDirtyRequest>(entity);
}

static void DrawShadowBakeControls(EditContext& edit, uint64_t shadowId)
{
    Engine::SystemRegistry& registry = edit.Registry();
    auto* state = registry.State();
    auto* ctx = registry.Context();
    const uint32_t lightSlot = Component::FindLightSlot(registry, edit.Primary());

    const AssetManager::ShadowMapInfo* info = ctx->assetManager->GetShadowMapInfo(shadowId);
    if (info == nullptr) {
        ImGui::TextDisabled("Not baked, using dynamic");
    }
    else if (lightSlot != AnalyticLightStore::INVALID_SLOT && !IsShadowBakeCurrent(info->key, state->analyticLightStore.Lights()[lightSlot])) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Stale, using dynamic until rebaked");
    }
    else {
        ImGui::Text("Baked shadow_%llu (%u)", static_cast<unsigned long long>(shadowId), info->resolution);
    }

    ImGui::BeginDisabled(edit.IsMulti());
    if (ImGui::Button("Bake Shadow Map")) {
        state->shadowBake.Enqueue(edit.Primary());
    }
    ImGui::EndDisabled();
}

static constexpr const char* SHADOW_MODE_LABELS[] = {"Off", "Dynamic", "Baked"};
static constexpr const char* SHADOW_BAKE_RESOLUTION_LABELS[] = {"256", "512", "1024", "2048"};

Engine::ComponentEditorResult Component::AreaLightComponent::DrawEditor(Core::ViewFamily& viewFamily, EditContext& edit, const char* name)
{
    Engine::SystemRegistry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    static entt::entity editEntity = entt::null;
    static bool bEditing = false;

    if (editEntity != entity || edit.IsMulti()) {
        editEntity = entity;
        bEditing = false;
    }

    auto* state = edit.State();
    if (bEditing) { state->editor.bExclusiveGizmoActive = true; }

    bool open = ImGui::CollapsingHeader("Area Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletearealight");
    ImGui::PopStyleColor();

    if (open) {
        const auto& comp = edit.Get<AreaLightComponent>();
        EditWidgets::Checkbox(edit, "Enabled##al", &AreaLightComponent::bEnabled);
        EditWidgets::ColorEdit3(edit, "Color##al", &AreaLightComponent::color);
        EditLightIntensity(edit, "Intensity##al", &AreaLightComponent::intensity, {.lumensPerNit = AreaLightLumensPerNit(comp, EditorLightScale(registry, entity))});
        EditWidgets::Checkbox(edit, "Disk##al", &AreaLightComponent::bDisk);
        ImGui::BeginDisabled(!bEditing);
        EditWidgets::DragFloat(edit, comp.bDisk ? "Radius##al" : "Half Width##al", &AreaLightComponent::halfWidth, 0.05f, 0.01f, 100.0f);
        if (!comp.bDisk) { EditWidgets::DragFloat(edit, "Half Height##al", &AreaLightComponent::halfHeight, 0.05f, 0.01f, 100.0f); }
        ImGui::EndDisabled();
        EditWidgets::DragFloat(edit, "Range##al", &AreaLightComponent::range, 0.5f, 0.0f, 1000.0f);
        EditWidgets::DragFloat(edit, "Falloff Exponent##al", &AreaLightComponent::falloffExponent, 0.01f, 0.0f, 4.0f);
        EditWidgets::DragFloat(edit, "Volumetric##al", &AreaLightComponent::volumetricScale, 0.01f, 0.0f, 100.0f);
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("How strongly this light scatters in volumetric fog; 0 = fog ignores it (fill and cheat lights)"); }
        EditWidgets::DragFloat(edit, "Cone Outer##al", &AreaLightComponent::coneOuterDegrees, 0.5f, MIN_CONE_OUTER_DEGREES, 90.0f, "%.1f deg");
        float coneInner = comp.coneInnerDegrees;
        if (ImGui::DragFloat("Cone Inner##al", &coneInner, 0.5f, 0.0f, 90.0f, EditWidgets::MixedFormat(edit.IsMixed(&AreaLightComponent::coneInnerDegrees), "%.1f deg"))) {
            edit.Preview<AreaLightComponent>([coneInner](AreaLightComponent& c) {
                c.coneOuterDegrees = glm::max(c.coneOuterDegrees, coneInner);
                c.coneInnerDegrees = coneInner;
            });
        }
        EditWidgets::CommitOnRelease<AreaLightComponent>(edit, false);
        EditWidgets::Checkbox(edit, "Normalize Cone##al", &AreaLightComponent::bNormalizeCone);
        EditWidgets::Checkbox(edit, "Draw Emissive Surface##al", &AreaLightComponent::drawEmissiveSurface);

        ImGui::PushStyleColor(ImGuiCol_Button, bEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled(edit.IsMulti() || ((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing));
        if (ImGui::Button(bEditing ? "Done##aledit" : "Edit##aledit")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();

        Widgets::SubHeader("Shadows");
        EditWidgets::Combo(edit, "Mode##al", &AreaLightComponent::shadowMode, SHADOW_MODE_LABELS, 3);
        if (comp.shadowMode == LightShadowMode::Baked) {
            EditWidgets::Combo(edit, "Bake Resolution##al", &AreaLightComponent::shadowBakeResolution, SHADOW_BAKE_RESOLUTION_LABELS, 4);
            EditWidgets::Checkbox(edit, "Moving Object Shadows##al", &AreaLightComponent::bMovingObjectShadows);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", "Movable objects inside the light still cast shadows, from a small dynamic map drawn only while something moves in range. Off: baked shadows only, no runtime cost."); }
            DrawShadowBakeControls(edit, comp.shadowId);
        }
    }

    const auto* transform = registry.TryGet<WorldTransformRuntime>(entity);
    if (transform && bEditing) {
        const auto& comp = edit.Get<AreaLightComponent>();
        auto* ctx = registry.Context();
        const auto& vd = viewFamily.mainView.currentViewData;
        const Vec3 center = transform->translation;
        const Quat rot = transform->rotation;
        const Vec3 right = rot * Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 up = rot * Vec3(0.0f, 1.0f, 0.0f);

        const Vec4 viewport{
            static_cast<float>(ctx->windowContext.viewportOffsetX),
            static_cast<float>(ctx->windowContext.viewportOffsetY),
            static_cast<float>(ctx->windowContext.viewportWidth),
            static_cast<float>(ctx->windowContext.viewportHeight),
        };

        const Vec3 widthPlaneNormal = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, right) * right);
        Editor::DotHandle(Editor::DotHandleId::LIGHT_AREA_BASE + 0, center + right * comp.halfWidth * transform->scale.x, widthPlaneNormal,
                          vd.view, vd.proj, viewport, vd.cameraPos, state,
                          [&](Vec3 newPt) { edit.PreviewSet(&AreaLightComponent::halfWidth, glm::max(0.01f, glm::dot(newPt - center, right) / transform->scale.x)); },
                          Editor::COLOR_AXIS_X);

        if (!comp.bDisk) {
            const Vec3 heightPlaneNormal = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, up) * up);
            Editor::DotHandle(Editor::DotHandleId::LIGHT_AREA_BASE + 1, center + up * comp.halfHeight * transform->scale.y, heightPlaneNormal,
                              vd.view, vd.proj, viewport, vd.cameraPos, state,
                              [&](Vec3 newPt) { edit.PreviewSet(&AreaLightComponent::halfHeight, glm::max(0.01f, glm::dot(newPt - center, up) / transform->scale.y)); },
                              Editor::COLOR_AXIS_Y);
        }
    }

    return {.bRequestRemoval = remove};
}

void Component::AreaLightComponent::Sanitize(AreaLightComponent& comp)
{
    comp.coneOuterDegrees = glm::clamp(comp.coneOuterDegrees, MIN_CONE_OUTER_DEGREES, 90.0f);
    comp.coneInnerDegrees = glm::clamp(comp.coneInnerDegrees, 0.0f, comp.coneOuterDegrees);
}

Engine::ComponentEditorResult Component::DirectionalLightComponent::DrawEditor(Core::ViewFamily& viewFamily, EditContext& edit, const char* name)
{
    bool open = ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletedirlight");
    ImGui::PopStyleColor();

    if (open) {
        EditWidgets::ColorEdit3(edit, "Color##dl", &DirectionalLightComponent::color);
        EditLightIntensity(edit, "Intensity##dl", &DirectionalLightComponent::intensity, {.bIlluminance = true});
        EditWidgets::DragFloat(edit, "Angular Radius (deg)##dl", &DirectionalLightComponent::angularRadiusDegrees, 0.02f, 0.0f, 30.0f);
        EditWidgets::DragFloat(edit, "Volumetric##dl", &DirectionalLightComponent::volumetricScale, 0.01f, 0.0f, 100.0f);
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("How strongly the sun scatters in volumetric fog; 0 = fog ignores it"); }
        EditWidgets::DragInt(edit, "Priority##dl", &DirectionalLightComponent::priority, 1.0f, -100, 100);
    }

    return {.bRequestRemoval = remove};
}

glm::mat4 Component::ComputeAreaLightQuadMatrix(const Transform& world, const AreaLightComponent& light)
{
    const glm::mat3 rot = glm::mat3_cast(world.rotation);
    const glm::vec3 right = rot[0];
    const glm::vec3 up = rot[1];
    const glm::vec3 normal = rot[2];
    const float halfWidth = light.halfWidth * world.scale.x;
    const float halfHeight = light.bDisk ? halfWidth : light.halfHeight * world.scale.y;

    glm::mat4 m(1.0f);
    m[0] = glm::vec4(right * (2.0f * halfWidth), 0.0f);
    m[1] = glm::vec4(normal, 0.0f);
    m[2] = glm::vec4(up * (-2.0f * halfHeight), 0.0f);
    m[3] = glm::vec4(world.translation, 1.0f);
    return m;
}

LightInfo Component::ComputeAreaLightInfo(const Transform& world, const AreaLightComponent& light)
{
    if (!light.bEnabled) { return LightInfo{}; }
    const glm::mat3 rot = glm::mat3_cast(world.rotation);
    const float halfWidth = light.halfWidth * world.scale.x;
    const float outerDegrees = glm::max(light.coneOuterDegrees, AREA_LIGHT_MIN_CONE_DEGREES);
    const float cosOuter = glm::cos(glm::radians(outerDegrees));
    const float cosInner = glm::cos(glm::radians(glm::min(light.coneInnerDegrees, outerDegrees)));
    return LightInfo{
        .position = {world.translation, cosOuter},
        .normal = {rot[2], cosInner},
        .right = {rot[0], halfWidth},
        .up = {rot[1], light.bDisk ? halfWidth : light.halfHeight * world.scale.y},
        .packedColor = Render::PackColorRGBA8(glm::vec4(light.color, light.drawEmissiveSurface ? 1.0f : 0.0f)),
        .intensity = light.intensity,
        .range = light.range,
        .type = light.bDisk ? LIGHT_TYPE_DISK : LIGHT_TYPE_AREA,
        .falloffBias = 2.0f - light.falloffExponent,
        .volumetricScale = glm::max(light.volumetricScale, 0.0f),
        .coneScale = light.bNormalizeCone ? AreaLightConeScale(cosInner, cosOuter) : 1.0f,
        .flags = light.shadowMode != LightShadowMode::Off ? LIGHT_FLAG_CAST_SHADOWS : 0u,
    };
}

void Component::AreaLightComponent::OnConstruct(Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& light = registry.GetMut<AreaLightComponent>(entity);
    ClaimShadowId(registry, entity, light.shadowMode, light.shadowId);
    registry.Set<AreaLightRuntime>(entity);
    registry.PushCommand({.type = CommandType::AreaLightConstruct, .entity = entity});
    registry.Set<MultiframeDirtyRequest>(entity);
    registry.Set<LightSurfaceRequest>(entity);
}

template<typename LightRuntime>
static void ReleaseLightRuntime(Engine::SystemRegistry& registry, entt::entity entity)
{
    const auto& runtime = registry.Get<LightRuntime>(entity);
    if (runtime.bakedShadow.IsValid()) {
        registry.Context()->assetManager->UnloadTextureArray(runtime.bakedShadow);
    }
    if (runtime.lightSlot != AnalyticLightStore::INVALID_SLOT) {
        registry.PushCommand({.type = CommandType::LightSlotFree, .payload = {.lightSlot = runtime.lightSlot}});
    }
}

void Component::AreaLightRuntime::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    ReleaseLightRuntime<AreaLightRuntime>(registry, entity);
}

void Component::SphereLightRuntime::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    ReleaseLightRuntime<SphereLightRuntime>(registry, entity);
}

void Component::AreaLightComponent::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    registry.Remove<AreaLightRuntime>(entity);
    registry.Remove<LightSurfaceRequest>(entity);
    registry.Remove<LightSurfaceRuntime>(entity);
}

Engine::ComponentEditorResult Component::SphereLightComponent::DrawEditor(Core::ViewFamily& viewFamily, EditContext& edit, const char* name)
{
    bool open = ImGui::CollapsingHeader("Sphere Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletespherelight");
    ImGui::PopStyleColor();

    if (open) {
        const auto& comp = edit.Get<SphereLightComponent>();
        EditWidgets::Checkbox(edit, "Enabled##sl", &SphereLightComponent::bEnabled);
        EditWidgets::ColorEdit3(edit, "Color##sl", &SphereLightComponent::color);
        EditLightIntensity(edit, "Intensity##sl", &SphereLightComponent::intensity, {.lumensPerNit = SphereLightLumensPerNit(comp, EditorLightScale(edit.Registry(), edit.Primary()))});
        EditWidgets::DragFloat(edit, "Radius##sl", &SphereLightComponent::radius, 0.05f, 0.01f, 100.0f);
        EditWidgets::DragFloat(edit, "Range##sl", &SphereLightComponent::range, 0.5f, 0.0f, 1000.0f);
        EditWidgets::DragFloat(edit, "Falloff Exponent##sl", &SphereLightComponent::falloffExponent, 0.01f, 0.0f, 4.0f);
        EditWidgets::DragFloat(edit, "Volumetric##sl", &SphereLightComponent::volumetricScale, 0.01f, 0.0f, 100.0f);
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("How strongly this light scatters in volumetric fog; 0 = fog ignores it (fill and cheat lights)"); }
        EditWidgets::Checkbox(edit, "Draw Emissive Surface##sl", &SphereLightComponent::drawEmissiveSurface);
        Widgets::SubHeader("Shadows");
        EditWidgets::Combo(edit, "Mode##sl", &SphereLightComponent::shadowMode, SHADOW_MODE_LABELS, 3);
        if (comp.shadowMode == LightShadowMode::Baked) {
            EditWidgets::Combo(edit, "Bake Resolution##sl", &SphereLightComponent::shadowBakeResolution, SHADOW_BAKE_RESOLUTION_LABELS, 4);
            EditWidgets::Checkbox(edit, "Moving Object Shadows##sl", &SphereLightComponent::bMovingObjectShadows);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", "Movable objects inside the light still cast shadows, from a small dynamic map drawn only while something moves in range. Off: baked shadows only, no runtime cost."); }
            DrawShadowBakeControls(edit, comp.shadowId);
        }
    }

    return {.bRequestRemoval = remove};
}

glm::mat4 Component::ComputeSphereLightMatrix(const Transform& world, const SphereLightComponent& light)
{
    const float scale = 2.0f * light.radius * world.scale.x; // unit sphere has radius 0.5
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(scale, 0.0f, 0.0f, 0.0f);
    m[1] = glm::vec4(0.0f, scale, 0.0f, 0.0f);
    m[2] = glm::vec4(0.0f, 0.0f, scale, 0.0f);
    m[3] = glm::vec4(world.translation, 1.0f);
    return m;
}

LightInfo Component::ComputeSphereLightInfo(const Transform& world, const SphereLightComponent& light)
{
    if (!light.bEnabled) { return LightInfo{}; }
    return LightInfo{
        .position = {world.translation, 0.0f},
        .normal = {0.0f, 0.0f, 0.0f, 0.0f},
        .right = {0.0f, 0.0f, 0.0f, light.radius * world.scale.x},
        .up = {0.0f, 0.0f, 0.0f, 0.0f},
        .packedColor = Render::PackColorRGBA8(glm::vec4(light.color, light.drawEmissiveSurface ? 1.0f : 0.0f)),
        .intensity = light.intensity,
        .range = light.range,
        .type = LIGHT_TYPE_SPHERE,
        .falloffBias = 2.0f - light.falloffExponent,
        .volumetricScale = glm::max(light.volumetricScale, 0.0f),
        .coneScale = 1.0f,
        .flags = light.shadowMode != LightShadowMode::Off ? LIGHT_FLAG_CAST_SHADOWS : 0u,
    };
}

void Component::SphereLightComponent::OnConstruct(Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& light = registry.GetMut<SphereLightComponent>(entity);
    ClaimShadowId(registry, entity, light.shadowMode, light.shadowId);
    registry.Set<SphereLightRuntime>(entity);
    registry.PushCommand({.type = CommandType::SphereLightConstruct, .entity = entity});
    registry.Set<MultiframeDirtyRequest>(entity);
    registry.Set<LightSurfaceRequest>(entity);
}

void Component::SphereLightComponent::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    registry.Remove<SphereLightRuntime>(entity);
    registry.Remove<LightSurfaceRequest>(entity);
    registry.Remove<LightSurfaceRuntime>(entity);
}

void Component::LightSurfaceRuntime::OnConstruct(Engine::SystemRegistry& registry, entt::entity entity)
{
    if (const auto* stable = registry.TryGet<StableIdComponent>(entity)) {
        registry.GetMut<LightSurfaceRuntime>(entity).stableId = stable->id.id;
    }
}

void Component::LightSurfaceRuntime::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& runtime = registry.GetMut<LightSurfaceRuntime>(entity);
    if (!runtime.range.IsValid() && !runtime.modelRange.IsValid()) { return; }

    registry.PushCommand({.type = CommandType::MeshRelease, .payload = {.meshRelease = {runtime.range.offset, runtime.range.count, runtime.modelRange.offset, runtime.modelRange.count, StaticModelHandle::INVALID}}});
    runtime.range = {};
    runtime.modelRange = {};
}

void Component::SkyboxComponent::OnEditCommit(Engine::SystemRegistry& registry, entt::entity entity)
{
    registry.Remove<SkyboxRuntime>(entity);
}

Engine::ComponentEditorResult Component::SkyboxComponent::DrawEditor(Core::ViewFamily& viewFamily, EditContext& edit, const char* name)
{
    bool open = ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteskybox");
    ImGui::PopStyleColor();

    if (open) {
        const auto& comp = edit.Get<SkyboxComponent>();
        auto* ctx = edit.Registry().Context();

        EditWidgets::Checkbox(edit, "Enabled##sky", &SkyboxComponent::bEnabled);

        static bool bShowProbes = false;

        const Engine::AssetManager::CachedCubemapMetadata* currentMeta = ctx->assetManager->GetCubemapMetadata(comp.envMap);
        const char* preview = edit.IsMixed(&SkyboxComponent::envMap) ? "--" : currentMeta ? currentMeta->name.c_str() : "None";
        if (ImGui::BeginCombo("Env Map##sky", preview)) {
            for (const auto& [id, meta] : ctx->assetManager->GetCubemapCache()) {
                if (!bShowProbes && meta.source.Extension() == ".wprobe" && id != comp.envMap) { continue; }
                if (ImGui::Selectable(meta.name.c_str(), id == comp.envMap)) {
                    edit.Set(&SkyboxComponent::envMap, Engine::EnvironmentMapID{id});
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Show Probes In Selection##sky", &bShowProbes);

        EditLightIntensity(edit, "Intensity##sky", &SkyboxComponent::intensity, {.tooltip = "Env map texel value to nits"});
        EditWidgets::DragInt(edit, "Priority##sky", &SkyboxComponent::priority, 1.0f, -100, 100);
    }

    return {.bRequestRemoval = remove};
}

Engine::ComponentEditorResult Component::VolumetricFogComponent::DrawEditor(Core::ViewFamily& viewFamily, EditContext& edit, const char* name)
{
    bool open = ImGui::CollapsingHeader("Volumetric Fog", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletefog");
    ImGui::PopStyleColor();

    if (open) {
        EditWidgets::Checkbox(edit, "Enabled##fog", &VolumetricFogComponent::bEnabled);
        EditWidgets::ColorEdit3(edit, "Albedo##fog", &VolumetricFogComponent::albedo);
        EditWidgets::DragFloat(edit, "Density##fog", &VolumetricFogComponent::density, 0.001f, 0.0f, 10.0f, "%.4f");
        EditWidgets::DragFloat(edit, "Height Falloff##fog", &VolumetricFogComponent::heightFalloff, 0.001f, 0.0f, 10.0f, "%.4f");
        EditWidgets::DragFloat(edit, "Base Height##fog", &VolumetricFogComponent::baseHeight, 0.1f);
        EditWidgets::DragFloat(edit, "Anisotropy##fog", &VolumetricFogComponent::anisotropy, 0.01f, -0.95f, 0.95f);
        EditWidgets::DragFloat(edit, "Ambient Scale##fog", &VolumetricFogComponent::ambientScale, 0.01f, 0.0f, 10.0f);
        EditWidgets::DragFloat(edit, "Max Distance##fog", &VolumetricFogComponent::maxDistance, 0.5f, 1.0f, 1000.0f);
        EditWidgets::DragInt(edit, "Priority##fog", &VolumetricFogComponent::priority, 1.0f, -100, 100);
    }

    return {.bRequestRemoval = remove};
}

// World-space face handles. Plain drag moves one face with the opposite face fixed; Shift scales X and Z together about the centre, Ctrl scales all axes together.
static void DrawFogVolumeFaceHandles(Core::ViewFamily& viewFamily, EditContext& edit, entt::entity entity)
{
    auto* ctx = edit.Registry().Context();
    Engine::EngineState* state = edit.State();
    const Engine::SystemRegistry& registry = edit.Registry();
    const auto& vd = viewFamily.mainView.currentViewData;
    const Vec4 viewport{
        static_cast<float>(ctx->windowContext.viewportOffsetX),
        static_cast<float>(ctx->windowContext.viewportOffsetY),
        static_cast<float>(ctx->windowContext.viewportWidth),
        static_cast<float>(ctx->windowContext.viewportHeight),
    };

    const Transform world = Component::ComputeWorldTransform(registry, entity);
    const auto* node = registry.TryGet<Component::HierarchyComponent>(entity);
    const Transform parentWorld = node && registry.Valid(node->parent) ? Component::ComputeWorldTransform(registry, node->parent) : Transform{};

    const Vec3 center = world.translation;
    const Quat rot = world.rotation;
    const Vec3 half = glm::abs(world.scale);
    const Vec3 axes[3] = {rot * Vec3(1.0f, 0.0f, 0.0f), rot * Vec3(0.0f, 1.0f, 0.0f), rot * Vec3(0.0f, 0.0f, 1.0f)};
    const ImU32 colors[3] = {Editor::COLOR_AXIS_X, Editor::COLOR_AXIS_Y, Editor::COLOR_AXIS_Z};
    for (int i = 0; i < 3; ++i) {
        for (int s = 0; s < 2; ++s) {
            const Vec3 outward = s == 0 ? axes[i] : -axes[i];
            Editor::AxisDotHandle(Editor::DotHandleId::FOG_VOLUME_BOUNDS_BASE + i * 2 + s, center + outward * half[i], outward,
                                  vd.view, vd.proj, viewport, vd.cameraPos, state,
                                  [&](Vec3 newPt) {
                                      const ImGuiIO& io = ImGui::GetIO();
                                      Transform target = world;
                                      target.scale = half;
                                      if (io.KeyCtrl) {
                                          target.scale = half * (glm::max(0.05f, glm::dot(newPt - center, outward)) / half[i]);
                                      }
                                      else if (io.KeyShift && i != 1) {
                                          const float newHalf = glm::max(0.05f, glm::dot(newPt - center, outward));
                                          target.scale.x = newHalf;
                                          target.scale.z = newHalf;
                                      }
                                      else {
                                          const Vec3 opposite = center - outward * half[i];
                                          const float newHalf = glm::max(0.05f, glm::dot(newPt - opposite, outward) * 0.5f);
                                          target.scale[i] = newHalf;
                                          target.translation = opposite + outward * newHalf;
                                      }
                                      const Transform local = Component::ComposeLocalFromWorld(parentWorld, target);
                                      edit.Preview<Component::TransformComponent>([&](Component::TransformComponent& t) {
                                          t.translation = local.translation;
                                          t.scale = local.scale;
                                      });
                                  },
                                  colors[i]);
        }
    }
}

Engine::ComponentEditorResult Component::FogVolumeComponent::DrawEditor(Core::ViewFamily& viewFamily, EditContext& edit, const char* name)
{
    Engine::SystemRegistry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    static entt::entity editEntity = entt::null;
    static bool bEditing = false;

    if (editEntity != entity || edit.IsMulti()) {
        editEntity = entity;
        bEditing = false;
    }

    Engine::EngineState* state = edit.State();
    if (bEditing) {
        state->editor.bExclusiveGizmoActive = true;
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!popupOpen && state->input.GetActionState(Actions::ACTION_ESCAPE).down) {
            bEditing = false;
        }
    }

    bool open = ImGui::CollapsingHeader("Fog Volume", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletefogvolume");
    ImGui::PopStyleColor();

    if (open) {
        static constexpr const char* SHAPE_LABELS[] = {"Box", "Cylinder"};
        EditWidgets::Checkbox(edit, "Enabled##fv", &FogVolumeComponent::bEnabled);
        EditWidgets::Combo(edit, "Shape##fv", &FogVolumeComponent::shape, SHAPE_LABELS, 2);
        ImGui::PushStyleColor(ImGuiCol_Button, bEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled(edit.IsMulti() || ((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing));
        if (ImGui::Button(bEditing ? "Done##fv" : "Edit Bounds##fv")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
        EditWidgets::DragFloat(edit, "Absorption##fv", &FogVolumeComponent::absorption, 0.005f, 0.0f, 100.0f, "%.4f");
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Darkens what is seen through the volume, per metre. Exact per pixel, no froxel leaking."); }
        EditWidgets::DragFloat(edit, "Scattering##fv", &FogVolumeComponent::scattering, 0.001f, 0.0f, 100.0f, "%.4f");
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Lit haze per metre, through the froxel grid. Needs Max Distance of the global fog (128 m without one)."); }
        EditWidgets::ColorEdit3(edit, "Albedo##fv", &FogVolumeComponent::albedo);
        EditWidgets::DragFloat(edit, "Edge Fade##fv", &FogVolumeComponent::edgeFade, 0.05f, 0.0f, 1000.0f, "%.2f m");
        EditWidgets::DragFloat(edit, "Height Fade##fv", &FogVolumeComponent::heightFade, 0.05f, 0.0f, 1000.0f, "%.2f m");
        EditWidgets::DragFloat(edit, "Bottom Scale##fv", &FogVolumeComponent::bottomScale, 0.01f, 0.0f, 100.0f);
        EditWidgets::DragFloat(edit, "Top Scale##fv", &FogVolumeComponent::topScale, 0.01f, 0.0f, 100.0f);
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Density multipliers at the bottom and top faces; the volume ramps linearly between them."); }
        EditWidgets::DragFloat(edit, "Ambient Scale##fv", &FogVolumeComponent::ambientScale, 0.01f, 0.0f, 10.0f);
        EditWidgets::DragFloat(edit, "Local Light Scale##fv", &FogVolumeComponent::localLightScale, 0.01f, 0.0f, 10.0f);
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("0 on both ambient and local lights leaves only sun shafts, so a partial volume shows no seam."); }
    }

    if (bEditing && registry.Has<TransformComponent>(entity)) {
        DrawFogVolumeFaceHandles(viewFamily, edit, entity);
        const Transform world = ComputeWorldTransform(registry, entity);
        DrawBounds(viewFamily, edit.Get<FogVolumeComponent>(), world.translation, world.rotation, world.scale, state->projectConfig.reflectionProbeLineWidth, true);
    }

    return {.bRequestRemoval = remove};
}

void Component::FogVolumeComponent::DrawBounds(Core::ViewFamily& viewFamily, const FogVolumeComponent& volume, const Vec3& center, const Quat& rotation, const Vec3& scale, float lineWidth, bool bSelected)
{
    constexpr Vec4 color{0.55f, 0.75f, 0.85f, 1.0f};
    constexpr Vec4 disabledColor{0.45f, 0.45f, 0.45f, 1.0f};
    const Vec4 drawColor = volume.bEnabled ? color : disabledColor;
    const Vec3 half = glm::abs(scale);
    if (volume.shape == Shape::Box) {
        DEBUG_ADD_BOX(viewFamily.debugBoxes, {center, half, rotation, drawColor, lineWidth});
        return;
    }
    constexpr int SEGMENTS = 32;
    const Vec3 right = rotation * Vec3(half.x, 0.0f, 0.0f);
    const Vec3 up = rotation * Vec3(0.0f, half.y, 0.0f);
    const Vec3 forward = rotation * Vec3(0.0f, 0.0f, half.z);
    for (int i = 0; i < SEGMENTS; ++i) {
        const float a0 = static_cast<float>(i) / SEGMENTS * 2.0f * LIGHT_PI;
        const float a1 = static_cast<float>(i + 1) / SEGMENTS * 2.0f * LIGHT_PI;
        const Vec3 d0 = glm::cos(a0) * right + glm::sin(a0) * forward;
        const Vec3 d1 = glm::cos(a1) * right + glm::sin(a1) * forward;
        DEBUG_ADD_LINE(viewFamily.debugLines, {center + up + d0, center + up + d1, drawColor, lineWidth});
        DEBUG_ADD_LINE(viewFamily.debugLines, {center - up + d0, center - up + d1, drawColor, lineWidth});
        if (i % (bSelected ? 4 : 8) == 0) {
            DEBUG_ADD_LINE(viewFamily.debugLines, {center - up + d0, center + up + d0, drawColor, lineWidth});
        }
    }
}

void Component::SkyboxComponent::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    registry.Remove<SkyboxRuntime>(entity);
}

void Component::SkyboxRuntime::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    const auto& runtime = registry.Get<SkyboxRuntime>(entity);
    if (runtime.handle.IsValid()) {
        registry.PushCommand({.type = CommandType::CubemapRelease, .payload = {.cubemapHandle = runtime.handle}});
    }
}
} // Engine
