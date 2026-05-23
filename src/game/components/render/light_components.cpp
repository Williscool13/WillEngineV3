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
#include "engine/include/engine_context.h"

namespace Game
{
Engine::ComponentEditorResult Component::PointLightComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletepointlight");
    ImGui::PopStyleColor();

    if (open) {
        auto& comp = registry.get<PointLightComponent>(entity);
        ImGui::ColorEdit3("Color##pl", &comp.color.r);
        ImGui::DragFloat("Intensity##pl", &comp.intensity, 0.05f, 0.0f, 100.0f);
        ImGui::DragFloat("Range##pl", &comp.range, 0.1f, 0.0f, 1000.0f);
    }

    return {.requestRemoval = remove};
}

Engine::ComponentEditorResult Component::AreaLightComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Area Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletearealight");
    ImGui::PopStyleColor();

    if (open) {
        auto& comp = registry.get<AreaLightComponent>(entity);
        ImGui::ColorEdit3("Color##al", &comp.color.r);
        ImGui::DragFloat("Intensity##al", &comp.intensity, 0.05f, 0.0f, 100.0f);
        ImGui::DragFloat("Half Width##al", &comp.halfWidth, 0.05f, 0.01f, 100.0f);
        ImGui::DragFloat("Half Height##al", &comp.halfHeight, 0.05f, 0.01f, 100.0f);
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (transform) {
            const auto& vd = viewFamily.mainView.currentViewData;
            const Vec3 center = transform->translation;
            const Quat rot = transform->rotation;
            const Vec3 right = rot * Vec3(1.0f, 0.0f, 0.0f);
            const Vec3 up = rot * Vec3(0.0f, 1.0f, 0.0f);
            const Vec3 forward = rot * Vec3(0.0f, 0.0f, 1.0f);

            auto* ctx = registry.ctx().get<Engine::EngineContext*>();
            auto* state = registry.ctx().get<Engine::EngineState*>();
            const Vec4 viewport{
                static_cast<float>(ctx->windowContext.viewportOffsetX),
                static_cast<float>(ctx->windowContext.viewportOffsetY),
                static_cast<float>(ctx->windowContext.viewportWidth),
                static_cast<float>(ctx->windowContext.viewportHeight),
            };

            const Vec3 widthPlaneNormal = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, right) * right);
            Editor::DotHandle(20000, center + right * comp.halfWidth, widthPlaneNormal,
                vd.view, vd.proj, viewport, vd.cameraPos, state,
                [&](Vec3 newPt) { comp.halfWidth = glm::max(0.01f, glm::dot(newPt - center, right)); },
                Editor::ColorAxisX);

            const Vec3 heightPlaneNormal = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, up) * up);
            Editor::DotHandle(20001, center + up * comp.halfHeight, heightPlaneNormal,
                vd.view, vd.proj, viewport, vd.cameraPos, state,
                [&](Vec3 newPt) { comp.halfHeight = glm::max(0.01f, glm::dot(newPt - center, up)); },
                Editor::ColorAxisY);

            constexpr Vec4 editColor{0.5f, 0.8f, 1.0f, 1.0f};
            DEBUG_ADD_RECT(viewFamily.debugRects, {center, comp.halfWidth, comp.halfHeight, right, up, editColor, 0.03f});
            DEBUG_ADD_ARROW(viewFamily.debugArrows, {center, center + forward * 0.5f, 0.08f, 0.02f, editColor, 0.01f});
        }
    }

    return {.requestRemoval = remove};
}

void Component::PointLightComponent::Serialize(const PointLightComponent& comp, nlohmann::json& json)
{
    json["color"] = comp.color;
    json["intensity"] = comp.intensity;
    json["range"] = comp.range;
}

void Component::PointLightComponent::Deserialize(PointLightComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.color = json.contains("color") ? json["color"].get<Vec3>() : Vec3{1.0f, 1.0f, 1.0f};
    comp.intensity = json.value("intensity", 1.0f);
    comp.range = json.value("range", 10.0f);
}

void Component::AreaLightComponent::Serialize(const AreaLightComponent& comp, nlohmann::json& json)
{
    json["color"] = comp.color;
    json["intensity"] = comp.intensity;
    json["halfWidth"] = comp.halfWidth;
    json["halfHeight"] = comp.halfHeight;
}

void Component::AreaLightComponent::Deserialize(AreaLightComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.color = json.contains("color") ? json["color"].get<Vec3>() : Vec3{1.0f, 1.0f, 1.0f};
    comp.intensity = json.value("intensity", 1.0f);
    comp.halfWidth = json.value("halfWidth", 1.0f);
    comp.halfHeight = json.value("halfHeight", 1.0f);
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
        ImGui::DragInt("Priority##dl", &comp.priority, 1.0f, -100, 100);

        auto* transform = registry.try_get<Component::TransformComponent>(entity);
        if (transform) {
            const glm::vec3 forward = transform->rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            const glm::vec3 pos = transform->translation;
            constexpr glm::vec4 dirColor{1.0f, 0.9f, 0.5f, 1.0f};
            DEBUG_ADD_ARROW(viewFamily.debugArrows, {pos, pos + forward * 2.0f, 0.15f, 0.04f, dirColor, 0.02f});
        }
    }

    return {.requestRemoval = remove};
}

void Component::DirectionalLightComponent::Serialize(const DirectionalLightComponent& comp, nlohmann::json& json)
{
    json["color"] = comp.color;
    json["intensity"] = comp.intensity;
    json["priority"] = comp.priority;
}

void Component::DirectionalLightComponent::Deserialize(DirectionalLightComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.color = json.contains("color") ? json["color"].get<Vec3>() : Vec3{1.0f, 1.0f, 1.0f};
    comp.intensity = json.value("intensity", 2.0f);
    comp.priority = json.value("priority", 0);
}
} // Game
