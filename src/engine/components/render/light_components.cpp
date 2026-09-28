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
#include "render/types/render_types.h"

namespace Engine
{
static constexpr float LIGHT_PI = 3.14159265358979f;

static Vec3 EditorLightScale(entt::registry& registry, entt::entity entity)
{
    const auto* transform = registry.try_get<Component::TransformComponent>(entity);
    return transform ? transform->scale : Vec3{1.0f, 1.0f, 1.0f};
}

static float AreaLightLumensPerNit(const Component::AreaLightComponent& light, const Vec3& scale)
{
    const float halfWidth = light.halfWidth * scale.x;
    const float area = light.bDisk ? LIGHT_PI * halfWidth * halfWidth : 4.0f * halfWidth * light.halfHeight * scale.y;
    const float sinOuter = glm::sin(glm::radians(light.coneOuterDegrees));
    return LIGHT_PI * area * sinOuter * sinOuter;
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

void Component::AreaLightComponent::OnEditPreview(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void Component::AreaLightComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<LightSurfacePendingTag>(entity);
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void Component::SphereLightComponent::OnEditPreview(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void Component::SphereLightComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

Engine::ComponentEditorResult Component::AreaLightComponent::DrawEditor(Core::ViewFamily& viewFamily, EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
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
        EditWidgets::ColorEdit3(edit, "Color##al", &AreaLightComponent::color);
        EditLightIntensity(edit, "Intensity##al", &AreaLightComponent::intensity, {.lumensPerNit = AreaLightLumensPerNit(comp, EditorLightScale(registry, entity))});
        EditWidgets::Checkbox(edit, "Disk##al", &AreaLightComponent::bDisk);
        ImGui::BeginDisabled(!bEditing);
        EditWidgets::DragFloat(edit, comp.bDisk ? "Radius##al" : "Half Width##al", &AreaLightComponent::halfWidth, 0.05f, 0.01f, 100.0f);
        if (!comp.bDisk) { EditWidgets::DragFloat(edit, "Half Height##al", &AreaLightComponent::halfHeight, 0.05f, 0.01f, 100.0f); }
        ImGui::EndDisabled();
        EditWidgets::DragFloat(edit, "Range##al", &AreaLightComponent::range, 0.5f, 0.0f, 1000.0f);
        EditWidgets::DragFloat(edit, "Cone Outer##al", &AreaLightComponent::coneOuterDegrees, 0.5f, 0.0f, 90.0f, "%.1f deg");
        float coneInner = comp.coneInnerDegrees;
        if (ImGui::DragFloat("Cone Inner##al", &coneInner, 0.5f, 0.0f, 90.0f, EditWidgets::MixedFormat(edit.IsMixed(&AreaLightComponent::coneInnerDegrees), "%.1f deg"))) {
            edit.Preview<AreaLightComponent>([coneInner](AreaLightComponent& c) {
                c.coneOuterDegrees = glm::max(c.coneOuterDegrees, coneInner);
                c.coneInnerDegrees = coneInner;
            });
        }
        EditWidgets::CommitOnRelease<AreaLightComponent>(edit, false);
        EditWidgets::Checkbox(edit, "Draw Emissive Surface##al", &AreaLightComponent::drawEmissiveSurface);
        EditWidgets::Checkbox(edit, "Probe Bake Exclude##al", &AreaLightComponent::bExcludeFromProbeBake);

        ImGui::PushStyleColor(ImGuiCol_Button, bEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled(edit.IsMulti() || ((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing));
        if (ImGui::Button(bEditing ? "Done##aledit" : "Edit##aledit")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    if (transform && bEditing) {
        const auto& comp = edit.Get<AreaLightComponent>();
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
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
    comp.coneOuterDegrees = glm::clamp(comp.coneOuterDegrees, 0.0f, 90.0f);
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
        EditWidgets::DragInt(edit, "Priority##dl", &DirectionalLightComponent::priority, 1.0f, -100, 100);
    }

    return {.bRequestRemoval = remove};
}

glm::mat4 Component::ComputeAreaLightQuadMatrix(const TransformComponent& transform, const AreaLightComponent& light)
{
    const glm::mat3 rot = glm::mat3_cast(transform.rotation);
    const glm::vec3 right = rot[0];
    const glm::vec3 up = rot[1];
    const glm::vec3 normal = rot[2];
    const float halfWidth = light.halfWidth * transform.scale.x;
    const float halfHeight = light.bDisk ? halfWidth : light.halfHeight * transform.scale.y;

    glm::mat4 m(1.0f);
    m[0] = glm::vec4(right * (2.0f * halfWidth), 0.0f);
    m[1] = glm::vec4(normal, 0.0f);
    m[2] = glm::vec4(up * (-2.0f * halfHeight), 0.0f);
    m[3] = glm::vec4(transform.translation, 1.0f);
    return m;
}

LightInfo Component::ComputeAreaLightInfo(const TransformComponent& transform, const AreaLightComponent& light)
{
    const glm::mat3 rot = glm::mat3_cast(transform.rotation);
    const float halfWidth = light.halfWidth * transform.scale.x;
    return LightInfo{
        .position = {transform.translation, glm::cos(glm::radians(light.coneOuterDegrees))},
        .normal = {rot[2], glm::cos(glm::radians(light.coneInnerDegrees))},
        .right = {rot[0], halfWidth},
        .up = {rot[1], light.bDisk ? halfWidth : light.halfHeight * transform.scale.y},
        .packedColor = Render::PackColorRGBA8(glm::vec4(light.color, light.drawEmissiveSurface ? 1.0f : 0.0f)),
        .intensity = light.intensity,
        .range = light.range,
        .type = light.bDisk ? LIGHT_TYPE_DISK : LIGHT_TYPE_AREA,
    };
}

void Component::AreaLightComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->commandQueue.Push({.type = CommandType::AreaLightConstruct, .entity = entity});
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    registry.emplace_or_replace<LightSurfacePendingTag>(entity);
}

void Component::AreaLightComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& light = registry.get<AreaLightComponent>(entity);
    if (light.lightSlot != AnalyticLightStore::INVALID_SLOT) {
        state->commandQueue.Push({.type = CommandType::LightSlotFree, .payload = {.lightSlot = light.lightSlot}});
        light.lightSlot = AnalyticLightStore::INVALID_SLOT;
    }
    registry.remove<LightSurfacePendingTag>(entity);
    registry.remove<LightSurfaceRuntime>(entity);
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
        EditWidgets::ColorEdit3(edit, "Color##sl", &SphereLightComponent::color);
        EditLightIntensity(edit, "Intensity##sl", &SphereLightComponent::intensity, {.lumensPerNit = SphereLightLumensPerNit(comp, EditorLightScale(edit.Registry(), edit.Primary()))});
        EditWidgets::DragFloat(edit, "Radius##sl", &SphereLightComponent::radius, 0.05f, 0.01f, 100.0f);
        EditWidgets::DragFloat(edit, "Range##sl", &SphereLightComponent::range, 0.5f, 0.0f, 1000.0f);
        EditWidgets::Checkbox(edit, "Draw Emissive Surface##sl", &SphereLightComponent::drawEmissiveSurface);
        EditWidgets::Checkbox(edit, "Probe Bake Exclude##sl", &SphereLightComponent::bExcludeFromProbeBake);
    }

    return {.bRequestRemoval = remove};
}

glm::mat4 Component::ComputeSphereLightMatrix(const TransformComponent& transform, const SphereLightComponent& light)
{
    const float scale = 2.0f * light.radius * transform.scale.x; // unit sphere has radius 0.5
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(scale, 0.0f, 0.0f, 0.0f);
    m[1] = glm::vec4(0.0f, scale, 0.0f, 0.0f);
    m[2] = glm::vec4(0.0f, 0.0f, scale, 0.0f);
    m[3] = glm::vec4(transform.translation, 1.0f);
    return m;
}

LightInfo Component::ComputeSphereLightInfo(const TransformComponent& transform, const SphereLightComponent& light)
{
    return LightInfo{
        .position = {transform.translation, 0.0f},
        .normal = {0.0f, 0.0f, 0.0f, 0.0f},
        .right = {0.0f, 0.0f, 0.0f, light.radius * transform.scale.x},
        .up = {0.0f, 0.0f, 0.0f, 0.0f},
        .packedColor = Render::PackColorRGBA8(glm::vec4(light.color, light.drawEmissiveSurface ? 1.0f : 0.0f)),
        .intensity = light.intensity,
        .range = light.range,
        .type = LIGHT_TYPE_SPHERE,
    };
}

void Component::SphereLightComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->commandQueue.Push({.type = CommandType::SphereLightConstruct, .entity = entity});
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    registry.emplace_or_replace<LightSurfacePendingTag>(entity);
}

void Component::SphereLightComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& light = registry.get<SphereLightComponent>(entity);
    if (light.lightSlot != AnalyticLightStore::INVALID_SLOT) {
        state->commandQueue.Push({.type = CommandType::LightSlotFree, .payload = {.lightSlot = light.lightSlot}});
        light.lightSlot = AnalyticLightStore::INVALID_SLOT;
    }
    registry.remove<LightSurfacePendingTag>(entity);
    registry.remove<LightSurfaceRuntime>(entity);
}

void Component::LightSurfaceRuntime::OnConstruct(entt::registry& registry, entt::entity entity)
{
    if (const auto* stable = registry.try_get<StableIdComponent>(entity)) {
        registry.get<LightSurfaceRuntime>(entity).stableId = stable->id.id;
    }
}

void Component::LightSurfaceRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& runtime = registry.get<LightSurfaceRuntime>(entity);
    if (!runtime.range.IsValid() && !runtime.modelRange.IsValid()) { return; }

    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->commandQueue.Push({.type = CommandType::MeshRelease, .payload = {.meshRelease = {runtime.range.offset, runtime.range.count, runtime.modelRange.offset, runtime.modelRange.count, StaticModelHandle::INVALID}}});
    runtime.range = {};
    runtime.modelRange = {};
}

void Component::SkyboxComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<SkyboxComponent>(entity);
    if (comp.handle.IsValid()) {
        registry.ctx().get<Engine::EngineContext*>()->assetManager->UnloadCubemap(comp.handle);
        comp.handle = Engine::CubemapHandle::INVALID;
    }
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
        auto* ctx = edit.Registry().ctx().get<Engine::EngineContext*>();

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

void Component::SkyboxComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get<SkyboxComponent>(entity).handle = Engine::CubemapHandle::INVALID;
}

void Component::SkyboxComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<SkyboxComponent>(entity);
    if (comp.handle.IsValid()) {
        auto* state = registry.ctx().get<Engine::EngineState*>();
        state->commandQueue.Push({.type = CommandType::CubemapRelease, .payload = {.cubemapHandle = comp.handle}});
        comp.handle = Engine::CubemapHandle::INVALID;
    }
}
} // Engine
