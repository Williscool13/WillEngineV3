//
// Created by William on 2026-05-23.
//

#include "light_components.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include "game/components/core_components.h"
#include "game/components/render_components.h"
#include "engine/include/engine_context.h"
#include "render/types/render_types.h"

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

    bool modified = false;
    if (open) {
        auto& comp = registry.get<AreaLightComponent>(entity);
        modified |= ImGui::ColorEdit3("Color##al", &comp.color.r);
        modified |= ImGui::DragFloat("Intensity##al", &comp.intensity, 0.05f, 0.0f, 100.0f);
        ImGui::BeginDisabled(!bEditing);
        bool extentChanged = false;
        extentChanged |= ImGui::DragFloat("Half Width##al", &comp.halfWidth, 0.05f, 0.01f, 100.0f);
        extentChanged |= ImGui::DragFloat("Half Height##al", &comp.halfHeight, 0.05f, 0.01f, 100.0f);
        modified |= extentChanged;
        ImGui::EndDisabled();
        modified |= ImGui::DragFloat("Range##al", &comp.range, 0.5f, 0.0f, 1000.0f);
        modified |= ImGui::Checkbox("Draw Emissive Surface##al", &comp.drawEmissiveSurface);
        modified |= ImGui::Checkbox("Probe Bake Exclude##al", &comp.bExcludeFromProbeBake);

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
                          [&](Vec3 newPt) { comp.halfWidth = glm::max(0.01f, glm::dot(newPt - center, right) / transform->scale.x); modified = true; },
                          Editor::COLOR_AXIS_X);

        const Vec3 heightPlaneNormal = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, up) * up);
        Editor::DotHandle(Editor::DotHandleId::LIGHT_AREA_BASE + 1, center + up * comp.halfHeight * transform->scale.y, heightPlaneNormal,
                          vd.view, vd.proj, viewport, vd.cameraPos, state,
                          [&](Vec3 newPt) { comp.halfHeight = glm::max(0.01f, glm::dot(newPt - center, up) / transform->scale.y); modified = true; },
                          Editor::COLOR_AXIS_Y);
    }

    if (modified) { registry.emplace_or_replace<MultiframeDirtyComponent>(entity); }

    return {.bRequestRemoval = remove, .bModified = modified};
}

void Component::AreaLightComponent::Serialize(const AreaLightComponent& comp, Engine::TextWriter& w)
{
    static const AreaLightComponent DEF{};
    w.KeyOpt("color", comp.color, DEF.color);
    w.KeyOpt("intensity", comp.intensity, DEF.intensity);
    w.KeyOpt("halfWidth", comp.halfWidth, DEF.halfWidth);
    w.KeyOpt("halfHeight", comp.halfHeight, DEF.halfHeight);
    w.KeyOpt("range", comp.range, DEF.range);
    w.KeyOpt("drawEmissiveSurface", comp.drawEmissiveSurface, DEF.drawEmissiveSurface);
    w.KeyOpt("bExcludeFromProbeBake", comp.bExcludeFromProbeBake, DEF.bExcludeFromProbeBake);
}

void Component::AreaLightComponent::Deserialize(AreaLightComponent& comp, const Engine::TextReader& r)
{
    comp.color = r.Vec3("color", comp.color);
    comp.intensity = r.Float("intensity", comp.intensity);
    comp.halfWidth = r.Float("halfWidth", comp.halfWidth);
    comp.halfHeight = r.Float("halfHeight", comp.halfHeight);
    comp.range = r.Float("range", comp.range);
    comp.drawEmissiveSurface = r.Bool("drawEmissiveSurface", comp.drawEmissiveSurface);
    comp.bExcludeFromProbeBake = r.Bool("bExcludeFromProbeBake", comp.bExcludeFromProbeBake);
}

Engine::ComponentEditorResult Component::DirectionalLightComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletedirlight");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        auto& comp = registry.get<DirectionalLightComponent>(entity);
        modified |= ImGui::ColorEdit3("Color##dl", &comp.color.r);
        modified |= ImGui::DragFloat("Intensity##dl", &comp.intensity, 0.05f, 0.0f, 100.0f);
        modified |= ImGui::DragFloat("Angular Radius (deg)##dl", &comp.angularRadiusDegrees, 0.02f, 0.0f, 30.0f);
        modified |= ImGui::DragInt("Priority##dl", &comp.priority, 1.0f, -100, 100);
    }

    return {.bRequestRemoval = remove, .bModified = modified};
}

void Component::DirectionalLightComponent::Serialize(const DirectionalLightComponent& comp, Engine::TextWriter& w)
{
    static const DirectionalLightComponent DEF{};
    w.KeyOpt("color", comp.color, DEF.color);
    w.KeyOpt("intensity", comp.intensity, DEF.intensity);
    w.KeyOpt("priority", comp.priority, DEF.priority);
    w.KeyOpt("angularRadiusDegrees", comp.angularRadiusDegrees, DEF.angularRadiusDegrees);
}

void Component::DirectionalLightComponent::Deserialize(DirectionalLightComponent& comp, const Engine::TextReader& r)
{
    comp.color = r.Vec3("color", comp.color);
    comp.intensity = r.Float("intensity", comp.intensity);
    comp.priority = r.Int("priority", comp.priority);
    comp.angularRadiusDegrees = r.Float("angularRadiusDegrees", comp.angularRadiusDegrees);
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

LightInfo Component::ComputeAreaLightInfo(const TransformComponent& transform, const AreaLightComponent& light)
{
    const glm::mat3 rot = glm::mat3_cast(transform.rotation);
    return LightInfo{
        .position = {transform.translation, 0.0f},
        .normal = {rot[2], 0.0f},
        .right = {rot[0], light.halfWidth * transform.scale.x},
        .up = {rot[1], light.halfHeight * transform.scale.y},
        .packedColor = Render::PackColorRGB8(light.color),
        .intensity = light.intensity,
        .range = light.range,
        .type = LIGHT_TYPE_AREA,
    };
}

void Component::AreaLightComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    registry.get<AreaLightComponent>(entity).lightSlot = state->analyticLightStore.Allocate();
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    registry.emplace_or_replace<LightSurfacePendingTag>(entity);
    state->assetLoad.bPendingModelResolve = true;
}

void Component::AreaLightComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->analyticLightStore.Free(registry.get<AreaLightComponent>(entity).lightSlot);
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

    bool modified = false;
    if (open) {
        auto& comp = registry.get<SphereLightComponent>(entity);
        modified |= ImGui::ColorEdit3("Color##sl", &comp.color.r);
        modified |= ImGui::DragFloat("Intensity##sl", &comp.intensity, 0.05f, 0.0f, 100.0f);
        modified |= ImGui::DragFloat("Radius##sl", &comp.radius, 0.05f, 0.01f, 100.0f);
        modified |= ImGui::DragFloat("Range##sl", &comp.range, 0.5f, 0.0f, 1000.0f);
        modified |= ImGui::Checkbox("Draw Emissive Surface##sl", &comp.drawEmissiveSurface);
        modified |= ImGui::Checkbox("Probe Bake Exclude##sl", &comp.bExcludeFromProbeBake);
    }

    if (modified) { registry.emplace_or_replace<MultiframeDirtyComponent>(entity); }

    return {.bRequestRemoval = remove, .bModified = modified};
}

void Component::SphereLightComponent::Serialize(const SphereLightComponent& comp, Engine::TextWriter& w)
{
    static const SphereLightComponent DEF{};
    w.KeyOpt("color", comp.color, DEF.color);
    w.KeyOpt("intensity", comp.intensity, DEF.intensity);
    w.KeyOpt("radius", comp.radius, DEF.radius);
    w.KeyOpt("range", comp.range, DEF.range);
    w.KeyOpt("drawEmissiveSurface", comp.drawEmissiveSurface, DEF.drawEmissiveSurface);
    w.KeyOpt("bExcludeFromProbeBake", comp.bExcludeFromProbeBake, DEF.bExcludeFromProbeBake);
}

void Component::SphereLightComponent::Deserialize(SphereLightComponent& comp, const Engine::TextReader& r)
{
    comp.color = r.Vec3("color", comp.color);
    comp.intensity = r.Float("intensity", comp.intensity);
    comp.radius = r.Float("radius", comp.radius);
    comp.range = r.Float("range", comp.range);
    comp.drawEmissiveSurface = r.Bool("drawEmissiveSurface", comp.drawEmissiveSurface);
    comp.bExcludeFromProbeBake = r.Bool("bExcludeFromProbeBake", comp.bExcludeFromProbeBake);
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
        .packedColor = Render::PackColorRGB8(light.color),
        .intensity = light.intensity,
        .range = light.range,
        .type = LIGHT_TYPE_SPHERE,
    };
}

void Component::SphereLightComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    registry.get<SphereLightComponent>(entity).lightSlot = state->analyticLightStore.Allocate();
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    registry.emplace_or_replace<LightSurfacePendingTag>(entity);
    state->assetLoad.bPendingModelResolve = true;
}

void Component::SphereLightComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->analyticLightStore.Free(registry.get<SphereLightComponent>(entity).lightSlot);
    registry.remove<LightSurfacePendingTag>(entity);
    registry.remove<LightSurfaceRuntime>(entity);
}

void Component::LightSurfaceRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& runtime = registry.get<LightSurfaceRuntime>(entity);
    if (!runtime.range.IsValid() && !runtime.modelRange.IsValid()) { return; }

    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime.range);
    state->modelStore.Free(runtime.modelRange);
}

Engine::ComponentEditorResult Component::SkyboxComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteskybox");
    ImGui::PopStyleColor();

    bool modified = false;
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
                    modified = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Show Probes In Selection##sky", &bShowProbes);

        modified |= ImGui::DragFloat("Intensity##sky", &comp.intensity, 0.05f, 0.0f, 100.0f);
        modified |= ImGui::DragInt("Priority##sky", &comp.priority, 1.0f, -100, 100);
    }

    return {.bRequestRemoval = remove, .bModified = modified};
}

void Component::SkyboxComponent::Serialize(const SkyboxComponent& comp, Engine::TextWriter& w)
{
    static const SkyboxComponent DEF{};
    w.KeyOpt("envMap", comp.envMap.id, DEF.envMap.id);
    w.KeyOpt("intensity", comp.intensity, DEF.intensity);
    w.KeyOpt("priority", comp.priority, DEF.priority);
}

void Component::SkyboxComponent::Deserialize(SkyboxComponent& comp, const Engine::TextReader& r)
{
    comp.envMap = Engine::EnvironmentMapID{r.U64("envMap", comp.envMap.id)};
    comp.intensity = r.Float("intensity", comp.intensity);
    comp.priority = r.Int("priority", comp.priority);
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
