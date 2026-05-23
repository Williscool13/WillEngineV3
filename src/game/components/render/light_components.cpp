//
// Created by William on 2026-05-23.
//

#include "light_components.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include "game/component-registry/component_editor.h"
#include "game/component-registry/json_helpers.h"
#include "game/components/core_components.h"

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
        auto* transform = registry.try_get<Component::TransformComponent>(entity);
        if (transform) {
            const glm::mat4 view = viewFamily.mainView.currentViewData.view;
            const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
            const glm::vec3 center = transform->translation;
            const glm::quat rot = transform->rotation;
            const glm::vec3 right = rot * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 up = rot * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 forward = rot * glm::vec3(0.0f, 0.0f, 1.0f);

            ImGuizmo::Style savedStyle = ImGuizmo::GetStyle();
            ImGuizmo::Style& s = ImGuizmo::GetStyle();
            s.Colors[ImGuizmo::DIRECTION_X] = {0.5f, 0.8f, 1.0f, 1.0f};
            s.Colors[ImGuizmo::DIRECTION_Y] = {0.5f, 0.8f, 1.0f, 1.0f};
            s.Colors[ImGuizmo::DIRECTION_Z] = {0.5f, 0.8f, 1.0f, 1.0f};
            s.Colors[ImGuizmo::PLANE_X] = {0.5f, 0.8f, 1.0f, 0.38f};
            s.Colors[ImGuizmo::PLANE_Y] = {0.5f, 0.8f, 1.0f, 0.38f};
            s.Colors[ImGuizmo::PLANE_Z] = {0.5f, 0.8f, 1.0f, 0.38f};
            s.Colors[ImGuizmo::TRANSLATION_LINE] = {0.5f, 0.8f, 1.0f, 1.0f};
            s.Colors[ImGuizmo::SELECTION] = {0.8f, 1.0f, 1.0f, 1.0f};
            ImGuizmo::SetGizmoSizeClipSpace(0.07f);

            int32_t gizmoId = 10000;
            auto gizmo = [&](glm::vec3 worldPos, auto onMoved) {
                glm::mat4 mat = glm::translate(glm::mat4(1.0f), worldPos);
                ImGuizmo::PushID(gizmoId++);
                if (ImGuizmo::Manipulate(
                    glm::value_ptr(view), glm::value_ptr(proj),
                    ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                    glm::value_ptr(mat))) {
                    onMoved(glm::vec3(mat[3]));
                }
                ImGuizmo::PopID();
            };

            gizmo(center + right * comp.halfWidth, [&](glm::vec3 newPt) {
                comp.halfWidth = glm::max(0.01f, glm::abs(glm::dot(newPt - center, right)));
            });
            gizmo(center + up * comp.halfHeight, [&](glm::vec3 newPt) {
                comp.halfHeight = glm::max(0.01f, glm::dot(newPt - center, up));
            });

            ImGuizmo::GetStyle() = savedStyle;
            ImGuizmo::SetGizmoSizeClipSpace(0.1f);

            constexpr glm::vec4 editColor{0.5f, 0.8f, 1.0f, 1.0f};
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
} // Game
