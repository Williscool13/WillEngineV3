//
// Created by William on 2026-05-23.
//

#include "light_components.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include "game/component-registry/json_helpers.h"
#include "game/components/core_components.h"
#include "game/components/render_components.h"
#include "engine/include/engine_context.h"

namespace Game
{
Engine::ComponentEditorResult Component::AreaLightComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    static entt::entity editEntity = entt::null;
    static bool bEditing = false;

    if (editEntity != entity) {
        editEntity = entity;
        bEditing = false;
    }

    auto* state = registry.ctx().get<Engine::EngineState*>();
    if (bEditing) { state->editor.bExclusiveGizmoActive = true; }

    bool open = ImGui::CollapsingHeader("Area Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletearealight");
    ImGui::PopStyleColor();

    if (open) {
        auto& comp = registry.get<AreaLightComponent>(entity);
        ImGui::ColorEdit3("Color##al", &comp.color.r);
        ImGui::DragFloat("Intensity##al", &comp.intensity, 0.05f, 0.0f, 100.0f);
        ImGui::BeginDisabled(!bEditing);
        bool extentChanged = false;
        extentChanged |= ImGui::DragFloat("Half Width##al", &comp.halfWidth, 0.05f, 0.01f, 100.0f);
        extentChanged |= ImGui::DragFloat("Half Height##al", &comp.halfHeight, 0.05f, 0.01f, 100.0f);
        if (extentChanged) {
            registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity); // recompute the emissive quad matrix; extents don't dirty the transform on their own
        }
        ImGui::EndDisabled();
        ImGui::DragFloat("Range##al", &comp.range, 0.5f, 0.0f, 1000.0f);
        ImGui::Checkbox("Draw Emissive Surface##al", &comp.drawEmissiveSurface);
        ImGui::Checkbox("Probe Bake Exclude##al", &comp.bExcludeFromProbeBake);

        ImGui::PushStyleColor(ImGuiCol_Button, bEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing);
        if (ImGui::Button(bEditing ? "Done##aledit" : "Edit##aledit")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    if (transform && bEditing) {
        auto& comp = registry.get<AreaLightComponent>(entity);
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
                          [&](Vec3 newPt) { comp.halfWidth = glm::max(0.01f, glm::dot(newPt - center, right) / transform->scale.x); registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity); },
                          Editor::COLOR_AXIS_X);

        const Vec3 heightPlaneNormal = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, up) * up);
        Editor::DotHandle(Editor::DotHandleId::LIGHT_AREA_BASE + 1, center + up * comp.halfHeight * transform->scale.y, heightPlaneNormal,
                          vd.view, vd.proj, viewport, vd.cameraPos, state,
                          [&](Vec3 newPt) { comp.halfHeight = glm::max(0.01f, glm::dot(newPt - center, up) / transform->scale.y); registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity); },
                          Editor::COLOR_AXIS_Y);
    }

    return {.requestRemoval = remove};
}

void Component::AreaLightComponent::Serialize(const AreaLightComponent& comp, nlohmann::json& json)
{
    json["color"] = comp.color;
    json["intensity"] = comp.intensity;
    json["halfWidth"] = comp.halfWidth;
    json["halfHeight"] = comp.halfHeight;
    json["range"] = comp.range;
    json["drawEmissiveSurface"] = comp.drawEmissiveSurface;
    json["bExcludeFromProbeBake"] = comp.bExcludeFromProbeBake;
}

void Component::AreaLightComponent::Deserialize(AreaLightComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.color = json.contains("color") ? json["color"].get<Vec3>() : Vec3{1.0f, 1.0f, 1.0f};
    comp.intensity = json.value("intensity", 1.0f);
    comp.halfWidth = json.value("halfWidth", 1.0f);
    comp.halfHeight = json.value("halfHeight", 1.0f);
    comp.range = json.value("range", 10.0f);
    comp.drawEmissiveSurface = json.value("drawEmissiveSurface", true);
    comp.bExcludeFromProbeBake = json.value("bExcludeFromProbeBake", false);
}

Engine::ComponentEditorResult Component::DirectionalLightComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletedirlight");
    ImGui::PopStyleColor();

    if (open) {
        auto& comp = registry.get<DirectionalLightComponent>(entity);
        ImGui::ColorEdit3("Color##dl", &comp.color.r);
        ImGui::DragFloat("Intensity##dl", &comp.intensity, 0.05f, 0.0f, 100.0f);
        ImGui::DragFloat("Angular Radius (deg)##dl", &comp.angularRadiusDegrees, 0.02f, 0.0f, 30.0f);
        ImGui::DragInt("Priority##dl", &comp.priority, 1.0f, -100, 100);
    }

    return {.requestRemoval = remove};
}

void Component::DirectionalLightComponent::Serialize(const DirectionalLightComponent& comp, nlohmann::json& json)
{
    json["color"] = comp.color;
    json["intensity"] = comp.intensity;
    json["priority"] = comp.priority;
    json["angularRadiusDegrees"] = comp.angularRadiusDegrees;
}

void Component::DirectionalLightComponent::Deserialize(DirectionalLightComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.color = json.contains("color") ? json["color"].get<Vec3>() : Vec3{1.0f, 1.0f, 1.0f};
    comp.intensity = json.value("intensity", 2.0f);
    comp.priority = json.value("priority", 0);
    comp.angularRadiusDegrees = json.value("angularRadiusDegrees", 1.0f);
}

glm::mat4 Component::ComputeAreaLightQuadMatrix(const TransformComponent& transform, const AreaLightComponent& light)
{
    const glm::mat3 rot = glm::mat3_cast(transform.rotation);
    const glm::vec3 right = rot[0];
    const glm::vec3 up = rot[1];
    const glm::vec3 normal = rot[2];
    const float halfWidth = light.halfWidth * transform.scale.x;
    const float halfHeight = light.halfHeight * transform.scale.y;

    glm::mat4 m(1.0f);
    m[0] = glm::vec4(right * (2.0f * halfWidth), 0.0f);
    m[1] = glm::vec4(normal, 0.0f);
    m[2] = glm::vec4(up * (-2.0f * halfHeight), 0.0f);
    m[3] = glm::vec4(transform.translation, 1.0f);
    return m;
}

void Component::AreaLightComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    registry.emplace_or_replace<LightSurfacePendingTag>(entity);
    state->assetLoad.bPendingModelResolve = true;
}

void Component::AreaLightComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<LightSurfacePendingTag>(entity);
    registry.remove<LightSurfaceRuntime>(entity);
}

Engine::ComponentEditorResult Component::SphereLightComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Sphere Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletespherelight");
    ImGui::PopStyleColor();

    if (open) {
        auto& comp = registry.get<SphereLightComponent>(entity);
        ImGui::ColorEdit3("Color##sl", &comp.color.r);
        ImGui::DragFloat("Intensity##sl", &comp.intensity, 0.05f, 0.0f, 100.0f);
        if (ImGui::DragFloat("Radius##sl", &comp.radius, 0.05f, 0.01f, 100.0f)) {
            registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity); // recompute the emissive mesh matrix; radius doesn't dirty the transform on its own
        }
        ImGui::DragFloat("Range##sl", &comp.range, 0.5f, 0.0f, 1000.0f);
        ImGui::Checkbox("Draw Emissive Surface##sl", &comp.drawEmissiveSurface);
        ImGui::Checkbox("Probe Bake Exclude##sl", &comp.bExcludeFromProbeBake);
    }

    return {.requestRemoval = remove};
}

void Component::SphereLightComponent::Serialize(const SphereLightComponent& comp, nlohmann::json& json)
{
    json["color"] = comp.color;
    json["intensity"] = comp.intensity;
    json["radius"] = comp.radius;
    json["range"] = comp.range;
    json["drawEmissiveSurface"] = comp.drawEmissiveSurface;
    json["bExcludeFromProbeBake"] = comp.bExcludeFromProbeBake;
}

void Component::SphereLightComponent::Deserialize(SphereLightComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.color = json.contains("color") ? json["color"].get<Vec3>() : Vec3{1.0f, 1.0f, 1.0f};
    comp.intensity = json.value("intensity", 1.0f);
    comp.radius = json.value("radius", 0.5f);
    comp.range = json.value("range", 10.0f);
    comp.drawEmissiveSurface = json.value("drawEmissiveSurface", true);
    comp.bExcludeFromProbeBake = json.value("bExcludeFromProbeBake", false);
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

void Component::SphereLightComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    registry.emplace_or_replace<LightSurfacePendingTag>(entity);
    state->assetLoad.bPendingModelResolve = true;
}

void Component::SphereLightComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<LightSurfacePendingTag>(entity);
    registry.remove<LightSurfaceRuntime>(entity);
}

void Component::LightSurfaceRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& runtime = registry.get<LightSurfaceRuntime>(entity);
    if (!runtime.range.IsValid()) { return; }

    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->meshPrimitiveStore.ReleaseAndFree(ctx->materialManager, runtime.range);
}

Engine::ComponentEditorResult Component::SkyboxComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteskybox");
    ImGui::PopStyleColor();

    if (open) {
        auto& comp = registry.get<SkyboxComponent>(entity);
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();

        static bool bShowProbes = false;

        const Engine::AssetManager::CachedCubemapMetadata* currentMeta = ctx->assetManager->GetCubemapMetadata(comp.envMap);
        const char* preview = currentMeta ? currentMeta->name.c_str() : "None";
        if (ImGui::BeginCombo("Env Map##sky", preview)) {
            for (const auto& [id, meta] : ctx->assetManager->GetCubemapCache()) {
                if (!bShowProbes && meta.source.Extension() == ".wprobe" && id != comp.envMap) { continue; }
                const bool selected = id == comp.envMap;
                if (ImGui::Selectable(meta.name.c_str(), selected) && id != comp.envMap) {
                    if (comp.handle.IsValid()) {
                        ctx->assetManager->UnloadCubemap(comp.handle);
                        comp.handle = Engine::CubemapHandle::INVALID;
                    }
                    comp.envMap = id;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Show Probes In Selection##sky", &bShowProbes);

        ImGui::DragFloat("Intensity##sky", &comp.intensity, 0.05f, 0.0f, 100.0f);
        ImGui::DragInt("Priority##sky", &comp.priority, 1.0f, -100, 100);
    }

    return {.requestRemoval = remove};
}

void Component::SkyboxComponent::Serialize(const SkyboxComponent& comp, nlohmann::json& json)
{
    json["envMap"] = comp.envMap.id;
    json["intensity"] = comp.intensity;
    json["priority"] = comp.priority;
}

void Component::SkyboxComponent::Deserialize(SkyboxComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.envMap = Engine::EnvironmentMapID{json.value("envMap", uint64_t{0})};
    comp.intensity = json.value("intensity", 1.0f);
    comp.priority = json.value("priority", 0);
}

void Component::SkyboxComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get<SkyboxComponent>(entity).handle = Engine::CubemapHandle::INVALID;
}

void Component::SkyboxComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<SkyboxComponent>(entity);
    if (comp.handle.IsValid()) {
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        ctx->assetManager->UnloadCubemap(comp.handle);
        comp.handle = Engine::CubemapHandle::INVALID;
    }
}
} // Game
